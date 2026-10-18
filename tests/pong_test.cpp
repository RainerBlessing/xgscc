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
const int PADDLE_TILE = 4;
const int BALL_TILE = 5;
const int LETTER = 6;

}

TEST(PongTest, Compiles) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
}

TEST(PongTest, TheCourtHasWallsAndBothPaddles) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    g.frames(3);
    for (int x = 0; x < 16; x++) {
        EXPECT_EQ(2, g.tile(x, 11)) << x;
    }
    int leftY = g.value("left_y");
    int rightY = g.value("right_y");
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(PADDLE_TILE, g.tile(0, leftY + i));
        EXPECT_EQ(PADDLE_TILE, g.tile(15, rightY + i));
    }
}

TEST(PongTest, TheBallMoves) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    g.frames(3);
    int x = g.value("ball_x");
    int y = g.value("ball_y");
    g.frames(10);
    EXPECT_TRUE(g.value("ball_x") != x || g.value("ball_y") != y);
}

TEST(PongTest, TheBallIsDrawnWhereItIs) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    for (int i = 0; i < 20; i++) {
        g.frames(2);
        EXPECT_EQ(BALL_TILE, g.tile(g.value("ball_x"), g.value("ball_y"))) << i;
    }
}

TEST(PongTest, TheBallStaysInTheCourt) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    for (int i = 0; i < 60; i++) {
        g.frames(3);
        EXPECT_GE(g.value("ball_y"), 1) << i;
        EXPECT_LE(g.value("ball_y"), 10) << i;
        EXPECT_LE(g.value("ball_x"), 15) << i;
    }
}

TEST(PongTest, UpOnTheJoystickMovesThePaddleUpToTheWall) {
    Game g;
    ASSERT_TRUE(g.start(0x11)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(1, g.value("left_y"));
    EXPECT_EQ(PADDLE_TILE, g.tile(0, 1));
    EXPECT_EQ(PADDLE_TILE, g.tile(0, 3));
    EXPECT_NE(PADDLE_TILE, g.tile(0, 4));
}

TEST(PongTest, DownOnTheJoystickMovesThePaddleDown) {
    Game g;
    ASSERT_TRUE(g.start(0x12)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(8, g.value("left_y"));
    EXPECT_EQ(PADDLE_TILE, g.tile(0, 10));
    EXPECT_NE(PADDLE_TILE, g.tile(0, 7));
}

TEST(PongTest, ThePaddlesAreNotLeftBehindOnTheScreen) {
    Game g;
    ASSERT_TRUE(g.start(0x12)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(6, g.tilesOf(PADDLE_TILE));
    EXPECT_EQ(1, g.tilesOf(BALL_TILE));
}

TEST(PongTest, AMissedBallIsAPointForTheOtherSide) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    int guard = 0;
    while (g.value("score_left") + g.value("score_right") == 0 && guard++ < 200) {
        g.frames(10);
    }
    EXPECT_LT(guard, 200) << "no point in 2000 frames";
}

TEST(PongTest, BounceAndScoreMakeSound) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    g.frames(300);
    EXPECT_GT(g.sound.notes, 4);
}

TEST(PongTest, TheScoreIsShownInTheTopRow) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    g.frames(900);
    int shown = 0;
    for (int x = 1; x < 15; x++) {
        shown += g.tile(x, 0) == BLOCK;
    }
    EXPECT_EQ(g.value("score_left") + g.value("score_right"), shown);
}

TEST(PongTest, FitsIntoTheMachine) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    EXPECT_LT(g.compiled.rom.size(), 4097u);
    EXPECT_GT(g.compiled.rom[0], 0);
}

TEST(PongTest, MovingThePaddleDownAndUpLeavesTheWallsAndTheMemoryAlone) {
    Game g;
    ASSERT_TRUE(g.start(0x12)) << g.compiled.error;
    g.frames(60);
    *g.cpu.getJoystrickr() = 0x11;
    g.frames(60);
    for (int x = 0; x < 16; x++) {
        EXPECT_EQ(2, g.tile(x, 11)) << x;
    }
    // the byte behind the map is no part of it
    EXPECT_EQ(0, g.cpu.getRAM()[g.compiled.variables.at("xgs_map") + 192]);
    EXPECT_EQ(1, g.value("left_y"));
}

