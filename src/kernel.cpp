#include "kernel.h"
#include <sstream>
#include <vector>
#include "errors.h"

using namespace std;

// The picture is made of writes to RE at exact cycles. Every instruction is counted:
//
//   mov, add, clr, inc, nop, ... 1 cycle      jmp, call, ret, retp 3 cycles
//   decsz, sb, snb               1 cycle, 2 if they skip the next instruction
//   mov re, w                    1 cycle; the write happens at the end of it
//
// A gap between two writes is the cycles of everything between the ends of the two
// "mov re, w". The gaps of a line are those of the example programs (doc/video.md).

namespace
{
	const int FRONT_PORCH = 120;
	const int HSYNC = 376;
	const int BACK_PORCH = 48;
	const int BURST = 200;
	const int AFTER_BURST = 128;
	const int PICTURE_START = FRONT_PORCH + HSYNC + BACK_PORCH + BURST + AFTER_BURST;	// 872
	const int TILE_CELL_WORK = 13;		// cycles of a cell that are not waiting
	const int ACTIVE = XGS_LINE_CYCLES - (FRONT_PORCH + HSYNC + BACK_PORCH + BURST + AFTER_BURST);	// 4208

	const int SYNC_LEVEL = 0xf0;
	const int BLANKING_LEVEL = 0xf6;
	const int BURST_LEVEL = 0x05;

	// The RTCC counts every 256th cycle. The wait for the next frame ends this many
	// cycles after the tick it waits for: the loop that looks for it needs between 0
	// and 5 cycles to see it (about 3 on average), and the rest of the code up to the
	// write that starts the vertical sync.
	const int POLL_LATENCY = 3;
	const int AFTER_DETECTION = 6;			// read, compare and skip = 4, the write = 2
	const int MARGIN = 300;				// the fine delay is never shorter than this

	string hexText(int value)
	{
		ostringstream out;
		out << hex << value;
		return out.str();
	}

	class Kernel
	{
	public:
		// counter is the register of the delay loops and of the wait
		explicit Kernel(const string& counter = "{a}", const string& prefix = "xgs_d") : counter(counter), prefix(prefix), labels(0) {}

		const string& counterRegister() const
		{
			return counter;
		}

		string text()
		{
			return out.str();
		}

		void line(const string& instruction)
		{
			out << "        " << instruction << "\n";
		}

		void label(const string& name)
		{
			out << name << ":\n";
		}

		void comment(const string& text)
		{
			out << "        ; " << text << "\n";
		}

		// a write to RE
		void write(int level)
		{
			line("mov w, #" + hexText(level));
			line("mov re, w");
		}

		// Instructions that take exactly the given number of cycles. A loop of
		// "decsz; jmp" takes 4 cycles per round (2 for setting it up, 2 for the
		// last round), the rest is filled with a jump to the next instruction (3
		// cycles) and nops.
		void delay(int cycles)
		{
			while (cycles >= 16)
			{
				int rounds = cycles / 4;
				if (rounds > 256)
					rounds = 256;
				string again = newLabel();
				line("mov w, #" + hexText(rounds % 256));
				line("mov " + counter + ", w");
				label(again);
				line("decsz " + counter);
				line("jmp " + again);
				cycles -= rounds * 4;
			}
			while (cycles >= 3)
			{
				string next = newLabel();
				line("jmp " + next);
				label(next);
				cycles -= 3;
			}
			for (; cycles > 0; cycles--)
				line("nop");
		}

	private:
		ostringstream out;
		string counter;
		string prefix;
		int labels;

		string newLabel()
		{
			return prefix + hexText(labels++);
		}
	};
}

namespace
{
	// The wait from the end of the last picture line to the start of the next frame.
	void waitForFrame(Kernel& k, int lines)
	{
		const string r = k.counterRegister();
	// The time from the end of the last picture line to the next vertical sync:
	// the frame minus the sync, the lines and the clr of the RTCC.
	int wait = XGS_FRAME_CYCLES - XGS_VSYNC_CYCLES - lines * XGS_LINE_CYCLES - 1;
	int ticks = (wait - MARGIN) / 256;
	if (ticks % 256 == 0)
		ticks--;			// the low count must not be 0, there would be nothing to wait for
	int fine = wait - ticks * 256 - POLL_LATENCY - AFTER_DETECTION;

	// One register counts the wraps of the RTCC, then the ticks, then the rounds
	// of the delay loops.
	if (ticks / 256 > 0)
	{
		k.comment("wait: the high part of the ticks, in wraps of the RTCC");
		k.line("mov w, #" + hexText(ticks / 256));
		k.line("mov " + r + ", w");
		k.label("xgs_wrap");
		k.label("xgs_leave");
		k.line("mov w, rtcc");
		k.line("snb status.2");
		k.line("jmp xgs_leave");			// while the RTCC is 0
		k.label("xgs_reach");
		k.line("mov w, rtcc");
		k.line("sb status.2");
		k.line("jmp xgs_reach");			// until it is 0 again: it has wrapped
		k.line("decsz " + r);
		k.line("jmp xgs_wrap");
	}
	k.comment("the low part of the ticks");
	k.line("mov w, #" + hexText(ticks % 256));
	k.line("mov " + r + ", w");
	k.label("xgs_tick");
	k.line("mov w, rtcc");
	k.line("xor w, " + r);
	k.line("sb status.2");
	k.line("jmp xgs_tick");
	k.delay(fine);
	}
}

string xgsInitSource()
{
	Kernel k;
	k.line("mov w, #0");
	k.line("mov !re, w");				// all of RE are outputs
	k.line("mov w, #7");
	k.line("mov !option, w");			// the RTCC counts every 256th cycle
	k.line("mov w, #" + hexText(BLANKING_LEVEL));
	k.line("mov re, w");
	k.line("mov w, #4");
	k.line("mov !ra, w");				// RA2 is the data of the joystick, the rest is output
	k.line("mov w, #0");
	k.line("mov !rc, w");				// the control lines of the SRAM
	k.line("mov w, #ff");
	k.line("mov !rd, w");				// the data bus of the SRAM is an input until a write
	k.line("mov w, #3");
	k.line("mov ra, w");				// joystick latch and clock idle, the sound chip idle
	k.line("mov w, #c0");
	k.line("mov rc, w");				// SRAM deselected, set to read
	k.line("clr rtcc");
	k.line("retp");
	return k.text();
}

