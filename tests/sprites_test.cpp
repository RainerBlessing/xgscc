// The tile map with sprites: a few players with 8 pixels, a colour for every line, copies and a
// mask of the copies. The programs run on the emulated SX52 with the writes to RE recorded, and
// every write of a frame (value and cycle) is compared with a model of the picture.
#include <gtest/gtest.h>
#include <algorithm>
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

struct Sprite {
    int x;
    int y;
    int image;
    std::vector<int> masks;     // one for every row of copies
};

struct Scene {
    int columns;
    int rows;
    int tileLines;
    int players;
    int spriteRows;
    int step;
    int pixelLines;
    int images;
    std::vector<Sprite> sprites;
    std::string loop;           // statements that run after every xgs_frame()
};

struct Result {
    CompileResult compiled;
    std::vector<Event> events;
};

std::string number(int value) {
    return std::to_string(value);
}

int tileColour(int tile, int line) {
    return (((tile * 3 + line) % 15) << 4) | (6 + (tile + line) % 10);
}

int imageShape(int image, int row) {
    return (image * 37 + row * 29 + 0x5a) * 13 & 0xff;
}

int imageColour(int image, int row) {
    return (((image * 5 + row * 3 + 1) % 15) << 4) | (6 + (image + row) % 10);
}

int patternTile(int x, int y, int tiles) {
    return (x * 5 + y * 3 + 1) % tiles;
}

std::string tileTable(int tileLines) {
    std::string text = "const uint8_t xgs_tiles[256] = {";
    for (int i = 0; i < 256; i++) {
        text += number(tileColour(i / tileLines, i % tileLines)) + (i < 255 ? "," : "");
    }
    return text + "};\n";
}

std::string imageTable(int images) {
    std::string text = "const uint8_t xgs_sprite_images[" + number(images * 16) + "] = {";
    for (int image = 0; image < images; image++) {
        for (int row = 0; row < 8; row++) {
            text += number(imageShape(image, row)) + "," + number(imageColour(image, row)) + (image == images - 1 && row == 7 ? "" : ",");
        }
    }
    return text + "};\n";
}

std::string program(const Scene& s) {
    std::string text = "#include <stdint.h>\n"
                       "#define XGS_COLUMNS " + number(s.columns) + "\n#define XGS_ROWS " + number(s.rows) + "\n#define XGS_TILE_LINES " + number(s.tileLines) + "\n"
                       "#define XGS_SPRITES " + number(s.players) + "\n#define XGS_SPRITE_ROWS " + number(s.spriteRows) + "\n"
                       "#define XGS_SPRITE_STEP " + number(s.step) + "\n#define XGS_SPRITE_LINES " + number(s.pixelLines) + "\n"
                       "#include <xgs.h>\n" + tileTable(s.tileLines) + imageTable(s.images);
    const int tiles = 256 / s.tileLines;
    text += "void main(void) {\n xgs_init();\n"
            " for (uint8_t y = 0; y < " + number(s.rows) + "; y++) for (uint8_t x = 0; x < " + number(s.columns) + "; x++) xgs_tile(x, y, (x * 5 + y * 3 + 1) % " + number(tiles) + ");\n";
    for (size_t i = 0; i < s.sprites.size(); i++) {
        const Sprite& sprite = s.sprites[i];
        text += " xgs_sprite(" + number(static_cast<int>(i)) + ", " + number(sprite.x) + ", " + number(sprite.y) + ", " + number(sprite.image) + ");\n";
        for (size_t row = 0; row < sprite.masks.size(); row++) {
            text += " xgs_sprite_copies(" + number(static_cast<int>(i)) + ", " + number(static_cast<int>(row)) + ", " + number(sprite.masks[row]) + ");\n";
        }
    }
    text += " for (;;) {\n  xgs_frame();\n" + s.loop + "\n }\n}\n";
    return text;
}

