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

template <typename T> const char* cname();
template <> const char* cname<uint8_t>() { return "uint8_t"; }
template <> const char* cname<int8_t>() { return "int8_t"; }
template <> const char* cname<uint16_t>() { return "uint16_t"; }
template <> const char* cname<int16_t>() { return "int16_t"; }

// the value of a global of the type T after the run
template <typename T> T valueOf(const ProgramRun& run, const std::string& name) {
    if (sizeof(T) == 1) {
        return static_cast<T>(run.value(name));
    }
    return static_cast<T>(run.value16(name));
}

template <typename T> std::vector<T> interestingValues() {
    std::vector<long> candidates = { 0, 1, 2, 3, 100, 127, 128, 129, 255, 256, 257, 1000, 32767, 32768, 40000, 65535,
                                     -1, -2, -100, -127, -128, -129, -256, -1000, -32767, -32768 };
    std::vector<T> values;
    for (long candidate : candidates) {
        T value = static_cast<T>(candidate);
        if (static_cast<long>(value) == candidate) {
            values.push_back(value);
        }
    }
    return values;
}

// runs "R r = x OP y;" and returns r
template <typename T, typename R = T> R binary(const std::string& op, T x, T y) {
    ProgramRun run = runProgram(source(
        std::string(cname<T>()) + " x = " + number(x) + "; " + cname<T>() + " y = " + number(y) + "; " + cname<R>() + " r;",
        "r = x " + op + " y;"));
    EXPECT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    return valueOf<R>(run, "r");
}

template <typename T> void checkArithmetic() {
    std::vector<T> values = interestingValues<T>();
    for (T x : values) {
        for (T y : values) {
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " and " << +y);
            EXPECT_EQ(static_cast<T>(x + y), binary<T>("+", x, y));
            EXPECT_EQ(static_cast<T>(x - y), binary<T>("-", x, y));
            EXPECT_EQ(static_cast<T>(x & y), binary<T>("&", x, y));
            EXPECT_EQ(static_cast<T>(x | y), binary<T>("|", x, y));
            EXPECT_EQ(static_cast<T>(x ^ y), binary<T>("^", x, y));
        }
    }
}

template <typename T> void checkComparisons() {
    std::vector<T> values = interestingValues<T>();
    for (T x : values) {
        for (T y : values) {
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " and " << +y);
            EXPECT_EQ(x == y, (binary<T, uint8_t>("==", x, y)));
            EXPECT_EQ(x != y, (binary<T, uint8_t>("!=", x, y)));
            EXPECT_EQ(x < y, (binary<T, uint8_t>("<", x, y)));
            EXPECT_EQ(x > y, (binary<T, uint8_t>(">", x, y)));
            EXPECT_EQ(x <= y, (binary<T, uint8_t>("<=", x, y)));
            EXPECT_EQ(x >= y, (binary<T, uint8_t>(">=", x, y)));
        }
    }
}

template <typename T> void checkShifts() {
    const int bits = sizeof(T) * 8;
    for (T x : interestingValues<T>()) {
        for (int count = 0; count <= bits + 1; count++) {
            SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x << " shifted by " << count);
            ProgramRun run = runProgram(source(
                std::string(cname<T>()) + " x = " + number(x) + "; " + cname<T>() + " l; " + cname<T>() + " r;",
                "l = x << " + number(count) + "; r = x >> " + number(count) + ";"));
            ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
            T left = count >= bits ? 0 : static_cast<T>(static_cast<uint32_t>(x) << count);
            T right;
            if (count >= bits) {
                right = x < 0 ? static_cast<T>(-1) : 0;
            } else {
                right = static_cast<T>(x >> count);
            }
            EXPECT_EQ(left, valueOf<T>(run, "l"));
            EXPECT_EQ(right, valueOf<T>(run, "r"));
        }
    }
}