string xgsFrameSource(int lines, int colour)
{
	Kernel k;

	waitForFrame(k, lines);

	k.comment("the vertical sync");
	k.write(SYNC_LEVEL);
	k.line("mov w, #" + hexText(lines));
	k.line("mov {n}, w");
	k.delay(XGS_VSYNC_CYCLES - 2 - 2);

	k.comment("a picture line: the gaps between the writes add up to 5080 cycles");
	k.label("xgs_line");
	k.write(BLANKING_LEVEL);
	k.delay(FRONT_PORCH - 2);
	k.write(SYNC_LEVEL);
	k.delay(HSYNC - 2);
	k.write(BLANKING_LEVEL);
	k.delay(BACK_PORCH - 2);
	k.write(BURST_LEVEL);
	k.delay(BURST - 2);
	k.write(BLANKING_LEVEL);
	k.delay(AFTER_BURST - 2);
	k.write(colour);
	k.delay(ACTIVE - 2 - 4);			// 4 for decsz and jmp
	k.line("decsz {n}");
	k.line("jmp xgs_line");
	k.comment("the last line: the same 6 cycles as jmp and the write of the next line");
	k.line("nop");
	k.line("nop");
	k.write(BLANKING_LEVEL);
	k.line("clr rtcc");
	k.line("retp");

	return k.text();
}

string xgsTileFrameSource(int columns, int rows, int tileLines)
{
	const int lines = rows * tileLines;
	const int cell = XGS_TILE_AREA / columns;		// cycles per cell
	const int gap = cell - TILE_CELL_WORK;			// the rest of a cell is waiting
	const int lineEnd = PICTURE_START + columns * cell;	// the blank write behind the last cell
	const int housekeeping = XGS_LINE_CYCLES - 2 - lineEnd;	// cycles from there to the next "mov w, #f6; mov re, w"
	Kernel k("{acc_hi}");

	waitForFrame(k, lines);

	k.comment("the vertical sync; {s} is the line in the tile, {r} the start of the row in the map");
	k.write(SYNC_LEVEL);
	k.line("clr {s}");
	k.line("mov w, #v_xgs_map");
	k.line("mov {r}, w");
	k.delay(XGS_VSYNC_CYCLES - 3 - 2);

	k.comment("a picture line: the prefix of every line, then one cell after the other");
	k.label("xgs_line");
	k.write(BLANKING_LEVEL);
	k.line("mov w, {r}");
	k.line("mov fsr, w");
	k.line("mov w, #" + hexText(columns));
	k.line("mov {acc_lo}, w");
	k.delay(FRONT_PORCH - 2 - 4);
	k.write(SYNC_LEVEL);
	k.delay(HSYNC - 2);
	k.write(BLANKING_LEVEL);
	k.delay(BACK_PORCH - 2);
	k.write(BURST_LEVEL);
	k.delay(BURST - 2);
	k.write(BLANKING_LEVEL);
	k.delay(AFTER_BURST - 8);			// the first cell takes 8 cycles up to its write

	k.comment("a cell: tile number plus the line in the tile is the address in the table");
	k.label("xgs_cell");
	k.line("mov w, indf");
	k.line("add w, {s}");
	k.line("mov m, #v_xgs_tiles_page");
	k.line("iread");
	k.line("mov re, w");
	k.line("inc fsr");
	k.delay(gap);
	k.line("decsz {acc_lo}");
	k.line("jmp xgs_cell");
	// the last round ends 2 cycles after its decsz instead of 4: 8 cycles to make up
	k.delay(cell - 1 - 2 - gap - 2);
	k.write(BLANKING_LEVEL);

	// Behind the last cell: next line of the tile, or the next row, or the end. The
	// paths take as many cycles as the straight one, so that the next line starts on time.
	k.comment("the end of the line");
	k.line("inc {s}");
	k.line("mov w, #" + hexText(tileLines));
	k.line("xor w, {s}");
	k.line("sb status.2");
	k.line("jmp xgs_same_row");
	k.line("clr {s}");
	k.line("mov w, fsr");
	k.line("mov {r}, w");
	k.line("mov w, #v_xgs_map_end");
	k.line("xor w, {r}");
	k.line("sb status.2");
	k.line("jmp xgs_next_row");
	k.delay(housekeeping - 12);
	k.write(BLANKING_LEVEL);
	k.line("clrb fsr.7");
	k.line("clr rtcc");
	k.line("retp");

	k.label("xgs_same_row");
	k.delay(housekeeping - 7 - 3);
	k.line("jmp xgs_line");
	k.label("xgs_next_row");
	k.delay(housekeeping - 17);
	k.line("jmp xgs_line");

	return k.text();
}

// The cycles of the fetch of a cell of the hi-res kernel (xgsHiresFetch).
static const int HIRES_FETCH = 33;

// The graphics of the next cell: {sh} gets the shape and {col} the colour of the pixel row {s} of the
// tile that FSR points to, FSR moves on. 33 cycles. The table has 16 bytes for a tile
// (shape and colour of 8 pixel rows), so the address is 16 * tile + 2 * pixel row, and
// MODE gets its upper bits. The low nibble of {s} is 2 * pixel row, the high nibble counts the
// lines of a pixel row. MODE keeps bit 4 (xgs_tiles_mode has it set).
static void xgsHiresFetch(Kernel& k)
{
	k.line("mov w, indf");			// the tile
	k.line("mov {col}, w");
	k.line("swap {col}");			// high nibble and low nibble change places
	k.line("mov w, #f");
	k.line("and w, {col}");
	k.line("mov {sh}, w");			// the high bits of the address, 0 to 15
	k.line("mov w, #v_xgs_tiles_mode");
	k.line("add w, {sh}");
	k.line("mov m, w");
	k.line("mov w, #f0");
	k.line("and {col}, w");			// the low nibble of the tile is the high nibble of the address
	k.line("mov w, {s}");
	k.line("and w, #f");
	k.line("or {col}, w");			// plus 2 * the pixel row: the address of the shape
	k.line("mov w, {col}");
	k.line("iread");
	k.line("mov {acc_hi}, w");		// the shape
	k.line("inc {col}");			// the colour is behind it
	k.line("mov w, #v_xgs_tiles_mode");
	k.line("add w, {sh}");
	k.line("mov m, w");			// iread left the high bits of the word (0) in MODE
	k.line("mov w, {col}");
	k.line("iread");
	k.line("mov {col}, w");
	k.line("mov w, {acc_hi}");
	k.line("mov {sh}, w");
	k.line("inc fsr");
}

