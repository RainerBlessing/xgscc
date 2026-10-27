#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "asm.h"

namespace {

struct Assembled {
    bool ok;
    std::string error;
    std::vector<unsigned short> rom;
};

Assembled assembleText(const std::string& source) {
    Assembled result;
    result.rom.assign(64, 0xfff);
    result.ok = assembleSource(source, &result.rom[0], 64, &result.error);
    return result;
}

unsigned short opcode(const std::string& line) {
    unsigned short code = 0;
    EXPECT_TRUE(assemble(line, &code)) << line;
    return code;
}

}

TEST(AsmMacroTest, ParametersAreReplacedInTheBody) {
    Assembled r = assembleText(
        "load macro reg, value\n"
        "    mov w, #value\n"
        "    mov reg, w\n"
        "endm\n"
        "    load 10, 5\n"
        "    load 11, 6\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("mov w, #5"), r.rom[0]);
    EXPECT_EQ(opcode("mov 10, w"), r.rom[1]);
    EXPECT_EQ(opcode("mov w, #6"), r.rom[2]);
    EXPECT_EQ(opcode("mov 11, w"), r.rom[3]);
    EXPECT_EQ(0xfff, r.rom[4]);
}

TEST(AsmMacroTest, CallWithParenthesesAndWithoutArgumentsAndInAnyCase) {
    Assembled r = assembleText(
        "Twice MACRO\n"
        "    nop\n"
        "    nop\n"
        "ENDM\n"
        "set macro v\n"
        "    mov w, #v\n"
        "endm\n"
        "    Twice\n"
        "    set(3)\n"
        "    set (4)\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("nop"), r.rom[0]);
    EXPECT_EQ(opcode("nop"), r.rom[1]);
    EXPECT_EQ(opcode("mov w, #3"), r.rom[2]);
    EXPECT_EQ(opcode("mov w, #4"), r.rom[3]);
    EXPECT_EQ(0xfff, r.rom[4]);
}

TEST(AsmMacroTest, ALabelInFrontOfACallMarksTheFirstWord) {
    Assembled r = assembleText(
        "nothing macro\n"
        "endm\n"
        "two macro\n"
        "    nop\n"
        "    nop\n"
        "endm\n"
        "    nop\n"
        "here: two\n"
        "there: nothing\n"
        "    jmp here\n"
        "    jmp there\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("jmp 1"), r.rom[3]);
    EXPECT_EQ(opcode("jmp 3"), r.rom[4]);
}

TEST(AsmMacroTest, LocalLabelsAreDifferentInEveryExpansion) {
    Assembled r = assembleText(
        "wait macro n\n"
        "    local again\n"
        "    mov w, #n\n"
        "again: decsz 10\n"
        "    jmp again\n"
        "endm\n"
        "    wait 2\n"
        "    wait 3\n");
    ASSERT_TRUE(r.ok) << r.error;
    // the jumps go to the decsz of their own expansion
    EXPECT_EQ(opcode("jmp 1"), r.rom[2]);
    EXPECT_EQ(opcode("jmp 4"), r.rom[5]);
}

TEST(AsmMacroTest, WithoutLocalALabelInTheBodyIsDefinedTwice) {
    Assembled r = assembleText(
        "wait macro\n"
        "again: jmp again\n"
        "endm\n"
        "    wait\n"
        "    wait\n");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(std::string::npos, r.error.find("again is defined twice")) << r.error;
    EXPECT_EQ(0u, r.error.find("line 5:")) << r.error;
}

TEST(AsmMacroTest, MacrosCallMacros) {
    Assembled r = assembleText(
        "one macro v\n"
        "    mov w, #v\n"
        "endm\n"
        "two macro a, b\n"
        "    one a\n"
        "    one(b)\n"
        "endm\n"
        "    two 1, 2\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("mov w, #1"), r.rom[0]);
    EXPECT_EQ(opcode("mov w, #2"), r.rom[1]);
    EXPECT_EQ(0xfff, r.rom[2]);
}

TEST(AsmMacroTest, AnArgumentIsNotExpandedAgain) {
    // b is the name of a parameter of the inner macro, but the argument is only text
    Assembled r = assembleText(
        "inner macro a, b\n"
        "    mov w, #a\n"
        "endm\n"
        "outer macro b\n"
        "    inner b, 7\n"
        "endm\n"
        "    outer 5\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("mov w, #5"), r.rom[0]);
}

TEST(AsmMacroTest, ArgumentsCanBeNamesOfEqu) {
    Assembled r = assembleText(
        "counter equ 10\n"
        "clear macro reg\n"
        "    clr reg\n"
        "endm\n"
        "    clear counter\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("clr 10"), r.rom[0]);
}

TEST(AsmMacroTest, ARecursiveMacroIsAnErrorNotAHang) {
    Assembled r = assembleText(
        "loop macro\n"
        "    loop\n"
        "endm\n"
        "    loop\n");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(std::string::npos, r.error.find("nested too deeply")) << r.error;
}

TEST(AsmMacroTest, ExponentialExpansionIsStopped) {
    Assembled r = assembleText(
        "twice macro\n"
        "    twice2\n"
        "    twice2\n"
        "endm\n"
        "twice2 macro\n"
        "    twice\n"
        "    twice\n"
        "endm\n"
        "    twice\n");
    EXPECT_FALSE(r.ok);
}

TEST(AsmMacroTest, MistakesInDefinitionsAndCalls) {
    struct Case { const char* source; const char* message; };
    const Case cases[] = {
        { "mac macro a\n nop\nendm\n mac\n", "line 4: macro mac takes 1 arguments, not 0" },
        { "mac macro\n nop\nendm\n mac 1\n", "line 4: macro mac takes 0 arguments, not 1" },
        { "mac macro a\n nop\nendm\n mac 1,\n", "line 4: empty argument in the call of macro mac" },
        { "mac macro a\n nop\nendm\n mac(1\n", "line 4: missing ) after the arguments in the call of macro mac" },
        { "mac macro\n nop\n", "line 1: macro mac has no endm" },
        { "endm\n", "line 1: endm outside of a macro" },
        { " local x\n", "line 1: local outside of a macro" },
        { "macro\n", "line 1: macro needs a name in front of it" },
        { "mac macro\nendm\nmac macro\nendm\n", "line 3: macro mac is defined twice" },
        { "mov macro\nendm\n", "line 1: bad macro name 'mov'" },
        { "org macro\nendm\n", "line 1: bad macro name 'org'" },
        { "mac macro a, a\nendm\n", "line 1: a is used twice in the parameters of macro mac" },
        { "mac macro w\nendm\n", "line 1: bad name 'w' in the parameters of macro mac" },
        { "mac macro a\n local a\nendm\n", "line 2: a is used twice" },
        { "mac macro\nn macro\nendm\nendm\n", "line 2: a macro cannot be defined inside of a macro" },
        { "mac macro\nendm x\n", "line 2: endm takes no operands" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Assembled r = assembleText(cases[i].source);
        EXPECT_FALSE(r.ok) << cases[i].source;
        EXPECT_EQ(std::string(cases[i].message), r.error) << cases[i].source;
    }
}

TEST(AsmMacroTest, ErrorsInAnExpansionNameTheLineOfTheCall) {
    Assembled r = assembleText(
        "bad macro\n"
        "    mov w, #nothing_here\n"
        "endm\n"
        "    nop\n"
        "    bad\n");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(0u, r.error.find("line 5: ")) << r.error;
}

TEST(AsmMacroTest, LineNumbersAfterAMacroDefinitionStayRight) {
    Assembled r = assembleText(
        "mac macro\n"
        "    nop\n"
        "endm\n"
        "    nop\n"
        "    bogus\n");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(0u, r.error.find("line 5: ")) << r.error;
}