template <typename T> void checkUnary() {
    for (T x : interestingValues<T>()) {
        SCOPED_TRACE(testing::Message() << cname<T>() << " " << +x);
        ProgramRun run = runProgram(source(
            std::string(cname<T>()) + " x = " + number(x) + "; " + cname<T>() + " a; " + cname<T>() + " b; uint8_t c;",
            "a = ~x; b = -x; c = !x;"));
        ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
        EXPECT_EQ(static_cast<T>(~x), valueOf<T>(run, "a"));
        EXPECT_EQ(static_cast<T>(-x), valueOf<T>(run, "b"));
        EXPECT_EQ(x == 0 ? 1 : 0, run.value("c"));
    }
}

}

// the four types: arithmetic, comparisons, shifts, unary operators

TEST(TypesTest, ArithmeticUint8) { checkArithmetic<uint8_t>(); }
TEST(TypesTest, ArithmeticInt8) { checkArithmetic<int8_t>(); }
TEST(TypesTest, ArithmeticUint16) { checkArithmetic<uint16_t>(); }
TEST(TypesTest, ArithmeticInt16) { checkArithmetic<int16_t>(); }

TEST(TypesTest, ComparisonsUint8) { checkComparisons<uint8_t>(); }
TEST(TypesTest, ComparisonsInt8) { checkComparisons<int8_t>(); }
TEST(TypesTest, ComparisonsUint16) { checkComparisons<uint16_t>(); }
TEST(TypesTest, ComparisonsInt16) { checkComparisons<int16_t>(); }

TEST(TypesTest, ShiftsUint8) { checkShifts<uint8_t>(); }
TEST(TypesTest, ShiftsInt8) { checkShifts<int8_t>(); }
TEST(TypesTest, ShiftsUint16) { checkShifts<uint16_t>(); }
TEST(TypesTest, ShiftsInt16) { checkShifts<int16_t>(); }

TEST(TypesTest, UnaryUint8) { checkUnary<uint8_t>(); }
TEST(TypesTest, UnaryInt8) { checkUnary<int8_t>(); }
TEST(TypesTest, UnaryUint16) { checkUnary<uint16_t>(); }
TEST(TypesTest, UnaryInt16) { checkUnary<int16_t>(); }

// constants and initializers

