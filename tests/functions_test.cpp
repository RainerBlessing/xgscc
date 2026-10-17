#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include "run_helper.h"

namespace {

// a program: globals and functions, then main with the given body
std::string source(const std::string& declarations, const std::string& mainBody) {
    return "#include <stdint.h>\n" + declarations + "\nvoid main(void) {\n" + mainBody + "\n}\n";
}

std::string number(int value) {
    return std::to_string(value);
}

std::string errorOf(const std::string& program) {
    CompileResult result = compile(program, "test.c");
    EXPECT_FALSE(result.ok) << program;
    return result.error;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const int VALUES[] = { 0, 1, 2, 5, 127, 128, 200, 255 };

// "uint8_t h0; uint8_t h1; ..."
std::string manyGlobals(int count) {
    std::string text;
    for (int i = 0; i < count; i++) {
        text += "uint8_t h" + number(i) + "; ";
    }
    return text;
}

}

// calls and parameters

TEST(FunctionsTest, VoidFunctionChangesAGlobal) {
    ProgramRun run = runProgram(source("uint8_t a; void set(void) { a = 42; }", "set();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(42, run.value("a"));
}

TEST(FunctionsTest, ParametersAndReturnValue) {
    for (int a : VALUES) {
        for (int b : VALUES) {
            ProgramRun run = runProgram(source(
                "uint8_t r; uint8_t sum(uint8_t x, uint8_t y) { return x + y; }",
                "r = sum(" + number(a) + ", " + number(b) + ");"));
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            EXPECT_EQ((a + b) & 0xff, run.value("r")) << a << " + " << b;
        }
    }
}

TEST(FunctionsTest, ArgumentsAreExpressions) {
    ProgramRun run = runProgram(source(
        "uint8_t x = 10; uint8_t r; uint8_t sub(uint8_t a, uint8_t b) { return a - b; }",
        "r = sub(x + 5, x - 3);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15 - 7, run.value("r"));
}

TEST(FunctionsTest, ThreeParameters) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t mix(uint8_t a, uint8_t b, uint8_t c) { return (a << 4) | (b << 2) | c; }",
        "r = mix(1, 2, 3);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ((1 << 4) | (2 << 2) | 3, run.value("r"));
}

TEST(FunctionsTest, ResultOfACallIsAnOperand) {
    ProgramRun run = runProgram(source(
        "uint8_t x = 7; uint8_t y = 3; uint8_t r1; uint8_t r2; uint8_t r3; uint8_t r4;"
        "uint8_t twice(uint8_t v) { return v + v; } uint8_t inc1(uint8_t v) { return v + 1; }",
        "r1 = twice(x) + inc1(y); r2 = twice(x) - inc1(y); r3 = (x + 1) + twice(y); r4 = inc1(x) & twice(y);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(14 + 4, run.value("r1"));
    EXPECT_EQ(14 - 4, run.value("r2"));
    EXPECT_EQ(8 + 6, run.value("r3"));
    EXPECT_EQ(8 & 6, run.value("r4"));
}

TEST(FunctionsTest, CallsInsideArguments) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t inc1(uint8_t v) { return v + 1; } uint8_t add(uint8_t a, uint8_t b) { return a + b; }",
        "r = add(inc1(1), inc1(inc1(10)));"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2 + 12, run.value("r"));
}

// an argument that calls the same function must not destroy the arguments already stored
TEST(FunctionsTest, SameFunctionInTwoArguments) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t sub(uint8_t a, uint8_t b) { return a - b; }",
        "r = sub(sub(20, 5), sub(9, 2));"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15 - 7, run.value("r"));
}

TEST(FunctionsTest, FirstArgumentSurvivesACallInTheSecond) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t sub(uint8_t a, uint8_t b) { return a - b; }",
        "r = sub(50, sub(9, 2));"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(50 - 7, run.value("r"));
}

TEST(FunctionsTest, CallsInConditions) {
    ProgramRun run = runProgram(source(
        "uint8_t a; uint8_t b; uint8_t c; uint8_t big(uint8_t v) { return v > 100; }",
        "if (big(200)) a = 1; if (!big(5)) b = 1; while (big(c)) c = 0; c = 0; for (c = 0; !big(c); c += 60) ;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(1, run.value("b"));
    EXPECT_EQ(120, run.value("c"));
}

TEST(FunctionsTest, FunctionsCallFunctions) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t one(void) { return 1; } uint8_t two(void) { return one() + one(); }"
        "uint8_t four(void) { return two() + two(); }",
        "r = four() + one();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, run.value("r"));
}

TEST(FunctionsTest, FunctionDefinedAfterTheCall) {
    ProgramRun run = runProgram(
        "uint8_t r; void main(void) { r = later(3); } uint8_t later(uint8_t x) { return x + 1; }");
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value("r"));
}

TEST(FunctionsTest, PrototypeThenDefinition) {
    ProgramRun run = runProgram(
        "uint8_t r; uint8_t later(uint8_t x); void main(void) { r = later(3); } uint8_t later(uint8_t x) { return x + 1; }");
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value("r"));
}

