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
const int XGS_TILE_AREA = 3600;			// cycles for the cells of a line
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

#endif /*KERNEL_H_*/
