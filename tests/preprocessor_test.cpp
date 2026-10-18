// Macros with parameters, #if and friends, #include of files
#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "errors.h"
#include "compiler.h"
#include "lexer.h"
#include "run_helper.h"

namespace {

std::string texts(const std::string& source, const std::string& dir = "", LexInfo* info = NULL) {
    std::vector<Token> tokens = tokenize(source, dir, info);
    std::string result;
    for (size_t i = 0; i + 1 < tokens.size(); i++) {
        result += (i ? " " : "") + tokens[i].text;
    }
    return result;
}

CompileError errorOf(const std::string& source, const std::string& dir = "") {
    try {
        tokenize(source, dir, NULL);
    } catch (const CompileError& e) {
        return e;
    }
    return CompileError(-1, "no error");
}

bool mentions(const CompileError& error, const std::string& part) {
    return error.message.find(part) != std::string::npos;
}

std::string makeDirectory() {
    char pattern[] = "/tmp/xgscc_pp_XXXXXX";
    return mkdtemp(pattern);
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path.c_str()) << content;
}

}

// macros with parameters

TEST(MacroTest, OneParameter) {
    EXPECT_EQ("y = ( ( 3 ) * ( 3 ) ) ;", texts("#define SQ(x) ((x)*(x))\ny = SQ(3);"));
}

TEST(MacroTest, TwoParameters) {
    EXPECT_EQ("m = ( ( a ) > ( b ) ? ( a ) : ( b ) ) ;", texts("#define MAX(a,b) ((a)>(b)?(a):(b))\nm = MAX(a, b);"));
}

TEST(MacroTest, NoParameters) {
    EXPECT_EQ("x = 7 ;", texts("#define SEVEN() 7\nx = SEVEN();"));
}

TEST(MacroTest, ArgumentsWithParenthesesAndCommas) {
    EXPECT_EQ("y = ( f ( 1 , 2 ) + 3 ) ;", texts("#define ADD(a,b) (a + b)\ny = ADD(f(1,2), 3);"));
}

TEST(MacroTest, NestedMacros) {
    EXPECT_EQ("y = ( ( ( 2 ) * ( 2 ) ) * ( 2 ) ) ;", texts("#define SQ(x) ((x)*(x))\n#define CUBE(x) (SQ(x)*(x))\ny = CUBE(2);"));
}

TEST(MacroTest, ArgumentsAreExpanded) {
    EXPECT_EQ("y = ( ( 4 ) * ( 4 ) ) ;", texts("#define A 4\n#define SQ(x) ((x)*(x))\ny = SQ(A);"));
}

TEST(MacroTest, AMacroInTheBodyIsExpanded) {
    EXPECT_EQ("y = ( 3 + 1 ) ;", texts("#define ONE 1\n#define INC(x) (x + ONE)\ny = INC(3);"));
}

TEST(MacroTest, WithoutParenthesesTheNameStaysAName) {
    EXPECT_EQ("f + 1 ;", texts("#define f(x) x\nf + 1;"));
}

TEST(MacroTest, SpaceBeforeTheParenthesisOfACall) {
    EXPECT_EQ("y = ( 3 ) ;", texts("#define P(x) (x)\ny = P (3);"));
}

TEST(MacroTest, AParameterNameThatLooksLikeAnotherMacroIsTheParameter) {
    EXPECT_EQ("y = ( 5 ) ;", texts("#define N 9\n#define P(N) (N)\ny = P(5);"));
}

TEST(MacroTest, ARecursiveMacroIsExpandedOnce) {
    EXPECT_EQ("F ( 1 )", texts("#define F(x) F(x)\nF(1)"));
}

TEST(MacroTest, ACallOverSeveralLinesKeepsTheLineNumbers) {
    std::vector<Token> tokens = tokenize("#define P(a,b) a b\nP(1,\n2)\nz", "", NULL);
    ASSERT_EQ(4u, tokens.size());
    EXPECT_EQ("z", tokens[2].text);
    EXPECT_EQ(4, tokens[2].line);
}

TEST(MacroTest, WrongNumberOfArguments) {
    CompileError error = errorOf("#define P(a,b) a\nP(1)");
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "P")) << error.message;
    EXPECT_TRUE(mentions(error, "2")) << error.message;
}

TEST(MacroTest, TooManyArguments) {
    EXPECT_TRUE(mentions(errorOf("#define P(a) a\nP(1, 2)"), "P"));
}

TEST(MacroTest, ACallThatIsNotClosed) {
    CompileError error = errorOf("#define P(a) a\nP(1");
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "P")) << error.message;
}

TEST(MacroTest, StringsInArgumentsMayHoldParentheses) {
    // the text of a string token is its characters
    EXPECT_EQ("( ) )", texts("#define P(a) (a)\nP(\")\")"));
}

