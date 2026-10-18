#ifndef CODEGEN_H_
#define CODEGEN_H_
#include <map>
#include <string>
#include <vector>
#include "ast.h"
#include "memorymap.h"

/** A place in the ROM that the code generator made and the assembler gives an address. */
struct CodeSymbol
{
	std::string label;
	std::string name;
	std::string kind;	// "start", "function", "runtime" or "table"
	int words;		// a table: its size
};

/** What the code generator knows about the program besides the assembler source. */
struct CodeInfo
{
	CodeInfo() : callDepth(0) {}
	std::map<std::string, int> variables;	// every global -> RAM address (of its low byte)
	std::map<std::string, int> highBytes;	// every 16 bit global -> RAM address of its high byte
	std::vector<RamEntry> ram;
	std::vector<CodeSymbol> symbols;
	int callDepth;
};

/**
 * Translates the program into assembler source for assembleSource() and tells
 * where the variables are. Throws CompileError.
 */
std::string generateAssembly(const Program& program, CodeInfo* info, bool lineMarkers = false);

#endif /*CODEGEN_H_*/