string xgsHiresFrameSource(int columns, int rows, int tileLines, int pixels)
{
	const int lines = rows * tileLines;
	const int groups = tileLines / 8;			// lines of one pixel row
	const int pitch = XGS_TILE_AREA / (columns * pixels);	// cycles per pixel
	const int cell = pixels * pitch;
	const int lineEnd = PICTURE_START + columns * cell;	// the blank write behind the last cell
	const int housekeeping = XGS_LINE_CYCLES - 2 - lineEnd;	// cycles from there to the next "mov w, #f6; mov re, w"
	const int slack = pitch - HIRES_FETCH - 4 - 5;		// waiting between the fetch and the loop
	Kernel k("{acc_hi}");

	waitForFrame(k, lines);

	k.comment("the vertical sync; {s} is the pixel row and the line in it, FSR walks over the map");
	k.write(SYNC_LEVEL);
	k.line("clr {s}");
	k.line("mov w, #v_xgs_map");
	k.line("mov fsr, w");
	k.delay(XGS_VSYNC_CYCLES - 3 - 2);

	k.comment("a picture line: the prefix of every line, then the first cell is fetched");
	k.label("xgs_line");
	k.write(BLANKING_LEVEL);
	k.line("mov w, #" + hexText(columns));
	k.line("mov {acc_lo}, w");
	k.delay(FRONT_PORCH - 2 - 2);
	k.write(SYNC_LEVEL);
	k.delay(HSYNC - 2);
	k.write(BLANKING_LEVEL);
	k.delay(BACK_PORCH - 2);
	k.write(BURST_LEVEL);
	k.delay(BURST - 2);
	k.write(BLANKING_LEVEL);
	k.delay(AFTER_BURST - 5 - HIRES_FETCH);		// the first pixel takes 5 cycles up to its write
	xgsHiresFetch(k);

	k.comment("a cell: the pixels, the shape shifts out to the left; then the next cell is fetched");
	k.label("xgs_cell");
	for (int pixel = 0; pixel < pixels; pixel++)
	{
		k.line("rl {sh}");
		k.line("mov w, #" + hexText(BLANKING_LEVEL));
		k.line("snb status.0");
		k.line("mov w, {col}");		// a set bit: the colour (the skip takes as long as the mov)
		k.line("mov re, w");
		if (pixel + 1 < pixels)
			k.delay(pitch - 5);
	}
	xgsHiresFetch(k);
	k.delay(slack);
	k.line("decsz {acc_lo}");
	k.line("jmp xgs_cell");
	// the last round ends 2 cycles after its decsz instead of 4, and without the 5 cycles of the pixel
	k.delay(5);
	k.write(BLANKING_LEVEL);

	// Behind the last cell: next line of the pixel row, next pixel row, next row of tiles, or the
	// end. The paths take as many cycles as the straight one, so that the next line starts on time.
	k.comment("the end of the line");
	k.line("mov w, #10");
	k.line("add {s}, w");
	k.line("mov w, {s}");
	k.line("and w, #f0");
	k.line("xor w, #" + hexText(groups << 4));
	k.line("sb status.2");
	k.line("jmp xgs_same_group");
	k.line("mov w, #f");
	k.line("and {s}, w");
	k.line("mov w, #2");
	k.line("add {s}, w");
	k.line("mov w, {s}");
	k.line("xor w, #10");
	k.line("sb status.2");
	k.line("jmp xgs_same_tile");
	k.line("clr {s}");
	k.line("dec fsr");
	k.line("mov w, #v_xgs_map_end");
	k.line("xor w, fsr");
	k.line("sb status.2");
	k.line("jmp xgs_next_row");
	k.delay(housekeeping - 21);
	k.write(BLANKING_LEVEL);
	k.line("clrb fsr.7");
	k.line("clr rtcc");
	k.line("retp");

	// the map pointer went one cell too far (the last fetch): back to the start of the row
	k.label("xgs_same_group");
	k.line("mov w, #" + hexText(columns + 1));
	k.line("sub fsr, w");
	k.delay(housekeeping - 14);
	k.line("jmp xgs_line");
	k.label("xgs_same_tile");
	k.line("mov w, #" + hexText(columns + 1));
	k.line("sub fsr, w");
	k.delay(housekeeping - 22);
	k.line("jmp xgs_line");
	k.label("xgs_next_row");
	k.delay(housekeeping - 26);
	k.line("jmp xgs_line");

	return k.text();
}

// ---------------------------------------------------------------------------------------------
// Sprites. The tile map kernel with objects on top, the way the Atari 2600 does it: a few
// "players" with a bitmap of 8 pixels, the colour of every line from a table in the ROM, copies
// at a fixed distance and a mask that says which copies are there (NUSIZ with holes). The
// picture is made of cells of 8 pixels (one tile wide): a pixel is the colour of the sprite
// where the mask of the cell has a bit and the colour of the tile where it has not. The masks
// of the next cell are put together while the pixels of this one go out, in the gaps between
// two writes (a pixel is XGS_TILE_AREA / (8 * columns) cycles wide, 5 of them for the write).
// Every instruction is counted, so that the writes come at the cycle that was planned: the text
// of a piece of code and its length in cycles go together (class Piece).

namespace
{
	// A piece of code and the cycles it takes. A skip instruction (snb, sb) and the instruction
	// it skips take 2 cycles either way, if that one has 1 cycle.
	class Piece
	{
	public:
		Piece(int* labels, const string& counter) : labels(labels), counter(counter), cycles(0) {}

		string text() const
		{
			return out.str();
		}

		int length() const
		{
			return cycles;
		}

		void ins(const string& instruction, int length = 1)
		{
			out << "        " << instruction << "\n";
			cycles += length;
		}

		// cycles that the text above has already spent in a way the counter did not follow
		void count(int n)
		{
			cycles += n;
		}

		void jump(const string& target)
		{
			ins("jmp " + target, 3);
		}

		void skip(const string& test, const string& instruction)
		{
			ins(test, 0);
			ins(instruction, 2);
		}

		void label(const string& name)
		{
			out << name << ":\n";
		}

		string newLabel(const string& stem)
		{
			return "xgs_" + stem + hexText((*labels)++);
		}

		void add(const Piece& other)
		{
			out << other.out.str();
			cycles += other.cycles;
		}

		// exactly n cycles without any register: jumps to the next instruction and nops
		void pad(int n)
		{
			if (n < 0)
				throw CompileError(1, "the sprite kernel does not fit: " + to_string(-n) + " cycles too many");
			for (; n >= 3; n -= 3)
			{
				string next = newLabel("p");
				jump(next);
				label(next);
			}
			for (; n > 0; n--)
				ins("nop");
		}

