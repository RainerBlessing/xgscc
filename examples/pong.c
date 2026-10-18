// Pong on the tile map: the fire button starts the game. You play the left paddle with
// the joystick (up, down), the computer the right one. The score is the number of
// blocks in the top row. A point to a side when the other one misses; at 8 points the
// game is over and the title comes back.
#include <stdint.h>
#define XGS_COLUMNS 17
#define XGS_ROWS 11
#define XGS_TILE_LINES 22
#include <xgs.h>

#define EMPTY 0
#define BLOCK 1
#define WALL 2
#define NET 3
#define PADDLE_TILE 4
#define BALL 5
#define LETTER 6

#define FIRE 0x10
#define LOCKOUT 60          // frames after a game in which fire does nothing
#define WIN 8

#define TOP 1               // the first and the last row of the court
#define BOTTOM 9
#define PADDLE 3            // cells high
#define LAST_COLUMN 16

// tiles of 22 lines: 11 rows make 242 lines, the height of the picture, and 17 columns have
// a middle column (8) for the net and the title; a byte for the video port per line
// (chroma, luminance); the rest of the 256 bytes is not used
const uint8_t xgs_tiles[256] = {
    // 0 empty: black
    0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6,
    // 1 block: white
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // 2 wall: blue
    0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a,
    // 3 net: a short white dash
    0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6,
    // 4 paddle: magenta, brighter towards the bottom
    0xa8, 0xa8, 0xa8, 0xa8, 0xa8, 0xa9, 0xa9, 0xa9, 0xa9, 0xaa, 0xaa, 0xaa, 0xaa, 0xab, 0xab, 0xab, 0xab, 0xac, 0xac, 0xac, 0xac, 0xad,
    // 5 ball: yellow
    0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
    // 6 title letter: from yellow to magenta
    0x0c, 0x0c, 0x0c, 0x1c, 0x1c, 0x2c, 0x2c, 0x3c, 0x3c, 0x4c, 0x4c, 0x5c, 0x5c, 0x6c, 0x6c, 0x7c, 0x7c, 0x8c, 0x8c, 0x9c, 0x9c, 0xac
};

// the letters of the title: 3 x 5 cells each, one row per byte (bit 2 is the left cell)
const uint8_t letters[20] = {
    7, 5, 7, 4, 4,          // P
    7, 5, 5, 5, 7,          // O
    7, 5, 5, 5, 5,          // N (as a small n)
    7, 4, 5, 5, 7           // G
};

uint8_t left_y = 4;         // the top cell of a paddle
uint8_t right_y = 4;
uint8_t ball_x = 8;
uint8_t ball_y = 5;
uint8_t ball_right = 1;     // the direction of the ball
uint8_t ball_down = 1;
uint8_t score_left;
uint8_t score_right;
uint8_t frame;
uint8_t joystick;
uint8_t playing;            // 0: the title is shown
uint8_t lockout;
uint8_t games;              // games that were played to the end

// what is in a cell without the ball: the net in the middle, else nothing
void clear_cell(uint8_t x, uint8_t y) {
    if (x == 8) {
        xgs_tile(x, y, NET);
    } else {
        xgs_tile(x, y, EMPTY);
    }
}

void paddle(uint8_t x, uint8_t y, uint8_t tile) {
    for (uint8_t i = 0; i < PADDLE; i++) {
        xgs_tile(x, y + i, tile);
    }
}

void show_score(void) {
    for (uint8_t i = 1; i < LAST_COLUMN; i++) {
        xgs_tile(i, 0, WALL);
    }
    for (uint8_t i = 0; i < score_left; i++) {
        xgs_tile(1 + i, 0, BLOCK);
    }
    for (uint8_t i = 0; i < score_right; i++) {
        xgs_tile(LAST_COLUMN - 1 - i, 0, BLOCK);
    }
}

void court(void) {
    for (uint8_t x = 0; x < XGS_COLUMNS; x++) {
        xgs_tile(x, BOTTOM + 1, WALL);
    }
    xgs_tile(0, 0, WALL);
    xgs_tile(LAST_COLUMN, 0, WALL);
    for (uint8_t y = TOP; y <= BOTTOM; y++) {
        clear_cell(0, y);
        clear_cell(LAST_COLUMN, y);
        clear_cell(8, y);
    }
    show_score();
    paddle(0, left_y, PADDLE_TILE);
    paddle(LAST_COLUMN, right_y, PADDLE_TILE);
}

void serve(uint8_t to_the_right) {
    clear_cell(ball_x, ball_y);
    ball_x = 8;
    ball_y = 5;
    ball_right = to_the_right;
    xgs_tile(ball_x, ball_y, BALL);
}