TEST(MacroTest, BadParameterLists) {
    EXPECT_TRUE(mentions(errorOf("#define P(a,) a\n"), "parameter"));
    EXPECT_TRUE(mentions(errorOf("#define P(a b) a\n"), "parameter"));
    EXPECT_TRUE(mentions(errorOf("#define P(a a) a\n"), "parameter"));
    EXPECT_TRUE(mentions(errorOf("#define P(a\n"), "parameter"));
}

TEST(MacroTest, UndefRemovesAFunctionMacro) {
    EXPECT_EQ("P ( 1 )", texts("#define P(x) x\n#undef P\nP(1)"));
}

TEST(MacroTest, AFunctionMacroCanBeRedefined) {
    EXPECT_EQ("2", texts("#define P(x) 1\n#undef P\n#define P(x) 2\nP(0)"));
}

// conditions

TEST(ConditionTest, IfdefTakesTheBranchOfADefinedName) {
    EXPECT_EQ("a", texts("#define X\n#ifdef X\na\n#else\nb\n#endif"));
    EXPECT_EQ("b", texts("#ifdef X\na\n#else\nb\n#endif"));
}

TEST(ConditionTest, Ifndef) {
    EXPECT_EQ("a", texts("#ifndef X\na\n#endif"));
    EXPECT_EQ("", texts("#define X\n#ifndef X\na\n#endif"));
}

TEST(ConditionTest, IfWithANumber) {
    EXPECT_EQ("a", texts("#if 1\na\n#endif"));
    EXPECT_EQ("", texts("#if 0\na\n#endif"));
}

TEST(ConditionTest, IfWithMacrosAndOperators) {
    EXPECT_EQ("a", texts("#define N 3\n#if N * 2 == 6 && N > 2\na\n#endif"));
    EXPECT_EQ("b", texts("#define N 3\n#if N < 2 || N == 4\na\n#else\nb\n#endif"));
}

TEST(ConditionTest, AllOperators) {
    EXPECT_EQ("ok", texts("#if (1 + 2 * 3 - 1) == 6 && (7 / 2) == 3 && (7 % 4) == 3 && (1 << 3) == 8 && (16 >> 2) == 4 && (6 & 3) == 2 && (6 | 3) == 7 && (6 ^ 3) == 5 && !0 && ~0 == -1 && -(-2) == 2 && (3 >= 3) && (3 <= 3) && (3 != 4)\nok\n#endif"));
}

TEST(ConditionTest, Ternary) {
    EXPECT_EQ("a", texts("#if (1 ? 5 : 6) == 5\na\n#endif"));
    EXPECT_EQ("a", texts("#if (0 ? 5 : 6) == 6\na\n#endif"));
}

TEST(ConditionTest, Defined) {
    EXPECT_EQ("a", texts("#define X\n#if defined(X) && !defined Y\na\n#endif"));
}

TEST(ConditionTest, DefinedDoesNotExpandItsName) {
    EXPECT_EQ("a", texts("#define X 0\n#if defined(X)\na\n#endif"));
}

TEST(ConditionTest, UnknownNamesAreZero) {
    EXPECT_EQ("a", texts("#if UNKNOWN == 0\na\n#endif"));
}

TEST(ConditionTest, ElifTakesTheFirstTrueBranchOnly) {
    EXPECT_EQ("b", texts("#if 0\na\n#elif 1\nb\n#elif 1\nc\n#else\nd\n#endif"));
    EXPECT_EQ("d", texts("#if 0\na\n#elif 0\nb\n#else\nd\n#endif"));
    EXPECT_EQ("a", texts("#if 1\na\n#elif 1\nb\n#endif"));
}

TEST(ConditionTest, Nested) {
    EXPECT_EQ("c", texts("#if 1\n#if 0\nb\n#else\nc\n#endif\n#else\nd\n#endif"));
    EXPECT_EQ("", texts("#if 0\n#if 1\nb\n#endif\n#endif"));
}

TEST(ConditionTest, TheConditionOfAnElifInADeadBranchIsNotEvaluated) {
    EXPECT_EQ("a", texts("#if 1\na\n#elif 1 / 0\nb\n#endif"));
    EXPECT_EQ("", texts("#if 0\n#if 1 / 0\n#endif\n#endif"));
}

TEST(ConditionTest, SkippedTextMayHoldAnythingThatIsNoDirective) {
    EXPECT_EQ("b", texts("#if 0\n@ ` ' \" unclosed\n/* #endif */\n#else\nb\n#endif"));
}

TEST(ConditionTest, DefinesInASkippedBranchDoNothing) {
    EXPECT_EQ("X", texts("#if 0\n#define X 1\n#endif\nX"));
}