// return

TEST(FunctionsTest, EarlyReturn) {
    for (int x : VALUES) {
        ProgramRun run = runProgram(source(
            "uint8_t r; uint8_t classify(uint8_t v) { if (v < 10) return 1; if (v < 150) return 2; return 3; }",
            "r = classify(" + number(x) + ");"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(x < 10 ? 1 : x < 150 ? 2 : 3, run.value("r")) << x;
    }
}

TEST(FunctionsTest, ReturnInAVoidFunction) {
    ProgramRun run = runProgram(source(
        "uint8_t a; void f(uint8_t v) { if (v) return; a = 9; }",
        "f(1); f(0);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(9, run.value("a"));
}

TEST(FunctionsTest, ReturnFromMainEndsTheProgram) {
    ProgramRun run = runProgram(source("uint8_t a;", "a = 1; return; a = 2;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(1, run.value("a"));
}

TEST(FunctionsTest, ReturnInsideALoop) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t find(uint8_t limit) { for (uint8_t i = 0; i < 200; i++) { if (i == limit) return i + 100; } return 0; }",
        "r = find(7);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(107, run.value("r"));
}

// local variables

TEST(FunctionsTest, LocalsOfACallerSurviveACall) {
    ProgramRun run = runProgram(source(
        "uint8_t r; void clobber(void) { uint8_t a = 99; uint8_t b = 98; r = a + b; }"
        "void outer(void) { uint8_t keep = 5; uint8_t more = 6; clobber(); r = keep + more; }",
        "outer();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, run.value("r"));
}

TEST(FunctionsTest, FunctionsThatNeverRunTogetherShareTheirLocals) {
    ProgramRun run = runProgram(source(
        "uint8_t r1; uint8_t r2;"
        "void f(void) { uint8_t a = 1; uint8_t b = 2; uint8_t c = 3; r1 = a + b + c; }"
        "void g(void) { uint8_t a = 10; uint8_t b = 20; uint8_t c = 30; r2 = a + b + c; }",
        "f(); g();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6, run.value("r1"));
    EXPECT_EQ(60, run.value("r2"));
}

TEST(FunctionsTest, ParametersAreLocalVariables) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t f(uint8_t n) { n++; n += 10; return n; }",
        "uint8_t x = 1; r = f(x) + x;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(12 + 1, run.value("r"));
}

TEST(FunctionsTest, ParameterHidesAGlobal) {
    ProgramRun run = runProgram(source(
        "uint8_t x = 100; uint8_t r; uint8_t f(uint8_t x) { return x + 1; }",
        "r = f(5);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6, run.value("r"));
}

TEST(FunctionsTest, StaticLocalKeepsItsValue) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t count(void) { static uint8_t n = 10; n++; return n; }",
        "count(); count(); r = count();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(13, run.value("r"));
}

TEST(FunctionsTest, StaticLocalsOfTwoFunctionsAreSeparate) {
    ProgramRun run = runProgram(source(
        "uint8_t r1; uint8_t r2;"
        "uint8_t f(void) { static uint8_t n; n++; return n; } uint8_t g(void) { static uint8_t n = 50; n--; return n; }",
        "f(); r1 = f(); g(); r2 = g();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2, run.value("r1"));
    EXPECT_EQ(48, run.value("r2"));
}

// const and #define

TEST(FunctionsTest, ConstGlobalIsAConstant) {
    ProgramRun run = runProgram(source(
        "const uint8_t STEP = 3; const uint8_t LIMIT = 4 << 1; uint8_t r;",
        "r = 1 << STEP; r += LIMIT;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(8 + 8, run.value("r"));
}

TEST(FunctionsTest, ConstLocal) {
    ProgramRun run = runProgram(source("uint8_t r;", "const uint8_t k = 5; r = k + k;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(10, run.value("r"));
}

TEST(FunctionsTest, ConstTakesNoRam) {
    std::string constants;
    for (int i = 0; i < 40; i++) {
        constants += "const uint8_t c" + number(i) + " = " + number(i) + ";";
    }
    ProgramRun run = runProgram(source(constants + "uint8_t r;", "r = c39;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(39, run.value("r"));
}

TEST(FunctionsTest, DefineConstants) {
    ProgramRun run = runProgram(
        "#define LIMIT 10\n#define STEP 2\n#define DOUBLE_LIMIT LIMIT + LIMIT\n"
        "uint8_t n; void main(void) { for (uint8_t i = 0; i < LIMIT; i += STEP) n++; n += DOUBLE_LIMIT; }");
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5 + 20, run.value("n"));
}

// break and continue

TEST(FunctionsTest, BreakLeavesTheLoop) {
    ProgramRun run = runProgram(source(
        "uint8_t n;", "for (uint8_t i = 0; i < 100; i++) { if (i == 7) break; n++; } uint8_t w = 0; while (1) { w++; if (w > 3) break; }"
        "uint8_t d = 0; do { d++; if (d == 5) break; } while (1); n += w + d;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(7 + 4 + 5, run.value("n"));
}

TEST(FunctionsTest, ContinueGoesToTheNextRound) {
    ProgramRun run = runProgram(source(
        "uint8_t n; uint8_t m;",
        "for (uint8_t i = 0; i < 10; i++) { if (i & 1) continue; n++; }"
        "uint8_t j = 0; while (j < 10) { j++; if (j < 6) continue; m++; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, run.value("n"));
    EXPECT_EQ(5, run.value("m"));
}

TEST(FunctionsTest, BreakAndContinueBelongToTheInnerLoop) {
    ProgramRun run = runProgram(source(
        "uint8_t n;",
        "for (uint8_t i = 0; i < 4; i++) { for (uint8_t j = 0; j < 10; j++) { if (j == 3) break; n++; } }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(12, run.value("n"));
}

TEST(FunctionsTest, DoWhileRunsAtLeastOnce) {
    ProgramRun run = runProgram(source(
        "uint8_t a; uint8_t b;", "do a++; while (0); uint8_t i = 0; do { b += 2; i++; } while (i < 5);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(10, run.value("b"));
}

TEST(FunctionsTest, ContinueInDoWhileTestsTheCondition) {
    ProgramRun run = runProgram(source(
        "uint8_t n;", "uint8_t i = 0; do { i++; if (i < 3) continue; n++; } while (i < 6);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value("n"));
}

// code on several pages

TEST(FunctionsTest, ManyBigFunctionsAcrossPages) {
    std::string functions;
    std::string calls;
    const int count = 12;
    for (int f = 0; f < count; f++) {
        std::string body;
        for (int i = 0; i < 40; i++) {
            body += "x = x + 1;\n";
        }
        functions += "void f" + number(f) + "(void) {\n" + body + "}\n";
        calls += "f" + number(f) + "();\n";
    }
    ProgramRun run = runProgram(source("uint8_t x;\n" + functions, calls));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ((count * 40) & 0xff, run.value("x"));
    // the code really needs more than one page
    int last = 0;
    for (int address = 0; address < 0xfff; address++) {
        if (run.compiled.rom[address] != 0xfff) {
            last = address;
        }
    }
    EXPECT_GT(last, 0x400);
}

TEST(FunctionsTest, CallsAcrossPagesAndLoopsThatCrossAPage) {
    // a long loop body that runs over the page boundary, calling a function on the way
    std::string body;
    for (int i = 0; i < 300; i++) {
        body += "x = x + 1;\n";
    }
    ProgramRun run = runProgram(source(
        "uint8_t x; uint8_t y; void bump(void) { y++; }",
        "for (uint8_t i = 0; i < 3; i++) {\n" + body + "bump();\n}"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ((900) & 0xff, run.value("x"));
    EXPECT_EQ(3, run.value("y"));
}

TEST(FunctionsTest, ProgramTooBigForTheRom) {
    std::string body;
    for (int i = 0; i < 1500; i++) {
        body += "x = x + 1;\n";
    }
    std::string message = errorOf(source("uint8_t x;", body));
    EXPECT_TRUE(contains(message, "ROM")) << message;
}

// errors

TEST(FunctionsErrorTest, UnknownFunction) {
    EXPECT_EQ("test.c:3: unknown function 'nope'", errorOf("uint8_t a;\nvoid main(void) {\n a = nope(1);\n}\n"));
}

TEST(FunctionsErrorTest, WrongNumberOfArguments) {
    std::string message = errorOf(source("uint8_t f(uint8_t a, uint8_t b) { return a; }", "f(1);"));
    EXPECT_TRUE(contains(message, "'f'") && contains(message, "2")) << message;
}

TEST(FunctionsErrorTest, MainCannotBeCalled) {
    EXPECT_TRUE(contains(errorOf("void f(void) { main(); } void main(void) { f(); }"), "main"));
}

TEST(FunctionsErrorTest, VoidFunctionHasNoValue) {
    std::string message = errorOf(source("uint8_t a; void f(void) { }", "a = f();"));
    EXPECT_TRUE(contains(message, "'f'") && contains(message, "value")) << message;
}

TEST(FunctionsErrorTest, ReturnNeedsAValueInAFunctionWithAResult) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t f(void) { return; }", "f();")), "return"));
}

TEST(FunctionsErrorTest, VoidFunctionReturnsNoValue) {
    EXPECT_TRUE(contains(errorOf(source("void f(void) { return 1; }", "f();")), "return"));
}

TEST(FunctionsErrorTest, MainMustBeVoid) {
    EXPECT_TRUE(contains(errorOf("uint8_t main(void) { return 0; }"), "main"));
    EXPECT_TRUE(contains(errorOf("void main(uint8_t a) { }"), "main"));
}

TEST(FunctionsErrorTest, BreakOutsideALoop) {
    std::string message = errorOf(source("", "break;"));
    EXPECT_TRUE(contains(message, "test.c:4:") && contains(message, "break")) << message;
    EXPECT_TRUE(contains(errorOf(source("", "continue;")), "continue"));
}

TEST(FunctionsErrorTest, ParameterDeclaredTwice) {
    EXPECT_TRUE(contains(errorOf(source("void f(uint8_t a, uint8_t a) { }", "f(1, 2);")), "'a'"));
}

TEST(FunctionsErrorTest, LocalWithTheNameOfAParameter) {
    EXPECT_TRUE(contains(errorOf(source("void f(uint8_t a) { uint8_t a; }", "f(1);")), "'a'"));
}

TEST(FunctionsErrorTest, FunctionDefinedTwice) {
    EXPECT_TRUE(contains(errorOf(source("void f(void) { } void f(void) { }", "f();")), "'f'"));
}

TEST(FunctionsErrorTest, PrototypeThatDoesNotMatch) {
    EXPECT_TRUE(contains(errorOf(source("void f(uint8_t a); void f(void) { }", "f(1);")), "'f'"));
    EXPECT_TRUE(contains(errorOf(source("uint8_t f(void); void f(void) { }", "f();")), "'f'"));
}

TEST(FunctionsErrorTest, PrototypeWithoutDefinitionThatIsCalled) {
    std::string message = errorOf(source("void f(void);", "f();"));
    EXPECT_TRUE(contains(message, "'f'") && contains(message, "defined")) << message;
}

TEST(FunctionsErrorTest, MistakesInFunctionsThatAreNeverCalledAreReported) {
    EXPECT_EQ("test.c:1: unknown variable 'zzz'", errorOf("void unused(void) { zzz = 1; } void main(void) { }"));
}

TEST(FunctionsErrorTest, DirectRecursion) {
    std::string message = errorOf(source("void f(void) { f(); }", "f();"));
    EXPECT_TRUE(contains(message, "recursion") && contains(message, "f -> f")) << message;
}

TEST(FunctionsErrorTest, MutualRecursionNamesTheCycle) {
    std::string message = errorOf(source("void g(void); void f(void) { g(); } void g(void) { f(); }", "f();"));
    EXPECT_TRUE(contains(message, "recursion") && contains(message, "f -> g -> f")) << message;
}

TEST(FunctionsErrorTest, RecursionInAFunctionNobodyCallsIsHarmless) {
    ProgramRun run = runProgram(source("void f(void) { f(); }", ""));
    EXPECT_TRUE(run.compiled.ok) << run.compiled.error;
}

namespace {
// main calls f1, f1 calls f2, ... the last one is f<depth>
std::string chain(int depth) {
    std::string functions = "uint8_t x;\n";
    for (int i = depth; i >= 1; i--) {
        functions += "void f" + number(i) + "(void) { " + (i < depth ? "f" + number(i + 1) + "();" : "x++;") + " }\n";
    }
    return source(functions, "f1();");
}
}

TEST(FunctionsErrorTest, EightNestedCallsFit) {
    ProgramRun run = runProgram(chain(8));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(1, run.value("x"));
}

TEST(FunctionsErrorTest, NineNestedCallsAreTooMany) {
    std::string message = errorOf(chain(9));
    EXPECT_TRUE(contains(message, "8") && contains(message, "f1 -> f2")) << message;
}

TEST(FunctionsErrorTest, OutOfRamWithParametersAndLocals) {
    std::string message = errorOf(source(
        "uint8_t f(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) { uint8_t i1; uint8_t i2; uint8_t i3; uint8_t i4; uint8_t i5; uint8_t i6; return a; }"
        "uint8_t g1; uint8_t g2; uint8_t g3; uint8_t g4; uint8_t g5; uint8_t g6; uint8_t g7; uint8_t g8;"
        + manyGlobals(104),
        "f(1, 2, 3, 4, 5);"));
    EXPECT_TRUE(contains(message, "RAM")) << message;
}

TEST(FunctionsErrorTest, ConstNeedsAConstantInitializer) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t v;", "const uint8_t k = v;")), "const"));
    EXPECT_TRUE(contains(errorOf(source("", "const uint8_t k;")), "const"));
}

TEST(FunctionsErrorTest, ConstCannotBeAssigned) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t k = 1;", "k = 2;")), "const"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t k = 1;", "k++;")), "const"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t k = 1;", "k += 1;")), "const"));
}

TEST(FunctionsErrorTest, StaticInitializerMustBeConstant) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t v; void f(void) { static uint8_t s = v; }", "f();")), "constant"));
}

TEST(FunctionsTest, TheFunctionsExample) {
    std::ifstream file(EXAMPLES_DIR "/functions.c");
    std::stringstream text;
    text << file.rdbuf();
    ProgramRun run = runProgram(text.str());
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);        // main ends in "while (1) { }", a jump to itself
    EXPECT_EQ(100, run.value("total"));     // 1 + 3 + ... + 19
}
