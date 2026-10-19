// Sprites on top of the tile map: a formation of 15 invaders (one sprite: 5 copies in 3 rows, a mask
// for every row says which of them are alive), a cannon that the joystick moves and a shot.
// The ground is a row of tiles.
//
//   left, right  move the cannon      fire  shoots (one shot at a time)
//
// A sprite is a bitmap of 8 x 8 pixels from the table xgs_sprite_images, with a colour for every pixel
// row. A cell of the map has 8 pixels, so a cell is as wide as a sprite; x counts these pixels (0 to
// 119), and the cell on the left is free of sprites (x is at least 8). A pixel row is
// XGS_SPRITE_LINES lines, the rows of copies follow each other without a gap.
#include <stdint.h>
#define XGS_COLUMNS 15
#define XGS_ROWS 10
#define XGS_TILE_LINES 24
#define XGS_SPRITES 3
#define XGS_SPRITE_ROWS 3       // rows of copies of a sprite below each other
#define XGS_SPRITE_STEP 2       // the copies are 2 cells apart
#define XGS_SPRITE_LINES 4      // a pixel row of a sprite is 4 lines high
#include <xgs.h>

#define ALIENS 0
#define CANNON 1
#define SHOT 2

#define EMPTY 0
#define GROUND 1

#define IMAGE_ALIEN_A 0         // the two frames of the invader, the cannon, the shot
#define IMAGE_ALIEN_B 1
#define IMAGE_CANNON 2
#define IMAGE_SHOT 3

#define LEFT 0x04
#define RIGHT 0x08
#define FIRE 0x10

#define BLACK 0xf6

// one colour for every line of every tile (24 lines: 10 tiles)
const uint8_t xgs_tiles[256] = {
    BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK,
    BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK,
    // the ground: a green-yellow bar on the top
    0x0a, 0x0b, 0x0c, 0x0c, 0x0b, 0x0a, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK,
    BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK
};

// shape and colour of the 8 pixel rows of an image: a pixel is set where the shape has a bit (the highest
// bit is the pixel on the left); the colour is the byte for the video port (chroma phase and luminance).
const uint8_t xgs_sprite_images[64] = {
    // the invader, legs in
    0x3c, 0x0f, 0x7e, 0x0e, 0xdb, 0x0d, 0xff, 0x0d, 0x5a, 0x0c, 0x81, 0x3b, 0x00, 0x00, 0x00, 0x00,
    // the invader, legs out
    0x3c, 0x0f, 0x7e, 0x0e, 0xdb, 0x0d, 0xff, 0x0d, 0x5a, 0x0c, 0x42, 0x3b, 0x00, 0x00, 0x00, 0x00,
    // the cannon
    0x18, 0xff, 0x18, 0xfe, 0x7e, 0xfd, 0xff, 0xfc, 0xff, 0xfb, 0xff, 0xfa, 0x00, 0x00, 0x00, 0x00,
    // the shot: one pixel wide, two pixel rows high
    0x10, 0xff, 0x10, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

uint8_t buttons;
uint8_t frames;
uint8_t alien_x;
uint8_t alien_y;
uint8_t going_right;
uint8_t frame_of_alien;
uint8_t cannon_x;
uint8_t shot_x;
uint8_t shot_y;
uint8_t shot_flies;

void shoot(void)
{
    // where the shot is (its pixel is the 4th of its bitmap) against the copies: 16 pixels apart, 8 wide
    uint8_t row = (uint8_t) (shot_y - alien_y) >> 5;
    uint8_t across = shot_x + 3 - alien_x;
    if (row < 3 && across < 72 && (across & 15) < 8) {
        uint8_t copy = across >> 4;
        uint8_t mask = xgs_sprites[3 + row];       // the masks of the first sprite are bytes 3, 4, 5 of its record
        if (mask & (1 << copy)) {
            xgs_sprite_copies(ALIENS, row, mask & ~(1 << copy));
            shot_flies = 0;
        }
    }
}

void main(void)
{
    xgs_init();
    for (uint8_t x = 0; x < XGS_COLUMNS; x++) {
        xgs_tile(x, 9, GROUND);
    }
    for (uint8_t row = 0; row < 3; row++) {
        xgs_sprite_copies(ALIENS, row, 0x1f);
    }
    xgs_sprite_copies(CANNON, 0, 1);
    xgs_sprite_copies(SHOT, 0, 0);          // no copy: the shot is not there
    alien_x = 16;
    alien_y = 12;
    going_right = 1;
    cannon_x = 56;
    xgs_sprite(CANNON, cannon_x, 200, IMAGE_CANNON);
    xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A);
    xgs_sprite(SHOT, 0, 0, IMAGE_SHOT);

    for (;;) {
        xgs_frame();
        frames++;
        buttons = xgs_joystick();

        if ((buttons & LEFT) && cannon_x > 8) {
            cannon_x--;
        }
        if ((buttons & RIGHT) && cannon_x < 104) {
            cannon_x++;
        }
        xgs_sprite(CANNON, cannon_x, 200, IMAGE_CANNON);

        if ((buttons & FIRE) && !shot_flies) {
            shot_flies = 1;
            shot_x = cannon_x + 0;
            shot_y = 196;
        }
        if (shot_flies) {
            shot_y -= 4;
            if (shot_y < 8) {
                shot_flies = 0;
            } else {
                shoot();
            }
        }
        if (shot_flies) {
            xgs_sprite(SHOT, shot_x, shot_y, IMAGE_SHOT);
        }
        xgs_sprite_copies(SHOT, 0, shot_flies);

        // the formation steps sideways and down every 8 frames, the invaders change their legs
        if ((frames & 7) == 0) {
            frame_of_alien ^= 1;
            if (going_right) {
                if (alien_x >= 48) {
                    going_right = 0;
                    alien_y += 8;
                } else {
                    alien_x += 2;
                }
            } else {
                if (alien_x <= 10) {
                    going_right = 1;
                    alien_y += 8;
                } else {
                    alien_x -= 2;
                }
            }
            if (alien_y > 80) {
                alien_y = 12;
            }
            xgs_sprite(ALIENS, alien_x, alien_y, IMAGE_ALIEN_A + frame_of_alien);
        }
    }
}
