// Sprites on top of the tile map: a formation of 15 invaders (one sprite: 5 copies in 3 rows, a mask
// for every row says which of them are alive), a cannon that the joystick moves, a shot and the bombs of the
// invaders (4 sprites).
// The ground is a row of tiles. The map is small (3 rows of 80 lines): every sprite keeps 16 bytes of shifted
// bitmaps in the RAM, and the map is the other big part of it.
//
//   left, right  move the cannon      fire  shoots (one shot at a time)
//   every 32 frames an invader drops a bomb on the cannon (the lowest one of the copy that is nearest to it);
//   when a bomb hits the cannon, or the last invader is hit, the invaders start again
//
// Sound, the setup of Rem Pac: channel 1 is a square wave with a fast envelope (shot, hit of an invader, hit of
// the cannon), channel 2 a sine wave with a slow one (the march of the invaders: 4 notes, one every 16 frames).
//
// A sprite is a bitmap of 8 x 8 pixels from the table xgs_sprite_images, with a colour for every pixel
// row. A cell of the map has 8 pixels, so a cell is as wide as a sprite; x counts these pixels (0 to
// 119); the sprites reach both edges of the picture. A pixel row is
// XGS_SPRITE_LINES lines, the rows of copies follow each other without a gap.
#include <stdint.h>
#define XGS_COLUMNS 15
#define XGS_ROWS 3
#define XGS_TILE_LINES 80
#define XGS_SPRITES 4
#define XGS_SPRITE_ROWS 3       // rows of copies of a sprite below each other
#define XGS_SPRITE_STEP 2       // the copies are 2 cells apart
#define XGS_SPRITE_LINES 4      // a pixel row of a sprite is 4 lines high
#include <xgs.h>

#define ALIENS 0
#define CANNON 1
#define SHOT 2
#define BOMB 3

#define EMPTY 0
#define GROUND 1

#define IMAGE_ALIEN_A 0         // the two frames of the invader, the cannon, the shot, the bomb
#define IMAGE_ALIEN_B 1
#define IMAGE_CANNON 2
#define IMAGE_SHOT 3
#define IMAGE_BOMB 4

#define LEFT 0x04
#define RIGHT 0x08
#define FIRE 0x10

#define BLACK 0xf6

#define B8 BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK
#define B16 B8, B8

// one colour for every line of every tile (80 lines: 3 tiles)
const uint8_t xgs_tiles[256] = {
    // tile 0: nothing
    B16, B16, B16, B16, B16,
    // tile 1: the ground, a green-yellow bar of 6 lines in the lines 56 to 61 of the tile (the lines 216 to 221)
    B16, B16, B16, B8,
    0x0a, 0x0b, 0x0c, 0x0c, 0x0b, 0x0a,
    B16, BLACK, BLACK
};

