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
		Kernel() : labels(0) {}

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
				line("mov {a}, w");
				label(again);
				line("decsz {a}");
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
		int labels;

		string newLabel()
		{
			return "xgs_d" + hexText(labels++);
		}
	};
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

	// The time from the end of the last picture line to the next vertical sync:
	// the frame minus the sync, the lines and the clr of the RTCC.
	int wait = XGS_FRAME_CYCLES - XGS_VSYNC_CYCLES - lines * XGS_LINE_CYCLES - 1;
	int ticks = (wait - MARGIN) / 256;
	if (ticks % 256 == 0)
		ticks--;			// the low count must not be 0, there would be nothing to wait for
	int fine = wait - ticks * 256 - POLL_LATENCY - AFTER_DETECTION;

	// One register, {a}, counts the wraps of the RTCC, then the ticks, then the rounds
	// of the delay loops; {n} counts the lines.
	if (ticks / 256 > 0)
	{
		k.comment("wait: the high part of the ticks, in wraps of the RTCC");
		k.line("mov w, #" + hexText(ticks / 256));
		k.line("mov {a}, w");
		k.label("xgs_wrap");
		k.label("xgs_leave");
		k.line("mov w, rtcc");
		k.line("snb status.2");
		k.line("jmp xgs_leave");			// while the RTCC is 0
		k.label("xgs_reach");
		k.line("mov w, rtcc");
		k.line("sb status.2");
		k.line("jmp xgs_reach");			// until it is 0 again: it has wrapped
		k.line("decsz {a}");
		k.line("jmp xgs_wrap");
	}
	k.comment("the low part of the ticks");
	k.line("mov w, #" + hexText(ticks % 256));
	k.line("mov {a}, w");
	k.label("xgs_tick");
	k.line("mov w, rtcc");
	k.line("xor w, {a}");
	k.line("sb status.2");
	k.line("jmp xgs_tick");
	k.delay(fine);

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
