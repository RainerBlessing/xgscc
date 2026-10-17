#include "compiler.h"
#include <sstream>
#include "asm.h"
#include "codegen.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"

using namespace std;

static const int ROM_WORDS = 4096;
static const int RESET_VECTOR = 0xfff;
static const int PAGE_WORDS = 512;

// Jumps and calls are not yet placed across pages, so the program has to fit.
static void checkSize(const vector<unsigned short>& rom, const string& fileName)
{
	for (int address = RESET_VECTOR - 1; address >= PAGE_WORDS; address--)
	{
		if (rom[address] != 0xfff)
			throw CompileError(0, fileName + ": the program is larger than one page (" + "512 words), more is not supported yet");
	}
}

CompileResult compile(const string& source, const string& fileName)
{
	CompileResult result;
	result.ok = false;
	try
	{
		Program program = parse(tokenize(source));
		result.assembly = generateAssembly(program, &result.variables);

		result.rom.assign(ROM_WORDS, 0xfff);
		string error;
		if (!assembleSource(result.assembly, &result.rom[0], ROM_WORDS, &error))
			throw CompileError(0, fileName + ": internal error in the generated code: " + error);
		checkSize(result.rom, fileName);
		result.ok = true;
	}
	catch (const CompileError& error)
	{
		ostringstream message;
		if (error.line > 0)
			message << fileName << ":" << error.line << ": " << error.message;
		else
			message << error.message;
		result.error = message.str();
	}
	return result;
}