TEST(TypesTest, GlobalsStartWithTheirInitializer) {
    ProgramRun run = runProgram(source(
        "uint16_t a = 1000; int16_t b = -1000; uint16_t c; int8_t d = -5; uint16_t e = 65535; int16_t f = -32768;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1000, run.value16("a"));
    EXPECT_EQ(static_cast<uint16_t>(-1000), run.value16("b"));
    EXPECT_EQ(0, run.value16("c"));
    EXPECT_EQ(0xfb, run.value("d"));
    EXPECT_EQ(65535, run.value16("e"));
    EXPECT_EQ(0x8000, run.value16("f"));
}

TEST(TypesTest, AssignsConstants) {
    ProgramRun run = runProgram(source("uint16_t a; int16_t b; int8_t c; uint8_t d;",
        "a = 40000; b = -300; c = -128; d = 255;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(40000, run.value16("a"));
    EXPECT_EQ(static_cast<uint16_t>(-300), run.value16("b"));
    EXPECT_EQ(0x80, run.value("c"));
    EXPECT_EQ(255, run.value("d"));
}

TEST(TypesTest, ConstantsAreExact) {
    ProgramRun run = runProgram(source("uint16_t a = 255 + 1; uint16_t b = 100 << 8; int16_t c = -(3 << 12);", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(256, run.value16("a"));
    EXPECT_EQ(25600, run.value16("b"));
    EXPECT_EQ(static_cast<uint16_t>(-(3 << 12)), run.value16("c"));
}

TEST(TypesTest, ConstantThatDoesNotFitIsAnError) {
    const char* programs[] = {
        "uint8_t a = 300; void main(void) { }",
        "int8_t a = -129; void main(void) { }",
        "uint16_t a = 70000; void main(void) { }",
        "int16_t a = -32769; void main(void) { }",
    };
    for (const char* program : programs) {
        CompileResult result = compile(program, "test.c");
        EXPECT_FALSE(result.ok) << program;
    }
}

TEST(TypesTest, ConstantAdoptsTheTypeOfTheOtherOperand) {
    // -3 < 5 is a signed comparison because x is signed; 253 < 5 would be false
    ProgramRun run = runProgram(source("int8_t x = -3; uint8_t less; uint8_t more;",
        "less = x < 5; more = x > -10;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("less"));
    EXPECT_EQ(1, run.value("more"));
}

TEST(TypesTest, ConstantTooBigForTheOtherOperandWidensTheOperation) {
    ProgramRun run = runProgram(source("uint8_t x = 200; uint16_t r; uint8_t s;", "r = x + 300; s = x + 100;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(500, run.value16("r"));
    EXPECT_EQ(44, run.value("s"));      // 8 bit stays 8 bit
}

// mixing sizes and signs

TEST(TypesTest, WideningAnUnsignedValueAddsZeros) {
    ProgramRun run = runProgram(source("uint8_t b = 200; uint16_t w = 1000; uint16_t r1; uint16_t r2; uint16_t r3;",
        "r1 = w + b; r2 = b + w; r3 = b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1200, run.value16("r1"));
    EXPECT_EQ(1200, run.value16("r2"));
    EXPECT_EQ(200, run.value16("r3"));
}

TEST(TypesTest, WideningASignedValueKeepsTheSign) {
    ProgramRun run = runProgram(source("int8_t b = -2; int16_t w = 1000; int16_t r1; int16_t r2; int16_t r3; uint16_t r4;",
        "r1 = w + b; r2 = b - w; r3 = b; r4 = b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(998, run.value16("r1"));
    EXPECT_EQ(static_cast<uint16_t>(-1002), run.value16("r2"));
    EXPECT_EQ(static_cast<uint16_t>(-2), run.value16("r3"));
    EXPECT_EQ(0xfffe, run.value16("r4"));
}

TEST(TypesTest, NarrowingKeepsTheLowByte) {
    ProgramRun run = runProgram(source("uint16_t w = 0x1234; int16_t n = -2; uint8_t a; int8_t b;", "a = w; b = n;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0x34, run.value("a"));
    EXPECT_EQ(0xfe, run.value("b"));
}

TEST(TypesTest, SameSizeDifferentSignIsCompareddUnsigned) {
    // like in C: -1 against an unsigned value of the same size is the large number
    ProgramRun run = runProgram(source("int8_t s = -1; uint8_t u = 1; uint8_t r; int16_t s16 = -1; uint16_t u16 = 1; uint8_t r16;",
        "r = s < u; r16 = s16 < u16;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0, run.value("r"));
    EXPECT_EQ(0, run.value("r16"));
}

TEST(TypesTest, SmallerSignedAgainstLargerUnsignedIsExtendedFirst) {
    ProgramRun run = runProgram(source("int8_t s = -1; uint16_t w = 5; uint16_t sum; uint8_t less;",
        "sum = s + w; less = s < w;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4, run.value16("sum"));   // 0xFFFF + 5
    EXPECT_EQ(0, run.value("less"));    // 0xFFFF < 5 is false
}

TEST(TypesTest, ArithmeticInEightBitsWrapsEvenWhenTheResultIsWider) {
    ProgramRun run = runProgram(source("uint8_t a = 200; uint8_t b = 100; uint16_t r;", "r = a + b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(44, run.value16("r"));
    run = runProgram(source("uint8_t a = 200; uint8_t b = 100; uint16_t r;", "r = (uint16_t)a + b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(300, run.value16("r"));
}

// casts

TEST(TypesTest, Casts) {
    ProgramRun run = runProgram(source(
        "uint16_t w = 0xabcd; uint8_t u = 200; int8_t s = -3; uint8_t r1; uint16_t r2; int8_t r3; int16_t r4; uint16_t r5; uint8_t r6;",
        "r1 = (uint8_t)w; r2 = (uint16_t)u; r3 = (int8_t)u; r4 = (int16_t)s; r5 = (uint16_t)s; r6 = (uint8_t)(w >> 8);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0xcd, run.value("r1"));
    EXPECT_EQ(200, run.value16("r2"));
    EXPECT_EQ(200, run.value("r3"));
    EXPECT_EQ(static_cast<uint16_t>(-3), run.value16("r4"));
    EXPECT_EQ(0xfffd, run.value16("r5"));
    EXPECT_EQ(0xab, run.value("r6"));
}

TEST(TypesTest, CastOfAConstantIsAConstant) {
    ProgramRun run = runProgram(source("uint8_t a = (uint8_t)300; uint8_t b = (uint8_t)-1; int8_t c = (int8_t)200; uint16_t d = (uint16_t)-1;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(44, run.value("a"));
    EXPECT_EQ(255, run.value("b"));
    EXPECT_EQ(200, run.value("c"));
    EXPECT_EQ(0xffff, run.value16("d"));
}

TEST(TypesTest, CastGivesTheTypeToTheOperation) {
    ProgramRun run = runProgram(source("int8_t s = -1; uint8_t u = 1; uint8_t less;", "less = (int8_t)u > s;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("less"));
}

// ++, --, compound assignment

TEST(TypesTest, IncrementAndDecrementCarryIntoTheHighByte) {
    ProgramRun run = runProgram(source(
        "uint16_t a = 255; uint16_t b = 256; uint16_t c = 0; int16_t d = -1; uint16_t e = 65535; int8_t f = 127;",
        "a++; b--; c--; d++; e++; f++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(256, run.value16("a"));
    EXPECT_EQ(255, run.value16("b"));
    EXPECT_EQ(65535, run.value16("c"));
    EXPECT_EQ(0, run.value16("d"));
    EXPECT_EQ(0, run.value16("e"));
    EXPECT_EQ(0x80, run.value("f"));
}

TEST(TypesTest, CompoundAssignmentsOnWideVariables) {
    ProgramRun run = runProgram(source(
        "uint16_t a = 1000; uint16_t b = 1000; uint16_t c = 0xff0f; uint16_t d = 0x0f0f; uint16_t e = 0x1234; uint16_t f = 3; uint16_t g = 40000; uint8_t k = 7;",
        "a += 500; b -= 1001; c &= 0x0ff0; d |= 0xf000; e ^= 0xffff; f <<= 9; g >>= 3; a += k;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1507, run.value16("a"));
    EXPECT_EQ(static_cast<uint16_t>(1000 - 1001), run.value16("b"));
    EXPECT_EQ(0x0f00, run.value16("c"));
    EXPECT_EQ(0xff0f, run.value16("d"));
    EXPECT_EQ(static_cast<uint16_t>(0x1234 ^ 0xffff), run.value16("e"));
    EXPECT_EQ(3 << 9, run.value16("f"));
    EXPECT_EQ(40000 >> 3, run.value16("g"));
}

TEST(TypesTest, CompoundAssignmentOnASmallVariableWithAWideValue) {
    ProgramRun run = runProgram(source("uint8_t a = 10; uint16_t w = 0x0105;", "a += w;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15, run.value("a"));
}

// conditions

TEST(TypesTest, WideValueIsTrueIfAnyByteIsNotZero) {
    ProgramRun run = runProgram(source(
        "uint16_t onlyHigh = 256; uint16_t onlyLow = 3; uint16_t zero = 0; uint8_t a; uint8_t b; uint8_t c; uint8_t d; uint8_t e;",
        "if (onlyHigh) a = 1; if (onlyLow) b = 1; if (zero) c = 1; d = !onlyHigh; e = onlyHigh && onlyLow;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(1, run.value("b"));
    EXPECT_EQ(0, run.value("c"));
    EXPECT_EQ(0, run.value("d"));
    EXPECT_EQ(1, run.value("e"));
}

TEST(TypesTest, SixteenBitLoopCounter) {
    ProgramRun run = runProgram(source("uint16_t sum; uint16_t i;", "for (i = 0; i < 1000; i++) sum += i;"), 1000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(1000, run.value16("i"));
    EXPECT_EQ((999 * 1000 / 2) & 0xffff, run.value16("sum"));
}

TEST(TypesTest, SignedLoopCountingDownThroughZero) {
    ProgramRun run = runProgram(source("int16_t i; uint8_t n;", "for (i = 5; i >= -5; i--) n++;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, run.value("n"));
    EXPECT_EQ(static_cast<uint16_t>(-6), run.value16("i"));
}

TEST(TypesTest, NestedExpressionsOfWideValues) {
    ProgramRun run = runProgram(source(
        "uint16_t a = 1234; uint16_t b = 4321; uint16_t c = 0x00ff; uint16_t d = 0x0f0f; uint16_t r1; uint16_t r2; int16_t r3;",
        "r1 = (a + b) - (c & d); r2 = ((a ^ b) + (c | d)) - (a - b); r3 = (int16_t)(a - b) + (int16_t)(c - d);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    uint16_t a = 1234, b = 4321, c = 0x00ff, d = 0x0f0f;
    EXPECT_EQ(static_cast<uint16_t>((a + b) - (c & d)), run.value16("r1"));
    EXPECT_EQ(static_cast<uint16_t>(((a ^ b) + (c | d)) - (a - b)), run.value16("r2"));
    EXPECT_EQ(static_cast<uint16_t>(static_cast<int16_t>(a - b) + static_cast<int16_t>(c - d)), run.value16("r3"));
}

// functions

TEST(TypesTest, WideParametersAndResults) {
    ProgramRun run = runProgram(source(
        "uint16_t sum; uint16_t add(uint16_t a, uint16_t b) { return a + b; }",
        "sum = add(40000, 25000);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(65000), run.value16("sum"));
}

TEST(TypesTest, ArgumentsAreConvertedToTheParameterTypes) {
    ProgramRun run = runProgram(source(
        "uint8_t small = 200; int8_t negative = -2; int16_t r1; int16_t r2; uint8_t r3;"
        "int16_t twice(int16_t v) { return v + v; } uint8_t low(uint16_t v) { return v; }",
        "r1 = twice(negative); r2 = twice(small); r3 = low(0x1234);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(-4), run.value16("r1"));
    EXPECT_EQ(400, run.value16("r2"));
    EXPECT_EQ(0x34, run.value("r3"));
}

TEST(TypesTest, ResultIsConvertedToTheReturnType) {
    ProgramRun run = runProgram(source(
        "int16_t r1; uint8_t r2; int16_t widen(int8_t v) { return v; } uint8_t narrow(uint16_t v) { return v >> 4; }",
        "r1 = widen(-7); r2 = narrow(0x1abc);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(-7), run.value16("r1"));
    EXPECT_EQ(0xab, run.value("r2"));
}

TEST(TypesTest, SameWideFunctionInTwoArguments) {
    ProgramRun run = runProgram(source(
        "uint16_t r; uint16_t sub(uint16_t a, uint16_t b) { return a - b; }",
        "r = sub(sub(3000, 5), sub(900, 2));"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ((3000 - 5) - (900 - 2), run.value16("r"));
}

TEST(TypesTest, WideResultInACondition) {
    ProgramRun run = runProgram(source(
        "uint8_t a; uint8_t b; uint16_t big(void) { return 300; }",
        "if (big() > 299) a = 1; if (big() > 300) b = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(0, run.value("b"));
}

TEST(TypesTest, WideStaticAndLocalVariables) {
    ProgramRun run = runProgram(source(
        "uint16_t r; uint16_t counter(void) { static uint16_t n = 250; n += 3; return n; }"
        "uint16_t twice(uint16_t v) { uint16_t t = v; t += v; return t; }",
        "counter(); counter(); r = twice(counter());"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2 * (250 + 9), run.value16("r"));
}

TEST(TypesTest, WideConstants) {
    ProgramRun run = runProgram(source("const uint16_t LIMIT = 1000; const int16_t LOW = -100; uint16_t r; int16_t s;",
        "r = LIMIT + 1; s = LOW - 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1001, run.value16("r"));
    EXPECT_EQ(static_cast<uint16_t>(-101), run.value16("s"));
}

TEST(TypesTest, WideVariablesNeedTwoBytes) {
    std::string globals;
    for (int i = 0; i < 59; i++) {
        globals += "uint16_t w" + number(i) + " = " + number(i * 300) + ";\n";
    }
    ProgramRun run = runProgram(source(globals, ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 59; i++) {
        EXPECT_EQ(i * 300, run.value16("w" + number(i))) << i;
    }
    globals += "uint16_t one_too_many;";
    EXPECT_FALSE(compile(source(globals, ""), "test.c").ok);
}
