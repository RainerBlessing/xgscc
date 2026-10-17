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

int programmedWords(const ProgramRun& run) {
    int count = 0;
    for (unsigned short word : run.compiled.rom) {
        if (word != 0xfff) {
            count++;
        }
    }
    return count;
}

// "{ 3, 6, 9, ... }" with the value i * step + start
std::string list(int count, int start, int step) {
    std::string text = "{ ";
    for (int i = 0; i < count; i++) {
        text += number((start + i * step) & 0xff) + (i + 1 < count ? ", " : " ");
    }
    return text + "}";
}

}

TEST(TablesTest, ConstantIndex) {
    ProgramRun run = runProgram(source("const uint8_t t[] = { 10, 20, 30, 40 }; uint8_t a; uint8_t b; uint8_t c;",
        "a = t[0]; b = t[3]; c = t[1] + t[2];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    EXPECT_EQ(10, run.value("a"));
    EXPECT_EQ(40, run.value("b"));
    EXPECT_EQ(50, run.value("c"));
}

TEST(TablesTest, VariableIndex) {
    ProgramRun run = runProgram(source("const uint8_t t[8] = { 5, 10, 15, 20, 25, 30, 35, 40 }; uint8_t sum; uint8_t last;",
        "for (uint8_t i = 0; i < 8; i++) sum += t[i]; uint8_t j = 7; last = t[j];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(180, run.value("sum"));
    EXPECT_EQ(40, run.value("last"));
}

TEST(TablesTest, SixteenBitTable) {
    ProgramRun run = runProgram(source("const uint16_t w[] = { 1000, 65535, 0, 300 }; uint16_t sum; uint16_t second; uint16_t pick; uint8_t i = 3;",
        "for (uint8_t k = 0; k < 4; k++) sum += w[k]; second = w[1]; pick = w[i];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(1000 + 65535 + 300), run.value16("sum"));
    EXPECT_EQ(65535, run.value16("second"));
    EXPECT_EQ(300, run.value16("pick"));
}

TEST(TablesTest, SignedTables) {
    ProgramRun run = runProgram(source("const int8_t s[] = { -1, 5, -128 }; const int16_t d[] = { -1000, 20000 }; int16_t a; int16_t b; int16_t c; int16_t e;",
        "a = s[0]; b = s[2]; c = d[0]; e = d[1] + s[1];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(static_cast<uint16_t>(-1), run.value16("a"));
    EXPECT_EQ(static_cast<uint16_t>(-128), run.value16("b"));
    EXPECT_EQ(static_cast<uint16_t>(-1000), run.value16("c"));
    EXPECT_EQ(20005, run.value16("e"));
}

TEST(TablesTest, SignedCompareOfAnElement) {
    ProgramRun run = runProgram(source("const int8_t s[] = { -3, 4 }; uint8_t a; uint8_t b;", "a = s[0] < 0; b = s[1] < 0;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("a"));
    EXPECT_EQ(0, run.value("b"));
}

TEST(TablesTest, IndexIsAnExpression) {
    ProgramRun run = runProgram(source("const uint8_t t[] = { 1, 2, 4, 8, 16, 32 }; const uint8_t order[] = { 5, 0, 3 }; uint8_t i = 1; uint16_t w = 4; uint8_t r1; uint8_t r2; uint8_t r3;",
        "r1 = t[i + 2]; r2 = t[w]; r3 = t[order[i + 1]];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(8, run.value("r1"));
    EXPECT_EQ(16, run.value("r2"));
    EXPECT_EQ(8, run.value("r3"));
}

TEST(TablesTest, ElementInConditions) {
    ProgramRun run = runProgram(source("const uint8_t t[] = { 0, 5, 50 }; uint8_t a; uint8_t b; uint8_t c; uint8_t i = 2;",
        "if (t[0]) a = 1; if (t[1] > 4) b = 1; if (t[i] > 40 && !t[0]) c = 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0, run.value("a"));
    EXPECT_EQ(1, run.value("b"));
    EXPECT_EQ(1, run.value("c"));
}

TEST(TablesTest, TableValueIndexesARamArray) {
    ProgramRun run = runProgram(source("const uint8_t where[] = { 3, 1, 2 }; uint8_t a[4];",
        "for (uint8_t i = 0; i < 3; i++) a[where[i]] = i + 10;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(11, run.ram[run.compiled.variables.at("a") + 1]);
    EXPECT_EQ(12, run.ram[run.compiled.variables.at("a") + 2]);
    EXPECT_EQ(10, run.ram[run.compiled.variables.at("a") + 3]);
}

TEST(TablesTest, VariablesInOtherBanksSurviveTableReads) {
    std::string globals;
    for (int i = 0; i < 40; i++) {
        globals += "uint8_t g" + number(i) + " = " + number(i + 1) + ";\n";
    }
    ProgramRun run = runProgram(source(globals + "const uint8_t t[] = { 100, 101, 102 }; const uint16_t w[] = { 500, 600 };",
        "g30 = t[1]; g31 = g30 + g25; g32 = w[1] + g20; g22 = g21;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(101, run.value("g30"));
    EXPECT_EQ(101 + 26, run.value("g31"));
    EXPECT_EQ((600 + 21) & 0xff, run.value("g32"));
    EXPECT_EQ(22, run.value("g22"));
}

TEST(TablesTest, ReadingATableKeepsTheBitOfMODEThatTheDirectionRegistersUse) {
    ProgramRun run = runProgram(source("const uint8_t t[] = { 1, 2, 3 }; const uint16_t w[] = { 4000, 5 }; uint8_t r; uint16_t v;",
        "r = t[2]; v = w[0];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_NE(0, run.mode & 0x10);
}

// strings

TEST(TablesTest, StringInROM) {
    ProgramRun run = runProgram(source("const uint8_t msg[] = \"Hello\"; uint8_t length; uint8_t sum;",
        "while (msg[length]) { sum += msg[length]; length++; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, run.value("length"));
    EXPECT_EQ(('H' + 'e' + 'l' + 'l' + 'o') & 0xff, run.value("sum"));       // wraps in uint8_t
}

TEST(TablesTest, CopyAStringToRam) {
    ProgramRun run = runProgram(source("const uint8_t msg[] = \"Hi there\"; uint8_t buffer[16];",
        "uint8_t i = 0; while (msg[i]) { buffer[i] = msg[i]; i++; } buffer[i] = 0;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    const char* text = "Hi there";
    for (int i = 0; i <= 8; i++) {
        EXPECT_EQ(static_cast<unsigned char>(text[i]), run.ram[run.compiled.variables.at("buffer") + i]) << i;
    }
}

TEST(TablesTest, StringWithEscapes) {
    ProgramRun run = runProgram(source("const uint8_t msg[] = \"a\\nb\\t\"; uint8_t x; uint8_t y; uint8_t z;", "x = msg[1]; y = msg[3]; z = msg[4];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ('\n', run.value("x"));
    EXPECT_EQ('\t', run.value("y"));
    EXPECT_EQ(0, run.value("z"));
}

TEST(TablesTest, StringFromADefine) {
    ProgramRun run = runProgram("#define NAME \"xy\"\nconst uint8_t n[] = NAME; uint8_t r; void main(void) { r = n[1]; }");
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ('y', run.value("r"));
}

// where the tables are

TEST(TablesTest, TableInAFunction) {
    ProgramRun run = runProgram(source("uint8_t r;"
        "uint8_t square(uint8_t n) { const uint8_t squares[] = { 0, 1, 4, 9, 16, 25 }; return squares[n]; }",
        "r = square(4) + square(5);"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(41, run.value("r"));
}

TEST(TablesTest, TablesOfFunctionsNobodyCallsTakeNoRom) {
    ProgramRun with = runProgram(source("void unused(void) { const uint8_t big[100] = { 1 }; uint8_t x = big[3]; } uint8_t r;", "r = 1;"));
    ProgramRun without = runProgram(source("uint8_t r;", "r = 1;"));
    ASSERT_TRUE(with.compiled.ok) << with.compiled.error;
    EXPECT_EQ(programmedWords(without), programmedWords(with));
}

TEST(TablesTest, TablesTakeRomWordsButNoRam) {
    ProgramRun with = runProgram(source("const uint8_t t[100] = { 1 }; uint8_t r;", "r = t[0];"));
    ProgramRun without = runProgram(source("uint8_t r;", "r = 0;"));
    ASSERT_TRUE(with.compiled.ok) << with.compiled.error;
    EXPECT_GE(programmedWords(with), programmedWords(without) + 100);
}

TEST(TablesTest, ManyTablesOfAllSizesAreFound) {
    // 200 + 100 + 100 + 50 + 256: some of them cannot share a block of 256 words
    std::string tables =
        "const uint8_t a[200] = " + list(200, 1, 1) + ";\n"
        "const uint8_t b[100] = " + list(100, 3, 2) + ";\n"
        "const uint8_t c[100] = " + list(100, 5, 3) + ";\n"
        "const uint8_t d[50] = " + list(50, 7, 5) + ";\n"
        "const uint8_t e[256] = " + list(256, 9, 7) + ";\n"
        "uint8_t r1; uint8_t r2; uint8_t r3; uint8_t r4; uint8_t r5; uint8_t r6; uint8_t r7; uint8_t r8; uint8_t r9; uint8_t r10; uint8_t k = 255;\n";
    ProgramRun run = runProgram(source(tables,
        "r1 = a[0]; r2 = a[199]; r3 = b[0]; r4 = b[99]; r5 = c[0]; r6 = c[99]; r7 = d[0]; r8 = d[49]; r9 = e[0]; r10 = e[k];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1, run.value("r1"));
    EXPECT_EQ((1 + 199) & 0xff, run.value("r2"));
    EXPECT_EQ(3, run.value("r3"));
    EXPECT_EQ((3 + 99 * 2) & 0xff, run.value("r4"));
    EXPECT_EQ(5, run.value("r5"));
    EXPECT_EQ((5 + 99 * 3) & 0xff, run.value("r6"));
    EXPECT_EQ(7, run.value("r7"));
    EXPECT_EQ((7 + 49 * 5) & 0xff, run.value("r8"));
    EXPECT_EQ(9, run.value("r9"));
    EXPECT_EQ((9 + 255 * 7) & 0xff, run.value("r10"));
}

TEST(TablesTest, EveryElementOfALargeSixteenBitTable) {
    std::string text = "{ ";
    for (int i = 0; i < 128; i++) {
        text += number(i * 500 % 65536) + (i < 127 ? ", " : " ");
    }
    text += "}";
    ProgramRun run = runProgram(source("const uint16_t w[128] = " + text + "; uint16_t sum; uint16_t last;",
        "for (uint8_t i = 0; i < 128; i++) sum += w[i]; last = w[127];"), 1000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    int expected = 0;
    for (int i = 0; i < 128; i++) {
        expected += i * 500 % 65536;
    }
    EXPECT_EQ(expected & 0xffff, run.value16("sum"));
    EXPECT_EQ(127 * 500 % 65536, run.value16("last"));
}

TEST(TablesTest, TablesAndCodeCanMeet) {
    std::string body;
    for (int i = 0; i < 1300; i++) {
        body += "x = x + 1;\n";
    }
    std::string message = errorOf(source("const uint8_t t[250] = { 1 }; uint8_t x;", body));
    EXPECT_TRUE(contains(message, "ROM")) << message;
}

// errors

TEST(TablesErrorTest, TableTooLarge) {
    std::string message = errorOf(source("const uint8_t t[257] = { 1 };", ""));
    EXPECT_TRUE(contains(message, "256")) << message;
    EXPECT_TRUE(contains(errorOf(source("const uint16_t t[129] = { 1 };", "")), "256"));
}

TEST(TablesErrorTest, IndexOutsideOfTheTable) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[3] = { 1, 2, 3 }; uint8_t r;", "r = t[3];")), "outside"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[3] = { 1, 2, 3 }; uint8_t r;", "r = t[-1];")), "outside"));
}

TEST(TablesErrorTest, TableNeedsInitialValues) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[3];", "")), "table"));
}

TEST(TablesErrorTest, ValuesMustBeConstantAndFit) {
    EXPECT_TRUE(contains(errorOf(source("uint8_t v; const uint8_t t[2] = { 1, v };", "")), "constant"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 300 };", "")), "300"));
}

TEST(TablesErrorTest, TableCanOnlyBeIndexed) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 }; uint8_t *p;", "p = t;")), "table"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 }; void f(uint8_t *p) { }", "f(t);")), "table"));
}

TEST(TablesErrorTest, TableCannotBeChanged) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 };", "t[0] = 3;")), "table"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 };", "t[1]++;")), "table"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 };", "t[1] += 2;")), "table"));
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 };", "t = 5;")), "table"));
}

TEST(TablesErrorTest, AddressOfAnElementOfATable) {
    EXPECT_TRUE(contains(errorOf(source("const uint8_t t[2] = { 1, 2 }; uint8_t *p;", "p = &t[1];")), "table"));
}
