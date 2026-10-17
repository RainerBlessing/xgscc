#include <gtest/gtest.h>
#include <cstdint>
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

// the byte at the RAM address of an array element
int byteAt(const ProgramRun& run, const std::string& array, int offset) {
    return run.ram[run.compiled.variables.at(array) + offset];
}

int wordAt(const ProgramRun& run, const std::string& array, int index) {
    return byteAt(run, array, 2 * index) | byteAt(run, array, 2 * index + 1) << 8;
}

}

// arrays

TEST(PointersTest, ArrayWithInitializerList) {
    ProgramRun run = runProgram(source("uint8_t a[5] = { 10, 20, 30, 40, 50 };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(10 * (i + 1), byteAt(run, "a", i)) << i;
    }
}

TEST(PointersTest, ArraySizeFromTheInitializer) {
    ProgramRun run = runProgram(source("uint8_t a[] = { 7, 8, 9 }; uint8_t after = 99;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(9, byteAt(run, "a", 2));
    EXPECT_EQ(99, run.value("after"));
}

TEST(PointersTest, MissingInitializersAreZero) {
    ProgramRun run = runProgram(source("uint8_t a[6] = { 1, 2 };", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, byteAt(run, "a", 0));
    EXPECT_EQ(2, byteAt(run, "a", 1));
    for (int i = 2; i < 6; i++) {
        EXPECT_EQ(0, byteAt(run, "a", i)) << i;
    }
}

TEST(PointersTest, ArrayWithoutInitializerIsZero) {
    ProgramRun run = runProgram(source("uint8_t big[60]; uint8_t marker = 5;", ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 60; i++) {
        EXPECT_EQ(0, byteAt(run, "big", i)) << i;
    }
    EXPECT_EQ(5, run.value("marker"));
}

TEST(PointersTest, ReadElementsWithConstantAndVariableIndex) {
    ProgramRun run = runProgram(source("uint8_t a[5] = { 10, 20, 30, 40, 50 }; uint8_t i = 3; uint8_t x; uint8_t y; uint8_t z;",
        "x = a[0]; y = a[i]; z = a[i + 1] + a[4 - i];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(10, run.value("x"));
    EXPECT_EQ(40, run.value("y"));
    EXPECT_EQ(50 + 20, run.value("z"));
}

TEST(PointersTest, WriteElements) {
    ProgramRun run = runProgram(source("uint8_t a[4]; uint8_t i = 2; uint8_t v = 77;",
        "a[0] = 5; a[i] = v; a[3] = a[0] + a[2];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, byteAt(run, "a", 0));
    EXPECT_EQ(0, byteAt(run, "a", 1));
    EXPECT_EQ(77, byteAt(run, "a", 2));
    EXPECT_EQ(82, byteAt(run, "a", 3));
}

TEST(PointersTest, FillAndSumInLoops) {
    ProgramRun run = runProgram(source("uint8_t a[20]; uint8_t sum;",
        "for (uint8_t i = 0; i < 20; i++) a[i] = i + 1; for (uint8_t i = 0; i < 20; i++) sum += a[i];"), 100000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(210, run.value("sum"));
    EXPECT_EQ(20, byteAt(run, "a", 19));
}

TEST(PointersTest, CopyBetweenArrays) {
    ProgramRun run = runProgram(source("uint8_t from[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; uint8_t to[8];",
        "for (uint8_t i = 0; i < 8; i++) to[i] = from[7 - i];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(8 - i, byteAt(run, "to", i)) << i;
    }
}

TEST(PointersTest, IndexOfAnIndex) {
    ProgramRun run = runProgram(source("uint8_t order[4] = { 3, 0, 2, 1 }; uint8_t names[4] = { 11, 22, 33, 44 }; uint8_t r;",
        "r = names[order[0]] + names[order[3]];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(44 + 22, run.value("r"));
}

TEST(PointersTest, SixteenBitArrays) {
    ProgramRun run = runProgram(source("uint16_t w[4] = { 1000, 2000, 300, 65535 }; int16_t s[2] = { -5, 7 }; uint16_t sum; int16_t d;",
        "for (uint8_t i = 0; i < 4; i++) sum += w[i]; d = s[0] - s[1]; w[1] = w[0] + 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(1000 + 2000 + 300 + 65535), run.value16("sum"));
    EXPECT_EQ(static_cast<uint16_t>(-12), run.value16("d"));
    EXPECT_EQ(1001, wordAt(run, "w", 1));
    EXPECT_EQ(65535, wordAt(run, "w", 3));
    EXPECT_EQ(static_cast<uint16_t>(-5), wordAt(run, "s", 0));
}

TEST(PointersTest, SixteenBitIndex) {
    ProgramRun run = runProgram(source("uint8_t a[4] = { 9, 8, 7, 6 }; uint16_t i = 2; uint8_t r;", "r = a[i];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(7, run.value("r"));
}

TEST(PointersTest, CompoundAssignmentAndStepOnElements) {
    ProgramRun run = runProgram(source(
        "uint8_t a[3] = { 10, 20, 30 }; uint16_t w[2] = { 250, 1 }; int8_t s[1] = { -3 }; uint8_t i = 1;",
        "a[0] += 5; a[i] -= 4; a[2]++; a[2]++; a[0]--; a[i] <<= 1; w[0] += 10; w[1]++; w[0]--; s[0] >>= 1; s[0]--;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(10 + 5 - 1, byteAt(run, "a", 0));
    EXPECT_EQ((20 - 4) << 1, byteAt(run, "a", 1));
    EXPECT_EQ(32, byteAt(run, "a", 2));
    EXPECT_EQ(250 + 10 - 1, wordAt(run, "w", 0));
    EXPECT_EQ(2, wordAt(run, "w", 1));
    EXPECT_EQ(static_cast<uint8_t>(-3 >> 1) - 1, byteAt(run, "s", 0));
}

TEST(PointersTest, ElementsInConditions) {
    ProgramRun run = runProgram(source("uint8_t flags[4] = { 0, 1, 0, 5 }; uint8_t a; uint8_t b; uint8_t c; uint8_t i = 3;",
        "if (flags[1]) a = 1; if (flags[0]) b = 1; if (flags[i] > 4 && !flags[2]) c = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(0, run.value("b"));
    EXPECT_EQ(1, run.value("c"));
}

TEST(PointersTest, LocalArraysAreInitializedOnEveryCall) {
    ProgramRun run = runProgram(source(
        "uint8_t r1; uint8_t r2;"
        "uint8_t bump(void) { uint8_t t[3] = { 5, 6, 7 }; t[1] += 10; return t[1]; }",
        "r1 = bump(); r2 = bump();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(16, run.value("r1"));
    EXPECT_EQ(16, run.value("r2"));
}

TEST(PointersTest, StaticLocalArrayKeepsItsValues) {
    ProgramRun run = runProgram(source(
        "uint8_t r; uint8_t count(void) { static uint8_t hits[2] = { 100, 0 }; hits[1]++; return hits[0] + hits[1]; }",
        "count(); count(); r = count();"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(103, run.value("r"));
}

// pointers

TEST(PointersTest, PointerToAScalar) {
    ProgramRun run = runProgram(source("uint8_t x = 5; uint8_t y; uint8_t *p;", "p = &x; y = *p; *p = 9;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(9, run.value("x"));
    EXPECT_EQ(5, run.value("y"));
    EXPECT_EQ(run.compiled.variables.at("x"), run.value("p"));
}

TEST(PointersTest, PointerToVariablesInAllBanks) {
    std::string globals;
    for (int i = 0; i < 100; i++) {
        globals += "uint8_t v" + number(i) + " = " + number(i + 1) + ";\n";
    }
    globals += "uint8_t *p; uint8_t sum;";
    ProgramRun run = runProgram(source(globals,
        "p = &v0; sum += *p; p = &v50; sum += *p; p = &v99; sum += *p; *p = 200;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1 + 51 + 100, run.value("sum"));
    EXPECT_EQ(200, run.value("v99"));
}

TEST(PointersTest, PointerToAnArrayWalksIt) {
    ProgramRun run = runProgram(source("uint8_t a[5] = { 1, 2, 3, 4, 5 }; uint8_t *p; uint8_t sum; uint8_t last;",
        "p = a; while (p < a + 5) { sum += *p; p++; } last = *(p - 1);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(15, run.value("sum"));
    EXPECT_EQ(5, run.value("last"));
}

TEST(PointersTest, PointerArithmeticScalesBySixteenBitElements) {
    ProgramRun run = runProgram(source("uint16_t w[4] = { 100, 200, 300, 400 }; uint16_t *p; uint16_t a; uint16_t b; uint16_t c;",
        "p = w; a = *p; p++; b = *p; p += 2; c = *p; p--; p = p - 2; w[0] = *p + *(p + 1);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(100, run.value16("a"));
    EXPECT_EQ(200, run.value16("b"));
    EXPECT_EQ(400, run.value16("c"));
    EXPECT_EQ(300, wordAt(run, "w", 0));    // *p is w[0]... p is w + 0 again: 100 + 200
}

TEST(PointersTest, PointerFromAnElementAddress) {
    ProgramRun run = runProgram(source("uint8_t a[6] = { 1, 2, 3, 4, 5, 6 }; uint8_t *p; uint8_t r1; uint8_t r2; uint8_t i = 4;",
        "p = &a[2]; r1 = *p; r2 = *(&a[i]);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("r1"));
    EXPECT_EQ(5, run.value("r2"));
}

TEST(PointersTest, PointerLoopWritesThroughThePointer) {
    ProgramRun run = runProgram(source("uint8_t a[8]; uint8_t *p;", "for (p = a; p < a + 8; p++) *p = 3;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(3, byteAt(run, "a", i)) << i;
    }
}

TEST(PointersTest, StepAndCompoundOnTheTargetOfAPointer) {
    ProgramRun run = runProgram(source("uint8_t x = 10; uint16_t w = 1000; uint8_t *p = &x; uint16_t *q = &w;",
        "(*p)++; *p += 5; (*q)--; *q += 3000;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(16, run.value("x"));
    EXPECT_EQ(999 + 3000, run.value16("w"));
}

TEST(PointersTest, PointerToASixteenBitScalar) {
    ProgramRun run = runProgram(source("uint16_t w = 0x1234; uint16_t r; uint16_t *p;", "p = &w; r = *p; *p = 0xabcd;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0x1234, run.value16("r"));
    EXPECT_EQ(0xabcd, run.value16("w"));
}

TEST(PointersTest, PointerInitializers) {
    ProgramRun run = runProgram(source("uint8_t a[4] = { 1, 2, 3, 4 }; uint8_t x = 7; uint8_t *p = a; uint8_t *q = &x; uint8_t *r = &a[2]; uint8_t s;",
        "s = *p + *q + *r;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1 + 7 + 3, run.value("s"));
}

TEST(PointersTest, NullPointerTest) {
    ProgramRun run = runProgram(source("uint8_t x; uint8_t *p; uint8_t a; uint8_t b;",
        "if (!p) a = 1; p = &x; if (p) b = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(1, run.value("b"));
}

TEST(PointersTest, PointerComparisons) {
    ProgramRun run = runProgram(source("uint8_t a[4]; uint8_t *p = a; uint8_t *q = a; uint8_t r1; uint8_t r2; uint8_t r3;",
        "q = q + 2; r1 = p < q; r2 = p == q; r3 = q != p;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("r1"));
    EXPECT_EQ(0, run.value("r2"));
    EXPECT_EQ(1, run.value("r3"));
}

TEST(PointersTest, AddressAsAnInteger) {
    ProgramRun run = runProgram(source("uint8_t x; uint8_t *p = &x; uint8_t address;", "address = (uint8_t)p;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(run.compiled.variables.at("x"), run.value("address"));
}

TEST(PointersTest, AbsoluteAddress) {
    ProgramRun run = runProgram(source("", "*(uint8_t *)0x70 = 0x5a; *((uint8_t *)0x71) = *(uint8_t *)0x70 + 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0x5a, run.ram[0x70]);
    EXPECT_EQ(0x5b, run.ram[0x71]);
}

// pointers in functions

TEST(PointersTest, SwapThroughPointers) {
    ProgramRun run = runProgram(source("uint8_t a = 3; uint8_t b = 9;"
        "void swap(uint8_t *x, uint8_t *y) { uint8_t t = *x; *x = *y; *y = t; }",
        "swap(&a, &b);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(9, run.value("a"));
    EXPECT_EQ(3, run.value("b"));
}

TEST(PointersTest, ArraysArePassedAsPointers) {
    ProgramRun run = runProgram(source("uint8_t data[6] = { 4, 8, 15, 16, 23, 42 }; uint8_t total; uint8_t biggest;"
        "uint8_t sum(uint8_t *p, uint8_t n) { uint8_t s = 0; for (uint8_t i = 0; i < n; i++) s += p[i]; return s; }"
        "uint8_t maximum(uint8_t *p, uint8_t n) { uint8_t m = *p; for (uint8_t i = 1; i < n; i++) if (p[i] > m) m = p[i]; return m; }",
        "total = sum(data, 6); biggest = maximum(data + 2, 3);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(4 + 8 + 15 + 16 + 23 + 42, run.value("total"));
    EXPECT_EQ(23, run.value("biggest"));
}

TEST(PointersTest, CopyFunction) {
    ProgramRun run = runProgram(source("uint8_t from[5] = { 9, 8, 7, 6, 5 }; uint8_t to[5];"
        "void copy(uint8_t *dst, uint8_t *src, uint8_t n) { while (n) { *dst = *src; dst++; src++; n--; } }",
        "copy(to, from, 5);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(9 - i, byteAt(run, "to", i)) << i;
    }
}

TEST(PointersTest, FunctionReturnsAPointer) {
    ProgramRun run = runProgram(source("uint8_t a[5] = { 3, 9, 4, 9, 1 }; uint8_t r;"
        "uint8_t *find(uint8_t *p, uint8_t n, uint8_t v) { while (n) { if (*p == v) return p; p++; n--; } return 0; }",
        "uint8_t *hit = find(a, 5, 4); r = *hit; if (find(a, 5, 77) == 0) r += 100;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(104, run.value("r"));
}

TEST(PointersTest, SortSixteenBytes) {
    std::string values = "{ 200, 3, 77, 1, 255, 0, 128, 64, 9, 33, 250, 5, 99, 100, 2, 17 }";
    ProgramRun run = runProgram(source("uint8_t a[16] = " + values + ";"
        "void sort(uint8_t *p, uint8_t n) {"
        "  for (uint8_t i = 0; i < n; i++) for (uint8_t j = 1; j < n - i; j++)"
        "    if (p[j - 1] > p[j]) { uint8_t t = p[j]; p[j] = p[j - 1]; p[j - 1] = t; } }",
        "sort(a, 16);"), 2000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    int expected[16] = { 0, 1, 2, 3, 5, 9, 17, 33, 64, 77, 99, 100, 128, 200, 250, 255 };
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(expected[i], byteAt(run, "a", i)) << i;
    }
}

TEST(PointersTest, PointerArgumentsAreHeldWhileACallRuns) {
    ProgramRun run = runProgram(source("uint8_t a[4] = { 5, 6, 7, 8 }; uint8_t r;"
        "uint8_t at(uint8_t *p, uint8_t i) { return p[i]; } uint8_t one(void) { return 1; }",
        "r = at(a, one()) + at(a + 1, one() + 1);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(6 + 8, run.value("r"));
}

// the bank and the pointer

TEST(PointersTest, ScalarsInOtherBanksSurviveIndirectAccess) {
    std::string globals;
    for (int i = 0; i < 40; i++) {
        globals += "uint8_t g" + number(i) + " = " + number(i + 1) + ";\n";
    }
    globals += "uint8_t a[4] = { 10, 20, 30, 40 }; uint8_t r;";
    ProgramRun run = runProgram(source(globals,
        "g30 = a[2]; g31 = g30 + g25; a[1] = g36; r = a[g1] + g20; g22 = a[0]; g23 = g22 + a[3];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(30, run.value("g30"));
    EXPECT_EQ(30 + 26, run.value("g31"));
    EXPECT_EQ(37, byteAt(run, "a", 1));
    EXPECT_EQ(30 + 21, run.value("r"));
    EXPECT_EQ(10, run.value("g22"));
    EXPECT_EQ(50, run.value("g23"));
}

// memory

TEST(PointersTest, ArraysAndVariablesShareTheRam) {
    ProgramRun run = runProgram(source("uint8_t big[100]; uint8_t a = 1; uint8_t b = 2; uint8_t c = 3; uint8_t d = 4; uint8_t e = 5; uint8_t f = 6; uint8_t g = 7; uint8_t h = 8; uint8_t i = 9; uint8_t j = 10;",
        "big[99] = a + j; big[0] = 7;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, byteAt(run, "big", 99));
    EXPECT_EQ(7, byteAt(run, "big", 0));
    EXPECT_EQ(10, run.value("j"));
}

TEST(PointersTest, ArraysDoNotOverlapEachOther) {
    ProgramRun run = runProgram(source("uint8_t a[10]; uint8_t b[10]; uint16_t c[10];",
        "for (uint8_t i = 0; i < 10; i++) { a[i] = i; b[i] = 100 + i; c[i] = 1000 + i; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(i, byteAt(run, "a", i));
        EXPECT_EQ(100 + i, byteAt(run, "b", i));
        EXPECT_EQ(1000 + i, wordAt(run, "c", i));
    }
}

TEST(PointersTest, TooMuchRamForArrays) {
    std::string message = errorOf(source("uint8_t big[113];", ""));
    EXPECT_TRUE(contains(message, "RAM")) << message;
}

TEST(PointersTest, ArraysPlusVariablesTooMuch) {
    std::string globals = "uint8_t big[100];";
    for (int i = 0; i < 19; i++) {
        globals += "uint8_t v" + number(i) + ";";
    }
    std::string message = errorOf(source(globals, ""));
    EXPECT_TRUE(contains(message, "RAM") && contains(message, "118")) << message;
}

TEST(PointersTest, ArraysOfUnusedFunctionsTakeNoRam) {
    ProgramRun run = runProgram(source("void unused(void) { uint8_t huge[100]; huge[0] = 1; } uint8_t x;", "x = 3;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3, run.value("x"));
}

// errors

TEST(PointersErrorTest, ArrayNameCannotBeAssigned) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[2]; uint8_t b[2];", "a = b;")), "array"));
}

TEST(PointersErrorTest, OnlyPointersCanBeDereferenced) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x; uint8_t y;", "y = *x;")), "dereference"));
}

TEST(PointersErrorTest, OnlyArraysAndPointersCanBeIndexed) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x; uint8_t y;", "y = x[1];")), "index"));
}

TEST(PointersErrorTest, TwoDimensionalIndexIsNotSupported) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[4]; uint8_t y;", "y = a[1][2];")), "index"));
}

TEST(PointersErrorTest, PointersToDifferentSizesNeedACast) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[2]; uint16_t *p;", "p = a;")), "pointer"));
}

TEST(PointersErrorTest, CastBetweenPointerTypesIsFine) {
    ProgramRun run = runProgram(source("uint8_t a[2] = { 1, 2 }; uint16_t *p; uint16_t r;", "p = (uint16_t *)a; r = *p;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0x0201, run.value16("r"));
}

TEST(PointersErrorTest, AddressOfAnExpression) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t x; uint8_t *p;", "p = &(x + 1);")), "address"));
}

TEST(PointersErrorTest, AddressOfAConstant) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t k = 3; uint8_t *p;", "p = &k;")), "const"));
}

TEST(PointersErrorTest, PointerPlusPointer) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[2]; uint8_t *p; uint8_t *q;", "p = a + q;")), "pointer"));
}

TEST(PointersErrorTest, DifferenceOfPointers) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[2]; uint8_t *p; uint8_t *q; uint8_t d;", "d = p - q;")), "pointer"));
}

TEST(PointersErrorTest, OtherOperatorsOnPointers) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p; uint8_t x;", "x = p & 3;")), "pointer"));
    EXPECT_TRUE(contains(errorOf(source("uint8_t *p; uint8_t x;", "x = ~p;")), "pointer"));
}

TEST(PointersErrorTest, ConstArrayIsNotSupportedYet) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t table[2] = { 1, 2 };", "")), "const"));
}

TEST(PointersErrorTest, ArrayInitializerMustBeConstant) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t v; uint8_t a[2] = { 1, v };", "")), "constant"));
}

TEST(PointersErrorTest, ArrayInitializerThatDoesNotFit) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t a[2] = { 1, 300 };", "")), "300"));
}
