#include <gtest/gtest.h>
#include <string>
#include "run_helper.h"

namespace {

std::string number(int value) {
    return std::to_string(value);
}

std::string source(const std::string& declarations, const std::string& mainBody) {
    return "#include <stdint.h>\n" + declarations + "\nvoid main(void) {\n" + mainBody + "\n}\n";
}

// "uint8_t g0 = 3; uint8_t g1 = 4; ..." the value of gN is N + 3
std::string globalsWithValues(int count) {
    std::string text;
    for (int i = 0; i < count; i++) {
        text += "uint8_t g" + number(i) + " = " + number(i + 3) + ";\n";
    }
    return text;
}

int countOf(const std::string& text, const std::string& part) {
    int count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        count++;
    }
    return count;
}

}

TEST(RamTest, ManyGlobalsKeepTheirValues) {
    ProgramRun run = runProgram(source(globalsWithValues(110), ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    for (int i = 0; i < 110; i++) {
        EXPECT_EQ(i + 3, run.value("g" + number(i))) << "g" << i;
    }
}

TEST(RamTest, EveryVariableHasItsOwnAddress) {
    ProgramRun run = runProgram(source(globalsWithValues(110), ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    std::map<int, int> users;
    for (const auto& variable : run.compiled.variables) {
        users[variable.second]++;
    }
    EXPECT_EQ(110u, users.size());
}

TEST(RamTest, AddressesAreRamAndNotSpecialRegisters) {
    ProgramRun run = runProgram(source(globalsWithValues(118), ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    for (const auto& variable : run.compiled.variables) {
        int address = variable.second;
        bool direct = address >= 0x0a && address <= 0x0f;
        bool banked = address >= 0x10 && address <= 0x7f;
        EXPECT_TRUE(direct || banked) << variable.first << " at " << address;
    }
}

TEST(RamTest, ComputesWithVariablesOfAllBanks) {
    std::string body = "uint8_t sum = 0;\n";
    int expected = 0;
    for (int i = 0; i < 110; i++) {
        body += "sum += g" + number(i) + ";\n";
        expected += i + 3;
    }
    body += "total = sum;";
    ProgramRun run = runProgram(source(globalsWithValues(110) + "uint8_t total;", body));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(expected & 0xff, run.value("total"));
}

TEST(RamTest, CopiesBetweenBanks) {
    ProgramRun run = runProgram(source(globalsWithValues(110), "g100 = g7; g8 = g109; g50 = g60 + g70;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(10, run.value("g100"));
    EXPECT_EQ(112, run.value("g8"));
    EXPECT_EQ(63 + 73, run.value("g50"));
}

TEST(RamTest, LoopTouchingManyBanks) {
    std::string globals;
    std::string body = "for (uint8_t i = 0; i < 5; i++) {\n";
    for (int v = 0; v < 40; v++) {
        globals += "uint8_t v" + number(v) + ";\n";
        body += "v" + number(v) + " += " + number(v + 1) + ";\n";
    }
    body += "}\n";
    ProgramRun run = runProgram(source(globals, body));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_TRUE(run.halted);
    for (int v = 0; v < 40; v++) {
        EXPECT_EQ(5 * (v + 1), run.value("v" + number(v))) << "v" << v;
    }
}

TEST(RamTest, FunctionsWithLocalsInManyBanks) {
    std::string globals = globalsWithValues(60);
    std::string functions =
        "uint8_t mix(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {\n"
        "  uint8_t x = a + b; uint8_t y = c + d; uint8_t z = x + y;\n"
        "  g59 = z; return z + g58; }\n"
        "uint8_t twice(uint8_t v) { uint8_t t = mix(v, v, 1, 2); return t + t; }\n";
    ProgramRun run = runProgram(source(globals + functions,
        "uint8_t r = 0; for (uint8_t i = 0; i < 3; i++) r += twice(i + g1);\n"
        "g0 = r;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    int expected = 0;
    for (int i = 0; i < 3; i++) {
        int v = i + 4;
        int z = v + v + 1 + 2;
        int t = z + 61;
        expected += t + t;
    }
    EXPECT_EQ(expected & 0xff, run.value("g0"));
    EXPECT_EQ(6 + 6 + 1 + 2, run.value("g59"));        // z of the last call: i = 2, v = 2 + g1 = 6
}

TEST(RamTest, ExactlyAsMuchRamAsThereIs) {
    ProgramRun run = runProgram(source(globalsWithValues(118), ""));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3 + 117, run.value("g117"));
}

TEST(RamTest, OneByteTooMuchSaysHowMuch) {
    CompileResult result = compile(source(globalsWithValues(119), ""), "test.c");
    ASSERT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("119")) << result.error;
    EXPECT_NE(std::string::npos, result.error.find("118")) << result.error;
}

TEST(RamTest, TheMostUsedVariablesGetTheDirectRegisters) {
    // 30 variables, the loop counter and the sum are used far more than the others
    std::string globals;
    std::string body;
    for (int v = 0; v < 30; v++) {
        globals += "uint8_t other" + number(v) + ";\n";
        body += "other" + number(v) + " = 1;\n";
    }
    globals += "uint8_t counter; uint8_t sum;\n";
    body += "for (counter = 0; counter < 20; counter++) { sum += counter; sum += 1; }\n";
    ProgramRun run = runProgram(source(globals, body));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_LT(run.compiled.variables["counter"], 0x10);
    EXPECT_LT(run.compiled.variables["sum"], 0x10);
    EXPECT_GE(run.compiled.variables["other0"], 0x0a);
    EXPECT_EQ(20 * 19 / 2 + 20, run.value("sum"));
}

TEST(RamTest, NoBankInstructionWhileTheBankStays) {
    // 30 globals: g0-g5 are direct, g6-g21 in bank 1, g22-g29 in bank 2
    ProgramRun run = runProgram(source(globalsWithValues(30), "g25 = 1; g26 = 2; g27 = 3; g22 = g27;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2, countOf(run.compiled.assembly, "        bank ")) << run.compiled.assembly;
    EXPECT_EQ(0, countOf(run.compiled.assembly, "@bank"));
    EXPECT_EQ(3, run.value("g22"));
}

TEST(RamTest, NoBankInstructionForDirectRegisters) {
    ProgramRun run = runProgram(source("uint8_t a = 1; uint8_t b = 2; uint8_t c;", "c = a + b;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0, countOf(run.compiled.assembly, "        bank ")) << run.compiled.assembly;
    EXPECT_EQ(3, run.value("c"));
}

TEST(RamTest, BankIsSetAgainAfterALabelAndAfterACall) {
    std::string globals = globalsWithValues(40);
    ProgramRun run = runProgram(source(globals +
        "void touch(void) { g30 = 7; g12 = 8; }",
        "g20 = 1;\n"            // bank 1 or 2
        "touch();\n"
        "g20 += 1;\n"           // after the call the bank is unknown
        "if (g20 == 2) g35 = 9; else g35 = 10;\n"   // after the labels again
        "g21 = g35;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(2, run.value("g20"));
    EXPECT_EQ(9, run.value("g35"));
    EXPECT_EQ(9, run.value("g21"));
    EXPECT_EQ(7, run.value("g30"));
    EXPECT_EQ(8, run.value("g12"));
}

// the upper half of the RAM (0x80-0xFF) holds arrays

TEST(RamTest, ArraysLiveInTheUpperHalfOfTheRam) {
    ProgramRun run = runProgram(source("uint8_t big[100]; uint8_t x = 3;", "big[0] = x; big[99] = x + 1;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_GE(run.compiled.variables.at("big"), 0x80);
    EXPECT_EQ(3, run.ram[run.compiled.variables.at("big")]);
    EXPECT_EQ(4, run.ram[run.compiled.variables.at("big") + 99]);
}

TEST(RamTest, ArraysUseAllOfTheUpperHalfButTheLastByte) {
    ProgramRun run = runProgram(source("uint8_t a[127];", "a[0] = 1; a[126] = 2; for (uint8_t i = 1; i < 126; i++) a[i] = i;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(0x80, run.compiled.variables.at("a"));
    EXPECT_EQ(1, run.ram[0x80]);
    EXPECT_EQ(2, run.ram[0xfe]);
    EXPECT_EQ(64, run.ram[0x80 + 64]);
    EXPECT_EQ(0, run.ram[0xff]);
}

TEST(RamTest, TheAddressBehindTheEndOfAnArrayFitsIntoAPointer) {
    ProgramRun run = runProgram(source("uint8_t a[5]; uint8_t *p; uint8_t n;", "for (p = a; p < a + 5; p++) { *p = 1; n++; }"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(5, run.value("n"));
    EXPECT_LE(run.compiled.variables.at("a") + 5, 0xff);
}

TEST(RamTest, ArraysBeyondTheUpperHalfContinueBelow) {
    // 200 bytes: the array reaches from the top of the RAM down into the lower half
    ProgramRun run = runProgram(source("uint8_t a[200]; uint8_t b[10];", "a[199] = 7; a[0] = 8; b[9] = 9; a[127] = 5; a[128] = 6;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(7, run.ram[run.compiled.variables.at("a") + 199]);
    EXPECT_EQ(8, run.ram[run.compiled.variables.at("a")]);
    EXPECT_EQ(9, run.ram[run.compiled.variables.at("b") + 9]);
    EXPECT_EQ(5, run.ram[run.compiled.variables.at("a") + 127]);
    EXPECT_EQ(6, run.ram[run.compiled.variables.at("a") + 128]);
}

TEST(RamTest, ScalarsKeepWorkingNextToArraysInTheUpperHalf) {
    // after a pointer in the upper half FSR has bit 7 set: the banks of the variables must not be confused
    std::string globals;
    for (int i = 0; i < 30; i++) {
        globals += "uint8_t g" + number(i) + " = " + number(i + 1) + ";\n";
    }
    ProgramRun run = runProgram(source(globals + "uint8_t a[100]; uint8_t sum;",
        "for (uint8_t i = 0; i < 100; i++) { a[i] = i + g20; sum += a[i]; g25 += g1; g10 = g15 + a[i]; }"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += i + 21;
    }
    EXPECT_EQ(sum & 0xff, run.value("sum"));
    EXPECT_EQ((26 + 100 * 2) & 0xff, run.value("g25"));
    EXPECT_EQ((16 + 99 + 21) & 0xff, run.value("g10"));
    EXPECT_EQ(120, run.ram[run.compiled.variables.at("a") + 99]);
}

TEST(RamTest, WordsAndStructsInTheUpperHalf) {
    ProgramRun run = runProgram(source("struct S { uint8_t x; uint16_t y; }; struct S s[20]; uint16_t w[30]; uint16_t total;",
        "for (uint8_t i = 0; i < 20; i++) { s[i].x = i; s[i].y = (uint16_t)i * 100; } for (uint8_t i = 0; i < 30; i++) w[i] = i + 1000;"
        "total = s[19].y + w[29];"), 3000000);
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(1900 + 1029, run.value16("total"));
}

TEST(RamTest, StructVariablesWithDirectMembersStayInTheLowerHalf) {
    ProgramRun run = runProgram(source("struct P { uint8_t x; uint16_t w; }; struct P p; uint8_t big[100];", "p.x = 3; p.w = 500; big[5] = p.x;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_LT(run.compiled.variables.at("p"), 0x80);
    EXPECT_GE(run.compiled.variables.at("big"), 0x80);
    EXPECT_EQ(3, run.ram[run.compiled.variables.at("big") + 5]);
}

TEST(RamTest, ScalarsAndArraysTogetherUseTheWholeRam) {
    // 118 bytes of variables, 128 bytes of array
    ProgramRun run = runProgram(source(globalsWithValues(110) + "uint8_t big[127];", "big[126] = g109;"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(3 + 109, run.ram[0xfe]);
}

TEST(RamTest, TooMuchForTheLowerHalfIsReportedWithNumbers) {
    CompileResult result = compile(source(globalsWithValues(119) + "uint8_t big[100];", ""), "test.c");
    ASSERT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("118")) << result.error;
    EXPECT_NE(std::string::npos, result.error.find("lower RAM")) << result.error;
}

TEST(RamTest, AnArrayThatIsLargerThanAllOfTheRam) {
    CompileResult result = compile(source("uint8_t big[250];", ""), "test.c");
    ASSERT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("RAM")) << result.error;
}

TEST(RamTest, NoClearOfFsrBitSevenWithoutTheUpperHalf) {
    ProgramRun run = runProgram(source("uint8_t a[20]; uint8_t x;", "a[3] = 1; x = a[3];"));
    ASSERT_TRUE(run.compiled.ok) << run.compiled.error;
    EXPECT_EQ(std::string::npos, run.compiled.assembly.find("clrb fsr.7\n        @")) ;
    EXPECT_EQ(1, run.value("x"));
}
