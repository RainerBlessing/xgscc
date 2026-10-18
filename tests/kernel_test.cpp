// The video kernel is run on the emulated SX52 and the writes to port RE are
// recorded with their clock cycle, the way the emulator's Video sees them.
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>
#include "asm.h"
#include "cpu.h"
#include "kernel.h"

namespace {

struct Event {
    unsigned cycle;
    int value;
};

class Recorder : public VideoPort {
public:
    std::vector<Event> events;
    void changeVideo(int cycle, unsigned char colour) {
        Event event = { static_cast<unsigned>(cycle), colour };
        events.push_back(event);
    }
};

std::string hex(int value) {
    char text[16];
    snprintf(text, sizeof(text), "%x", value);
    return text;
}

// The registers the kernel uses, and a program around it: init, then frames with
// a delay in between that stands for the C code of the game.
std::string program(int lines, int colour, int updateLoops) {
    std::string text =
        "xgs_a equ 0a\nxgs_n equ 0b\nupdate_a equ 0e\nupdate_b equ 0f\n"
        "        org fff\n        jmp start\n        org 0\n"
        "start:\n        call xgs_init\n"
        "frames:\n        call xgs_frame\n";
    if (updateLoops > 0) {
        // updateLoops rounds of 4 cycles, then a few more
        text += "        mov w, #" + hex(updateLoops % 256) + "\n        mov update_a, w\n"
                "        mov w, #" + hex(updateLoops / 256 + 1) + "\n        mov update_b, w\n"
                "spend:\n        decsz update_a\n        jmp spend\n        decsz update_b\n        jmp spend\n";
    }
    text += "        jmp frames\n";
    // the registers of the kernel are named {a} and {n} in its text
    std::string kernel = "xgs_init:\n" + xgsInitSource() + "xgs_frame:\n" + xgsFrameSource(lines, colour);
    for (const char* name : { "a", "n" }) {
        std::string placeholder = std::string("{") + name + "}";
        for (size_t at = kernel.find(placeholder); at != std::string::npos; at = kernel.find(placeholder, at)) {
            kernel.replace(at, placeholder.size(), std::string("xgs_") + name);
        }
    }
    return text + "        firsthalf\n" + kernel;
}

struct Recording {
    std::vector<Event> events;
    std::string error;
    bool ok;
};

Recording record(int lines, int colour, int updateLoops, int frames) {
    Recording result;
    static unsigned short rom[4096];
    result.ok = assembleSource(program(lines, colour, updateLoops), rom, 4096, &result.error);
    if (!result.ok) {
        return result;
    }
    Recorder recorder;
    Cpu cpu(&recorder);
    cpu.reset();
    for (int i = 0; i < 4096; i++) {
        cpu.getROM()[i] = rom[i];
    }
    unsigned long long steps = 0;
    while (cpu.getCycle() < static_cast<unsigned>(XGS_FRAME_CYCLES) * frames && steps < 200000000ull) {
        cpu.execute();
        steps++;
    }
    result.events = recorder.events;
    return result;
}

// the index of the first event of every frame: the one after a sync longer than 4000 cycles
std::vector<size_t> frameStarts(const std::vector<Event>& e) {
    std::vector<size_t> starts;
    for (size_t i = 0; i + 1 < e.size(); i++) {
        if ((e[i].value & 0xf) == 0 && e[i + 1].cycle - e[i].cycle > 4000) {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

}

TEST(KernelTest, TheKernelAssembles) {
    Recording r = record(100, 0xe6, 0, 1);
    ASSERT_TRUE(r.ok) << r.error;
}

TEST(KernelTest, KernelFitsIntoTheFirstHalfOfAPage) {
    static unsigned short rom[4096];
    std::string error;
    ASSERT_TRUE(assembleSource(program(192, 0xe6, 0), rom, 4096, &error)) << error;
    int last = 0;
    for (int i = 0; i < 0xfff; i++) {
        if (rom[i] != 0xfff) {
            last = i;
        }
    }
    EXPECT_LT(last, 0x200) << "the kernel has to stay on one page";
}

TEST(KernelTest, EveryLineHasTheSameSixWritesAtTheSameOffsets) {
    Recording r = record(50, 0xe6, 0, 6);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 4u);
    size_t a = frames[2];
    const int values[6] = { 0xf6, 0xf0, 0xf6, 0x05, 0xf6, 0xe6 };
    const unsigned offsets[6] = { 0, 120, 496, 544, 744, 872 };
    for (int line = 0; line < 50; line++) {
        size_t first = a + line * 6;
        unsigned base = r.events[first].cycle;
        for (int k = 0; k < 6; k++) {
            EXPECT_EQ(values[k], r.events[first + k].value) << "line " << line << " write " << k;
            EXPECT_EQ(offsets[k], r.events[first + k].cycle - base) << "line " << line << " write " << k;
        }
        if (line > 0) {
            EXPECT_EQ(static_cast<unsigned>(XGS_LINE_CYCLES), base - r.events[first - 6].cycle) << "line " << line;
        }
    }
}

TEST(KernelTest, FrameHasExactlyTheRequestedNumberOfLines) {
    for (int lines : { 1, 2, 17, 100, 192, 255 }) {
        Recording r = record(lines, 0xe6, 0, 6);
        ASSERT_TRUE(r.ok) << r.error;
        std::vector<size_t> frames = frameStarts(r.events);
        ASSERT_GE(frames.size(), 4u) << lines;
        // an event per write: six per line, the f6 after the last line, then the vertical sync
        size_t events = frames[3] - frames[2];
        EXPECT_EQ(static_cast<size_t>(lines * 6 + 2), events) << lines << " lines";
    }
}

TEST(KernelTest, TheLastLineIsFollowedByTheBlankingLevelAtTheRightTime) {
    Recording r = record(40, 0xe6, 0, 6);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    size_t a = frames[2];
    size_t afterLast = a + 40 * 6;
    EXPECT_EQ(0xf6, r.events[afterLast].value);
    EXPECT_EQ(static_cast<unsigned>(XGS_LINE_CYCLES) * 40, r.events[afterLast].cycle - r.events[a].cycle);
}

TEST(KernelTest, VerticalSyncIsALongSyncLevelFollowedByTheFirstLine) {
    Recording r = record(40, 0xe6, 0, 6);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    size_t a = frames[2];
    EXPECT_EQ(0xf0, r.events[a - 1].value);
    EXPECT_EQ(static_cast<unsigned>(XGS_VSYNC_CYCLES), r.events[a].cycle - r.events[a - 1].cycle);
}

TEST(KernelTest, FrameLastsTheFramePeriod) {
    Recording r = record(192, 0xe6, 0, 8);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
    }
}

TEST(KernelTest, TheFramePeriodDoesNotDependOnTheTimeThatTheGameTakes) {
    // updateLoops rounds of 4 cycles: 0, 1000, 5000, 15000 rounds = up to 60 000 cycles
    for (int loops : { 0, 100, 1000, 5000, 15000 }) {
        Recording r = record(192, 0xe6, loops, 8);
        ASSERT_TRUE(r.ok) << r.error;
        std::vector<size_t> frames = frameStarts(r.events);
        ASSERT_GE(frames.size(), 5u) << loops;
        for (size_t i = 1; i + 1 < frames.size(); i++) {
            long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
            EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "update " << loops * 4 << " cycles, frame " << i;
        }
    }
}

TEST(KernelTest, AGameThatTakesTooLongMakesOneLongerFrameAndNothingBreaks) {
    // more than the RTCC can measure (65 536 cycles): the frame becomes longer, the lines stay exact
    Recording r = record(100, 0xe6, 20000, 12);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 4u);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        size_t events = frames[i + 1] - frames[i];
        EXPECT_EQ(static_cast<size_t>(100 * 6 + 2), events) << "frame " << i;
    }
}

TEST(KernelTest, ColourOfThePictureIsTheRequestedOne) {
    Recording r = record(10, 0x9a, 0, 6);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<size_t> frames = frameStarts(r.events);
    EXPECT_EQ(0x9a, r.events[frames[2] + 5].value);
}

TEST(KernelTest, NoPageInstructionsAndNoUnknownCycles) {
    // the kernel is timed to the cycle: nothing may be inserted into it
    std::string text = xgsInitSource() + xgsFrameSource(100, 0xe6);
    EXPECT_EQ(std::string::npos, text.find("autopage"));
    EXPECT_EQ(std::string::npos, text.find("page "));
}