TEST(PongTest, TheRightPaddleLeavesTheWallsAloneToo) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    for (int i = 0; i < 40; i++) {
        g.frames(20);
        EXPECT_EQ(2, g.tile(15, 11)) << i;
        EXPECT_EQ(2, g.tile(15, 0)) << i;
        EXPECT_EQ(2, g.tile(0, 11)) << i;
    }
}

// the title and the start

TEST(PongTitleTest, WithoutFireTheTitleStaysAndNothingMoves) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(120);
    EXPECT_EQ(0, g.value("playing"));
    EXPECT_EQ(8, g.value("ball_x"));
    EXPECT_EQ(5, g.value("ball_y"));
    EXPECT_EQ(0, g.tilesOf(PADDLE_TILE));
}

TEST(PongTitleTest, TheTitleSpellsPongInFiveByThreeCells) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(3);
    // P: top row 111, left column 1 in rows 3..7; O has a hole in the middle
    EXPECT_EQ(LETTER, g.tile(1, 3));
    EXPECT_EQ(LETTER, g.tile(2, 3));
    EXPECT_EQ(LETTER, g.tile(3, 3));
    EXPECT_EQ(LETTER, g.tile(1, 7));
    EXPECT_NE(LETTER, g.tile(3, 7));
    EXPECT_NE(LETTER, g.tile(6, 5));      // the hole of the O (columns 5-7, middle row)
    EXPECT_EQ(LETTER, g.tile(5, 5));
    EXPECT_EQ(LETTER, g.tile(7, 5));
    EXPECT_EQ(LETTER, g.tile(13, 3));     // G
    // P has 10 cells, O 12, N 11, G 11
    EXPECT_EQ(44, g.tilesOf(LETTER));
}

TEST(PongTitleTest, FireStartsTheGame) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(30);
    EXPECT_EQ(0, g.value("playing"));
    *g.cpu.getJoystrickr() = 0x10;
    g.frames(5);
    EXPECT_EQ(1, g.value("playing"));
    EXPECT_EQ(0, g.tilesOf(LETTER));
    EXPECT_EQ(6, g.tilesOf(PADDLE_TILE));
}

TEST(PongTitleTest, OtherButtonsDoNotStartTheGame) {
    Game g;
    ASSERT_TRUE(g.start(0x0f)) << g.compiled.error;
    g.frames(60);
    EXPECT_EQ(0, g.value("playing"));
}

TEST(PongTitleTest, TheStartMakesASound) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(10);
    int before = g.sound.notes;
    *g.cpu.getJoystrickr() = 0x10;
    g.frames(5);
    EXPECT_GT(g.sound.notes, before);
}

TEST(PongTitleTest, TheBallUnderTheTitleBlinks) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    bool on = false;
    bool off = false;
    for (int i = 0; i < 80; i++) {
        g.frames(1);
        (g.tile(8, 10) == BALL_TILE ? on : off) = true;
    }
    EXPECT_TRUE(on);
    EXPECT_TRUE(off);
}

TEST(PongTitleTest, AGameEndsAtEightPointsAndTheTitleComesBack) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    int guard = 0;
    while (g.value("games") == 0 && guard++ < 400) {
        g.frames(20);
        EXPECT_LE(g.value("score_left"), 8);
        EXPECT_LE(g.value("score_right"), 8);
    }
    ASSERT_EQ(1, g.value("games")) << "no game was finished";
    // fire is still held, but for a second nothing starts
    EXPECT_EQ(0, g.value("playing"));
    g.frames(30);
    EXPECT_EQ(0, g.value("playing"));
    EXPECT_GT(g.tilesOf(LETTER), 0);
    // then it starts again
    g.frames(60);
    EXPECT_EQ(1, g.value("playing"));
    EXPECT_EQ(0, g.value("score_left") + g.value("score_right"));
}
