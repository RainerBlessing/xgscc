#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "errors.h"
#include "lexer.h"

namespace {

std::string texts(const std::string& source) {
    std::string out;
    for (const Token& token : tokenize(source)) {
        if (token.type == Token::END) {
            break;
        }
        out += (out.empty() ? "" : " ") + token.text;
    }
    return out;
}

CompileError errorOf(const std::string& source) {
    try {
        tokenize(source);
    } catch (const CompileError& error) {
        return error;
    }
    ADD_FAILURE() << "no error for: " << source;
    return CompileError(0, "");
}

}

TEST(LexerTest, EndsWithAnEndToken) {
    std::vector<Token> tokens = tokenize("");
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ(Token::END, tokens[0].type);
}

TEST(LexerTest, SplitsIdentifiersNumbersAndPunctuation) {
    EXPECT_EQ("uint8_t counter ; counter = 5 ;", texts("uint8_t counter; counter=5;"));
}

TEST(LexerTest, KnowsTheTypeOfAToken) {
    std::vector<Token> tokens = tokenize("x 12 +");
    EXPECT_EQ(Token::IDENT, tokens[0].type);
    EXPECT_EQ(Token::NUMBER, tokens[1].type);
    EXPECT_EQ(Token::OPERATOR, tokens[2].type);
}

TEST(LexerTest, ReadsDecimalHexAndCharacterNumbers) {
    std::vector<Token> tokens = tokenize("10 0x1F 0XfF 'A' 0");
    EXPECT_EQ(10, tokens[0].value);
    EXPECT_EQ(0x1f, tokens[1].value);
    EXPECT_EQ(0xff, tokens[2].value);
    EXPECT_EQ(65, tokens[3].value);
    EXPECT_EQ(0, tokens[4].value);
}

TEST(LexerTest, AcceptsTheUnsignedSuffix) {
    std::vector<Token> tokens = tokenize("5u 7U");
    EXPECT_EQ(5, tokens[0].value);
    EXPECT_EQ(7, tokens[1].value);
}

TEST(LexerTest, TakesTheLongestOperator) {
    EXPECT_EQ("<<= << <= < ++ + += == = != ! && & || |", texts("<<= << <= < ++ + += == = != ! && & || |"));
    EXPECT_EQ("a << = b", texts("a << = b"));
    EXPECT_EQ("a ++ + b", texts("a+++b"));
}

TEST(LexerTest, SkipsComments) {
    EXPECT_EQ("a b c", texts("a // line comment\nb /* block\ncomment */ c"));
}

TEST(LexerTest, CountsLines) {
    std::vector<Token> tokens = tokenize("a\n\nb /* x\ny */ c");
    EXPECT_EQ(1, tokens[0].line);
    EXPECT_EQ(3, tokens[1].line);
    EXPECT_EQ(4, tokens[2].line);
}

TEST(LexerTest, IgnoresIncludeLines) {
    EXPECT_EQ("a b", texts("#include <stdint.h>\na\n#include \"x.h\"\nb"));
}

TEST(LexerTest, OtherDirectivesAreNotSupportedYet) {
    CompileError error = errorOf("a\n#ifdef X\n");
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("#ifdef")) << error.message;
}

// #define

TEST(LexerTest, DefineReplacesTheName) {
    EXPECT_EQ("x = 5 + 1 ;", texts("#define LIMIT 5 + 1\nx = LIMIT;"));
}

TEST(LexerTest, DefineWithoutValueIsEmpty) {
    EXPECT_EQ("a b", texts("#define NOTHING\na NOTHING b"));
}

TEST(LexerTest, DefineKnowsEarlierDefines) {
    EXPECT_EQ("x = 3 ;", texts("#define A 3\n#define B A\nx = B;"));
}

TEST(LexerTest, DefineIsReadWhereItIsUsedNotWhereItIsDefined) {
    EXPECT_EQ("x = 4 ;", texts("#define B A\n#define A 4\nx = B;"));
}