Result run(const Scene& s, int frames) {
    Result result;
    result.compiled = compile(program(s), "sprites.c");
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

int spritePixels = 0;       // the pixels of sprites that the model has in the frame it was asked for

// the writes of a line of the picture, from the model
std::vector<Event> expectedLine(const Scene& s, int line) {
    const int pitch = XGS_TILE_AREA / (s.columns * 8);
    const int cell = pitch * 8;
    const int tiles = 256 / s.tileLines;
    const int height = s.spriteRows * 8 * s.pixelLines;
    std::vector<int> mask(s.columns + 2, 0);
    std::vector<int> colour(s.columns + 2, 0);
    for (size_t u = 0; u < s.sprites.size(); u++) {
        const Sprite& sprite = s.sprites[u];
        int d = (line - sprite.y) & 0xff;
        if (d >= height) {
            continue;
        }
        int row = d / (8 * s.pixelLines);
        int pixelRow = (d / s.pixelLines) & 7;
        int shape = imageShape(sprite.image, pixelRow);
        int sprColour = imageColour(sprite.image, pixelRow);
        int shift = sprite.x & 7;
        int hi = shape >> shift;
        int lo = (shape << (8 - shift)) & 0xff;
        for (int copy = 0; copy < 8; copy++) {
            if (!((sprite.masks[row] >> copy) & 1)) {
                continue;
            }
            int start = (sprite.x >> 3) + copy * s.step;
            if (start < 16 && start < s.columns) {
                mask[start] |= hi;
                colour[start] = sprColour;
            }
            if (start + 1 < 16 && start + 1 < s.columns) {
                mask[start + 1] |= lo;
                colour[start + 1] = sprColour;
            }
        }
    }
    // the cell on the left has sprites like all the others
    std::vector<Event> events;
    const int prefix[5] = { 0xf6, 0xf0, 0xf6, 0x05, 0xf6 };
    const unsigned prefixAt[5] = { 0, 120, 496, 544, 744 };
    for (int k = 0; k < 5; k++) {
        Event e = { prefixAt[k], prefix[k] };
        events.push_back(e);
    }
    for (int k = 0; k < s.columns; k++) {
        int bg = tileColour(patternTile(k, line / s.tileLines, tiles), line % s.tileLines);
        for (int j = 0; j < 8; j++) {
            bool set = (mask[k] >> (7 - j)) & 1;
            Event e = { static_cast<unsigned>(872 + k * cell + j * pitch), set ? colour[k] : bg };
            if (set) {
                spritePixels++;
            }
            events.push_back(e);
        }
    }
    Event end = { static_cast<unsigned>(872 + s.columns * cell), 0xf6 };
    events.push_back(end);
    return events;
}

// every line of a frame against the model; returns the number of mistakes (the first ones are reported)
int checkFrame(const Result& r, const Scene& s, size_t frame) {
    spritePixels = 0;
    std::vector<size_t> frames = frameStarts(r.events);
    EXPECT_GT(frames.size(), frame + 1);
    if (frames.size() <= frame + 1) {
        return 1;
    }
    const int lines = s.rows * s.tileLines;
    const size_t perLine = 6 + 8 * s.columns;
    EXPECT_EQ(static_cast<size_t>(lines) * perLine + 2, frames[frame + 1] - frames[frame]);
    int mistakes = 0;
    size_t a = frames[frame];
    for (int line = 0; line < lines && mistakes < 12; line++) {
        std::vector<Event> expected = expectedLine(s, line);
        size_t first = a + line * perLine;
        unsigned base = r.events[first].cycle;
        for (size_t i = 0; i < expected.size() && mistakes < 12; i++) {
            const Event& event = r.events[first + i];
            if (event.value != expected[i].value || event.cycle - base != expected[i].cycle) {
                ADD_FAILURE() << "line " << line << " write " << i << ": " << std::hex << event.value << " at " << std::dec << event.cycle - base
                              << ", expected " << std::hex << expected[i].value << " at " << std::dec << expected[i].cycle;
                mistakes++;
            }
        }
        if (line > 0) {
            unsigned before = r.events[first - 1].cycle;
            (void) before;
        }
    }
    return mistakes;
}

Scene basic() {
    Scene s;
    s.columns = 15;
    s.rows = 10;
    s.tileLines = 24;
    s.players = 1;
    s.spriteRows = 1;
    s.step = 2;
    s.pixelLines = 1;
    s.images = 2;
    Sprite sprite = { 20, 30, 0, std::vector<int>(1, 1) };
    s.sprites.push_back(sprite);
    return s;
}

}  // namespace

void expectScene(const Scene& s) {
    Result r = run(s, 6);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    EXPECT_EQ(0, checkFrame(r, s, 3));
    EXPECT_GE(spritePixels, 1) << "the model has no sprite pixel in the frame: the test shows nothing";
}

TEST(SpritesTest, OneSpriteIsDrawnOverTheTiles) {
    expectScene(basic());
}

TEST(SpritesTest, EveryPositionInsideACellShiftsTheBitmap) {
    for (int x = 8; x < 16; x++) {
        Scene s = basic();
        s.sprites[0].x = 24 + x;
        expectScene(s);
    }
}

TEST(SpritesTest, ASpriteReachesTheLeftEdgeOfThePicture) {
    for (int x = 0; x < 8; x++) {
        Scene s = basic();
        s.sprites[0].x = x;
        s.sprites[0].masks[0] = 0x07;
        expectScene(s);
    }
}

TEST(SpritesTest, ASecondSpriteInTheCellOnTheLeft) {
    Scene s = basic();
    s.players = 2;
    s.sprites[0].x = 5;
    Sprite second = { 2, 33, 1, std::vector<int>(1, 3) };
    s.sprites.push_back(second);
    expectScene(s);
}