		// exactly n cycles with delay loops, which use the counter register; long ones with a loop of
		// loops, which uses {acc_hi} as well
		void padWith(int n)
		{
			if (n < 0)
				throw CompileError(1, "the sprite kernel does not fit: " + to_string(-n) + " cycles too many");
			if (n >= 2400)
			{
				// a round of the outer loop is 1028 cycles (256 rounds of the inner one, 1022, and the 6 of the
				// instructions around it), the last one 1026: all rounds and the 2 cycles to start them are
				// rounds * 1028
				int rounds = (n - 300) / 1028;
				string again = newLabel("lo");
				string inner = newLabel("li");
				ins("mov w, #" + hexText(rounds), 0);
				ins("mov {acc_hi}, w", 0);
				label(again);
				ins("mov w, #0", 0);
				ins("mov " + counter + ", w", 0);
				label(inner);
				ins("decsz " + counter, 0);
				ins("jmp " + inner, 0);
				ins("decsz {acc_hi}", 0);
				ins("jmp " + again, 0);
				cycles += rounds * 1028;
				n -= rounds * 1028;
			}
			Kernel loops(counter, "xgs_w" + hexText((*labels)++) + "_");
			loops.delay(n);
			out << loops.text();
			cycles += n;
		}

		// FSR = the address of the array plus offset
		void address(const string& array, int offset)
		{
			ins("mov w, #v_" + array);
			ins("mov fsr, w");
			if (offset <= 3)
			{
				for (int i = 0; i < offset; i++)
					ins("inc fsr");
			}
			else
			{
				ins("mov w, #" + hexText(offset));
				ins("clrb status.0");
				ins("add fsr, w");
			}
		}

		// FSR from the offset *at in a record to target (*at is kept up to date)
		void seek(int* at, int target)
		{
			if (*at < 0)
				throw CompileError(1, "internal error in the sprite kernel: FSR is not known");
			int delta = target - *at;
			if (delta >= -3 && delta <= 3)
			{
				for (int i = 0; i < (delta < 0 ? -delta : delta); i++)
					ins(delta < 0 ? "dec fsr" : "inc fsr");
			}
			else if (delta > 0)
			{
				ins("mov w, #" + hexText(delta));
				ins("clrb status.0");
				ins("add fsr, w");
			}
			else
			{
				ins("mov w, #" + hexText(-delta));
				ins("setb status.0");
				ins("sub fsr, w");
			}
			*at = target;
		}

	private:
		ostringstream out;
		int* labels;
		string counter;
		int cycles;
	};
}

namespace
{
	// A record of xgs_sprites, for every sprite: x, y, image * 16, the masks of its rows of copies,
	// the patterns of the copies of these rows (16 bits each, made by xgs_sprite_update), and
	// the state of the kernel in a line: pattern (2 bytes), shape of the next cell (2), colour.
	struct SpriteLayout
	{
		int masks;
		int patterns;
		int state;
		int size;
	};

	SpriteLayout layoutOf(int spriteRows)
	{
		SpriteLayout layout;
		layout.masks = 3;
		layout.patterns = 3 + spriteRows;
		layout.state = 3 + 3 * spriteRows;
		layout.size = layout.state + 5;
		return layout;
	}
}

int xgsSpriteRecord(int spriteRows)
{
	return layoutOf(spriteRows).size;
}

void xgsCheckSprites(const SpriteConfig& c)
{
	if (c.players < 1 || c.players > XGS_MAX_SPRITES)
		throw CompileError(1, "XGS_SPRITES has to be a number from 1 to " + to_string(XGS_MAX_SPRITES));
	if (c.spriteRows < 1 || c.spriteRows > 5)
		throw CompileError(1, "XGS_SPRITE_ROWS has to be a number from 1 to 5");
	if (c.step < 2 || c.step > 15)
		throw CompileError(1, "XGS_SPRITE_STEP (the cells from one copy to the next) has to be a number from 2 to 15");
	if (c.pixelLines != 1 && c.pixelLines != 2 && c.pixelLines != 4 && c.pixelLines != 8)
		throw CompileError(1, "XGS_SPRITE_LINES (the lines of a pixel row) has to be 1, 2, 4 or 8");
	if (c.spriteRows * 8 * c.pixelLines > 255)
		throw CompileError(1, "XGS_SPRITE_ROWS times 8 times XGS_SPRITE_LINES is " + to_string(c.spriteRows * 8 * c.pixelLines) + ": a sprite has at most 255 lines");
	if (c.columns < 2 || c.columns > XGS_SPRITE_MAX_COLUMNS)
		throw CompileError(1, "XGS_COLUMNS has to be a number from 2 to " + to_string(XGS_SPRITE_MAX_COLUMNS) + " with sprites: every cell has 8 pixels of at least " +
			to_string(XGS_SPRITE_MIN_PITCH) + " cycles");
	if (XGS_TILE_AREA / (c.columns * 8) < XGS_SPRITE_MIN_PITCH)
		throw CompileError(1, "XGS_COLUMNS is " + to_string(c.columns) + ": with sprites the pixels would be less than " + to_string(XGS_SPRITE_MIN_PITCH) + " cycles apart");
}

