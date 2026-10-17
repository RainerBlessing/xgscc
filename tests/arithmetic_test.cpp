#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
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

template <typename T> const char* cname();
template <> const char* cname<uint8_t>() { return "uint8_t"; }
template <> const char* cname<int8_t>() { return "int8_t"; }
template <> const char* cname<uint16_t>() { return "uint16_t"; }
template <> const char* cname<int16_t>() { return "int16_t"; }

template <typename T> T valueOf(const ProgramRun& run, const std::string& name) {
    if (sizeof(T) == 1) {
        return static_cast<T>(run.value(name));
    }
    return static_cast<T>(run.value16(name));
}

template <typename T> std::vector<T> values() {
    std::vector<long> candidates = { 0, 1, 2, 3, 7, 10, 100, 127, 128, 200, 255, 256, 1000, 12345, 32767, 40000, 65535,
                                     -1, -2, -3, -7, -10, -100, -127, -128, -1000, -12345, -32768 };
    std::vector<T> result;
    for (long candidate : candidates) {
        T value = static_cast<T>(candidate);
        if (static_cast<long>(value) == candidate) {
            result.push_back(value);
        }
    }
    return result;
}

template <typename T> T run2(const std::string& op, T x, T y) {
    ProgramRun run = runProgram(source(
        std::string(cname<T>()) + " x = " + number(x) + "; " + cname<T>() + " y = " + number(y) + "; " + cname<T>() + " r;",
        "r = x " + op + " y;"), 3000000);
    EXPECT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted) << x << " " << op << " " << y;
    return valueOf<T>(run, "r");
}

template <typename T> void checkMultiplication() {
    for (T x : values<T>()) {
        for (T y : values<T>()) {
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " * " << +y);
            EXPECT_EQ(static_cast<T>(x * y), run2<T>("*", x, y));
        }
    }
}

template <typename T> void checkDivision() {
    for (T x : values<T>()) {
        for (T y : values<T>()) {
            if (y == 0) {
                continue;
            }
            if (std::is_signed<T>::value && x == static_cast<T>(1ull << (8 * sizeof(T) - 1)) && y == -1) {
                continue;       // the result does not fit: undefined in C
            }
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " / and % " << +y);
            EXPECT_EQ(static_cast<T>(x / y), run2<T>("/", x, y));
            EXPECT_EQ(static_cast<T>(x % y), run2<T>("%", x, y));
        }
    }
}

template <typename T> void checkVariableShifts() {
    const int bits = sizeof(T) * 8;
    for (T x : values<T>()) {
        for (int count : { 0, 1, 2, 5, 7, 8, 9, 15, 16, 17 }) {
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " shifted by " << count);
            ProgramRun run = runProgram(source(
                std::string(cname<T>()) + " x = " + number(x) + "; uint8_t n = " + number(count) + "; " + cname<T>() + " l; " + cname<T>() + " r;",
                "l = x << n; r = x >> n;"), 3000000);
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            T left = count >= bits ? 0 : static_cast<T>(static_cast<uint32_t>(x) << count);
            T right = count >= bits ? (x < 0 ? static_cast<T>(-1) : 0) : static_cast<T>(x >> count);
            EXPECT_EQ(left, valueOf<T>(run, "l"));
            EXPECT_EQ(right, valueOf<T>(run, "r"));
        }
    }
}

}

TEST(ArithmeticTest, MultiplyUint8) { checkMultiplication<uint8_t>(); }
TEST(ArithmeticTest, MultiplyInt8) { checkMultiplication<int8_t>(); }
TEST(ArithmeticTest, MultiplyUint16) { checkMultiplication<uint16_t>(); }
TEST(ArithmeticTest, MultiplyInt16) { checkMultiplication<int16_t>(); }

TEST(ArithmeticTest, DivideUint8) { checkDivision<uint8_t>(); }
TEST(ArithmeticTest, DivideInt8) { checkDivision<int8_t>(); }
TEST(ArithmeticTest, DivideUint16) { checkDivision<uint16_t>(); }
TEST(ArithmeticTest, DivideInt16) { checkDivision<int16_t>(); }

TEST(ArithmeticTest, VariableShiftsUint8) { checkVariableShifts<uint8_t>(); }
TEST(ArithmeticTest, VariableShiftsInt8) { checkVariableShifts<int8_t>(); }
TEST(ArithmeticTest, VariableShiftsUint16) { checkVariableShifts<uint16_t>(); }
TEST(ArithmeticTest, VariableShiftsInt16) { checkVariableShifts<int16_t>(); }

