// A tile that follows the joystick; the fire button plays a note.
#include <stdint.h>
#define XGS_COLUMNS 17
#define XGS_ROWS 11
#define XGS_TILE_LINES 22
#include <xgs.h>

const uint8_t xgs_tiles[256] = {
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

uint8_t x = 8;
uint8_t y = 5;
uint8_t joystick;

void main(void) {
    xgs_init();
    xgs_sound(0, 0x80);                 // channel 1 on
    xgs_sound(3, 0x00);                 // full volume
    for (;;) {
        xgs_frame();
        xgs_tile(x, y, 0);
        joystick = xgs_joystick();
        if ((joystick & 0x04) && x > 0) x--;
        if ((joystick & 0x08) && x < XGS_COLUMNS - 1) x++;
        if ((joystick & 0x01) && y > 0) y--;
        if ((joystick & 0x02) && y < XGS_ROWS - 1) y++;
        if (joystick & 0x10) xgs_note(0, 24);
        xgs_tile(x, y, 1);
    }
}
