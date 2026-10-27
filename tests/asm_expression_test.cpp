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

Assembled assembleText(const std::string& source, int words = 0x400) {
    Assembled result;
    result.rom.assign(words, 0xfff);
    result.ok = assembleSource(source, &result.rom[0], words, &result.error);
    return result;
}

unsigned short opcode(const std::string& line) {
    unsigned short code = 0;
    EXPECT_TRUE(assemble(line, &code)) << line;
    return code;
}

// the opcode of the one instruction, assembled at address 0
unsigned short assembled(const std::string& instruction) {
    Assembled r = assembleText(instruction + "\n");
    EXPECT_TRUE(r.ok) << instruction << ": " << r.error;
    return r.rom[0];
}

std::string failure(const std::string& source) {
    Assembled r = assembleText(source);
    EXPECT_FALSE(r.ok) << source;
    return r.error;
}

}

TEST(AsmExpressionTest, OperatorsHaveThePrecedenceOfC) {
    EXPECT_EQ(opcode("mov w, #7"), assembled("mov w, #1+2*3"));
    EXPECT_EQ(opcode("mov w, #9"), assembled("mov w, #(1+2)*3"));
    EXPECT_EQ(opcode("mov w, #a"), assembled("mov w, #14/2"));
    EXPECT_EQ(opcode("mov w, #4"), assembled("mov w, #a-3-3"));
    EXPECT_EQ(opcode("mov w, #28"), assembled("mov w, #5<<3"));
    EXPECT_EQ(opcode("mov w, #5"), assembled("mov w, #a>>1"));
    EXPECT_EQ(opcode("mov w, #3"), assembled("mov w, #f&7&3"));
    EXPECT_EQ(opcode("mov w, #f"), assembled("mov w, #c|3"));
    EXPECT_EQ(opcode("mov w, #6"), assembled("mov w, #5^3"));
    // + binds tighter than <<, << tighter than &, & tighter than ^, ^ tighter than |
    EXPECT_EQ(opcode("mov w, #10"), assembled("mov w, #1<<1+3"));
    EXPECT_EQ(opcode("mov w, #1"), assembled("mov w, #3&1<<0"));
    EXPECT_EQ(opcode("mov w, #7"), assembled("mov w, #4|2^1"));
}

TEST(AsmExpressionTest, UnaryOperatorsAndANegativeImmediate) {
    EXPECT_EQ(opcode("retw #ff"), assembled("retw #-1"));
    EXPECT_EQ(opcode("and w, #f7"), assembled("and w, #~(1<<3)"));
    EXPECT_EQ(opcode("mov w, #5"), assembled("mov w, #-(-5)"));
    EXPECT_EQ(opcode("mov w, #5"), assembled("mov w, #+5"));
}

TEST(AsmExpressionTest, NumbersInEveryBase) {
    EXPECT_EQ(opcode("mov w, #a"), assembled("mov w, #d'10'"));
    EXPECT_EQ(opcode("mov w, #a"), assembled("mov w, #D'10'"));
    EXPECT_EQ(opcode("mov w, #5"), assembled("mov w, #b'101'"));
    EXPECT_EQ(opcode("mov w, #10"), assembled("mov w, #0x10"));
    EXPECT_EQ(opcode("mov w, #ff"), assembled("mov w, #d'255'"));
    // 0b1 is still a hexadecimal number
    EXPECT_EQ(opcode("mov w, #b1"), assembled("mov w, #0b1"));
}

