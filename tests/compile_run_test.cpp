#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include "run_helper.h"

namespace {

std::string program(const std::string& globals, const std::string& body) {
    return "#include <stdint.h>\n" + globals + "\nvoid main(void) {\n" + body + "\n}\n";
}

std::string number(int value) {
    return std::to_string(value);
}

// runs "r = x OP y;" with the values and gives r
int binary(const std::string& op, int x, int y) {
    ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t y = " + number(y) + "; uint8_t r;",
                                  "r = x " + op + " y;"));
    EXPECT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    return run.value("r");
}

const int VALUES[] = { 0, 1, 2, 5, 127, 128, 200, 254, 255 };

}

TEST(CompileRunTest, EmptyMainHalts) {
    ProgramRun run = runProgram(program("", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
}

TEST(CompileRunTest, GlobalsStartWithTheirInitializerOrZero) {
    ProgramRun run = runProgram(program("uint8_t a = 5; uint8_t b; uint8_t c = 255;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, run.value("a"));
    EXPECT_EQ(0, run.value("b"));
    EXPECT_EQ(255, run.value("c"));
}

TEST(CompileRunTest, GlobalInitializerMayBeAConstantExpression) {
    ProgramRun run = runProgram(program("uint8_t a = (3 + 4) << 1; uint8_t b = ~0; uint8_t c = 2 - 3;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(14, run.value("a"));
    EXPECT_EQ(255, run.value("b"));
    EXPECT_EQ(255, run.value("c"));
}

TEST(CompileRunTest, AssignsAConstant) {
    for (int value : VALUES) {
        ProgramRun run = runProgram(program("uint8_t a;", "a = " + number(value) + ";"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(value, run.value("a"));
    }
}

TEST(CompileRunTest, CopiesAVariable) {
    ProgramRun run = runProgram(program("uint8_t a = 77; uint8_t b;", "b = a;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(77, run.value("b"));
}

TEST(CompileRunTest, BinaryOperatorsOfTwoVariables) {
    for (int x : VALUES) {
        for (int y : VALUES) {
            SCOPED_TRACE(testing::Message() << x << " and " << y);
            EXPECT_EQ((x + y) & 0xff, binary("+", x, y));
            EXPECT_EQ((x - y) & 0xff, binary("-", x, y));
            EXPECT_EQ(x & y, binary("&", x, y));
            EXPECT_EQ(x | y, binary("|", x, y));
            EXPECT_EQ(x ^ y, binary("^", x, y));
        }
    }
}

// the constant on the right, on the left, and in between
TEST(CompileRunTest, BinaryOperatorsWithAConstantOperand) {
    const char* const ops[] = { "+", "-", "&", "|", "^" };
    for (const char* op : ops) {
        for (int x : VALUES) {
            for (int k : VALUES) {
                SCOPED_TRACE(testing::Message() << x << " " << op << " " << k);
                ProgramRun right = runProgram(program("uint8_t x = " + number(x) + "; uint8_t r;",
                                                std::string("r = x ") + op + " " + number(k) + ";"));
                ProgramRun left = runProgram(program("uint8_t x = " + number(x) + "; uint8_t r;",
                                               "r = " + number(k) + " " + op + " x;"));
                ASSERT_TRUE(right.compiled.ok) << right.compiled.error;
                ASSERT_TRUE(left.compiled.ok) << left.compiled.error;
                int expectedRight = 0, expectedLeft = 0;
                switch (op[0]) {
                case '+': expectedRight = x + k; expectedLeft = k + x; break;
                case '-': expectedRight = x - k; expectedLeft = k - x; break;
                case '&': expectedRight = expectedLeft = x & k; break;
                case '|': expectedRight = expectedLeft = x | k; break;
                case '^': expectedRight = expectedLeft = x ^ k; break;
                }
                EXPECT_EQ(expectedRight & 0xff, right.value("r"));
                EXPECT_EQ(expectedLeft & 0xff, left.value("r"));
            }
        }
    }
}

TEST(CompileRunTest, NestedExpressionsUseTemporaries) {
    ProgramRun run = runProgram(program("uint8_t x = 12; uint8_t y = 5; uint8_t r1; uint8_t r2; uint8_t r3;",
        "r1 = (x + y) - (x & y);"
        "r2 = (x ^ y) + ((x | y) - (x & 3));"
        "r3 = (x - y) - (y - 1);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ((12 + 5) - (12 & 5), run.value("r1"));
    EXPECT_EQ(((12 ^ 5) + ((12 | 5) - (12 & 3))) & 0xff, run.value("r2"));
    EXPECT_EQ(((12 - 5) - (5 - 1)) & 0xff, run.value("r3"));
}

TEST(CompileRunTest, UnaryOperators) {
    for (int x : VALUES) {
        ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t a; uint8_t b; uint8_t c;",
                                      "a = ~x; b = -x; c = !x;"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(~x & 0xff, run.value("a")) << x;
        EXPECT_EQ(-x & 0xff, run.value("b")) << x;
        EXPECT_EQ(x == 0 ? 1 : 0, run.value("c")) << x;
    }
}

TEST(CompileRunTest, ShiftsByConstants) {
    for (int x : VALUES) {
        for (int count = 0; count <= 9; count++) {
            ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t l; uint8_t r;",
                                          "l = x << " + number(count) + "; r = x >> " + number(count) + ";"));
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            EXPECT_EQ(count >= 8 ? 0 : (x << count) & 0xff, run.value("l")) << x << " << " << count;
            EXPECT_EQ(count >= 8 ? 0 : x >> count, run.value("r")) << x << " >> " << count;
        }
    }
}

TEST(CompileRunTest, ComparisonsGiveZeroOrOne) {
    for (int x : VALUES) {
        for (int y : VALUES) {
            SCOPED_TRACE(testing::Message() << x << " and " << y);
            EXPECT_EQ(x == y, binary("==", x, y));
            EXPECT_EQ(x != y, binary("!=", x, y));
            EXPECT_EQ(x < y, binary("<", x, y));
            EXPECT_EQ(x > y, binary(">", x, y));
            EXPECT_EQ(x <= y, binary("<=", x, y));
            EXPECT_EQ(x >= y, binary(">=", x, y));
        }
    }
}

TEST(CompileRunTest, LogicalOperators) {
    const int logical[] = { 0, 1, 2, 255 };
    for (int x : logical) {
        for (int y : logical) {
            SCOPED_TRACE(testing::Message() << x << " and " << y);
            EXPECT_EQ(x && y, binary("&&", x, y));
            EXPECT_EQ(x || y, binary("||", x, y));
        }
    }
}

TEST(CompileRunTest, CompoundAssignments) {
    ProgramRun run = runProgram(program("uint8_t a = 10; uint8_t b = 10; uint8_t c = 12; uint8_t d = 12; uint8_t e = 12; uint8_t f = 3; uint8_t g = 200;",
        "a += 5; b -= 15; c &= 6; d |= 3; e ^= 5; f <<= 2; g >>= 3;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15, run.value("a"));
    EXPECT_EQ((10 - 15) & 0xff, run.value("b"));
    EXPECT_EQ(12 & 6, run.value("c"));
    EXPECT_EQ(12 | 3, run.value("d"));
    EXPECT_EQ(12 ^ 5, run.value("e"));
    EXPECT_EQ(12, run.value("f"));
    EXPECT_EQ(25, run.value("g"));
}

TEST(CompileRunTest, CompoundAssignmentWithAnExpression) {
    ProgramRun run = runProgram(program("uint8_t a = 10; uint8_t b = 3; uint8_t c = 4;", "a -= b + c; b |= c << 2;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("a"));
    EXPECT_EQ(3 | 16, run.value("b"));
}

TEST(CompileRunTest, IncrementAndDecrementWrapAround) {
    ProgramRun run = runProgram(program("uint8_t a = 255; uint8_t b = 0; uint8_t c = 5; uint8_t d = 5;",
                                  "a++; b--; ++c; --d;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0, run.value("a"));
    EXPECT_EQ(255, run.value("b"));
    EXPECT_EQ(6, run.value("c"));
    EXPECT_EQ(4, run.value("d"));
}

TEST(CompileRunTest, IfAndElse) {
    for (int x : VALUES) {
        ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t a; uint8_t b;",
            "if (x) a = 1; else a = 2;"
            "if (x > 100) b = 3;"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(x ? 1 : 2, run.value("a")) << x;
        EXPECT_EQ(x > 100 ? 3 : 0, run.value("b")) << x;
    }
}

TEST(CompileRunTest, ElseIfChain) {
    for (int x : VALUES) {
        ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t r;",
            "if (x < 2) r = 10; else if (x < 128) r = 20; else if (x == 128) r = 30; else r = 40;"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(x < 2 ? 10 : x < 128 ? 20 : x == 128 ? 30 : 40, run.value("r")) << x;
    }
}

TEST(CompileRunTest, ConditionsWithLogicalOperators) {
    for (int x : { 0, 1, 5, 10 }) {
        for (int y : { 0, 1, 5, 10 }) {
            ProgramRun run = runProgram(program("uint8_t x = " + number(x) + "; uint8_t y = " + number(y) + "; uint8_t a; uint8_t b; uint8_t c;",
                "if (x > 2 && y < 8) a = 1;"
                "if (x == 0 || y == 0) b = 1;"
                "if (!(x == y)) c = 1;"));
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            EXPECT_EQ(x > 2 && y < 8, run.value("a")) << x << "," << y;
            EXPECT_EQ(x == 0 || y == 0, run.value("b")) << x << "," << y;
            EXPECT_EQ(!(x == y), run.value("c")) << x << "," << y;
        }
    }
}

TEST(CompileRunTest, ConstantConditions) {
    ProgramRun run = runProgram(program("uint8_t a; uint8_t b; uint8_t c;",
        "if (1) a = 1; else a = 2;"
        "if (0) b = 1; else b = 2;"
        "if (3 - 3) c = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(2, run.value("b"));
    EXPECT_EQ(0, run.value("c"));
}

TEST(CompileRunTest, WhileLoopSumsTheNumbers) {
    ProgramRun run = runProgram(program("uint8_t i = 1; uint8_t sum;", "while (i <= 10) { sum += i; i++; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(55, run.value("sum"));
    EXPECT_EQ(11, run.value("i"));
}

TEST(CompileRunTest, WhileWithAFalseConditionNeverRuns) {
    ProgramRun run = runProgram(program("uint8_t a;", "while (0) a = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(0, run.value("a"));
}

TEST(CompileRunTest, ForLoopWithDeclaredVariable) {
    ProgramRun run = runProgram(program("uint8_t sum;", "for (uint8_t i = 0; i < 5; i++) sum += 3;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15, run.value("sum"));
}

TEST(CompileRunTest, ForLoopCountingDown) {
    ProgramRun run = runProgram(program("uint8_t n; uint8_t i;", "for (i = 200; i != 0; i--) n++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(200, run.value("n"));
}

TEST(CompileRunTest, NestedLoops) {
    ProgramRun run = runProgram(program("uint8_t count;",
        "for (uint8_t i = 0; i < 6; i++) for (uint8_t j = 0; j < 7; j++) count++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(42, run.value("count"));
}

TEST(CompileRunTest, InfiniteLoopKeepsRunning) {
    ProgramRun run = runProgram(program("uint8_t counter;", "while (1) { counter++; }"), 1000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_FALSE(run.halted);
    EXPECT_GT(run.value("counter"), 0);
}

TEST(CompileRunTest, ForWithoutParts) {
    ProgramRun run = runProgram(program("uint8_t counter;", "for (;;) counter++;"), 1000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_FALSE(run.halted);
}

TEST(CompileRunTest, LocalVariablesAndBlocks) {
    ProgramRun run = runProgram(program("uint8_t r;",
        "uint8_t a = 4; { uint8_t b = a + 1; r = b; } uint8_t b = 9; r += b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5 + 9, run.value("r"));
}

TEST(CompileRunTest, InnerVariableHidesTheOuterOne) {
    ProgramRun run = runProgram(program("uint8_t a = 1; uint8_t r;",
        "{ uint8_t a = 7; r = a; } r += a;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(8, run.value("r"));
}

// errors

namespace {
std::string errorOf(const std::string& source) {
    CompileResult result = compile(source, "test.c");
    EXPECT_FALSE(result.ok) << source;
    return result.error;
}
}

TEST(CompileErrorTest, MessageHasFileAndLine) {
    EXPECT_EQ("test.c:3: unknown variable 'nope'",
              errorOf("uint8_t a;\nvoid main(void) {\n a = nope;\n}\n"));
}

TEST(CompileErrorTest, SyntaxErrorsHaveFileAndLine) {
    EXPECT_EQ(0u, errorOf("void main(void) { a = 1 }").find("test.c:1: "));
}

TEST(CompileErrorTest, AssigningToAnUnknownVariable) {
    EXPECT_NE(std::string::npos, errorOf(program("", "x = 1;")).find("unknown variable 'x'"));
    EXPECT_NE(std::string::npos, errorOf(program("", "x++;")).find("unknown variable 'x'"));
}

TEST(CompileErrorTest, VariableDeclaredTwice) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a; uint8_t a; void main(void) { }").find("'a' is declared twice"));
    EXPECT_NE(std::string::npos, errorOf(program("", "uint8_t b; uint8_t b;")).find("'b' is declared twice"));
}

TEST(CompileErrorTest, ConstantOutOfRange) {
    EXPECT_NE(std::string::npos, errorOf(program("uint8_t a;", "a = 256;")).find("256"));
    EXPECT_NE(std::string::npos, errorOf(program("uint8_t a = 300;", "")).find("300"));
}

TEST(CompileErrorTest, GlobalInitializerMustBeConstant) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a; uint8_t b = a; void main(void) { }").find("not constant"));
}

TEST(CompileErrorTest, ShiftCountMustBeConstant) {
    EXPECT_NE(std::string::npos, errorOf(program("uint8_t a; uint8_t n;", "a = a << n;")).find("constant"));
}

TEST(CompileErrorTest, MainIsRequired) {
    EXPECT_NE(std::string::npos, errorOf("uint8_t a;").find("main"));
}

TEST(CompileErrorTest, MainTwice) {
    EXPECT_NE(std::string::npos, errorOf("void main(void) { } void main(void) { }").find("main"));
}

TEST(CompileErrorTest, OutOfRam) {
    std::string globals;
    for (int i = 0; i < 119; i++) {
        globals += "uint8_t v" + number(i) + ";";
    }
    EXPECT_NE(std::string::npos, errorOf(program(globals, "")).find("RAM"));
}

TEST(CompileErrorTest, ExpressionTooComplex) {
    // every level holds the left half in a temporary while it computes the right one
    std::string expression = "(x + 1)";
    for (int i = 2; i <= 14; i++) {
        expression = "(" + expression + " + (x + " + number(i) + "))";
    }
    EXPECT_NE(std::string::npos, errorOf(program("uint8_t x; uint8_t r;", "r = " + expression + ";")).find("too complex"));
}

TEST(CompileRunTest, TheCounterExampleCounts) {
    std::ifstream file(EXAMPLES_DIR "/counter.c");
    std::stringstream source;
    source << file.rdbuf();
    ProgramRun run = runProgram(source.str(), 1000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_FALSE(run.halted);
    // jmp _start, the clear of the global and the clear of "counter = 0", then
    // inc + jmp per round and the last step is an inc; the counter wraps at 256
    EXPECT_EQ(((1000 - 3 + 1) / 2) & 0xff, run.value("counter"));
}