TEST(LexerTest, DefineThatNamesItselfStops) {
    EXPECT_EQ("x = X + 1 ;", texts("#define X X + 1\nx = X;"));
}

TEST(LexerTest, DefineEndsAtTheEndOfTheLine) {
    EXPECT_EQ("a 1 b", texts("#define ONE 1\na ONE\nb"));
}

TEST(LexerTest, DefineMayHaveAComment) {
    EXPECT_EQ("x = 7 ;", texts("#define SEVEN 7 // lucky\nx = SEVEN;"));
}

TEST(LexerTest, UndefRemovesTheName) {
    EXPECT_EQ("1 N", texts("#define N 1\nN\n#undef N\nN"));
}

TEST(LexerTest, ReplacedTokensHaveTheLineOfTheUse) {
    std::vector<Token> tokens = tokenize("#define V 9\n\n\nx = V;");
    EXPECT_EQ(4, tokens[2].line);
    EXPECT_EQ(9, tokens[2].value);
}

TEST(LexerTest, DefineWithParametersIsNotSupportedYet) {
    CompileError error = errorOf("#define F(x) x\n");
    EXPECT_EQ(1, error.line);
    EXPECT_NE(std::string::npos, error.message.find("parameters")) << error.message;
}

TEST(LexerTest, DefineNeedsAName) {
    EXPECT_EQ(1, errorOf("#define\n").line);
    EXPECT_EQ(1, errorOf("#define 5 6\n").line);
}

TEST(LexerTest, UnknownCharacterIsAnError) {
    CompileError error = errorOf("a\n  @");
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("@")) << error.message;
}

TEST(LexerTest, UnterminatedCommentIsAnError) {
    EXPECT_EQ(1, errorOf("a /* never closed").line);
}

TEST(LexerTest, BadNumbersAreErrors) {
    EXPECT_EQ(1, errorOf("0x").line);
    EXPECT_EQ(1, errorOf("12abc").line);
    EXPECT_EQ(1, errorOf("1.5").line);
}

TEST(LexerTest, BadCharacterLiteralIsAnError) {
    EXPECT_EQ(1, errorOf("'ab'").line);
    EXPECT_EQ(1, errorOf("''").line);
}

// strings

TEST(LexerTest, StringLiteral) {
    std::vector<Token> tokens = tokenize("\"hello\" x");
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(Token::STRING, tokens[0].type);
    EXPECT_EQ("hello", tokens[0].text);
    EXPECT_EQ(Token::IDENT, tokens[1].type);
}

TEST(LexerTest, EmptyString) {
    EXPECT_EQ("", tokenize("\"\"")[0].text);
}

TEST(LexerTest, StringEscapes) {
    EXPECT_EQ(std::string("a\nb\tc\\d\"e\0f", 11), tokenize("\"a\\nb\\tc\\\\d\\\"e\\0f\"")[0].text);
}

TEST(LexerTest, StringMayHoldCommentMarks) {
    EXPECT_EQ("a // b /* c", tokenize("\"a // b /* c\"")[0].text);
}

TEST(LexerTest, UnterminatedStringIsAnError) {
    EXPECT_EQ(1, errorOf("\"never closed").line);
    EXPECT_EQ(1, errorOf("\"line\nbreak\"").line);
}

TEST(LexerTest, UnknownEscapeInAString) {
    EXPECT_NE(std::string::npos, errorOf("\"a\\qb\"").message.find("\\q"));
}

TEST(LexerTest, DefineCanHoldAString) {
    std::vector<Token> tokens = tokenize("#define GREETING \"hi\"\nGREETING");
    EXPECT_EQ(Token::STRING, tokens[0].type);
    EXPECT_EQ("hi", tokens[0].text);
}

TEST(LexerTest, ArrowIsOneToken) {
    EXPECT_EQ("p -> x - > y", texts("p->x - > y"));
    EXPECT_EQ("a - - b", texts("a - - b"));
    EXPECT_EQ("a -- > b", texts("a-->b"));
}