TEST(AsmExpressionTest, NamesTakePartInExpressions) {
    Assembled r = assembleText(
        "size equ 4\n"
        "mask equ size*2-1\n"
        "    mov w, #mask\n"
        "    mov w, #later+1\n"
        "    mov w, #(size<<2)|1\n"
        "later equ 20\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("mov w, #7"), r.rom[0]);
    EXPECT_EQ(opcode("mov w, #21"), r.rom[1]);
    EXPECT_EQ(opcode("mov w, #11"), r.rom[2]);
}

TEST(AsmExpressionTest, LabelsAndTheCurrentAddress) {
    Assembled r = assembleText(
        "    nop\n"
        "loop: nop\n"
        "    nop\n"
        "    jmp loop+1\n"
        "    jmp $\n"
        "    jmp $+2\n"
        "    jmp ($-1)&ff\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("jmp 2"), r.rom[3]);
    EXPECT_EQ(opcode("jmp 4"), r.rom[4]);
    EXPECT_EQ(opcode("jmp 7"), r.rom[5]);
    EXPECT_EQ(opcode("jmp 5"), r.rom[6]);
}

TEST(AsmExpressionTest, DollarInEquOrgAndDw) {
    Assembled r = assembleText(
        "    org 10\n"
        "here equ $\n"
        "    dw 1+1, $, b'11'\n"
        "    org here+8\n"
        "    dw $\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(2, r.rom[0x10]);
    EXPECT_EQ(0x11, r.rom[0x11]);
    EXPECT_EQ(3, r.rom[0x12]);
    EXPECT_EQ(0x18, r.rom[0x18]);
}

TEST(AsmExpressionTest, TheSyntaxOfTheOperandsStays) {
    EXPECT_EQ(opcode("mov w, /6"), assembled("mov w, /(5+1)"));
    EXPECT_EQ(opcode("mov w, ++6"), assembled("mov w, ++5+1"));
    EXPECT_EQ(opcode("mov w, --6"), assembled("mov w, -- 5+1"));
    EXPECT_EQ(opcode("mov w, <<6"), assembled("mov w, <<5+1"));
    EXPECT_EQ(opcode("mov w, >>6"), assembled("mov w, >>(5+1)"));
    EXPECT_EQ(opcode("mov w, 6-w"), assembled("mov w, 5+1-w"));
    EXPECT_EQ(opcode("mov w, 6-w"), assembled("mov w, (5+1) - W"));
    EXPECT_EQ(opcode("mov 6, w"), assembled("mov 5+1, w"));
    EXPECT_EQ(opcode("mov !ra, w"), assembled("mov !ra, w"));
    EXPECT_EQ(opcode("mov m, #3"), assembled("mov m, #1+2"));
    EXPECT_EQ(opcode("mov w, m"), assembled("mov w, m"));
    EXPECT_EQ(opcode("add w, 5"), assembled("add w, 2+3"));
}

TEST(AsmExpressionTest, BitInstructionsTakeExpressionsOnBothSides) {
    Assembled r = assembleText(
        "flags equ 10\n"
        "n equ 2\n"
        "    setb flags+1.n+1\n"
        "    clrb flags.(n)\n"
        "    sb 5.3\n"
        "    snb flags.7\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("setb 11.3"), r.rom[0]);
    EXPECT_EQ(opcode("clrb 10.2"), r.rom[1]);
    EXPECT_EQ(opcode("sb 5.3"), r.rom[2]);
    EXPECT_EQ(opcode("snb 10.7"), r.rom[3]);
}

TEST(AsmExpressionTest, JmpAndCallKeepTheLowBitsOfAnAddress) {
    Assembled r = assembleText(
        "    jmp far+1\n"
        "    call near+1\n"
        "    org 305\n"
        "far: nop\n"
        "    org 10\n"
        "near: nop\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("jmp 106"), r.rom[0]);
    EXPECT_EQ(opcode("call 11"), r.rom[1]);
}

TEST(AsmExpressionTest, AutopageFollowsAnExpression) {
    Assembled r = assembleText(
        "    autopage on\n"
        "    jmp far+1\n"
        "    org 200\n"
        "far: nop\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("page 1"), r.rom[0]);
    EXPECT_EQ(opcode("jmp 1"), r.rom[1]);
}

TEST(AsmExpressionTest, MacroArgumentsCanBeExpressions) {
    Assembled r = assembleText(
        "load macro a, b\n"
        "    mov w, #a\n"
        "    mov w, #b\n"
        "endm\n"
        "    load (1+2)*2, 4\n"
        "    load(3, 1<<2)\n");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(opcode("mov w, #6"), r.rom[0]);
    EXPECT_EQ(opcode("mov w, #4"), r.rom[1]);
    EXPECT_EQ(opcode("mov w, #3"), r.rom[2]);
    EXPECT_EQ(opcode("mov w, #4"), r.rom[3]);
}

TEST(AsmExpressionTest, MistakesInExpressions) {
    struct Case { const char* source; const char* message; };
    const Case cases[] = {
        { "mov w, #1/0\n", "line 1: division by zero" },
        { "mov w, #nope+1\n", "line 1: unknown name nope" },
        { "mov w, #1x\n", "line 1: bad number 1x" },
        { "mov w, #(1+2\n", "line 1: missing )" },
        { "mov w, #(1))\n", "line 1: unexpected ')'" },
        { "mov w, #1+\n", "line 1: a value is missing" },
        { "mov w, #1<<25\n", "line 1: the shift count must be from 0 to 24" },
        { "mov w, #ffff*ffff\n", "line 1: the value is out of range" },
        { "mov w, #d'12a'\n", "line 1: bad digit 'a' in a decimal number" },
        { "mov w, #b'102'\n", "line 1: bad digit '2' in a binary number" },
        { "mov w, #d'1\n", "line 1: missing ' in a number" },
        { "mov w, 0-1\n", "line 1: the value of 0-1 is negative" },
        { "mov w, #w+1\n", "line 1: w cannot be used in an expression" },
        { "x equ w+1\n", "line 1: bad value 'w+1' for equ (w cannot be used in an expression)" },
        { "x equ 0-1\n", "line 1: bad value '0-1' for equ (the value is negative)" },
        { "org 0-1\n", "line 1: bad org address (the value is negative)" },
        { "org 1/0\n", "line 1: bad org address (division by zero)" },
        { "dw 1, 2/0\n", "line 1: bad value '2/0' for dw (division by zero)" },
        { "dw 1000+1\n", "line 1: bad value '1000+1' for dw" },
        { "    org 100\nlbl: nop\n    call lbl\n", "line 3: call target lbl is in the second half of a page" },
        { "    org 100\nlbl: nop\n    call lbl+1\n", "line 3: call target lbl+1 is in the second half of a page" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        EXPECT_EQ(std::string(cases[i].message), failure(cases[i].source)) << cases[i].source;
}

TEST(AsmExpressionTest, DeepNestingIsAnErrorNotACrash) {
    std::string opening(10000, '(');
    EXPECT_EQ("line 1: the expression is nested too deeply", failure("mov w, #" + opening + "1\n"));
    std::string minus(10000, '-');
    EXPECT_EQ("line 1: the expression is nested too deeply", failure("mov w, #" + minus + "1\n"));
}

TEST(AsmExpressionTest, ErrorsInAMacroNameTheMacroAndItsLine) {
    EXPECT_EQ("line 8: cannot assemble 'bogus' (in macro inner, line 3, called from macro outer, line 6)",
        failure(
            "inner macro v\n"
            "    mov w, #v\n"
            "    bogus\n"
            "endm\n"
            "outer macro\n"
            "    inner 1\n"
            "endm\n"
            "    outer\n"));
    EXPECT_EQ("line 7: macro wait takes 1 arguments, not 0 (in macro twice, line 4)",
        failure(
            "wait macro n\n"
            "endm\n"
            "twice macro\n"
            "    wait\n"
            "endm\n"
            "    nop\n"
            "    twice\n"));
    EXPECT_EQ("line 2: cannot assemble 'bogus'", failure("    nop\n    bogus\n"));
}

TEST(AsmExpressionTest, AnErrorInAnExpansionOfExpressionsNamesTheMacro) {
    EXPECT_EQ("line 4: division by zero (in macro div, line 2)",
        failure(
            "div macro a\n"
            "    mov w, #1/a\n"
            "endm\n"
            "    div 0\n"));
}