string xgsSpriteFrameSource(const SpriteConfig& c)
{
	xgsCheckSprites(c);
	const int columns = c.columns;
	const int lines = c.rows * c.tileLines;
	const int pitch = XGS_TILE_AREA / (columns * 8);
	const int gap = pitch - 5;					// cycles between the end of a pixel and the 5 cycles of the next
	const int cell = pitch * 8;
	const SpriteLayout layout = layoutOf(c.spriteRows);
	const int height = c.spriteRows * 8 * c.pixelLines;		// lines of a sprite with all its rows
	int pixelShift = 0;
	while ((1 << pixelShift) < c.pixelLines)
		pixelShift++;
	int labels = 0;

	const string O = "{o}";			// the mask of the cell that is drawn: the pixels shift out to the left
	const string BG = "{bg}";			// its tile colour
	const string COL = "{col}";		// its sprite colour
	const string NO = "{no}";			// the mask of the next cell
	const string NC = "{acc_hi}";		// and its sprite colour
	const string T = "{acc_lo}";		// the register of the delay loops and of the temporaries

	Kernel k(T);
	waitForFrame(k, lines);
	k.comment("the vertical sync; the state of the lines is set up in it");
	k.write(SYNC_LEVEL);
	string source = k.text();

	Piece start(&labels, T);
	start.address("xgs_ln", 0);
	start.ins("mov w, #ff");
	start.ins("mov indf, w");				// the line before the first
	start.address("xgs_s", 0);
	start.ins("mov w, #" + hexText(c.tileLines - 1));
	start.ins("mov indf, w");				// the last line of a tile: the first line starts a row
	start.ins("mov w, #v_xgs_map");
	start.ins("mov " + T + ", w");
	start.ins("mov w, #" + hexText(columns));
	start.ins("setb status.0");
	start.ins("sub " + T + ", w");
	start.address("xgs_r", 0);
	start.ins("mov w, " + T);
	start.ins("mov indf, w");				// the row before the first
	Piece wait(&labels, T);
	wait.padWith(XGS_VSYNC_CYCLES - start.length() - 2);
	source += start.text() + wait.text();

	// ---- what has to be done in every line before the first cell ----
	// the line, the line in the tile, the row of the map, the map pointer, the cells to go, the first sprite
	Piece book(&labels, T);
	book.address("xgs_ln", 0);
	book.ins("inc indf");
	book.address("xgs_s", 0);
	book.ins("inc indf");
	book.ins("mov w, #" + hexText(c.tileLines));
	book.ins("xor w, indf");					// Z: the tile is done
	book.ins("mov w, #0");
	book.skip("snb status.2", "mov w, #" + hexText(columns));
	book.ins("mov " + T + ", w");
	book.skip("snb status.2", "clr indf");		// the line in the tile starts again
	book.address("xgs_r", 0);
	book.ins("mov w, " + T);
	book.ins("clrb status.0");
	book.ins("add indf, w");					// the next row of the map
	book.ins("mov w, indf");
	book.ins("mov " + T + ", w");
	book.address("xgs_mp", 0);
	book.ins("mov w, " + T);
	book.ins("mov indf, w");
	book.address("xgs_cnt", 0);
	book.ins("mov w, #" + hexText(columns));
	book.ins("mov indf, w");
	book.address("xgs_up", 0);
	book.ins("mov w, #v_xgs_sprites");
	book.ins("mov indf, w");

	// ---- one sprite in this line: the pixel row, the shifted bitmap, the pattern of the copies ----
	// One routine for all of them, entered from a place for each sprite (the number is in xgs_ret),
	// ending in a jump back to it. xgs_up is the record of the sprite and moves on to the next.
	Piece sub(&labels, T);
	int at = 0;						// where FSR is in the record, -1: not known
	sub.address("xgs_ln", 0);
	sub.ins("mov w, indf");
	sub.ins("mov " + BG + ", w");
	sub.address("xgs_up", 0);
	sub.ins("mov w, indf");
	sub.ins("mov fsr, w");
	sub.ins("mov w, indf");
	sub.ins("and w, #7");
	sub.ins("mov " + T + ", w");				// the pixels the bitmap is shifted to the right
	sub.seek(&at, 1);
	sub.ins("mov w, " + BG);
	sub.ins("mov " + NC + ", w");
	sub.ins("mov w, indf");
	sub.ins("setb status.0");
	sub.ins("sub " + NC + ", w");				// the line in the sprite
	sub.ins("mov w, " + NC);
	sub.ins("mov " + O + ", w");
	for (int i = 0; i < pixelShift; i++)
	{
		sub.ins("clrb status.0");
		sub.ins("rr " + O);
	}										// the pixel row, with the sprite row above it
	sub.ins("mov w, " + O);
	sub.ins("mov " + COL + ", w");
	sub.ins("mov w, #7");
	sub.ins("and " + O + ", w");				// the pixel row
	for (int i = 0; i < 3; i++)
	{
		sub.ins("clrb status.0");
		sub.ins("rr " + COL);
	}										// the sprite row
	sub.ins("mov w, #" + hexText(height));
	sub.ins("setb status.0");
	sub.ins("sub " + NC + ", w");				// no borrow: the line is behind the sprite
	sub.ins("mov w, #0");
	sub.skip("sb status.0", "mov w, #ff");
	sub.ins("mov " + NC + ", w");				// ff if the sprite is in this line, else 0
	sub.ins("clrb status.0");
	sub.ins("rl " + O);
	sub.seek(&at, 2);
	sub.ins("mov w, indf");
	sub.ins("clrb status.0");
	sub.ins("add " + O + ", w");
	sub.ins("mov w, #v_xgs_sprite_images_base");
	sub.ins("clrb status.0");
	sub.ins("add " + O + ", w");				// the address of the shape in the table
	sub.ins("mov m, #v_xgs_sprite_images_page");
	sub.ins("mov w, " + O);
	sub.ins("iread", 4);
	sub.ins("mov " + NO + ", w");
	sub.ins("inc " + O);
	sub.ins("mov m, #v_xgs_sprite_images_page");
	sub.ins("mov w, " + O);
	sub.ins("iread", 4);
	sub.ins("mov " + O + ", w");				// the colour
	sub.ins("mov w, " + NC);
	sub.ins("and " + NO + ", w");				// no sprite in this line: nothing to show
	sub.seek(&at, layout.state + 4);
	sub.ins("mov w, " + O);
	sub.ins("mov indf, w");
	sub.seek(&at, layout.patterns);
	sub.ins("clrb status.0");
	sub.ins("mov w, <<" + COL);
	sub.ins("add fsr, w");					// the pattern of the copies of this sprite row
	at = -1;
	sub.ins("mov w, indf");
	sub.ins("and w, " + NC);
	sub.ins("mov " + O + ", w");
	sub.ins("inc fsr");
	sub.ins("mov w, indf");
	sub.ins("and w, " + NC);
	sub.ins("mov " + COL + ", w");
	sub.address("xgs_up", 0);
	sub.ins("mov w, indf");
	sub.ins("mov fsr, w");
	at = 0;
	sub.seek(&at, layout.state);
	sub.ins("mov w, " + O);
	sub.ins("mov indf, w");
	sub.ins("inc fsr");
	sub.ins("mov w, " + COL);
	sub.ins("mov indf, w");
	at = layout.state + 1;
	// The bitmap goes to the right by T pixels (0 to 7), into the pair of bytes NO:O of this cell and of
	// the next one, in three steps that each do their part or nothing, in the same cycles: 4 pixels
	// (the nibbles change places), 2 and 1. {col} is a mask here: ff if the step is done.
	sub.ins("clr " + O);
	sub.ins("mov w, #0");
	sub.skip("snb " + T + ".2", "mov w, #ff");
	sub.ins("mov " + COL + ", w");
	sub.skip("snb " + T + ".2", "swap " + NO);
	sub.ins("mov w, #f0");
	sub.ins("and w, " + COL);
	sub.ins("mov " + COL + ", w");				// f0 if the step is done
	sub.ins("mov w, " + NO);
	sub.ins("and w, " + COL);
	sub.ins("mov " + O + ", w");				// the low nibble of the shape, moved up, is in the next cell
	sub.ins("mov w, /" + COL);
	sub.ins("and " + NO + ", w");				// and the high nibble stays
	sub.ins("clrb status.0");
	sub.skip("snb " + T + ".1", "rr " + NO);
	sub.skip("snb " + T + ".1", "rr " + O);
	sub.ins("clrb status.0");
	sub.skip("snb " + T + ".1", "rr " + NO);
	sub.skip("snb " + T + ".1", "rr " + O);
	sub.ins("clrb status.0");
	sub.skip("snb " + T + ".0", "rr " + NO);
	sub.skip("snb " + T + ".0", "rr " + O);
	sub.seek(&at, layout.state + 2);
	sub.ins("mov w, " + NO);
	sub.ins("mov indf, w");
	sub.ins("inc fsr");
	sub.ins("mov w, " + O);
	sub.ins("mov indf, w");
	sub.address("xgs_up", 0);
	sub.ins("mov w, #" + hexText(layout.size));
	sub.ins("clrb status.0");
	sub.ins("add indf, w");					// the next sprite
	// the way back: the number of the sprite (xgs_ret) says where
	const int body = sub.length();
	sub.address("xgs_ret", 0);
	sub.ins("mov w, indf");
	sub.ins("mov " + T + ", w");
	vector<int> back(c.players, 0);		// the cycles of the way back to the place of sprite u
	for (int u = 0; u < c.players; u++)
	{
		string target = "xgs_back" + hexText(u);
		if (u + 1 < c.players)
		{
			sub.ins("mov w, " + T);
			sub.ins("xor w, #" + hexText(u));
			sub.ins("snb status.2", 0);
			sub.ins("jmp " + target, 0);
			sub.count(2);							// the jump is not taken: the skip takes 2 cycles
			back[u] = sub.length() - body - 2 + 4;	// taken: 1 cycle for snb, 3 for the jump
		}
		else
		{
			sub.jump(target);
			back[u] = sub.length() - body;
		}
	}

	vector<Piece> places;
	for (int u = 0; u < c.players; u++)
	{
		Piece p(&labels, T);
		p.address("xgs_ret", 0);
		p.ins("mov w, #" + hexText(u));
		p.ins("mov indf, w");
		p.jump("xgs_sub");
		p.label("xgs_back" + hexText(u));
		// what the routine took: the body and the way back (the jump into it is counted above)
		p.count(body + back[u]);
		places.push_back(std::move(p));
	}

	// the first cell of the line: tile colour, no sprite (the cell on the left is free of them)
	Piece first(&labels, T);
	first.address("xgs_mp", 0);
	first.ins("mov w, indf");
	first.ins("inc indf");
	first.ins("mov fsr, w");
	first.ins("mov w, indf");
	first.ins("mov " + T + ", w");
	first.address("xgs_s", 0);
	first.ins("mov w, indf");
	first.ins("clrb status.0");
	first.ins("add w, " + T);
	first.ins("mov m, #v_xgs_tiles_page");
	first.ins("iread", 4);
	first.ins("mov " + BG + ", w");
	first.ins("clr " + O);

	// ---- put them into the stretches between the writes of the line's prefix ----
	vector<const Piece*> work;
	work.push_back(&book);
	for (size_t u = 0; u < places.size(); u++)
		work.push_back(&places[u]);
	work.push_back(&first);
	const int capacity[5] = { FRONT_PORCH - 2, HSYNC - 2, BACK_PORCH - 2, BURST - 2, PICTURE_START - (FRONT_PORCH + HSYNC + BACK_PORCH + BURST) - 5 };
	vector<vector<const Piece*> > placed(5);
	int used[5] = { 0, 0, 0, 0, 0 };
	int stretch = 0;
	for (size_t i = 0; i < work.size(); i++)
	{
		while (stretch < 5 && used[stretch] + work[i]->length() > capacity[stretch])
			stretch++;
		if (stretch == 5)
			throw CompileError(1, "the sprite kernel does not fit into the start of a line: " + to_string(c.players) + " sprites are too many (one takes " +
				to_string(places[0].length()) + " cycles, the pieces between the writes of the start of a line are " + to_string(capacity[0]) + ", " + to_string(capacity[1]) + ", " +
				to_string(capacity[2]) + ", " + to_string(capacity[3]) + " and " + to_string(capacity[4]) + " cycles)");
		placed[stretch].push_back(work[i]);
		used[stretch] += work[i]->length();
	}

	const string pixel = "        rl " + O + "\n        mov w, " + BG + "\n        snb status.0\n        mov w, " + COL + "\n        mov re, w\n";
	const string levels[5] = { hexText(BLANKING_LEVEL), hexText(SYNC_LEVEL), hexText(BLANKING_LEVEL), hexText(BURST_LEVEL), hexText(BLANKING_LEVEL) };
	string line = "xgs_line:\n";
	for (int s = 0; s < 5; s++)
	{
		line += "        mov w, #" + levels[s] + "\n        mov re, w\n";
		Piece stretchCode(&labels, T);
		// the last stretch ends with the registers of the first cell: the delay comes first
		if (s == 4)
			stretchCode.padWith(capacity[s] - used[s]);
		for (size_t i = 0; i < placed[s].size(); i++)
			stretchCode.add(*placed[s][i]);
		if (s != 4)
			stretchCode.padWith(capacity[s] - used[s]);
		line += stretchCode.text();
	}

	// ---- a cell: 8 pixels, and between them the masks of the next cell ----
	vector<Piece> gaps;
	for (int g = 0; g < 8; g++)
		gaps.push_back(Piece(&labels, T));
	gaps[0].ins("clr " + NO);
	gaps[0].ins("clr " + NC);
	int cellAt = 0;
	gaps[0].address("xgs_sprites", 0);
	gaps[0].seek(&cellAt, layout.state);
	for (int u = 0; u < c.players; u++)
	{
		// the pattern of the copies shifts one cell to the right: bit 1 is a copy that starts in the next
		// cell, bit 0 one that started in this one. Where there is a copy the sprite colour is set,
		// the bitmap bytes of the sprite go into the mask of the next cell.
		Piece& p = gaps[1 + u];
		p.ins("mov w, indf");
		p.ins("mov " + T + ", w");
		p.ins("inc fsr");
		p.ins("clrb status.0");
		p.ins("rr indf");
		p.ins("dec fsr");
		p.ins("rr indf");
		p.ins("inc fsr");
		p.ins("inc fsr");
		p.ins("mov w, indf");
		p.skip("snb " + T + ".1", "or " + NO + ", w");
		p.ins("inc fsr");
		p.ins("mov w, indf");
		p.skip("snb " + T + ".0", "or " + NO + ", w");
		p.ins("inc fsr");
		p.ins("mov w, indf");
		p.skip("snb " + T + ".1", "mov " + NC + ", w");
		p.skip("snb " + T + ".0", "mov " + NC + ", w");
		// the state of the next sprite: a record further, from the colour
		p.ins("mov w, #" + hexText(layout.size - 4));
		p.ins("clrb status.0");
		p.ins("add fsr, w");
	}
	// behind the last pixel: the next cell is the one that the work was done for
	Piece& next = gaps[7];
	next.address("xgs_mp", 0);
	next.ins("mov w, indf");
	next.ins("inc indf");
	next.ins("mov fsr, w");
	next.ins("mov w, indf");
	next.ins("mov " + T + ", w");
	next.address("xgs_s", 0);
	next.ins("mov w, indf");
	next.ins("clrb status.0");
	next.ins("add w, " + T);
	next.ins("mov m, #v_xgs_tiles_page");
	next.ins("iread", 4);
	next.ins("mov " + BG + ", w");
	next.ins("mov w, " + NO);
	next.ins("mov " + O + ", w");
	next.ins("mov w, " + NC);
	next.ins("mov " + COL + ", w");
	next.address("xgs_cnt", 0);
	const int control = next.length() + 4;		// and decsz, jmp
	for (int g = 0; g < 7; g++)
	{
		if (gaps[g].length() > gap)
			throw CompileError(1, "the sprite kernel does not fit into a cell: " + to_string(gaps[g].length() - gap) + " cycles too many in a gap of " + to_string(gap));
	}
	if (control > gap)
		throw CompileError(1, "the sprite kernel does not fit into a cell: " + to_string(control - gap) + " cycles too many in the last gap of " + to_string(gap));
	for (int g = 0; g < 7; g++)
		gaps[g].padWith(gap - gaps[g].length());
	next.padWith(gap - control);
	string cells = "xgs_cell:\n";
	for (int g = 0; g < 8; g++)
	{
		cells += pixel + gaps[g].text();
		if (g == 7)
			cells += "        decsz indf\n        jmp xgs_cell\n";
	}

	// ---- behind the last cell: the level goes back to blanking, then the end of the line ----
	const int lineEnd = PICTURE_START + columns * cell;		// the cycle of that write
	Piece exit(&labels, T);
	exit.pad(5);
	exit.ins("mov w, #" + hexText(BLANKING_LEVEL));
	exit.ins("mov re, w");
	Piece last(&labels, T);
	last.address("xgs_ln", 0);
	last.ins("mov w, indf");
	last.ins("xor w, #" + hexText(lines - 1));
	last.ins("snb status.2", 0);
	last.ins("jmp xgs_last", 0);
	last.count(2);							// the jump is not taken
	Piece again(&labels, T);
	again.padWith(XGS_LINE_CYCLES - 5 - (lineEnd + last.length()));
	again.jump("xgs_line");
	Piece done(&labels, T);
	// the jump is taken: 1 cycle for the test and 3 for the jump
	done.padWith(XGS_LINE_CYCLES - 2 - (lineEnd + last.length() - 2 + 4));
	done.ins("mov w, #" + hexText(BLANKING_LEVEL));
	done.ins("mov re, w");
	done.ins("clrb fsr.7");
	done.ins("clr rtcc");
	done.ins("retp", 0);

	source += line + cells + exit.text() + last.text() + again.text() + "xgs_last:\n" + done.text();
	source += "xgs_sub:\n" + sub.text();
	int words = 0;
	{
		istringstream counted(source);
		string countedLine;
		while (getline(counted, countedLine))
		{
			size_t first = countedLine.find_first_not_of(" \t");
			if (first == string::npos || countedLine[first] == ';' || countedLine[countedLine.find_last_not_of(" \t")] == ':')
				continue;
			words++;
		}
	}
	if (words > 512)
		throw CompileError(1, "the sprite kernel for " + to_string(c.players) + " sprites and " + to_string(c.columns) + " columns is " + to_string(words) +
			" words long, more than the 512 words of a page of the ROM");
	return source;
}