TEST(ConditionTest, LineNumbersContinueAfterSkippedLines) {
    std::vector<Token> tokens = tokenize("#if 0\na\nb\n#else\nc\n#endif\nd", "", NULL);
    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ(5, tokens[0].line);
    EXPECT_EQ(7, tokens[1].line);
}

TEST(ConditionTest, ACommentAfterTheDirective) {
    EXPECT_EQ("a", texts("#if 1 // yes\na\n#endif // done"));
}

TEST(ConditionTest, EndifWithoutIf) {
    CompileError error = errorOf("a\n#endif");
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "#endif")) << error.message;
}

TEST(ConditionTest, ElseWithoutIf) {
    EXPECT_TRUE(mentions(errorOf("#else\n"), "#else"));
    EXPECT_TRUE(mentions(errorOf("#elif 1\n"), "#elif"));
}

TEST(ConditionTest, ElseAfterElse) {
    EXPECT_TRUE(mentions(errorOf("#if 1\n#else\n#else\n#endif"), "#else"));
    EXPECT_TRUE(mentions(errorOf("#if 1\n#else\n#elif 1\n#endif"), "#elif"));
}

TEST(ConditionTest, MissingEndif) {
    CompileError error = errorOf("a\n#if 1\nb\n");
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "#endif")) << error.message;
}

TEST(ConditionTest, BadExpressions) {
    EXPECT_TRUE(mentions(errorOf("#if\n#endif"), "#if"));
    EXPECT_TRUE(mentions(errorOf("#if 1 +\n#endif"), "#if"));
    EXPECT_TRUE(mentions(errorOf("#if (1\n#endif"), "#if"));
    EXPECT_TRUE(mentions(errorOf("#if 1 2\n#endif"), "#if"));
    EXPECT_TRUE(mentions(errorOf("#if 1 / 0\n#endif"), "zero"));
    EXPECT_TRUE(mentions(errorOf("#if 1 % 0\n#endif"), "zero"));
    EXPECT_TRUE(mentions(errorOf("#ifdef\n#endif"), "name"));
}

TEST(ConditionTest, ErrorDirective) {
    CompileError error = errorOf("a\n#error this is wrong\n");
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "this is wrong")) << error.message;
}

TEST(ConditionTest, ErrorDirectiveInASkippedBranch) {
    EXPECT_EQ("a", texts("#if 0\n#error no\n#endif\na"));
}

TEST(ConditionTest, UnknownDirectivesAreStillRefused) {
    EXPECT_TRUE(mentions(errorOf("#pragma once\n"), "#pragma"));
}

TEST(ConditionTest, UnknownDirectivesInASkippedBranchAreSkipped) {
    EXPECT_EQ("a", texts("#if 0\n#pragma once\n#endif\na"));
}

// #include

TEST(IncludeTest, AFileIsReadInPlace) {
    std::string dir = makeDirectory();
    writeFile(dir + "/defs.h", "#define LIMIT 10\nint x;\n");
    EXPECT_EQ("int x ; y = 10 ;", texts("#include \"defs.h\"\ny = LIMIT;", dir));
}

TEST(IncludeTest, TheIncludedFileCanIncludeAnother) {
    std::string dir = makeDirectory();
    writeFile(dir + "/a.h", "#include \"b.h\"\na\n");
    writeFile(dir + "/b.h", "b\n");
    EXPECT_EQ("b a c", texts("#include \"a.h\"\nc", dir));
}

TEST(IncludeTest, AGuardKeepsAFileFromBeingReadTwice) {
    std::string dir = makeDirectory();
    writeFile(dir + "/g.h", "#ifndef G_H\n#define G_H\nonce\n#endif\n");
    EXPECT_EQ("once", texts("#include \"g.h\"\n#include \"g.h\"", dir));
}

TEST(IncludeTest, AFileThatIncludesItselfIsRefused) {
    std::string dir = makeDirectory();
    writeFile(dir + "/loop.h", "#include \"loop.h\"\n");
    EXPECT_TRUE(mentions(errorOf("#include \"loop.h\"", dir), "loop.h"));
}

TEST(IncludeTest, AMissingFile) {
    std::string dir = makeDirectory();
    CompileError error = errorOf("a\n#include \"missing.h\"", dir);
    EXPECT_EQ(2, error.line);
    EXPECT_TRUE(mentions(error, "missing.h")) << error.message;
}

TEST(IncludeTest, WithoutADirectoryAQuotedIncludeIsIgnored) {
    EXPECT_EQ("a b", texts("a\n#include \"x.h\"\nb"));
}

