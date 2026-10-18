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

    void poke(const char* name, int value) {
        cpu.getRAM()[compiled.variables.at(name)] = value;
    }

    // The game logic runs between two calls of xgs_frame(). These stop the program inside the
    // picture kernel, where no game code runs and the variables can be changed.
    bool inKernel() {
        for (size_t i = 0; i < compiled.map.rom.size(); i++) {
            const RomEntry& entry = compiled.map.rom[i];
            if (entry.name == "xgs_frame") {
                int pc = *cpu.getPC();
                return pc >= entry.address && pc < entry.address + entry.words;
            }
        }
        return false;
    }

    void enterKernel() {
        while (!inKernel()) {
            cpu.execute();
        }
    }

    void leaveKernel() {
        while (inKernel()) {
            cpu.execute();
        }
    }

    // Sets the state of the ball and the paddles just before a move of the ball (the ball
    // moves in the frames after which frame & 3 is 1; no paddle moves in that frame), lets
    // the game run through that frame and stops again in the kernel.
    void moveBallFrom(int x, int y, int right, int down, int leftY, int rightY) {
        enterKernel();
        while ((value("frame") & 3) != 0) {
            leaveKernel();
            enterKernel();
        }
        poke("ball_x", x);
        poke("ball_y", y);
        poke("ball_right", right);
        poke("ball_down", down);
        poke("left_y", leftY);
        poke("right_y", rightY);
        poke("score_left", 0);
        poke("score_right", 0);
        leaveKernel();
        enterKernel();
    }

    // the tile number at a cell
    int tile(int x, int y) {
        return cpu.getRAM()[compiled.variables.at("xgs_map") + y * 17 + x] / 22;
    }

    int tilesOf(int tileNumber) {
        int count = 0;
        for (int y = 0; y < 11; y++) {
            for (int x = 0; x < 17; x++) {
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
    for (int x = 0; x < 17; x++) {
        EXPECT_EQ(2, g.tile(x, 10)) << x;
    }
    int leftY = g.value("left_y");
    int rightY = g.value("right_y");
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(PADDLE_TILE, g.tile(0, leftY + i));
        EXPECT_EQ(PADDLE_TILE, g.tile(16, rightY + i));
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
        EXPECT_LE(g.value("ball_y"), 9) << i;
        EXPECT_LE(g.value("ball_x"), 16) << i;
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
    EXPECT_EQ(7, g.value("left_y"));
    EXPECT_EQ(PADDLE_TILE, g.tile(0, 9));
    EXPECT_NE(PADDLE_TILE, g.tile(0, 6));
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
    for (int x = 1; x < 16; x++) {
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
    for (int x = 0; x < 17; x++) {
        EXPECT_EQ(2, g.tile(x, 10)) << x;
    }
    // the byte behind the map is no part of it
    EXPECT_EQ(0, g.cpu.getRAM()[g.compiled.variables.at("xgs_map") + 187]);
    EXPECT_EQ(1, g.value("left_y"));
}

TEST(PongTest, TheRightPaddleLeavesTheWallsAloneToo) {
    Game g;
    ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
    for (int i = 0; i < 40; i++) {
        g.frames(20);
        EXPECT_EQ(2, g.tile(16, 10)) << i;
        EXPECT_EQ(2, g.tile(16, 0)) << i;
        EXPECT_EQ(2, g.tile(0, 10)) << i;
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
        (g.tile(8, 9) == BALL_TILE ? on : off) = true;
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

TEST(PongTitleTest, TheTitleIsInTheMiddleOfTheScreen) {
    Game g;
    ASSERT_TRUE(g.start(0)) << g.compiled.error;
    g.frames(3);
    int left = 99, right = -1, top = 99, bottom = -1;
    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 17; x++) {
            if (g.tile(x, y) == LETTER) {
                left = x < left ? x : left;
                right = x > right ? x : right;
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
        }
    }
    EXPECT_EQ(16 - right, left);        // as many empty columns on the left as on the right
    EXPECT_EQ(10 - bottom, top);        // and as many empty rows above as below
}

// the collision of the ball and the paddles

class PongCollisionTest : public ::testing::Test {
protected:
    Game g;
    void SetUp() override {
        ASSERT_TRUE(g.start(0x10)) << g.compiled.error;
        g.frames(3);
        ASSERT_EQ(1, g.value("playing"));
    }
};

// The ball next to a paddle is caught if it is in a row of the paddle as drawn, whichever way it moves on.
TEST_F(PongCollisionTest, TheBallAtTheRightPaddleBouncesOnEveryRowOfIt) {
    for (int down = 0; down <= 1; down++) {
        for (int paddle = 1; paddle <= 7; paddle++) {
            for (int row = 1; row <= 9; row++) {
                g.moveBallFrom(15, row, 1, down, 4, paddle);
                bool hit = row >= paddle && row < paddle + 3;
                std::string where = std::string(down ? "down" : "up") + ", paddle " + std::to_string(paddle) + ", ball row " + std::to_string(row);
                if (hit) {
                    int next = down ? (row == 9 ? 8 : row + 1) : (row == 1 ? 2 : row - 1);
                    EXPECT_EQ(0, g.value("ball_right")) << where;
                    EXPECT_EQ(14, g.value("ball_x")) << where;
                    EXPECT_EQ(next, g.value("ball_y")) << where;
                    EXPECT_EQ(0, g.value("score_left") + g.value("score_right")) << where;
                } else {
                    EXPECT_EQ(1, g.value("score_left")) << where;
                    EXPECT_EQ(0, g.value("score_right")) << where;
                    EXPECT_EQ(8, g.value("ball_x")) << where;     // served again
                }
            }
        }
    }
}

TEST_F(PongCollisionTest, TheBallAtTheLeftPaddleBouncesOnEveryRowOfIt) {
    for (int down = 0; down <= 1; down++) {
        for (int paddle = 1; paddle <= 7; paddle++) {
            for (int row = 1; row <= 9; row++) {
                g.moveBallFrom(1, row, 0, down, paddle, 4);
                bool hit = row >= paddle && row < paddle + 3;
                std::string where = std::string(down ? "down" : "up") + ", paddle " + std::to_string(paddle) + ", ball row " + std::to_string(row);
                if (hit) {
                    int next = down ? (row == 9 ? 8 : row + 1) : (row == 1 ? 2 : row - 1);
                    EXPECT_EQ(1, g.value("ball_right")) << where;
                    EXPECT_EQ(2, g.value("ball_x")) << where;
                    EXPECT_EQ(next, g.value("ball_y")) << where;
                    EXPECT_EQ(0, g.value("score_left") + g.value("score_right")) << where;
                } else {
                    EXPECT_EQ(1, g.value("score_right")) << where;
                    EXPECT_EQ(0, g.value("score_left")) << where;
                    EXPECT_EQ(8, g.value("ball_x")) << where;
                }
            }
        }
    }
}

TEST_F(PongCollisionTest, ABallThatTouchesTheEdgeOfThePaddleIsCaught) {
    // the paddle is drawn at rows 3..5 and the ball next to its last row, on its way down
    g.moveBallFrom(15, 5, 1, 1, 4, 3);
    EXPECT_EQ(0, g.value("ball_right"));
    EXPECT_EQ(0, g.value("score_left"));
    // and next to its first row, on its way up
    g.moveBallFrom(15, 3, 1, 0, 4, 3);
    EXPECT_EQ(0, g.value("ball_right"));
}

TEST_F(PongCollisionTest, ABallThatOnlyTouchesTheCornerOfThePaddleIsMissed) {
    g.moveBallFrom(15, 2, 1, 1, 4, 3);      // one row above the paddle, moving down towards it
    EXPECT_EQ(1, g.value("score_left"));
    g.moveBallFrom(15, 6, 1, 0, 4, 3);      // one row below the paddle, moving up towards it
    EXPECT_EQ(1, g.value("score_left"));
}

TEST_F(PongCollisionTest, TheBallMovesOneCellAtATimeAndNeverReachesAPaddleColumn) {
    g.moveBallFrom(14, 5, 1, 1, 4, 4);
    EXPECT_EQ(15, g.value("ball_x"));
    EXPECT_EQ(6, g.value("ball_y"));
    g.moveBallFrom(2, 5, 0, 1, 4, 4);
    EXPECT_EQ(1, g.value("ball_x"));
}

TEST_F(PongCollisionTest, TheBallBouncesOffTheTopAndTheBottomWall) {
    g.moveBallFrom(8, 1, 1, 0, 4, 4);          // going up at the top row
    EXPECT_EQ(2, g.value("ball_y"));
    EXPECT_EQ(1, g.value("ball_down"));
    g.moveBallFrom(8, 9, 1, 1, 4, 4);          // going down at the bottom row
    EXPECT_EQ(8, g.value("ball_y"));
    EXPECT_EQ(0, g.value("ball_down"));
}

TEST_F(PongCollisionTest, ABounceIsDrawnAndMakesASound) {
    g.moveBallFrom(15, 4, 1, 1, 4, 5);
    int notes = g.sound.notes;
    g.moveBallFrom(15, 4, 1, 1, 4, 5);
    EXPECT_GT(g.sound.notes, notes);
    EXPECT_EQ(BALL_TILE, g.tile(g.value("ball_x"), g.value("ball_y")));
}