// The pattern of the copies of every row of every sprite, once before the frame: bit n of the
// 16 bits of a pattern is set where a copy starts in the cell n (x / 8 for the first copy, then
// every config.step cells for the bits of the mask). The kernel only shifts these. The
// pointers of the loops are the variables of the kernel that are set up again in its first line.
string xgsSpriteUpdateSource(const SpriteConfig& c)
{
	xgsCheckSprites(c);
	int labels = 0;
	const string O = "{o}";			// the bit of the cell that the next copy starts in: 16 bits, O low
	const string BG = "{bg}";			// and BG high
	const string COL = "{col}";		// the pattern, low
	const string NO = "{no}";			// and high
	const string NC = "{acc_hi}";		// the mask of the copies
	const string T = "{acc_lo}";		// counters
	const SpriteLayout layout = layoutOf(c.spriteRows);
	Piece p(&labels, T);

	// the sprite (xgs_r), its mask (xgs_mp) and pattern (xgs_cnt), the sprites (xgs_ln) and the rows (xgs_s) to go
	p.address("xgs_r", 0);
	p.ins("mov w, #v_xgs_sprites");
	p.ins("mov indf, w");
	p.address("xgs_ln", 0);
	p.ins("mov w, #" + hexText(c.players));
	p.ins("mov indf, w");

	string unit = p.newLabel("u");
	string row = p.newLabel("r");
	p.label(unit);
	p.address("xgs_s", 0);
	p.ins("mov w, #" + hexText(c.spriteRows));
	p.ins("mov indf, w");
	p.address("xgs_r", 0);
	p.ins("mov w, indf");
	p.ins("mov " + T + ", w");
	p.ins("mov w, #" + hexText(layout.masks));
	p.ins("clrb status.0");
	p.ins("add " + T + ", w");
	p.address("xgs_mp", 0);
	p.ins("mov w, " + T);
	p.ins("mov indf, w");
	p.address("xgs_r", 0);
	p.ins("mov w, indf");
	p.ins("mov " + T + ", w");
	p.ins("mov w, #" + hexText(layout.patterns));
	p.ins("clrb status.0");
	p.ins("add " + T + ", w");
	p.address("xgs_cnt", 0);
	p.ins("mov w, " + T);
	p.ins("mov indf, w");

	p.label(row);
	p.address("xgs_mp", 0);
	p.ins("mov w, indf");
	p.ins("mov fsr, w");
	p.ins("mov w, indf");
	p.ins("mov " + NC + ", w");				// the mask of copies
	p.address("xgs_r", 0);
	p.ins("mov w, indf");
	p.ins("mov fsr, w");
	p.ins("mov w, indf");
	p.ins("mov " + T + ", w");
	for (int i = 0; i < 3; i++)
	{
		p.ins("clrb status.0");
		p.ins("rr " + T);
	}										// the cell of the first copy
	p.ins("mov w, #1");
	p.ins("mov " + O + ", w");
	p.ins("clr " + BG);
	string shift = p.newLabel("x");
	string shifted = p.newLabel("y");
	p.label(shift);
	p.ins("mov w, " + T);
	p.ins("snb status.2");
	p.ins("jmp " + shifted);
	p.ins("clrb status.0");
	p.ins("rl " + O);
	p.ins("rl " + BG);
	p.ins("dec " + T);
	p.jump(shift);
	p.label(shifted);
	p.ins("clr " + COL);
	p.ins("clr " + NO);
	p.ins("mov w, #8");
	p.ins("mov " + T + ", w");
	string copy = p.newLabel("c");
	p.label(copy);
	p.ins("rr " + NC);
	p.ins("mov w, " + O);
	p.skip("snb status.0", "or " + COL + ", w");
	p.ins("mov w, " + BG);
	p.skip("snb status.0", "or " + NO + ", w");
	for (int i = 0; i < c.step; i++)
	{
		p.ins("clrb status.0");
		p.ins("rl " + O);
		p.ins("rl " + BG);
	}
	p.ins("decsz " + T);
	p.jump(copy);
	p.address("xgs_cnt", 0);
	p.ins("mov w, indf");
	p.ins("mov fsr, w");
	p.ins("mov w, " + COL);
	p.ins("mov indf, w");
	p.ins("inc fsr");
	p.ins("mov w, " + NO);
	p.ins("mov indf, w");
	p.address("xgs_cnt", 0);
	p.ins("inc indf");
	p.ins("inc indf");
	p.address("xgs_mp", 0);
	p.ins("inc indf");
	p.address("xgs_s", 0);
	p.ins("decsz indf");
	p.jump(row);

	p.address("xgs_r", 0);
	p.ins("mov w, #" + hexText(layout.size));
	p.ins("clrb status.0");
	p.ins("add indf, w");
	p.address("xgs_ln", 0);
	p.ins("decsz indf");
	p.jump(unit);
	p.ins("clrb fsr.7");
	p.ins("retp", 0);
	return p.text();
}


