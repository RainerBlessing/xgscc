#include <gtest/gtest.h>
#include <string>
#include "run_helper.h"

namespace {

std::string number(long value) {
    return std::to_string(value);
}

std::string source(const std::string& declarations, const std::string& mainBody) {
    return "#include <stdint.h>\n" + declarations + "\nvoid main(void) {\n" + mainBody + "\n}\n";
}

std::string errorOf(const std::string& program) {
    CompileResult result = compile(program, "test.c");
    EXPECT_FALSE(result.ok) << program;
    return result.error;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// the program with a switch over x that sets r
int classify(int x, const std::string& type = "uint8_t") {
    ProgramRun run = runProgram(source(
        type + " x = " + number(x) + "; uint8_t r;",
        "switch (x) { case 1: r = 10; break; case 2: r = 20; break; case 5: r = 50; break; default: r = 99; }"));
    EXPECT_TRUE(run.compiled.ok) << run.compiled.error;
    return run.value("r");
}

}

TEST(SwitchTest, PicksTheCase) {
    EXPECT_EQ(10, classify(1));
    EXPECT_EQ(20, classify(2));
    EXPECT_EQ(50, classify(5));
}

TEST(SwitchTest, DefaultTakesTheRest) {
    EXPECT_EQ(99, classify(0));
    EXPECT_EQ(99, classify(3));
    EXPECT_EQ(99, classify(255));
}

TEST(SwitchTest, WithoutDefaultNothingHappens) {
    ProgramRun run = runProgram(source("uint8_t x = 9; uint8_t r = 7;", "switch (x) { case 1: r = 1; break; case 2: r = 2; break; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(7, run.value("r"));
}

TEST(SwitchTest, WithoutBreakTheNextCaseRuns) {
    ProgramRun run = runProgram(source("uint8_t x = 1; uint8_t r;", "switch (x) { case 1: r += 1; case 2: r += 10; break; case 3: r += 100; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, run.value("r"));
}

TEST(SwitchTest, StackedCases) {
    for (int x = 0; x < 8; x++) {
        ProgramRun run = runProgram(source("uint8_t x = " + number(x) + "; uint8_t r;",
            "switch (x) { case 1: case 3: case 5: r = 1; break; case 2: case 4: r = 2; break; default: r = 3; }"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        int expected = (x == 1 || x == 3 || x == 5) ? 1 : (x == 2 || x == 4) ? 2 : 3;
        EXPECT_EQ(expected, run.value("r")) << x;
    }
}

TEST(SwitchTest, DefaultInTheMiddleFallsThroughInSourceOrder) {
    for (int x : { 1, 2, 3 }) {
        ProgramRun run = runProgram(source("uint8_t x = " + number(x) + "; uint8_t r;",
            "switch (x) { case 1: r += 1; break; default: r += 10; case 2: r += 100; break; }"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(x == 1 ? 1 : x == 2 ? 100 : 110, run.value("r")) << x;
    }
}

TEST(SwitchTest, SixteenBitValue) {
    ProgramRun run = runProgram(source("uint16_t x = 300; uint8_t r1; uint8_t r2;",
        "switch (x) { case 44: r1 = 1; break; case 300: r1 = 2; break; default: r1 = 3; }"
        "x = 44; switch (x) { case 44: r2 = 1; break; case 300: r2 = 2; break; default: r2 = 3; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2, run.value("r1"));
    EXPECT_EQ(1, run.value("r2"));
}

TEST(SwitchTest, NegativeValues) {
    for (int x : { -1, 0, 1, -128 }) {
        ProgramRun run = runProgram(source("int8_t x = " + number(x) + "; uint8_t r;",
            "switch (x) { case -1: r = 1; break; case 1: r = 2; break; case -128: r = 3; break; default: r = 4; }"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(x == -1 ? 1 : x == 1 ? 2 : x == -128 ? 3 : 4, run.value("r")) << x;
    }
}

TEST(SwitchTest, CharacterAndConstantExpressionsAsCases) {
    ProgramRun run = runProgram(source("const uint8_t BASE = 10; uint8_t x = 13; uint8_t y = 'b'; uint8_t r1; uint8_t r2;",
        "switch (x) { case BASE + 3: r1 = 1; break; default: r1 = 2; } switch (y) { case 'a': r2 = 1; break; case 'b': r2 = 2; break; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("r1"));
    EXPECT_EQ(2, run.value("r2"));
}

TEST(SwitchTest, ExpressionIsEvaluatedOnce) {
    ProgramRun run = runProgram(source("uint8_t calls; uint8_t r;"
        "uint8_t next(void) { calls++; return calls; }",
        "switch (next()) { case 1: r = 5; break; case 2: r = 6; break; default: r = 7; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("calls"));
    EXPECT_EQ(5, run.value("r"));
}

TEST(SwitchTest, BreakLeavesOnlyTheSwitchAndContinueGoesToTheLoop) {
    ProgramRun run = runProgram(source("uint8_t sum; uint8_t evens;",
        "for (uint8_t i = 0; i < 6; i++) {"
        "  switch (i & 3) { case 0: evens++; break; case 1: continue; case 2: evens++; default: break; }"
        "  sum += i;"
        "}"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    // i = 0: evens, sum 0; 1: continue; 2: evens, sum 2; 3: sum 5; 4: evens, sum 9; 5: continue
    EXPECT_EQ(3, run.value("evens"));
    EXPECT_EQ(0 + 2 + 3 + 4, run.value("sum"));
}

TEST(SwitchTest, NestedSwitches) {
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            ProgramRun run = runProgram(source("uint8_t a = " + number(a) + "; uint8_t b = " + number(b) + "; uint8_t r;",
                "switch (a) { case 1: switch (b) { case 1: r = 11; break; default: r = 10; } break; case 2: r = 20; break; default: r = 1; }"));
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            int expected = a == 1 ? (b == 1 ? 11 : 10) : a == 2 ? 20 : 1;
            EXPECT_EQ(expected, run.value("r")) << a << "," << b;
        }
    }
}

TEST(SwitchTest, ReturnInsideASwitch) {
    ProgramRun run = runProgram(source("uint8_t r1; uint8_t r2; uint8_t r3;"
        "uint8_t name(uint8_t n) { switch (n) { case 0: return 100; case 1: return 101; } return 0; }",
        "r1 = name(0); r2 = name(1); r3 = name(7);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(100, run.value("r1"));
    EXPECT_EQ(101, run.value("r2"));
    EXPECT_EQ(0, run.value("r3"));
}

TEST(SwitchTest, StateMachine) {
    ProgramRun run = runProgram(source("uint8_t state; uint8_t steps; uint8_t log[8];",
        "for (steps = 0; steps < 8; steps++) {"
        "  log[steps] = state;"
        "  switch (state) { case 0: state = 1; break; case 1: state = 2; break; case 2: state = 0; steps++; break; }"
        "}"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0, run.ram[run.compiled.variables.at("log")]);
    EXPECT_EQ(1, run.ram[run.compiled.variables.at("log") + 1]);
    EXPECT_EQ(2, run.ram[run.compiled.variables.at("log") + 2]);
}

TEST(SwitchTest, DeclarationInTheBody) {
    ProgramRun run = runProgram(source("uint8_t x = 2; uint8_t r;",
        "switch (x) { case 1: { uint8_t t = 5; r = t; break; } case 2: { uint8_t t = 6; r = t + 1; break; } }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(7, run.value("r"));
}

TEST(SwitchTest, ManyCases) {
    std::string cases;
    for (int i = 0; i < 40; i++) {
        cases += "case " + number(i * 3) + ": r = " + number(i + 1) + "; break; ";
    }
    for (int x : { 0, 3, 60, 117, 118, 4 }) {
        ProgramRun run = runProgram(source("uint8_t x = " + number(x) + "; uint8_t r;", "switch (x) { " + cases + "default: r = 200; }"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        int expected = (x % 3 == 0 && x / 3 < 40) ? x / 3 + 1 : 200;
        EXPECT_EQ(expected, run.value("r")) << x;
    }
}

TEST(SwitchTest, EmptyAndOnlyDefault) {
    ProgramRun run = runProgram(source("uint8_t x = 3; uint8_t r;", "switch (x) { } switch (x) { default: r = 4; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value("r"));
}

TEST(SwitchTest, ConstantExpression) {
    ProgramRun run = runProgram(source("uint8_t r;", "switch (2 + 1) { case 3: r = 1; break; default: r = 2; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("r"));
}

// errors

TEST(SwitchErrorTest, DuplicateCase) {
    std::string message = errorOf(source("uint8_t x;", "switch (x) { case 1: break; case 1: break; }"));
    EXPECT_TRUE(contains(message, "1") && contains(message, "twice")) << message;
}

TEST(SwitchErrorTest, DuplicateDefault) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x;", "switch (x) { default: break; default: break; }")), "default"));
}

TEST(SwitchErrorTest, CaseMustBeConstant) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x; uint8_t y;", "switch (x) { case y: break; }")), "constant"));
}

TEST(SwitchErrorTest, CaseThatDoesNotFit) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x;", "switch (x) { case 300: break; }")), "300"));
}

TEST(SwitchErrorTest, SwitchOnAPointer) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p;", "switch (p) { default: break; }")), "pointer"));
}

TEST(SwitchErrorTest, ContinueOutsideOfALoopEvenInASwitch) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x;", "switch (x) { case 1: continue; }")), "continue"));
}