TEST(ArithmeticTest, PrecedenceOfMultiplication) {
    ProgramRun run = runProgram(source("uint8_t a = 3; uint8_t b = 4; uint8_t c = 5; uint8_t r1; uint8_t r2; uint8_t r3;",
        "r1 = a + b * c; r2 = (a + b) * c; r3 = a * b + c;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3 + 4 * 5, run.value("r1"));
    EXPECT_EQ((3 + 4) * 5, run.value("r2"));
    EXPECT_EQ(3 * 4 + 5, run.value("r3"));
}

TEST(ArithmeticTest, MultiplyWithConstants) {
    ProgramRun run = runProgram(source("uint8_t x = 13; uint16_t w = 1000; uint8_t r1; uint8_t r2; uint16_t r3;",
        "r1 = x * 5; r2 = 7 * x; r3 = w * 3;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(65, run.value("r1"));
    EXPECT_EQ(91, run.value("r2"));
    EXPECT_EQ(3000, run.value16("r3"));
}

TEST(ArithmeticTest, MixedSizesWiden) {
    ProgramRun run = runProgram(source("uint8_t b = 200; uint16_t w = 300; int8_t s = -3; uint16_t r1; uint16_t r2; int16_t r3;",
        "r1 = b * w; r2 = w / b; r3 = s * (int16_t)w;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(200 * 300), run.value16("r1"));
    EXPECT_EQ(1, run.value16("r2"));
    EXPECT_EQ(static_cast<uint16_t>(-900), run.value16("r3"));
}

TEST(ArithmeticTest, CompoundAssignments) {
    ProgramRun run = runProgram(source("uint8_t a = 6; uint8_t b = 200; uint8_t c = 200; uint16_t d = 1000; uint8_t n = 3; uint8_t e = 5;",
        "a *= 7; b /= 3; c %= 7; d *= 5; e <<= n; d >>= n;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(42, run.value("a"));
    EXPECT_EQ(66, run.value("b"));
    EXPECT_EQ(200 % 7, run.value("c"));
    EXPECT_EQ((1000 * 5) >> 3, run.value16("d"));
    EXPECT_EQ(40, run.value("e"));
}

TEST(ArithmeticTest, Elements) {
    ProgramRun run = runProgram(source("uint8_t a[3] = { 4, 5, 6 }; uint16_t w[2] = { 300, 7 };",
        "a[0] *= a[2]; a[1] %= 3; w[0] /= w[1];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(24, run.ram[run.compiled.variables.at("a")]);
    EXPECT_EQ(2, run.ram[run.compiled.variables.at("a") + 1]);
    EXPECT_EQ(300 / 7, run.ram[run.compiled.variables.at("w")] | run.ram[run.compiled.variables.at("w") + 1] << 8);
}

TEST(ArithmeticTest, InConditionsAndLoops) {
    ProgramRun run = runProgram(source("uint8_t n; uint16_t sum;",
        "for (uint8_t i = 1; i <= 10; i++) { if (i % 3 == 0) n++; sum += i * i; }"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("n"));
    EXPECT_EQ(385, run.value16("sum"));
}

TEST(ArithmeticTest, FactorialAndGreatestCommonDivisor) {
    ProgramRun run = runProgram(source("uint16_t f; uint8_t g;"
        "uint8_t gcd(uint8_t a, uint8_t b) { while (b) { uint8_t t = a % b; a = b; b = t; } return a; }",
        "f = 1; for (uint8_t i = 2; i <= 8; i++) f *= i; g = gcd(84, 36);"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(40320, run.value16("f"));
    EXPECT_EQ(12, run.value("g"));
}

TEST(ArithmeticTest, DigitsOfANumber) {
    ProgramRun run = runProgram(source("uint16_t n = 4823; uint8_t d0; uint8_t d1; uint8_t d2; uint8_t d3;",
        "d0 = n % 10; n /= 10; d1 = n % 10; n /= 10; d2 = n % 10; n /= 10; d3 = n % 10;"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("d0"));
    EXPECT_EQ(2, run.value("d1"));
    EXPECT_EQ(8, run.value("d2"));
    EXPECT_EQ(4, run.value("d3"));
}

TEST(ArithmeticTest, ConstantsAreFolded) {
    ProgramRun run = runProgram(source("int8_t a = 3 * 4; int8_t b = -7 / 2; int8_t c = -7 % 3; uint16_t d = 100 * 100; uint8_t e = 17 % 5 + 2 * 3;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(12, run.value("a"));
    EXPECT_EQ(static_cast<uint8_t>(-3), run.value("b"));
    EXPECT_EQ(static_cast<uint8_t>(-1), run.value("c"));
    EXPECT_EQ(10000, run.value16("d"));
    EXPECT_EQ(8, run.value("e"));
}

TEST(ArithmeticTest, NoRuntimeCodeWithoutTheOperators) {
    ProgramRun run = runProgram(source("uint8_t a; uint8_t b = 3;", "a = b + 1; a <<= 2;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_FALSE(contains(run.compiled.assembly, "f___"));
}

TEST(ArithmeticTest, TheRuntimeIsCompiledWhenUsed) {
    ProgramRun run = runProgram(source("uint8_t a = 6; uint8_t b = 7; uint8_t r;", "r = a * b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(contains(run.compiled.assembly, "f___mul8u"));
    EXPECT_FALSE(contains(run.compiled.assembly, "f___div"));
}

TEST(ArithmeticTest, TheCallsDeepIntoTheRuntimeCount) {
    // main -> f1 .. f6 -> the signed division, which calls the unsigned one: 8 deep
    std::string functions = "int8_t x = -9; int8_t y = 2; int8_t r;\n";
    for (int i = 6; i >= 1; i--) {
        functions += "void f" + number(i) + "(void) { " + (i < 6 ? "f" + number(i + 1) + "();" : "r = x / y;") + " }\n";
    }
    ProgramRun fits = runProgram(source(functions, "f1();"), 3000000);
    ASSERT_TRUE(fits.compiled.ok) << fits.compiled.error;
    EXPECT_EQ(static_cast<uint8_t>(-4), fits.value("r"));
    functions = "int8_t x = -9; int8_t y = 2; int8_t r;\n";
    for (int i = 7; i >= 1; i--) {
        functions += "void f" + number(i) + "(void) { " + (i < 7 ? "f" + number(i + 1) + "();" : "r = x / y;") + " }\n";
    }
    EXPECT_TRUE(contains(errorOf(source(functions, "f1();")), "return stack"));
}

// errors

TEST(ArithmeticErrorTest, ConstantDivisionByZero) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a;", "a = 5 / 0;")), "zero"));
    EXPECT_TRUE(contains(errorOf(source("uint8_t a;", "a = 5 % 0;")), "zero"));
}

TEST(ArithmeticErrorTest, OnPointers) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p; uint8_t x;", "x = p * 2;")), "pointer"));
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p;", "p /= 2;")), "pointer"));
}
