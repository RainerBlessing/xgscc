// The peephole optimizer works on the assembler text; every rule has a test with the text before and after.
#include <gtest/gtest.h>
#include <initializer_list>
#include <string>
#include "optimizer.h"

namespace {

std::string lines(std::initializer_list<std::string> all) {
    std::string text;
    for (const std::string& line : all) {
        text += line + "\n";
    }
    return text;
}

}

TEST(OptimizerTest, OrWithZeroAfterAMoveFromARegisterIsRedundant) {
    EXPECT_EQ(lines({"        mov w, x", "        snb status.2"}),
              optimize(lines({"        mov w, x", "        or w, #0", "        snb status.2"})));
}

TEST(OptimizerTest, OrWithZeroAfterALiteralIsKept) {
    // mov w, #lit does not set the zero flag
    std::string text = lines({"        mov w, #0", "        or w, #0", "        snb status.2"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, AnUnusedLabelBetweenDoesNotStopTheRule) {
    EXPECT_EQ(lines({"        mov w, x"}), optimize(lines({"        mov w, x", "_L1:", "        or w, #0"})));
}

TEST(OptimizerTest, OrWithZeroBehindAUsedLabelIsKept) {
    std::string text = lines({"        snb status.2", "        jmp _L1", "        mov w, x", "_L1:", "        or w, #0"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, JumpToTheNextInstructionGoes) {
    EXPECT_EQ(lines({"        nop", "        nop"}), optimize(lines({"        nop", "        jmp _L1", "_L1:", "        nop"})));
}

TEST(OptimizerTest, JumpToTheNextInstructionBehindSeveralLabelsGoes) {
    EXPECT_EQ(lines({"        nop", "f_x:", "        nop"}), optimize(lines({"        nop", "        jmp f_x", "_L1:", "f_x:", "        nop"})));
}

TEST(OptimizerTest, ConditionalJumpToTheNextInstructionGoesWithItsSkip) {
    // snb followed by jmp to the next instruction: both are useless, but only the jmp is removed (the skip still skips the next)
    std::string text = lines({"        snb status.2", "        jmp _L1", "_L1:", "        nop"});
    EXPECT_EQ(lines({"        snb status.2", "        nop"}), optimize(text));
}

TEST(OptimizerTest, JumpToAJumpGoesToTheTarget) {
    std::string text = lines({"        snb status.2", "        jmp _L1", "        nop", "        retp", "_L1:", "        jmp f_b", "f_b:"});
    std::string result = optimize(text);
    EXPECT_NE(std::string::npos, result.find("jmp f_b"));
    EXPECT_EQ(std::string::npos, result.find("jmp _L1"));
}

TEST(OptimizerTest, CodeAfterAJumpUpToTheNextLabelIsDead) {
    std::string text = lines({"f_a:", "        nop", "        jmp f_a", "        mov w, x", "        mov y, w", "f_b:", "        retp"});
    EXPECT_EQ(lines({"f_a:", "        nop", "        jmp f_a", "f_b:", "        retp"}), optimize(text));
}

TEST(OptimizerTest, CodeAfterARetpIsDead) {
    std::string text = lines({"f_a:", "        retp", "        nop", "f_b:", "        retp"});
    EXPECT_EQ(lines({"f_a:", "        retp", "f_b:", "        retp"}), optimize(text));
}

TEST(OptimizerTest, CodeAfterAConditionalJumpIsNotDead) {
    std::string text = lines({"f_a:", "        sb status.2", "        jmp f_a", "        nop", "        retp"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, CodeAfterAJumpThatFollowsABankIsNotDeadIfTheBankFollowsASkip) {
    std::string text = lines({"f_a:", "        snb status.2", "        bank 1", "        jmp f_a", "        nop", "        retp"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, TheSameBankTwiceInARow) {
    EXPECT_EQ(lines({"        bank 1", "        mov w, x"}), optimize(lines({"        bank 1", "        bank 1", "        mov w, x"})));
}

TEST(OptimizerTest, TheSameBankBehindASkipIsKept) {
    std::string text = lines({"        snb status.2", "        bank 1", "        bank 1", "        mov w, x"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, ADifferentBankIsKept) {
    std::string text = lines({"        bank 1", "        bank 2", "        mov w, x"});
    EXPECT_EQ(lines({"        bank 2", "        mov w, x"}), optimize(text));
}

TEST(OptimizerTest, UsedLabelsAreKept) {
    std::string text = lines({"_L1:", "        nop", "        jmp _L1"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, FunctionLabelsAreKeptEvenIfNothingJumpsToThem) {
    std::string text = lines({"f_unused:", "        retp"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, AssemblerRoutinesAreNotTouched) {
    std::string text = lines({"        autopage off", "f_k:", "                jmp xgs_a", "xgs_a:", "                or w, #0", "        autopage on"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerTest, DirectivesAreBarriers) {
    std::string text = lines({"        jmp f_a", "        firsthalf", "f_a:", "        nop"});
    // the jump may not be dropped over a firsthalf: the next instruction is not where the label is
    std::string result = optimize(text);
    EXPECT_NE(std::string::npos, result.find("jmp f_a"));
}

TEST(OptimizerTest, ChainsOfJumpsAreFollowedToTheEnd) {
    std::string text = lines({"        snb status.2", "        jmp _L1", "        nop", "_L1:", "        jmp _L2", "_L2:", "        jmp f_end"});
    std::string result = optimize(text);
    EXPECT_NE(std::string::npos, result.find("jmp f_end"));
    EXPECT_EQ(std::string::npos, result.find("_L2"));
}

TEST(OptimizerTest, ALoopOfJumpsDoesNotHangTheOptimizer) {
    std::string text = lines({"_L1:", "        jmp _L2", "_L2:", "        jmp _L1"});
    EXPECT_FALSE(optimize(text).empty());
}

TEST(OptimizerTest, NothingToDoLeavesTheTextAlone) {
    std::string text = lines({"_start:", "        mov w, #1", "        mov x, w", "_exit:", "        jmp _exit"});
    EXPECT_EQ(text, optimize(text));
}

// bank tracking: a "bank n" is dropped where every way to it has selected n already

TEST(OptimizerBankTest, TheSameBankAgainAfterOtherInstructions) {
    EXPECT_EQ(lines({"_start:", "        bank 1", "        mov w, x", "        mov y, w"}),
              optimize(lines({"_start:", "        bank 1", "        mov w, x", "        bank 1", "        mov y, w"})));
}

TEST(OptimizerBankTest, ADifferentBankIsKept) {
    std::string text = lines({"_start:", "        bank 1", "        mov w, x", "        bank 2", "        mov y, w"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerBankTest, AfterResetTheBankIsZero) {
    EXPECT_EQ(lines({"        org fff", "        jmp _start", "        org 0", "_start:", "        mov w, x"}),
              optimize(lines({"        org fff", "        jmp _start", "        org 0", "_start:", "        bank 0", "        mov w, x"})));
}

TEST(OptimizerBankTest, NothingIsKnownAfterACallOfAFunctionThatChangesTheBank) {
    std::string text = lines({"_start:", "        bank 1", "        call f_a", "        bank 1", "        mov w, x", "        retp", "f_a:", "        bank 2", "        retp"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerBankTest, AfterACallTheBankIsThatOfTheEndOfTheFunction) {
    std::string text = lines({"_start:", "        call f_a", "        bank 2", "        mov w, x", "        retp", "f_a:", "        bank 2", "        mov w, y", "        retp"});
    std::string result = optimize(text);
    EXPECT_EQ(std::string::npos, result.find("        bank 2\n        mov w, x")) << result;
}

TEST(OptimizerBankTest, AFunctionWithTwoExitsInDifferentBanksLeavesTheBankUnknown) {
    std::string text = lines({"_start:", "        call f_a", "        bank 2", "        mov w, x", "        retp", "f_a:", "        snb status.2", "        jmp _L1", "        bank 2", "        retp", "_L1:", "        bank 3", "        retp"});
    EXPECT_NE(std::string::npos, optimize(text).find("        bank 2\n        mov w, x"));
}

TEST(OptimizerBankTest, AfterACallOfAnAssemblerRoutineNothingIsKnown) {
    std::string text = lines({"_start:", "        bank 1", "        call f_k", "        bank 1", "        mov w, x", "        retp", "        autopage off", "f_k:", "                retp", "        autopage on"});
    EXPECT_NE(std::string::npos, optimize(text).find("        bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, TheExitOfAFunctionThatCallsAnotherFollowsThatCall) {
    std::string text = lines({"_start:", "        call f_a", "        bank 2", "        mov w, x", "        retp", "f_a:", "        call f_b", "        retp", "f_b:", "        bank 2", "        retp"});
    EXPECT_EQ(std::string::npos, optimize(text).find("        bank 2\n        mov w, x"));
}

TEST(OptimizerBankTest, AFunctionKnowsTheBankOfAllItsCallers) {
    std::string text = lines({"_start:", "        bank 1", "        call f_a", "        bank 1", "        call f_a", "        retp", "f_a:", "        bank 1", "        mov w, x", "        retp"});
    std::string result = optimize(text);
    EXPECT_EQ(std::string::npos, result.find("        bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, AFunctionWithCallersInDifferentBanksKeepsItsBank) {
    std::string text = lines({"_start:", "        bank 1", "        call f_a", "        bank 2", "        call f_a", "f_a:", "        bank 1", "        mov w, x", "        retp"});
    EXPECT_NE(std::string::npos, optimize(text).find("        bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, ALabelWithOneWayInKnowsTheBank) {
    std::string text = lines({"_start:", "        bank 1", "        snb status.2", "        jmp _L1", "        nop", "_L1:", "        bank 1", "        mov w, x"});
    EXPECT_EQ(std::string::npos, optimize(text).find("_L1:\n        bank 1"));
    EXPECT_EQ(std::string::npos, optimize(text).find("bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, ALabelReachedInTwoBanksDoesNotKnowIt) {
    std::string text = lines({"_start:", "        bank 1", "        snb status.2", "        jmp _L1", "        bank 2", "_L1:", "        bank 1", "        mov w, x"});
    EXPECT_NE(std::string::npos, optimize(text).find("        bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, ALoopKeepsTheBankOfItsEntry) {
    std::string text = lines({"_start:", "        bank 1", "_L1:", "        bank 1", "        inc x", "        snb status.2", "        jmp _L1", "        retp"});
    EXPECT_EQ(lines({"_start:", "        bank 1", "_L1:", "        inc x", "        snb status.2", "        jmp _L1", "        retp"}), optimize(text));
}

TEST(OptimizerBankTest, AWriteToFsrForgetsTheBank) {
    std::string text = lines({"_start:", "        bank 1", "        mov fsr, w", "        bank 1", "        mov w, x"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerBankTest, ClearingBitSevenOfFsrKeepsTheBank) {
    EXPECT_EQ(lines({"_start:", "        bank 1", "        clrb fsr.7", "        mov w, x"}),
              optimize(lines({"_start:", "        bank 1", "        clrb fsr.7", "        bank 1", "        mov w, x"})));
}

TEST(OptimizerBankTest, ABankBehindASkipIsKept) {
    std::string text = lines({"_start:", "        bank 1", "        sb status.2", "        bank 1", "        mov w, x"});
    EXPECT_EQ(text, optimize(text));
}

TEST(OptimizerBankTest, ASkippedBankMakesTheBankUnknownBehindIt) {
    std::string text = lines({"_start:", "        bank 1", "        sb status.2", "        bank 2", "        bank 2", "        mov w, x"});
    // behind the skip the bank is 1 or 2
    std::string text2 = lines({"_start:", "        bank 1", "        sb status.2", "        bank 2", "        mov w, x", "        bank 2", "        mov y, w"});
    EXPECT_NE(std::string::npos, optimize(text2).find("        bank 2\n        mov w, x"));
}

TEST(OptimizerBankTest, AssemblerRoutinesForgetTheBank) {
    std::string text = lines({"_start:", "        bank 1", "        autopage off", "f_k:", "                retp", "        autopage on", "        bank 1", "        mov w, x"});
    EXPECT_NE(std::string::npos, optimize(text).find("        bank 1\n        mov w, x"));
}

TEST(OptimizerBankTest, TheCodeAfterAnUnconditionalJumpStartsFromTheLabel) {
    std::string text = lines({"_start:", "        bank 1", "        jmp _L1", "_L2:", "        mov w, x", "        jmp _L2", "_L1:", "        bank 1", "        mov w, y"});
    std::string result = optimize(text);
    EXPECT_EQ(std::string::npos, result.find("_L1:\n        bank 1"));
}
