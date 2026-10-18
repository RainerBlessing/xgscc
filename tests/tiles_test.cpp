// The tile map kernel: a map of tile numbers in RAM, the graphics of the tiles in a
// table in the ROM, one colour per tile and line of the tile. Run on the emulated SX52
// with the writes to RE recorded.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
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

Result runProgram(const std::string& source, int frames) {
    Result result;
    result.compiled = compile(source, "tiles.c");
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
    while (cpu.getCycle() < static_cast<unsigned>(XGS_FRAME_CYCLES) * frames && steps < 400000000ull) {
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

std::string number(int value) {
    return std::to_string(value);
}

// the colour of a tile in a line of the tile: a luminance of 6 to 15 (0 would be a sync) and a chroma
int tileColour(int tile, int line) {
    return (((tile * 3 + line) % 15) << 4) | (6 + (tile + line) % 10);
}

// "const uint8_t xgs_tiles[256] = { ... };" for tiles of tileLines lines
std::string tileTable(int tileLines) {
    std::string text = "const uint8_t xgs_tiles[256] = {";
    for (int i = 0; i < 256; i++) {
        text += number(tileColour(i / tileLines, i % tileLines)) + (i < 255 ? "," : "");
    }
    return text + "};\n";
}

std::string program(int columns, int rows, int tileLines, const std::string& body) {
    return "#include <stdint.h>\n"
           "#define XGS_COLUMNS " + number(columns) + "\n#define XGS_ROWS " + number(rows) + "\n#define XGS_TILE_LINES " + number(tileLines) + "\n"
           "#include <xgs.h>\n" + tileTable(tileLines) +
           "void main(void) {\n xgs_init();\n" + body + "\n for (;;) xgs_frame();\n}\n";
}

// the tile at (x, y) of the test pattern
int patternTile(int x, int y, int tiles) {
    return (x * 5 + y * 3 + 1) % tiles;
}

std::string patternBody(int columns, int rows, int tiles) {
    std::string body;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            body += " xgs_tile(" + number(x) + ", " + number(y) + ", " + number(patternTile(x, y, tiles)) + ");\n";
        }
    }
    return body;
}

// checks every line of a frame against the map and the table
void checkPicture(const Result& r, int columns, int rows, int tileLines) {
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    const int tiles = 256 / tileLines;
    const int cell = XGS_TILE_AREA / columns;
    const int lines = rows * tileLines;
    size_t a = frames[2];
    const size_t perLine = columns + 6;
    ASSERT_EQ(static_cast<size_t>(lines) * perLine + 2, frames[3] - frames[2]);
    for (int line = 0; line < lines; line++) {
        size_t first = a + line * perLine;
        unsigned base = r.events[first].cycle;
        const int prefix[5] = { 0xf6, 0xf0, 0xf6, 0x05, 0xf6 };
        const unsigned prefixAt[5] = { 0, 120, 496, 544, 744 };
        for (int k = 0; k < 5; k++) {
            EXPECT_EQ(prefix[k], r.events[first + k].value) << "line " << line << " prefix " << k;
            EXPECT_EQ(prefixAt[k], r.events[first + k].cycle - base) << "line " << line << " prefix " << k;
        }
        int row = line / tileLines;
        int sub = line % tileLines;
        for (int x = 0; x < columns; x++) {
            const Event& event = r.events[first + 5 + x];
            EXPECT_EQ(tileColour(patternTile(x, row, tiles), sub), event.value) << "line " << line << " column " << x;
            EXPECT_EQ(static_cast<unsigned>(872 + x * cell), event.cycle - base) << "line " << line << " column " << x;
        }
        const Event& blank = r.events[first + 5 + columns];
        EXPECT_EQ(0xf6, blank.value) << "line " << line;
        EXPECT_EQ(static_cast<unsigned>(872 + columns * cell), blank.cycle - base) << "line " << line;
        if (line > 0) {
            EXPECT_EQ(static_cast<unsigned>(XGS_LINE_CYCLES), base - r.events[first - perLine].cycle) << "line " << line;
        }
    }
}

}