// shape and colour of the 8 pixel rows of an image: a pixel is set where the shape has a bit (the highest
// bit is the pixel on the left); the colour is the byte for the video port (chroma phase and luminance).
const uint8_t xgs_sprite_images[80] = {
    // the invader, legs in
    0x3c, 0x0f, 0x7e, 0x0e, 0xdb, 0x0d, 0xff, 0x0d, 0x5a, 0x0c, 0x81, 0x3b, 0x00, 0x00, 0x00, 0x00,
    // the invader, legs out
    0x3c, 0x0f, 0x7e, 0x0e, 0xdb, 0x0d, 0xff, 0x0d, 0x5a, 0x0c, 0x42, 0x3b, 0x00, 0x00, 0x00, 0x00,
    // the cannon
    0x18, 0xff, 0x18, 0xfe, 0x7e, 0xfd, 0xff, 0xfc, 0xff, 0xfb, 0xff, 0xfa, 0x00, 0x00, 0x00, 0x00,
    // the shot: one pixel wide, two pixel rows high
    0x10, 0xff, 0x10, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // the bomb: a zigzag of three pixel rows
    0x10, 0x4c, 0x08, 0x4c, 0x10, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t bit[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };     // a table: a shift by a variable takes a routine and RAM
const uint8_t march[4] = { 12, 10, 8, 7 };                  // notes, a semitone apart: 0 is A2, at most 31

uint8_t buttons;
uint8_t frames;
uint8_t alien_x;
uint8_t alien_y;
uint8_t going_right;
uint8_t cannon_x;
uint8_t shot_x;
uint8_t shot_y;             // 0: no shot
uint8_t bomb_x;
uint8_t bomb_y;             // 0: no bomb
uint8_t hits;               // how often the cannon was hit
uint8_t rounds;             // how often all invaders were hit

void shoot(void)
{
    // where the shot is (its pixel is the 4th of its bitmap) against the copies: 16 pixels apart, 8 wide
    uint8_t row = (uint8_t) (shot_y - alien_y) >> 5;
    uint8_t across = shot_x + 3 - alien_x;
    if (row < 3 && across < 72 && (across & 15) < 8) {
        uint8_t copy = across >> 4;
        uint8_t mask = xgs_sprites[3 + row];       // the masks of the first sprite are bytes 3, 4, 5 of its record
        if (mask & bit[copy]) {
            xgs_sprite_copies(ALIENS, row, mask & ~bit[copy]);
            shot_y = 0;
            xgs_note(0, 20);
        }
    }
}

// a bomb starts below the lowest invader of the copy that is nearest to the cannon (the next copy if
// that one is gone); its pixel is the 4th of its bitmap, like the one of the shot
void drop(void)
{
    uint8_t across = cannon_x + 4 - alien_x;
    uint8_t copy = 0;
    if (across < 80) {
        copy = across >> 4;
    } else if (across < 128) {
        copy = 4;
    }
    for (uint8_t n = 0; n < 5; n++) {
        uint8_t row = 3;
        while (row) {
            row--;
            if (xgs_sprites[3 + row] & bit[copy]) {
                bomb_x = alien_x + (copy << 4);
                bomb_y = alien_y + (row << 5) + 24;
                return;
            }
        }
        copy++;
        if (copy > 4) {
            copy = 0;
        }
    }
}

// all invaders, in the first place, no shot and no bomb
void start_formation(void)
{
    for (uint8_t row = 0; row < 3; row++) {
        xgs_sprite_copies(ALIENS, row, 0x1f);
    }
    alien_x = 16;
    alien_y = 12;
    going_right = 1;
    shot_y = 0;
    bomb_y = 0;
}

void main(void)
{
    xgs_init();
    xgs_sound(0, 0xc0);                     // channels 1 and 2 on
    xgs_sound(1, 0x18);                     // channel 1: square, envelope, fast
    xgs_sound(1, 0x73);                     // channel 2: sine, envelope, slow, 8.5 dB less
    xgs_sound(3, 0x00);                     // the loudest
    // the map is written directly: xgs_tile(x, 2, GROUND) would take a multiplication (y * 15) and 5 bytes
    for (uint8_t x = 0; x < XGS_COLUMNS; x++) {
        xgs_map[2 * XGS_COLUMNS + x] = GROUND * XGS_TILE_LINES;
    }
    xgs_sprite_copies(CANNON, 0, 1);
    xgs_sprite_copies(SHOT, 0, 0);          // no copy: the shot is not there
    xgs_sprite_copies(BOMB, 0, 0);
    start_formation();
    cannon_x = 56;
    xgs_sprite(CANNON, cannon_x, 200, IMAGE_CANNON);
    xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A);
    xgs_sprite(SHOT, 0, 0, IMAGE_SHOT);
    xgs_sprite(BOMB, 0, 0, IMAGE_BOMB);

    for (;;) {
        xgs_frame();
        frames++;
        buttons = xgs_joystick();

        if ((buttons & LEFT) && cannon_x > 0) {
            cannon_x--;
        }
        if ((buttons & RIGHT) && cannon_x < 104) {
            cannon_x++;
        }
        xgs_sprite(CANNON, cannon_x, 200, IMAGE_CANNON);

        if ((buttons & FIRE) && shot_y == 0) {
            shot_x = cannon_x;
            shot_y = 196;
            xgs_note(0, 31);
        }
        if (shot_y) {
            shot_y -= 4;
            if (shot_y < 8) {
                shot_y = 0;
            } else {
                shoot();
                if ((xgs_sprites[3] | xgs_sprites[4] | xgs_sprites[5]) == 0) {      // the masks of the rows
                    rounds++;
                    start_formation();
                    xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A);
                }
            }
        }
        if (shot_y) {
            xgs_sprite(SHOT, shot_x, shot_y, IMAGE_SHOT);
        }
        xgs_sprite_copies(SHOT, 0, shot_y != 0);

        // the bomb falls 2 lines a frame, the bitmap is 12 lines high and the cannon is on the lines 200 to 223
        if (bomb_y) {
            bomb_y += 2;
            if (bomb_y >= 232) {
                bomb_y = 0;
            } else if (bomb_y + 12 > 200 && (uint8_t) (bomb_x + 3 - cannon_x) < 8) {
                hits++;
                start_formation();
                xgs_note(0, 0);
                xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A);
            }
        } else if ((frames & 31) == 0) {
            drop();
        }
        if (bomb_y) {
            xgs_sprite(BOMB, bomb_x, bomb_y, IMAGE_BOMB);
        }
        xgs_sprite_copies(BOMB, 0, bomb_y != 0);

        // the march: a note every 16 frames, the 4 notes one after the other
        if ((frames & 15) == 0) {
            xgs_note(1, march[(frames >> 4) & 3]);
        }

        // the formation steps sideways and down every 8 frames, the invaders change their legs
        if ((frames & 7) == 0) {
            if (going_right) {
                if (alien_x >= 48) {
                    going_right = 0;
                    alien_y += 8;
                } else {
                    alien_x += 2;
                }
            } else {
                if (alien_x <= 2) {
                    going_right = 1;
                    alien_y += 8;
                } else {
                    alien_x -= 2;
                }
            }
            if (alien_y > 80) {
                alien_y = 12;
            }
            xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A + ((frames >> 3) & 1));
        }
    }
}