TEST(SpritesTest, SpritesBehindTheRightEdgeAreCut) {
    Scene s = basic();
    s.sprites[0].x = 8 * 14 + 5;
    expectScene(s);
}

TEST(SpritesTest, ASpriteBelowTheEndOfTheLineNumbersComesInAtTheTop) {
    Scene s = basic();
    s.sprites[0].y = 252;       // its lines 252 to 259: the last four are the lines 0 to 3
    expectScene(s);
}

TEST(SpritesTest, ASpriteOnTheFirstAndTheLastLine) {
    Scene s = basic();
    s.sprites[0].y = 0;
    expectScene(s);
    s.sprites[0].y = 233;
    expectScene(s);
    s.sprites[0].y = 237;
    expectScene(s);
}

TEST(SpritesTest, CopiesAtAFixedDistanceAndAMaskWithHoles) {
    Scene s = basic();
    s.sprites[0].x = 8 + 3;
    s.sprites[0].masks[0] = 0x2d;
    expectScene(s);
    s.step = 3;
    s.sprites[0].masks[0] = 0x07;
    expectScene(s);
    s.step = 5;
    s.sprites[0].masks[0] = 0xff;
    expectScene(s);
}

TEST(SpritesTest, RowsOfCopiesHaveTheirOwnMask) {
    Scene s = basic();
    s.spriteRows = 3;
    s.sprites[0].y = 20;
    s.sprites[0].x = 8 + 6;
    s.sprites[0].masks.clear();
    s.sprites[0].masks.push_back(0x15);
    s.sprites[0].masks.push_back(0x0a);
    s.sprites[0].masks.push_back(0x1f);
    expectScene(s);
}

TEST(SpritesTest, PixelRowsOfSeveralLines) {
    for (int lines = 1; lines <= 8; lines *= 2) {
        Scene s = basic();
        s.pixelLines = lines;
        s.sprites[0].y = 17;
        s.sprites[0].image = 1;
        expectScene(s);
    }
}

TEST(SpritesTest, TwoSpritesWithPriority) {
    Scene s = basic();
    s.players = 2;
    s.sprites[0].x = 8 + 4;
    s.sprites[0].y = 40;
    Sprite second = { 8 + 9, 43, 1, std::vector<int>(1, 3) };
    s.sprites.push_back(second);
    expectScene(s);
}

TEST(SpritesTest, ThreeSprites) {
    Scene s = basic();
    s.players = 3;
    s.sprites[0].x = 8 + 4;
    s.sprites[0].y = 40;
    Sprite second = { 8 + 13, 44, 1, std::vector<int>(1, 1) };
    Sprite third = { 8 + 70, 38, 0, std::vector<int>(1, 5) };
    s.sprites.push_back(second);
    s.sprites.push_back(third);
    expectScene(s);
}

TEST(SpritesTest, ASpriteWithoutCopiesIsNotThere) {
    Scene s = basic();
    s.sprites[0].masks[0] = 0;
    Result r = run(s, 6);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    EXPECT_EQ(0, checkFrame(r, s, 3));
    EXPECT_EQ(0, spritePixels);
}

TEST(SpritesTest, FewerColumnsHaveWiderPixels) {
    for (int columns = 8; columns <= 14; columns += 3) {
        Scene s = basic();
        s.columns = columns;
        s.sprites[0].x = 8 + 5;
        expectScene(s);
    }
}

TEST(SpritesTest, ASpriteThatMovesFromFrameToFrame) {
    Scene s = basic();
    s.sprites[0].x = 16;
    s.loop = "  xgs_sprite(0, xgs_sprites[0] + 1, 30, 0);";
    Result r = run(s, 9);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    for (int frame = 2; frame < 6; frame++) {
        Scene shown = s;
        shown.sprites[0].x = 16 + frame;
        EXPECT_EQ(0, checkFrame(r, shown, frame)) << "frame " << frame;
        EXPECT_GE(spritePixels, 1);
    }
}

TEST(SpritesTest, TheCopiesAreMadeAgainWhenTheMaskChanges) {
    Scene s = basic();
    s.players = 2;
    s.spriteRows = 2;
    s.sprites[0].masks.push_back(0x11);
    s.sprites[0].y = 20;
    Sprite second = { 8 + 40, 24, 1, std::vector<int>(2, 0x01) };
    s.sprites.push_back(second);
    const int record = 3 + 3 * 2 + 5;       // x, y, image, 2 masks, 2 patterns, the state of the kernel
    s.loop = "  xgs_sprite_copies(1, 1, xgs_sprites[" + number(record + 3 + 1) + "] << 1);";
    Result r = run(s, 9);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    // the second sprite is the second record: its second row of copies gets a copy more every frame
    for (int frame = 2; frame < 5; frame++) {
        Scene shown = s;
        shown.sprites[1].masks[1] = 1 << frame;
        EXPECT_EQ(0, checkFrame(r, shown, frame)) << "frame " << frame;
    }
}