// The joystick: 16 bits are clocked out of the shift register, the last five are the buttons.
string xgsJoystickSource()
{
	Kernel k("{acc_hi}");
	k.line("setb ra.0");				// the clock is high before the first falling edge
	k.line("clrb ra.1");				// the first bit that comes is bit 15
	k.line("setb ra.1");
	k.line("mov w, #10");
	k.line("mov {acc_hi}, w");
	k.label("xgs_joy_bit");
	k.line("setb ra.0");
	k.line("clrb ra.0");				// falling edge: the next bit is on RA2, 0 means pressed
	k.line("setb status.0");
	k.line("snb ra.2");
	k.line("clrb status.0");
	k.line("rl {acc_lo}");
	k.line("decsz {acc_hi}");
	k.line("jmp xgs_joy_bit");
	k.line("mov w, #1f");
	k.line("and w, {acc_lo}");
	k.line("retp");
	return k.text();
}

// A command of the sound chip: 11 bits, the 3 of the address and the 8 of the data, the
// highest first. SDATA is taken over at the rising edge of SCLK, the command at the rising edge of SSTB.
string xgsSoundSource()
{
	Kernel k("{acc_hi}");
	k.line("mov w, #5");
	k.line("mov {acc_hi}, w");
	k.label("xgs_snd_align");
	k.line("clrb status.0");			// address and data as one number, moved up by 5 bits
	k.line("rl {data}");
	k.line("rl {address}");
	k.line("decsz {acc_hi}");
	k.line("jmp xgs_snd_align");
	k.line("mov w, #b");
	k.line("mov {acc_hi}, w");
	k.label("xgs_snd_bit");
	k.line("clrb status.0");
	k.line("rl {data}");
	k.line("rl {address}");				// the carry is the next bit
	k.line("clrb ra.4");
	k.line("snb status.0");
	k.line("setb ra.4");
	k.line("setb ra.3");
	k.line("clrb ra.3");
	k.line("decsz {acc_hi}");
	k.line("jmp xgs_snd_bit");
	k.line("setb ra.5");
	k.line("clrb ra.5");
	k.line("retp");
	return k.text();
}

