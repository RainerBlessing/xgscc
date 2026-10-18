// Programs in C that use the video kernel through <xgs.h>, run on the emulated
// SX52 with the writes to RE recorded.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "asm.h"
#include "compiler.h"
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

struct Result {
    CompileResult compiled;
    std::vector<Event> events;
    unsigned char ram[256];
};

Result runVideo(const std::string& source, int frames) {
    Result result;
    result.compiled = compile(source, "game.c");
    for (int i = 0; i < 256; i++) {
        result.ram[i] = 0;
    }
    if (!result.compiled.ok) {
        return result;
    }
    Recorder recorder;
    Cpu cpu(&recorder);
    cpu.reset();
    for (size_t i = 0; i < result.compiled.rom.size(); i++) {
        cpu.getROM()[i] = result.compiled.rom[i];
    }
    unsigned long long steps = 0;
    while (cpu.getCycle() < static_cast<unsigned>(XGS_FRAME_CYCLES) * frames && steps < 300000000ull) {
        cpu.execute();
        steps++;
    }
    result.events = recorder.events;
    for (int i = 0; i < 256; i++) {
        result.ram[i] = cpu.getRAM()[i];
    }
    return result;
}

std::vector<size_t> frameStarts(const std::vector<Event>& e) {
    std::vector<size_t> starts;
    for (size_t i = 0; i + 1 < e.size(); i++) {
        if ((e[i].value & 0xf) == 0 && e[i + 1].cycle - e[i].cycle > 4000) {
            starts.push_back(i + 1);
        }
    }
    return starts;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const char* const GAME =
    "#include <stdint.h>\n#include <xgs.h>\n"
    "uint8_t frames;\n"
    "void update(void) { frames++; }\n"
    "void main(void) { xgs_init(); for (;;) { xgs_frame(); update(); } }\n";

}

TEST(XgsTest, ACProgramMakesAPicture) {
    Result r = runVideo(GAME, 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    // the default is 192 lines of 6 writes, the blanking level after them, then the vertical sync
    EXPECT_EQ(static_cast<size_t>(192 * 6 + 2), frames[3] - frames[2]);
}

TEST(XgsTest, LinesAreExactAndSoIsTheFramePeriod) {
    Result r = runVideo(GAME, 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    size_t a = frames[2];
    const unsigned offsets[6] = { 0, 120, 496, 544, 744, 872 };
    for (int line = 0; line < 192; line++) {
        size_t first = a + line * 6;
        for (int k = 0; k < 6; k++) {
            EXPECT_EQ(offsets[k], r.events[first + k].cycle - r.events[first].cycle) << "line " << line << " write " << k;
        }
        if (line > 0) {
            EXPECT_EQ(static_cast<unsigned>(XGS_LINE_CYCLES), r.events[first].cycle - r.events[first - 6].cycle) << line;
        }
    }
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
    }
}

TEST(XgsTest, TheGameRunsOncePerFrame) {
    Result r = runVideo(GAME, 10);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    int counted = r.ram[r.compiled.variables.at("frames")];
    std::vector<size_t> frames = frameStarts(r.events);
    EXPECT_NEAR(static_cast<int>(frames.size()), counted, 1);
    EXPECT_GE(counted, 8);
}

TEST(XgsTest, TheNumberOfLinesAndTheColourComeFromDefines) {
    Result r = runVideo("#include <stdint.h>\n#define XGS_LINES 120\n#define XGS_COLOR 0x9a\n#include <xgs.h>\n"
                        "void main(void) { xgs_init(); for (;;) xgs_frame(); }\n", 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    EXPECT_EQ(static_cast<size_t>(120 * 6 + 2), frames[3] - frames[2]);
    EXPECT_EQ(0x9a, r.events[frames[2] + 5].value);
}

TEST(XgsTest, TheGameMayTakeTimeWithoutChangingTheFrame) {
    Result r = runVideo("#include <stdint.h>\n#include <xgs.h>\nuint16_t sum;\n"
                        "void update(void) { for (uint8_t i = 0; i < 200; i++) sum += i; }\n"
                        "void main(void) { xgs_init(); for (;;) { xgs_frame(); update(); } }\n", 10);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 6u);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
    }
}

