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
    CompileError error = errorOf("a\n#define X 1\n");
    EXPECT_EQ(2, error.line);
    EXPECT_NE(std::string::npos, error.message.find("#define")) << error.message;
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
