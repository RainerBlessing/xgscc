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
