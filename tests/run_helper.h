#ifndef RUN_HELPER_H_
#define RUN_HELPER_H_
#include <string>
#include "compiler.h"

// Compiles a C program, runs it on the emulated SX52 (without video) and gives
// access to the RAM afterwards.
struct ProgramRun
{
	CompileResult compiled;
	bool halted;			// the program ended in its "jmp to itself"
	long steps;			// executed instructions
	unsigned char ram[256];

	// the value of a global variable after the run (the low byte of a 16 bit one)
	int value(const std::string& name) const;
	// the value of a 16 bit global variable, as unsigned
	int value16(const std::string& name) const;
};

ProgramRun runProgram(const std::string& source, long maxSteps = 200000);

#endif /*RUN_HELPER_H_*/
