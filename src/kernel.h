#ifndef KERNEL_H_
#define KERNEL_H_
#include <string>

/**
 * The video kernel of the XGS ME. The picture is a signal that the program writes
 * to port RE, to the clock cycle; see doc/video.md. The kernel is assembler that
 * the compiler does not touch.
 */

const int XGS_LINE_CYCLES = 5080;       // 63.5 us at 80 MHz
const int XGS_VSYNC_CYCLES = 19283;     // the long sync that starts a frame
const int XGS_TILE_AREA = 4090;			// cycles for the cells of a line: what xgsemu shows of the picture (560 pixels of about 7.3 cycles)
const int XGS_FRAME_CYCLES = 1330956;   // 60.1 Hz, like the example programs

/**
 * The assembler of the routine that sets up the video: port RE is an output, the
 * RTCC counts every 256th cycle, the level is blanking. No registers. It ends with
 * retp and has no label of its own: the caller names it.
 */
std::string xgsInitSource();

/**
 * The assembler of the routine that makes a frame: it waits until the frame period
 * since the last frame is over (the RTCC measures what the game has done
 * meanwhile, up to 65 000 cycles), then makes the vertical sync and `lines`
 * picture lines of one colour (5080 cycles each), and returns with the level
 * held at blanking. `lines` has to be 1 to 255.
 *
 * It uses two registers, written {a} and {n} in the text, which have to be
 * direct registers: a bank instruction in the middle would change the timing.
 * It ends with retp, has no label of its own, and has to stay on one page.
 *
 * colour is the byte for RE: luminance in the low nibble, chroma in the high one.
 */
std::string xgsFrameSource(int lines, int colour);

/**
 * xgs_frame() for the tile map: a picture of columns x rows tiles, every tile
 * tileLines lines high. The map is the array xgs_map (one byte per tile: the
 * tile number times tileLines), the graphics are the table xgs_tiles of 256 bytes
 * (one colour for every line of a tile). {s}, {r}, {acc_hi} and {acc_lo} are the
 * registers. Every cell is XGS_TILE_AREA / columns cycles wide.
 */
std::string xgsTileFrameSource(int columns, int rows, int tileLines);

/** The fastest pixel clock of the hi-res tiles: the cycles between two pixels (see xgsHiresFrameSource). */
const int XGS_HIRES_MIN_PITCH = 42;

/**
 * xgs_frame() for hi-res tiles: every cell of the map is 8 pixel rows of up to 8 pixels,
 * drawn with a shape and a colour for every pixel row (the picture is columns * pixels pixels
 * wide). The map is the array xgs_map (one byte per tile: the tile number). The graphics are
 * the table xgs_tiles: for every tile 8 pairs of bytes, the shape (bit 7 is the left pixel,
 * a set bit is a pixel of the colour) and the colour (a value for RE); the other pixels are
 * black. The pairs of tile t are at 16 * t, the first of them is the top pixel row. A pixel
 * row is drawn in tileLines / 8 lines. {s}, {sh}, {col}, {acc_hi} and {acc_lo} are the
 * registers. Every pixel is XGS_TILE_AREA / (columns * pixels) cycles wide, at least
 * XGS_HIRES_MIN_PITCH.
 */
std::string xgsHiresFrameSource(int columns, int rows, int tileLines, int pixels);

/** The most sprites of the sprite kernel: every one takes about 150 cycles in the start of a line (374, 198 and 118 cycles are free between its writes). */
const int XGS_MAX_SPRITES = 3;
/** The widest picture and the narrowest pixel of the sprite kernel: 8 pixels in every cell, 34 cycles wide with 15 columns. */
const int XGS_SPRITE_MAX_COLUMNS = 15;
const int XGS_SPRITE_MIN_PITCH = 32;

/** What the sprite kernel is made for (XGS_COLUMNS, XGS_ROWS, XGS_TILE_LINES, XGS_SPRITES, XGS_SPRITE_ROWS, XGS_SPRITE_STEP, XGS_SPRITE_LINES). */
struct SpriteConfig
{
    int columns;
    int rows;
    int tileLines;
    int players;        // sprites
    int spriteRows;     // copies of a sprite below each other (a mask of copies for each row)
    int step;           // cells from one copy to the next
    int pixelLines;     // lines of a pixel row of a sprite: 1, 2, 4 or 8
};

/**
 * The bytes of a record of the table xgs_sprites: x, y, image * 16, the mask of every row of copies,
 * the 16 bit pattern of every row (made by xgs_sprite_update) and 5 bytes of state of the kernel.
 */
int xgsSpriteRecord(int spriteRows);
/** Throws a CompileError if the sprite kernel cannot be made for this configuration. */
void xgsCheckSprites(const SpriteConfig& config);

/**
 * xgs_sprite_frame(): xgs_frame() for the tile map with sprites: the tiles of the map (xgs_map,
 * xgs_tiles) with up to XGS_MAX_SPRITES sprites on top. Every cell has 8 pixels that are either the
 * colour of the tile or, where the shape of a sprite has a set bit, the colour of the sprite. A sprite
 * is a bitmap of 8 pixels for every one of its 8 pixel rows (table xgs_sprite_images: 16 bytes for
 * an image, shape and colour of 8 rows), at x (the pixel of the first copy, 0 to 8 * columns - 1), y (the
 * first line; the lines count modulo 256), with copies every
 * config.step cells and a mask for them. {o}, {bg}, {col}, {no}, {acc_hi} and {acc_lo} are the
 * registers; the arrays are xgs_map, xgs_sprites and, one byte each, xgs_ln, xgs_s, xgs_r, xgs_mp,
 * xgs_cnt, xgs_up, xgs_ret, xgs_m0, xgs_k0. A line takes the same 5080 cycles as always, the cells are 8 writes each.
 */
std::string xgsSpriteFrameSource(const SpriteConfig& config);

/**
 * xgs_sprite_update(): the patterns of the copies (in xgs_sprites) from x and the masks. Called
 * before every frame, takes a few thousand cycles. {o}, {bg}, {col}, {no}, {acc_hi} and {acc_lo}
 * are the registers.
 */
std::string xgsSpriteUpdateSource(const SpriteConfig& config);

/** xgs_joystick(): the buttons (0x01 up, 0x02 down, 0x04 left, 0x08 right, 0x10 fire; 1 = pressed). */
std::string xgsJoystickSource();
/** xgs_sound(address, data): one command to the sound chip. Parameters {address} and {data}. */
std::string xgsSoundSource();
/** xgs_sram_read(addr), xgs_sram_write(addr, value), xgs_sram_bank(bank): the 128 KB SRAM. */
std::string xgsSramReadSource();
std::string xgsSramWriteSource();
std::string xgsSramBankSource();

#endif /*KERNEL_H_*/
