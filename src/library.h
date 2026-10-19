#ifndef LIBRARY_H_
#define LIBRARY_H_
#include <string>
#include "ast.h"
#include "lexer.h"
#include "kernel.h"

/**
 * The XGS library, available with #include <xgs.h>:
 *
 *   void xgs_init(void)    sets up the video; call it once
 *   void xgs_frame(void)   waits for the end of the frame, makes the vertical sync
 *                          and the picture lines; the game runs after it returns
 *
 * Adds these functions, written in assembler, to the program.
 * lines is the number of picture lines (#define XGS_LINES, 1 to 255), colour the
 * byte for RE (#define XGS_COLOR).
 */
void addXgsLibrary(Program* program, int lines, int colour);

/**
 * The same with a tile map instead of one colour: xgs_frame() draws columns x rows
 * tiles of tileLines lines each (#define XGS_COLUMNS, XGS_ROWS, XGS_TILE_LINES), from
 * the array xgs_map and the table xgs_tiles of the program. Also adds xgs_tile() and
 * xgs_get_tile(). Throws CompileError if the table is missing or has the wrong size.
 */
void addXgsTileLibrary(Program* program, int columns, int rows, int tileLines);

/**
 * The same with hi-res tiles (#define XGS_PIXELS as well): every cell is XGS_PIXELS pixels
 * wide and 8 pixel rows high (a pixel row is XGS_TILE_LINES / 8 lines), a pixel has a shape
 * and a colour per pixel row. The table xgs_tiles of the program has 16 bytes for every tile;
 * it may be longer than 256 bytes. xgs_map holds the tile numbers. Throws CompileError if the
 * table is missing, the pixels would be too close, or the lines are not a multiple of 8.
 */
void addXgsHiresLibrary(Program* program, int columns, int rows, int tileLines, int pixels);

/**
 * The tile map with sprites (#define XGS_SPRITES as well): xgs_frame() draws the tiles and, on
 * top, config.players sprites of 8 pixels, with copies; see xgsSpriteFrameSource. The table
 * xgs_sprite_images of the program (a multiple of 16 bytes, at most 256: shape and colour of 8
 * pixel rows for every image) holds the pictures. Adds xgs_sprite(), xgs_sprite_copies(),
 * xgs_tile() and xgs_get_tile(). Throws CompileError if a table is missing or wrong.
 */
void addXgsSpriteLibrary(Program* program, const SpriteConfig& config);

/** Whether the source has the line #include <name> or #include "name". */
bool includes(const LexInfo& info, const std::string& name);

/**
 * The number that name is defined as (#define name number), if it is defined.
 * Returns false if it is not; throws CompileError if it is no number.
 */
bool definedNumber(const LexInfo& info, const std::string& name, long* value);

#endif /*LIBRARY_H_*/