TEST(XgsTest, ManyVariablesNextToTheKernel) {
    // variables in the banks, set again after every call of the kernel
    std::string globals;
    std::string body;
    for (int i = 0; i < 40; i++) {
        globals += "uint8_t g" + std::to_string(i) + ";\n";
        body += "g" + std::to_string(i) + " += " + std::to_string(i + 1) + ";\n";
    }
    Result r = runVideo("#include <stdint.h>\n#include <xgs.h>\n" + globals +
                        "void update(void) {\n" + body + "}\n"
                        "void main(void) { xgs_init(); for (;;) { xgs_frame(); update(); } }\n", 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    int rounds = r.ram[r.compiled.variables.at("g0")];
    EXPECT_GE(rounds, 5);
    for (int i = 0; i < 40; i++) {
        EXPECT_EQ(((i + 1) * rounds) & 0xff, r.ram[r.compiled.variables.at("g" + std::to_string(i))]) << i;
    }
}

TEST(XgsTest, TheKernelNeedsTheHeader) {
    CompileResult result = compile("void main(void) { xgs_frame(); }", "game.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "xgs_frame")) << result.error;
}

TEST(XgsTest, ProgramsWithoutTheHeaderHaveNoKernel) {
    CompileResult result = compile("#include <stdint.h>\nuint8_t x; void main(void) { x = 1; }", "game.c");
    ASSERT_TRUE(result.ok) << result.error;
    for (const RomEntry& entry : result.map.rom) {
        EXPECT_NE("assembler", entry.kind) << entry.name;
    }
}

TEST(XgsTest, KernelFunctionsAreListedInTheMap) {
    CompileResult result = compile(GAME, "game.c");
    ASSERT_TRUE(result.ok) << result.error;
    const RomEntry* frame = NULL;
    const RomEntry* init = NULL;
    for (const RomEntry& entry : result.map.rom) {
        if (entry.name == "xgs_frame") frame = &entry;
        if (entry.name == "xgs_init") init = &entry;
    }
    ASSERT_TRUE(frame != NULL);
    ASSERT_TRUE(init != NULL);
    EXPECT_EQ("assembler", frame->kind);
    EXPECT_GT(frame->words, 100);
}

TEST(XgsTest, TheKernelStaysOnOnePage) {
    CompileResult result = compile(GAME, "game.c");
    ASSERT_TRUE(result.ok) << result.error;
    for (const RomEntry& entry : result.map.rom) {
        if (entry.kind != "assembler") {
            continue;
        }
        int last = entry.address;
        for (int a = entry.address; a < entry.address + 400 && a < 0xfff; a++) {
            if (result.rom[a] != 0xfff) {
                last = a;
            }
        }
        EXPECT_EQ(entry.address / 512, last / 512) << entry.name;
        EXPECT_LT(entry.address % 512, 256) << entry.name << " has to be callable";
    }
}

TEST(XgsTest, NoCodeIsInsertedIntoTheKernel) {
    CompileResult result = compile(GAME, "game.c");
    ASSERT_TRUE(result.ok) << result.error;
    size_t at = result.assembly.find("f_xgs_frame:");
    ASSERT_NE(std::string::npos, at);
    std::string kernel = result.assembly.substr(at);
    size_t end = kernel.find("autopage on");
    ASSERT_NE(std::string::npos, end);
    kernel = kernel.substr(0, end);
    EXPECT_FALSE(contains(kernel, "bank "));
    EXPECT_FALSE(contains(kernel, "@"));
    EXPECT_TRUE(contains(result.assembly.substr(at > 40 ? at - 40 : 0, 40), "autopage off"));
}

TEST(XgsTest, TheRegistersOfTheKernelAreDirectRegisters) {
    CompileResult result = compile(GAME, "game.c");
    ASSERT_TRUE(result.ok) << result.error;
    int found = 0;
    for (const RamEntry& entry : result.map.ram) {
        if (entry.name.find("xgs_frame.") == 0) {
            EXPECT_LT(entry.address, 0x10) << entry.name;
            EXPECT_GE(entry.address, 0x0a) << entry.name;
            found++;
        }
    }
    EXPECT_EQ(2, found);        // the counter and the number of the line
}

TEST(XgsTest, KernelRegistersAreSharedWithFunctionsThatNeverRunAtTheSameTime) {
    // helper() and the kernel are both called from main only: they share their frames
    Result r = runVideo("#include <stdint.h>\n#include <xgs.h>\nuint8_t out;\n"
                        "void helper(void) { uint8_t a = 1; uint8_t b = 2; out += a + b; }\n"
                        "void main(void) { xgs_init(); for (;;) { xgs_frame(); helper(); } }\n", 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    EXPECT_GE(r.ram[r.compiled.variables.at("out")], 15);
}

TEST(XgsTest, KernelIsCalledFromAnotherPage) {
    // a long main pushes the kernel and the call onto different pages
    std::string body;
    for (int i = 0; i < 200; i++) {
        body += "x = x + 1;\n";
    }
    Result r = runVideo("#include <stdint.h>\n#include <xgs.h>\nuint8_t x;\n"
                        "void filler(void) {\n" + body + "}\n"
                        "void main(void) { xgs_init(); for (;;) { xgs_frame(); filler(); } }\n", 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    EXPECT_EQ(static_cast<size_t>(192 * 6 + 2), frames[3] - frames[2]);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
    }
}

TEST(XgsTest, TooManyLinesForTheFrame) {
    CompileResult result = compile("#define XGS_LINES 300\n#include <xgs.h>\nvoid main(void) { xgs_frame(); }", "game.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "XGS_LINES")) << result.error;
}

TEST(XgsTest, NoLines) {
    CompileResult result = compile("#define XGS_LINES 0\n#include <xgs.h>\nvoid main(void) { xgs_frame(); }", "game.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "XGS_LINES")) << result.error;
}