TEST(SpritesTest, TheFrameKeepsItsPeriodAndTheVerticalSync) {
    Scene s = basic();
    s.players = 3;
    s.spriteRows = 3;
    s.sprites[0].masks = std::vector<int>(3, 0x1f);
    Sprite second = { 8 + 13, 44, 1, std::vector<int>(3, 0x03) };
    Sprite third = { 8 + 70, 38, 0, std::vector<int>(3, 0x05) };
    s.sprites.push_back(second);
    s.sprites.push_back(third);
    Result r = run(s, 8);
    ASSERT_TRUE(r.compiled.ok) << r.compiled.error;
    std::vector<size_t> frames = frameStarts(r.events);
    ASSERT_GE(frames.size(), 6u);
    for (size_t i = 1; i + 1 < frames.size(); i++) {
        long period = static_cast<long>(r.events[frames[i + 1] - 1].cycle) - static_cast<long>(r.events[frames[i] - 1].cycle);
        EXPECT_NEAR(XGS_FRAME_CYCLES, period, 8) << "frame " << i;
        EXPECT_EQ(static_cast<unsigned>(XGS_VSYNC_CYCLES), r.events[frames[i]].cycle - r.events[frames[i] - 1].cycle) << "frame " << i;
    }
}

namespace {

std::string errorOf(const std::string& defines, bool tables = true) {
    std::string source = "#include <stdint.h>\n#define XGS_COLUMNS 15\n#define XGS_ROWS 10\n#define XGS_TILE_LINES 24\n" + defines + "#include <xgs.h>\n" +
                         tileTable(24) + (tables ? imageTable(2) : "") + "void main(void) { xgs_init(); for (;;) xgs_frame(); }\n";
    CompileResult compiled = compile(source, "sprites.c");
    return compiled.ok ? "" : compiled.error;
}

}  // namespace

TEST(SpritesTest, WhatIsWrongWithTheSetupIsSaid) {
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 1\n", false).find("xgs_sprite_images"));
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 7\n").find("XGS_SPRITES"));
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 1\n#define XGS_SPRITE_LINES 3\n").find("XGS_SPRITE_LINES"));
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 1\n#define XGS_SPRITE_ROWS 6\n").find("XGS_SPRITE_ROWS"));
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 1\n#define XGS_SPRITE_STEP 1\n").find("XGS_SPRITE_STEP"));
    EXPECT_NE(std::string::npos, errorOf("#define XGS_SPRITES 1\n#define XGS_PIXELS 4\n").find("XGS_PIXELS"));
    EXPECT_EQ("", errorOf("#define XGS_SPRITES 1\n"));
}

TEST(SpritesTest, TooManyColumnsForTheNarrowPixels) {
    std::string source = "#include <stdint.h>\n#define XGS_COLUMNS 16\n#define XGS_ROWS 10\n#define XGS_TILE_LINES 24\n#define XGS_SPRITES 1\n#include <xgs.h>\n" +
                         tileTable(24) + imageTable(2) + "void main(void) { xgs_init(); for (;;) xgs_frame(); }\n";
    CompileResult compiled = compile(source, "sprites.c");
    EXPECT_FALSE(compiled.ok);
    EXPECT_NE(std::string::npos, compiled.error.find("XGS_COLUMNS"));
}

// examples/xgs_sprites.c: the invaders, the cannon and the shot, with the fire button held
TEST(SpritesTest, TheExampleRunsAndTheShotFlies) {
    std::ifstream file(EXAMPLES_DIR "/xgs_sprites.c");
    std::stringstream text;
    text << file.rdbuf();
    CompileResult compiled = compile(text.str(), "xgs_sprites.c");
    ASSERT_TRUE(compiled.ok) << compiled.error;
    Recorder recorder;
    Cpu cpu(&recorder);
    cpu.reset();
    *cpu.getJoystrickr() = 0x10;
    for (size_t i = 0; i < compiled.rom.size(); i++) {
        cpu.getROM()[i] = compiled.rom[i];
    }
    while (cpu.getCycle() < static_cast<unsigned>(XGS_FRAME_CYCLES) * 12) {
        cpu.execute();
    }
    EXPECT_LT(cpu.getRAM()[compiled.variables.at("shot_y")], 196);
    std::vector<size_t> frames = frameStarts(recorder.events);
    ASSERT_GE(frames.size(), 6u);
    EXPECT_EQ(static_cast<size_t>(240) * (6 + 8 * 15) + 2, frames[4] - frames[3]);
}
