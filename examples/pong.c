// Pong on the tile map: you play the left paddle with the joystick (up, down), the
// computer the right one. The score is the number of blocks in the top row. A point
// to a side when the other one misses; at 8 points the score starts again.
#include <stdint.h>
#define XGS_COLUMNS 16
#define XGS_ROWS 12
#define XGS_TILE_LINES 16
#include <xgs.h>

#define EMPTY 0
#define BLOCK 1
#define WALL 2
#define NET 3

#define TOP 1               // the first and the last row of the court
#define BOTTOM 10
#define PADDLE 3            // cells high
#define LAST_COLUMN 15

// 16 tiles of 16 lines, a byte for the video port per line (chroma, luminance)
const uint8_t xgs_tiles[256] = {
    // 0 empty: black
    0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6,
    // 1 block: white
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // 2 wall: blue
    0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a,
    // 3 net: a short white dash
    0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xff, 0xff, 0xff, 0xff, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6, 0xf6
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
    for (uint8_t x = 0; x < 16; x++) {
        xgs_tile(x, 11, WALL);
    }
    xgs_tile(0, 0, WALL);
    xgs_tile(LAST_COLUMN, 0, WALL);
    for (uint8_t y = TOP; y <= BOTTOM; y++) {
        clear_cell(0, y);
        clear_cell(LAST_COLUMN, y);
        clear_cell(8, y);
    }
    show_score();
    paddle(0, left_y, BLOCK);
    paddle(LAST_COLUMN, right_y, BLOCK);
}

void serve(uint8_t to_the_right) {
    clear_cell(ball_x, ball_y);
    ball_x = 8;
    ball_y = 5;
    ball_right = to_the_right;
    xgs_tile(ball_x, ball_y, BLOCK);
}

void point(uint8_t left_scores) {
    xgs_note(0, 12);
    if (left_scores) {
        score_left++;
    } else {
        score_right++;
    }
    if (score_left == 8 || score_right == 8) {
        score_left = 0;
        score_right = 0;
    }
    show_score();
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
    xgs_tile(ball_x, ball_y, BLOCK);
}

void main(void) {
    xgs_init();
    xgs_sound(0, 0x80);         // channel 1 on
    xgs_sound(1, 0x10);         // square wave with envelope
    xgs_sound(3, 0x00);         // loudest
    court();
    xgs_tile(ball_x, ball_y, BLOCK);

    for (;;) {
        xgs_frame();
        frame++;
        joystick = xgs_joystick();

        if ((frame & 3) == 0) {
            if ((joystick & 1) && left_y > TOP) {
                xgs_tile(0, left_y + PADDLE - 1, EMPTY);
                left_y--;
                paddle(0, left_y, BLOCK);
            }
            if ((joystick & 2) && left_y + PADDLE <= BOTTOM) {
                paddle(0, left_y, EMPTY);
                left_y++;
                paddle(0, left_y, BLOCK);
            }
        }
        if ((frame & 7) == 0) {         // the computer is slower than the ball
            if (right_y + 1 < ball_y && right_y + PADDLE <= BOTTOM) {
                paddle(LAST_COLUMN, right_y, EMPTY);
                right_y++;
                paddle(LAST_COLUMN, right_y, BLOCK);
            }
            if (right_y + 1 > ball_y && right_y > TOP) {
                xgs_tile(LAST_COLUMN, right_y + PADDLE - 1, EMPTY);
                right_y--;
                paddle(LAST_COLUMN, right_y, BLOCK);
            }
        }
        if ((frame & 3) == 1) {
            move_ball();
        }
    }
}
