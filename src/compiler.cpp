#include "compiler.h"
#include <sstream>
#include "asm.h"
#include "codegen.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"

using namespace std;

static const int ROM_WORDS = 4096;
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
		{
			// the only mistake of the program that only the assembler sees
			if (error.find("beyond the end of the ROM") != string::npos || error.find("is used twice") != string::npos)
				throw CompileError(0, fileName + ": the program is too big for the ROM (4096 words)");
			throw CompileError(0, fileName + ": internal error in the generated code: " + error);
		}
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