TEST(TilesTest, EveryCellOfEveryLineShowsItsTileAtTheRightTime) {
    Result r = runProgram(program(8, 4, 4, patternBody(8, 4, 64)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 8, 4, 4);
}

TEST(TilesTest, SixteenByTwelveWithTilesOfSixteenLines) {
    Result r = runProgram(program(16, 12, 16, patternBody(16, 12, 16)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 16, 12, 16);
}

TEST(TilesTest, TilesOfOneLine) {
    Result r = runProgram(program(5, 30, 1, patternBody(5, 30, 256)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 5, 30, 1);
}

TEST(TilesTest, TilesOfTwoAndEightLines) {
    Result two = runProgram(program(10, 20, 2, patternBody(10, 20, 128)), 8);
    ASSERT_TRUE(two.compiled.ok) << two.compiled.error;
    checkPicture(two, 10, 20, 2);
    Result eight = runProgram(program(12, 6, 8, patternBody(12, 6, 32)), 8);
    ASSERT_TRUE(eight.compiled.ok) << eight.compiled.error;
    checkPicture(eight, 12, 6, 8);
}

TEST(TilesTest, TilesOfTwentyLinesFillTheFullHeight) {
    // 12 rows of 20 lines: the 240 lines of a television picture; 12 tiles of 20 lines
    Result r = runProgram(program(16, 12, 20, patternBody(16, 12, 12)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 16, 12, 20);
}

TEST(TilesTest, TilesOfThreeAndSevenLines) {
    Result three = runProgram(program(6, 10, 3, patternBody(6, 10, 85)), 8);
    ASSERT_TRUE(three.compiled.ok) << three.compiled.error;
    checkPicture(three, 6, 10, 3);
    Result seven = runProgram(program(5, 9, 7, patternBody(5, 9, 36)), 8);
    ASSERT_TRUE(seven.compiled.ok) << seven.compiled.error;
    checkPicture(seven, 5, 9, 7);
}

TEST(TilesTest, ReadingBackATileOfTwentyLines) {
    Result r = runProgram(
        "#include <stdint.h>\n#define XGS_COLUMNS 8\n#define XGS_ROWS 4\n#define XGS_TILE_LINES 20\n#include <xgs.h>\n" + tileTable(20) +
        "uint8_t a; uint8_t b; uint8_t m;\n"
        "void main(void) { xgs_init(); xgs_tile(3, 2, 11); xgs_tile(7, 3, 5); a = xgs_get_tile(3, 2); b = xgs_get_tile(7, 3); m = xgs_map[2 * 8 + 3]; }\n", 2);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    EXPECT_EQ(11, r.ram[r.compiled.variables.at("a")]);
    EXPECT_EQ(5, r.ram[r.compiled.variables.at("b")]);
    EXPECT_EQ(220, r.ram[r.compiled.variables.at("m")]);
}

TEST(TilesTest, ThirtyTwoColumnsOfNarrowCells) {
    Result r = runProgram(program(32, 6, 4, patternBody(32, 6, 64)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 32, 6, 4);
}

TEST(TilesTest, OneColumn) {
    Result r = runProgram(program(1, 8, 4, patternBody(1, 8, 64)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    checkPicture(r, 1, 8, 4);
}

TEST(TilesTest, FramePeriodIsTheSameAsWithOneColour) {
    Result r = runProgram(program(16, 12, 16, patternBody(16, 12, 16)), 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1]].cycle - r.events[frames[i]].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
    }
}

TEST(TilesTest, TheMapIsChangedWhileTheGameRuns) {
    // one tile moves along the first row, one step per frame
    Result r = runProgram(
        "#include <stdint.h>\n#define XGS_COLUMNS 8\n#define XGS_ROWS 2\n#define XGS_TILE_LINES 4\n#include <xgs.h>\n" + tileTable(4) +
        "uint8_t x;\nvoid main(void) { xgs_init(); for (;;) { xgs_frame(); xgs_tile(x, 0, 0); x = (x + 1) & 7; xgs_tile(x, 0, 9); } }\n", 14);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 10u);
    const size_t perLine = 8 + 6;
    for (size_t f = 3; f + 1 < frames.size() && f < 10; f++) {
        // the first line of the frame: the 9 is in column (f - 1) % 8 (the first frame draws before the game ran)
        size_t first = frames[f];
        int column9 = -1;
        for (int x = 0; x < 8; x++) {
            if (r.events[first + 5 + x].value == tileColour(9, 0)) {
                column9 = x;
            }
        }
        ASSERT_GE(column9, 0) << "frame " << f;
        size_t next = frames[f + 1];
        int nextColumn9 = -1;
        for (int x = 0; x < 8; x++) {
            if (r.events[next + 5 + x].value == tileColour(9, 0)) {
                nextColumn9 = x;
            }
        }
        EXPECT_EQ((column9 + 1) % 8, nextColumn9) << "frame " << f;
        (void) perLine;
    }
}

TEST(TilesTest, TheGameKeepsItsVariablesAndTheBanks) {
    // 30 variables in the banks and the map in the upper half: the kernel leaves FSR with bit 7 clear
    std::string globals;
    std::string body;
    for (int i = 0; i < 30; i++) {
        globals += "uint8_t g" + number(i) + ";\n";
        body += "g" + number(i) + " += " + number(i + 1) + ";\n";
    }
    Result r = runProgram(
        "#include <stdint.h>\n#define XGS_COLUMNS 8\n#define XGS_ROWS 2\n#define XGS_TILE_LINES 4\n#include <xgs.h>\n" + tileTable(4) + globals +
        "void update(void) {\n" + body + "xgs_tile(g0 & 7, 1, g1 & 15);\n}\n"
        "void main(void) { xgs_init(); for (;;) { xgs_frame(); update(); } }\n", 10);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    int rounds = r.ram[r.compiled.variables.at("g0")];
    EXPECT_GE(rounds, 6);
    for (int i = 0; i < 30; i++) {
        EXPECT_EQ(((i + 1) * rounds) & 0xff, r.ram[r.compiled.variables.at("g" + number(i))]) << i;
    }
}

TEST(TilesTest, ReadingATileBack) {
    Result r = runProgram(
        "#include <stdint.h>\n#define XGS_COLUMNS 8\n#define XGS_ROWS 4\n#define XGS_TILE_LINES 4\n#include <xgs.h>\n" + tileTable(4) +
        "uint8_t a; uint8_t b; uint8_t c;\n"
        "void main(void) { xgs_init(); xgs_tile(3, 2, 17); xgs_tile(7, 3, 63); a = xgs_get_tile(3, 2); b = xgs_get_tile(7, 3); c = xgs_get_tile(0, 0); }\n", 2);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    EXPECT_EQ(17, r.ram[r.compiled.variables.at("a")]);
    EXPECT_EQ(63, r.ram[r.compiled.variables.at("b")]);
    EXPECT_EQ(0, r.ram[r.compiled.variables.at("c")]);
}

TEST(TilesTest, TheMapIsAnArrayThatTheGameCanUseDirectly) {
    Result r = runProgram(
        "#include <stdint.h>\n#define XGS_COLUMNS 8\n#define XGS_ROWS 2\n#define XGS_TILE_LINES 16\n#include <xgs.h>\n" + tileTable(16) +
        "void main(void) { xgs_init(); xgs_map[0] = 3 * 16; xgs_map[15] = 5 * 16; for (;;) xgs_frame(); }\n", 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 5u);
    size_t first = frames[3];
    EXPECT_EQ(tileColour(3, 0), r.events[first + 5].value);
    EXPECT_EQ(tileColour(0, 0), r.events[first + 5 + 7].value);
    // the second row starts at line 16
    size_t secondRow = frames[3] + 16 * 14;
    EXPECT_EQ(tileColour(0, 0), r.events[secondRow + 5].value);
    EXPECT_EQ(tileColour(5, 0), r.events[secondRow + 5 + 7].value);
}

// errors

TEST(TilesErrorTest, TheTableIsMissing) {
    CompileResult result = compile("#define XGS_COLUMNS 8\n#include <xgs.h>\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "xgs_tiles")) << result.error;
}

TEST(TilesErrorTest, TheTableHasTheWrongSize) {
    CompileResult result = compile("#include <stdint.h>\n#define XGS_COLUMNS 8\n#include <xgs.h>\nconst uint8_t xgs_tiles[100] = { 1 };\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "256")) << result.error;
}

TEST(TilesErrorTest, TooManyLines) {
    CompileResult result = compile("#include <stdint.h>\n#define XGS_ROWS 20\n#define XGS_TILE_LINES 16\n#include <xgs.h>\nconst uint8_t xgs_tiles[256] = { 1 };\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "255")) << result.error;
}

TEST(TilesErrorTest, TileLinesFrom1To255) {
    for (const char* bad : { "0", "256", "300" }) {
        CompileResult result = compile(std::string("#include <stdint.h>\n#define XGS_ROWS 1\n#define XGS_TILE_LINES ") + bad +
            "\n#include <xgs.h>\nconst uint8_t xgs_tiles[256] = { 1 };\nvoid main(void) { xgs_init(); }", "tiles.c");
        ASSERT_FALSE(result.ok) << bad;
        EXPECT_TRUE(contains(result.error, "XGS_TILE_LINES")) << result.error;
    }
}

TEST(TilesErrorTest, TooManyColumns) {
    CompileResult result = compile("#include <stdint.h>\n#define XGS_COLUMNS 300\n#define XGS_ROWS 1\n#define XGS_TILE_LINES 4\n#include <xgs.h>\nconst uint8_t xgs_tiles[256] = { 1 };\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "XGS_COLUMNS")) << result.error;
}

TEST(TilesErrorTest, TheMapDoesNotFitIntoTheRam) {
    CompileResult result = compile("#include <stdint.h>\n#define XGS_COLUMNS 30\n#define XGS_ROWS 12\n#define XGS_TILE_LINES 4\n#include <xgs.h>\nconst uint8_t xgs_tiles[256] = { 1 };\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "RAM")) << result.error;
}

TEST(TilesErrorTest, TheNumbersHaveToBeNumbers) {
    CompileResult result = compile("#define XGS_COLUMNS lots\n#include <xgs.h>\nvoid main(void) { xgs_init(); }", "tiles.c");
    ASSERT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "XGS_COLUMNS")) << result.error;
}

// the examples that draw tiles fill the screen: about 240 lines, 17 columns

namespace {
std::string readExample(const char* name) {
    std::ifstream file(std::string(EXAMPLES_DIR "/") + name);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// the number of picture lines in the third frame
int pictureLines(const std::string& source) {
    Result r = runProgram(source, 6);
    EXPECT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    if (frames.size() < 5) {
        return -1;
    }
    // every line has 6 writes plus one per column: the events of a frame are lines * (columns + 6) + 2
    return static_cast<int>(frames[3] - frames[2]);
}
}

TEST(TilesExampleTest, TheTileExamplesFillTheScreen) {
    // 17 columns: 23 writes per line, 242 lines
    EXPECT_EQ(242 * 23 + 2, pictureLines(readExample("xgs_tiles.c")));
    EXPECT_EQ(242 * 23 + 2, pictureLines(readExample("xgs_joystick.c")));
    EXPECT_EQ(242 * 23 + 2, pictureLines(readExample("pong.c")));
}