namespace
{
	// The address of the SRAM: RA7 is bit 16, the 12 bits of bits 15..4 are shifted in
	// (RC5 is the bit, RC4 the clock), the low four bits are on RC0..3. The chip is
	// deselected (RC6 high) until all of it is set.
	void sramAddress(Kernel& k, const string& name)
	{
		k.line("mov w, #f");
		k.line("and w, {addr}");
		k.line("mov {acc_lo}, w");
		k.line("setb rc.6");
		k.line("clrb rc.4");
		k.line("mov w, #c");
		k.line("mov {acc_hi}, w");
		k.label(name);
		k.line("clrb status.0");
		k.line("rl {addr}");
		k.line("rl {addr.hi}");
		k.line("clrb rc.5");
		k.line("snb status.0");
		k.line("setb rc.5");
		k.line("setb rc.4");
		k.line("clrb rc.4");
		k.line("decsz {acc_hi}");
		k.line("jmp " + name);
		k.line("mov w, rc");
		k.line("and w, #f0");
		k.line("or w, {acc_lo}");
		k.line("mov rc, w");
	}
}

string xgsSramReadSource()
{
	Kernel k("{acc_hi}");
	sramAddress(k, "xgs_rd_bit");
	k.line("setb rc.7");
	k.line("clrb rc.6");				// the chip is selected: the byte is read
	k.line("mov w, rd");
	k.line("setb rc.6");
	k.line("retp");
	return k.text();
}

string xgsSramWriteSource()
{
	Kernel k("{acc_hi}");
	sramAddress(k, "xgs_wr_bit");
	k.line("mov m, #f");				// MODE selects the direction registers
	k.line("mov w, #0");
	k.line("mov !rd, w");				// the data bus is an output
	k.line("mov w, {value}");
	k.line("mov rd, w");
	k.line("clrb rc.7");
	k.line("clrb rc.6");				// the chip is selected: the byte is written
	k.line("setb rc.6");
	k.line("setb rc.7");
	k.line("mov w, #ff");
	k.line("mov !rd, w");
	k.line("retp");
	return k.text();
}

string xgsSramBankSource()
{
	Kernel k("{acc_hi}");
	k.line("clrb ra.7");
	k.line("snb {bank}.0");
	k.line("setb ra.7");
	k.line("retp");
	return k.text();
}
