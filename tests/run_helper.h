#ifndef RUN_HELPER_H_
#define RUN_HELPER_H_
#include <string>
#include <vector>
#include "compiler.h"

// Compiles a C program, runs it on the emulated SX52 (without video) and gives
// access to the RAM afterwards.
struct ProgramRun
{
	CompileResult compiled;
	bool halted;			// the program ended in its "jmp to itself"
	long steps;			// executed instructions
	unsigned char ram[256];
	unsigned char mode;		// the MODE register after the run

	// the value of a global variable after the run (the low byte of a 16 bit one)
	int value(const std::string& name) const;
	// the value of a 16 bit global variable, as unsigned
	int value16(const std::string& name) const;
};

ProgramRun runProgram(const std::string& source, long maxSteps = 200000);

// The commands that the sound chip received, decoded like the BU8763 does from the writes to port RA:
// 11 bits, the highest first, SDATA (RA4) is taken at the rising edge of SCLK (RA3), the command is
// applied at the rising edge of SSTB (RA5). A command is the address (3 bits) and the data (8 bits).
std::vector<int> decodeSoundCommands(const std::vector<unsigned char>& raWrites);

#endif /*RUN_HELPER_H_*/
