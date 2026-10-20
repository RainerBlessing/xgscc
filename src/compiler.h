#ifndef COMPILER_H_
#define COMPILER_H_
#include <map>
#include <string>
#include <vector>
#include "memorymap.h"

struct CompileResult
{
	bool ok;
	std::string error;		// "file:line: message" if !ok
	std::string assembly;		// the generated source for assembleSource()
	std::vector<unsigned short> rom;	// the assembled program, 4096 words (0xFFF is empty)
	std::map<std::string, int> variables;	// name of a global -> RAM address (of the low byte)
	std::map<std::string, int> highBytes;	// name of a 16 bit global -> RAM address of the high byte
	std::map<std::string, int> labels;	// every label of the assembler source -> ROM address
	MemoryMap map;			// where everything is
	std::string listing;		// the program words under the C lines they come from (only if asked for)
};

/**
 * Compiles a C source into SX52 assembler. fileName is only used in messages.
 * optimize 0 is the code as the code generator writes it, 1 runs the peephole optimizer.
 * listing true fills CompileResult::listing; the program is the same as without.
 */
CompileResult compile(const std::string& source, const std::string& fileName, int optimize = 1, bool listing = false);

#endif /*COMPILER_H_*/