void point(uint8_t left_scores) {
    xgs_note(0, 12);
    if (left_scores) {
        score_left++;
    } else {
        score_right++;
    }
    show_score();
    if (score_left == WIN || score_right == WIN) {
        game_over();
        return;
    }
    serve(left_scores);
}

// is the paddle with its top cell at y in the row of the ball?
uint8_t hits(uint8_t y) {
    return ball_y >= y && ball_y < y + PADDLE;
}

void move_ball(void) {
    clear_cell(ball_x, ball_y);

    if (ball_down) {
        if (ball_y == BOTTOM) {
            ball_down = 0;
            xgs_note(0, 30);
        }
    } else {
        if (ball_y == TOP) {
            ball_down = 1;
            xgs_note(0, 30);
        }
    }
    if (ball_down) {
        ball_y++;
    } else {
        ball_y--;
    }

    if (ball_right) {
        if (ball_x == LAST_COLUMN - 1) {
            if (hits(right_y)) {
                ball_right = 0;
                xgs_note(0, 24);
            } else {
                point(1);
                return;
            }
        }
    } else {
        if (ball_x == 1) {
            if (hits(left_y)) {
                ball_right = 1;
                xgs_note(0, 24);
            } else {
                point(0);
                return;
            }
        }
    }
    if (ball_right) {
        ball_x++;
    } else {
        ball_x--;
    }
    xgs_tile(ball_x, ball_y, BALL);
}

void clear_map(void) {
    for (uint8_t i = 0; i < XGS_COLUMNS * XGS_ROWS; i++) {
        xgs_map[i] = 0;
    }
}

// one letter of the title with its left column at x, from row 3 down
void letter(uint8_t x, uint8_t which) {
    for (uint8_t row = 0; row < 5; row++) {
        uint8_t bits = letters[which * 5 + row];
        for (uint8_t column = 0; column < 3; column++) {
            if (bits & (4 >> column)) {
                xgs_tile(x + column, 3 + row, LETTER);
            }
        }
    }
}

void title(void) {
    clear_map();
    for (uint8_t which = 0; which < 4; which++) {
        letter(1 + which * 4, which);
    }
    xgs_tile(8, 9, BALL);
    playing = 0;
}

void new_game(void) {
    clear_map();
    score_left = 0;
    score_right = 0;
    left_y = 4;
    right_y = 4;
    ball_x = 8;
    ball_y = 5;
    court();
    xgs_tile(ball_x, ball_y, BALL);
    playing = 1;
    xgs_note(0, 36);
}

void game_over(void) {
    games++;
    xgs_note(0, 6);
    lockout = LOCKOUT;
    title();
}

void play(void) {
    if ((frame & 3) == 0) {
        if ((joystick & 1) && left_y > TOP) {
            xgs_tile(0, left_y + PADDLE - 1, EMPTY);
            left_y--;
            paddle(0, left_y, PADDLE_TILE);
        }
        if ((joystick & 2) && left_y + PADDLE <= BOTTOM) {
            xgs_tile(0, left_y, EMPTY);
            left_y++;
            paddle(0, left_y, PADDLE_TILE);
        }
    }
    if ((frame & 7) == 0) {         // the computer is slower than the ball
        if (right_y + 1 < ball_y && right_y + PADDLE <= BOTTOM) {
            xgs_tile(LAST_COLUMN, right_y, EMPTY);
            right_y++;
            paddle(LAST_COLUMN, right_y, PADDLE_TILE);
        }
        if (right_y + 1 > ball_y && right_y > TOP) {
            xgs_tile(LAST_COLUMN, right_y + PADDLE - 1, EMPTY);
            right_y--;
            paddle(LAST_COLUMN, right_y, PADDLE_TILE);
        }
    }
    if ((frame & 3) == 1) {
        move_ball();
    }
}

void main(void) {
    xgs_init();
    xgs_sound(0, 0x80);         // channel 1 on
    xgs_sound(1, 0x10);         // square wave with envelope
    xgs_sound(3, 0x00);         // loudest
    title();

    for (;;) {
        xgs_frame();
        frame++;
        joystick = xgs_joystick();
        if (playing) {
            play();
        } else {
            if (lockout) {
                lockout--;
            } else if (joystick & FIRE) {
                new_game();
            }
            if ((frame & 31) == 0) {            // the ball under the title blinks
                xgs_tile(8, 9, EMPTY);
            }
            if ((frame & 31) == 16) {
                xgs_tile(8, 9, BALL);
            }
        }
    }
}