TEST(IncludeTest, SystemHeadersAreBuiltIn) {
    LexInfo info;
    EXPECT_EQ("a", texts("#include <stdint.h>\n#include <xgs.h>\na", "", &info));
    EXPECT_EQ(1u, info.includes.count("xgs.h"));
    EXPECT_EQ(1u, info.includes.count("stdint.h"));
}

TEST(IncludeTest, AnIncludeInASkippedBranchIsNotRead) {
    LexInfo info;
    EXPECT_EQ("a", texts("#if 0\n#include <xgs.h>\n#endif\na", "", &info));
    EXPECT_EQ(0u, info.includes.count("xgs.h"));
}

TEST(IncludeTest, AnIncludeInAFileIsFoundInTheDirectoryOfTheFile) {
    std::string dir = makeDirectory();
    ASSERT_EQ(0, system(("mkdir " + dir + "/sub").c_str()));
    writeFile(dir + "/sub/inner.h", "inner\n");
    writeFile(dir + "/outer.h", "#include \"sub/inner.h\"\n");
    EXPECT_EQ("inner", texts("#include \"outer.h\"", dir));
}

TEST(IncludeTest, BadIncludeLines) {
    EXPECT_TRUE(mentions(errorOf("#include\n"), "#include"));
    EXPECT_TRUE(mentions(errorOf("#include foo\n"), "#include"));
}

// what the program defined, for the library

TEST(LexInfoTest, TheMacrosThatAreDefinedAtTheEnd) {
    LexInfo info;
    texts("#define A 1\n#define B 2\n#undef A\n#ifdef B\n#define C 3\n#endif", "", &info);
    EXPECT_EQ(0u, info.macros.count("A"));
    ASSERT_EQ(1u, info.macros.count("B"));
    EXPECT_EQ(2, info.macros["B"][0].value);
    EXPECT_EQ(1u, info.macros.count("C"));
}

// in a program

TEST(PreprocessorProgramTest, AMacroWithParametersInAProgram) {
    ProgramRun run = runProgram("#include <stdint.h>\n#define ADD3(a, b, c) ((a) + (b) + (c))\nuint8_t r;\nvoid main(void) { uint8_t x = 3; r = ADD3(x, 9, 1) + ADD3(20, x, x); }");
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(39, run.value("r"));
}

TEST(PreprocessorProgramTest, IfSelectsTheCode) {
    const std::string body = "#include <stdint.h>\n#define LEVEL 2\nuint8_t r;\nvoid main(void) {\n#if LEVEL == 1\nr = 10;\n#elif LEVEL == 2\nr = 20;\n#else\nr = 30;\n#endif\n}";
    ProgramRun run = runProgram(body);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(20, run.value("r"));
}

TEST(PreprocessorProgramTest, AnIncludedHeaderBringsDeclarations) {
    std::string dir = makeDirectory();
    writeFile(dir + "/lib.h", "#ifndef LIB_H\n#define LIB_H\n#define STEP 4\nuint8_t total;\nvoid add(uint8_t v) { total += v; }\n#endif\n");
    CompileResult result = compile("#include <stdint.h>\n#include \"lib.h\"\n#include \"lib.h\"\nvoid main(void) { add(STEP); add(STEP); }", dir + "/main.c");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(1u, result.variables.count("total"));
}

TEST(PreprocessorProgramTest, TheVideoSettingsMayComeFromAHeader) {
    std::string dir = makeDirectory();
    writeFile(dir + "/config.h", "#define XGS_LINES 100\n");
    CompileResult result = compile("#include <stdint.h>\n#include \"config.h\"\n#include <xgs.h>\nvoid main(void) { xgs_init(); xgs_frame(); }", dir + "/game.c");
    ASSERT_TRUE(result.ok) << result.error;
    // 100 lines: the kernel counts them
    EXPECT_NE(std::string::npos, result.assembly.find("mov w, #64"));
}

TEST(PreprocessorProgramTest, NoXgsSettingsInASkippedBranch) {
    CompileResult result = compile("#include <stdint.h>\n#if 0\n#define XGS_COLUMNS 99\n#endif\n#include <xgs.h>\nvoid main(void) { xgs_init(); xgs_frame(); }", "game.c");
    ASSERT_TRUE(result.ok) << result.error;
}

TEST(PreprocessorProgramTest, ErrorDirectiveSaysFileAndLine) {
    CompileResult result = compile("#include <stdint.h>\n#ifndef BOARD\n#error BOARD is not defined\n#endif\nvoid main(void) {}", "game.c");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ("game.c:3: #error BOARD is not defined", result.error);
}

TEST(PreprocessorProgramTest, AMissingHeaderIsReported) {
    CompileResult result = compile("#include \"nothing.h\"\nvoid main(void) {}", "game.c");
    ASSERT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("nothing.h")) << result.error;
}
