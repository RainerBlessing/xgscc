// xgscc [-S] [--map] input.c -o output: compiles C for the XGameStation Micro Edition.
// output is a hex file, with -S the assembler source. --map writes where everything
// is into a file next to the output (game.hex -> game.map).
#include <fstream>
#include <iostream>
#include <sstream>
#include <string.h>
#include "asm.h"
#include "compiler.h"
#include "hexwriter.h"

static const int ROM_WORDS = 4096;

static int usage()
{
	std::cerr << "usage: xgscc [-S] [--map] input.c -o output\n";
	return 2;
}

// game.hex -> game.map
static std::string mapFileOf(const std::string& output)
{
	size_t slash = output.find_last_of("/\\");
	size_t dot = output.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return output + ".map";
	return output.substr(0, dot) + ".map";
}

int main(int argc, char** argv)
{
	bool assemblyOnly = false;
	bool writeMap = false;
	const char* input = NULL;
	const char* output = NULL;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-S") == 0)
			assemblyOnly = true;
		else if (strcmp(argv[i], "--map") == 0)
			writeMap = true;
		else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			output = argv[++i];
		else if (argv[i][0] != '-' && input == NULL)
			input = argv[i];
		else
			return usage();
	}
	if (input == NULL || output == NULL)
		return usage();

	std::ifstream file(input);
	if (!file)
	{
		std::cerr << input << ": cannot open\n";
		return 1;
	}
	std::stringstream source;
	source << file.rdbuf();

	CompileResult result = compile(source.str(), input);
	if (!result.ok)
	{
		std::cerr << result.error << "\n";
		return 1;
	}
	if (writeMap)
	{
		std::ofstream map(mapFileOf(output).c_str());
		map << mapText(result.map, input);
		if (!map)
		{
			std::cerr << mapFileOf(output) << ": cannot write\n";
			return 1;
		}
	}
	if (assemblyOnly)
	{
		std::ofstream(output) << result.assembly;
		return 0;
	}

	static unsigned short rom[ROM_WORDS];
	std::string error;
	if (!assembleSource(result.assembly, rom, ROM_WORDS, &error))
	{
		std::cerr << input << ": internal error in the generated code: " << error << "\n";
		return 1;
	}
	if (!writeHexFile(output, rom, ROM_WORDS))
	{
		std::cerr << output << ": cannot write\n";
		return 1;
	}
	return 0;
}
