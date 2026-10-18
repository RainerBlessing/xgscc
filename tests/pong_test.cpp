// examples/pong.c: Pong in C on the tile map, run for some frames on the emulated SX52.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "compiler.h"
#include "cpu.h"
#include "kernel.h"

namespace {

class Sound : public SoundPort {
public:
    int notes;
    Sound() : notes(0) {}
    void portWrite(unsigned char ra) {
        // a strobe (SSTB rising) is a command
        bool strobe = (ra & 0x20) != 0;
        if (strobe && !last) {
            notes++;
        }
        last = strobe;
    }
private:
    bool last = false;
};

class Game {
public:
    CompileResult compiled;
    Cpu cpu;
    Sound sound;

    Game() : cpu(NULL) {}

    bool start(unsigned short joystick) {
        std::ifstream file(EXAMPLES_DIR "/pong.c");
        std::stringstream text;
        text << file.rdbuf();
        compiled = compile(text.str(), "pong.c");
        if (!compiled.ok) {
            return false;
        }
        cpu.reset();
        cpu.setSound(&sound);
        *cpu.getJoystrickr() = joystick;
        for (size_t i = 0; i < compiled.rom.size(); i++) {
            cpu.getROM()[i] = compiled.rom[i];
        }
        return true;
    }

    void frames(int count) {
        unsigned target = cpu.getCycle() + static_cast<unsigned>(XGS_FRAME_CYCLES) * count;
        while (cpu.getCycle() < target) {
            cpu.execute();
        }
    }

    int value(const char* name) {
        return cpu.getRAM()[compiled.variables.at(name)];
    }

    // the tile number at a cell
    int tile(int x, int y) {
        return cpu.getRAM()[compiled.variables.at("xgs_map") + y * 16 + x] / 16;
    }

    int tilesOf(int tileNumber) {
        int count = 0;
        for (int y = 0; y < 12; y++) {
            for (int x = 0; x < 16; x++) {
                count += tile(x, y) == tileNumber;
            }
        }
        return count;
    }
};

const int BLOCK = 1;

}

TEST(PongTest, Compiles) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
}

TEST(PongTest, TheCourtHasWallsAndBothPaddles) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(3);
    for (int x = 0; x < 16; x++) {
        EXPECT_EQ(2, g.tile(x, 11)) << x;
    }
    int leftY = g.value("left_y");
    int rightY = g.value("right_y");
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(BLOCK, g.tile(0, leftY + i));
        EXPECT_EQ(BLOCK, g.tile(15, rightY + i));
    }
}

TEST(PongTest, TheBallMoves) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(3);
    int x = g.value("ball_x");
    int y = g.value("ball_y");
    g.frames(10);
    EXPECT_TRUE(g.value("ball_x") != x || g.value("ball_y") != y);
}

TEST(PongTest, TheBallIsDrawnWhereItIs) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    for (int i = 0; i < 20; i++) {
        g.frames(2);
        EXPECT_EQ(BLOCK, g.tile(g.value("ball_x"), g.value("ball_y"))) << i;
    }
}

TEST(PongTest, TheBallStaysInTheCourt) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    for (int i = 0; i < 60; i++) {
        g.frames(3);
        EXPECT_GE(g.value("ball_y"), 1) << i;
        EXPECT_LE(g.value("ball_y"), 10) << i;
        EXPECT_LE(g.value("ball_x"), 15) << i;
    }
}

TEST(PongTest, UpOnTheJoystickMovesThePaddleUpToTheWall) {
    Game g;
    ASSERT_TRUE(g.start(0x01)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(1, g.value("left_y"));
    EXPECT_EQ(BLOCK, g.tile(0, 1));
    EXPECT_EQ(BLOCK, g.tile(0, 3));
    EXPECT_NE(BLOCK, g.tile(0, 4));
}

TEST(PongTest, DownOnTheJoystickMovesThePaddleDown) {
    Game g;
    ASSERT_TRUE(g.start(0x02)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(8, g.value("left_y"));
    EXPECT_EQ(BLOCK, g.tile(0, 10));
    EXPECT_NE(BLOCK, g.tile(0, 7));
}

TEST(PongTest, ThePaddlesAreNotLeftBehindOnTheScreen) {
    Game g;
    ASSERT_TRUE(g.start(0x02)) << g.compiled.error;
    g.frames(60);
    // 3 cells of the left paddle, 3 of the right, 1 of the ball: nothing else is a block except the score
    EXPECT_LE(g.tilesOf(BLOCK), 3 + 3 + 1 + 16);
    EXPECT_EQ(3, [&] { int c = 0; for (int y = 0; y < 12; y++) c += g.tile(0, y) == BLOCK; return c; }());
}

TEST(PongTest, AMissedBallIsAPointForTheOtherSide) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(900);
    EXPECT_GE(g.value("score_right") + g.value("score_left"), 1);
}

TEST(PongTest, BounceAndScoreMakeSound) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(300);
    EXPECT_GT(g.sound.notes, 4);
}

TEST(PongTest, TheScoreIsShownInTheTopRow) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(900);
    int shown = 0;
    for (int x = 1; x < 15; x++) {
        shown += g.tile(x, 0) == BLOCK;
    }
    EXPECT_EQ(g.value("score_left") + g.value("score_right"), shown);
}

TEST(PongTest, FitsIntoTheMachine) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    EXPECT_LT(g.compiled.rom.size(), 4097u);
    EXPECT_GT(g.compiled.rom[0], 0);
}
