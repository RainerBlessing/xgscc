#include "kernel.h"
#include <sstream>

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
		explicit Kernel(const string& counter = "{a}") : counter(counter), labels(0) {}

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
		int labels;

		string newLabel()
		{
			return "xgs_d" + hexText(labels++);
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
