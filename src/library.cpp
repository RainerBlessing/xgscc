#include "library.h"
#include <cstdlib>
#include <sstream>
#include "errors.h"
#include "kernel.h"
#include "lexer.h"
#include "parser.h"

using namespace std;

static Parameter parameter(const string& name, int size)
{
	Parameter result;
	result.name = name;
	result.line = 0;
	result.type = Type(size, false);
	return result;
}

static Function assemblerFunction(const string& name, const string& text, const vector<string>& registers)
{
	Function function;
	function.name = name;
	function.line = 0;
	function.returnsValue = false;
	function.body = StmtPtr(new Stmt(Stmt::BLOCK, 0));
	function.assembly = text;
	function.registers = registers;
	return function;
}

// the functions for the joystick, the sound chip and the SRAM
static void addXgsHardware(Program* program)
{
	vector<string> none;
	Function joystick = assemblerFunction("xgs_joystick", xgsJoystickSource(), none);
	joystick.returnsValue = true;
	joystick.returnType = Type(1, false);
	program->functions.push_back(std::move(joystick));

	Function sound = assemblerFunction("xgs_sound", xgsSoundSource(), none);
	sound.parameters.push_back(parameter("address", 1));
	sound.parameters.push_back(parameter("data", 1));
	program->functions.push_back(std::move(sound));

	Function read = assemblerFunction("xgs_sram_read", xgsSramReadSource(), none);
	read.parameters.push_back(parameter("addr", 2));
	read.returnsValue = true;
	read.returnType = Type(1, false);
	program->functions.push_back(std::move(read));

	Function write = assemblerFunction("xgs_sram_write", xgsSramWriteSource(), none);
	write.parameters.push_back(parameter("addr", 2));
	write.parameters.push_back(parameter("value", 1));
	program->functions.push_back(std::move(write));

	Function bank = assemblerFunction("xgs_sram_bank", xgsSramBankSource(), none);
	bank.parameters.push_back(parameter("bank", 1));
	program->functions.push_back(std::move(bank));

	Program library = parse(tokenize("typedef unsigned char uint8_t;\n"
		"void xgs_note(uint8_t channel, uint8_t note) { xgs_sound(2, (channel << 6) | note); }\n"));
	for (size_t i = 0; i < library.functions.size(); i++)
		program->functions.push_back(std::move(library.functions[i]));
}

void addXgsLibrary(Program* program, int lines, int colour)
{
	vector<string> none;
	vector<string> frameRegisters;
	frameRegisters.push_back("a");
	frameRegisters.push_back("n");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_frame", xgsFrameSource(lines, colour), frameRegisters));
	addXgsHardware(program);
}

void addXgsTileLibrary(Program* program, int columns, int rows, int tileLines)
{
	// the table of the tile graphics is the program's own
	bool found = false;
	for (size_t i = 0; i < program->globals.size(); i++)
	{
		const Stmt& declaration = *program->globals[i];
		if (declaration.name != "xgs_tiles")
			continue;
		found = true;
		if (declaration.arraySize != 256 || !declaration.isConst || declaration.type.size != 1)
			throw CompileError(declaration.line, "xgs_tiles has to be declared as const uint8_t xgs_tiles[256]: one byte for every line of every tile, "
				"in all 256 / XGS_TILE_LINES tiles");
	}
	if (!found)
		throw CompileError(1, "the tile map needs the table of the tile graphics: const uint8_t xgs_tiles[256] = { ... } (one colour for every line of every tile)");

	int shift = 0;
	while ((1 << shift) < tileLines)
		shift++;
	ostringstream text;
	text << "typedef unsigned char uint8_t;\n"
		"uint8_t xgs_map[" << columns * rows << "];\n"
		"void xgs_tile(uint8_t x, uint8_t y, uint8_t tile) { xgs_map[y * " << columns << " + x] = tile << " << shift << "; }\n"
		"uint8_t xgs_get_tile(uint8_t x, uint8_t y) { return xgs_map[y * " << columns << " + x] >> " << shift << "; }\n";
	Program library = parse(tokenize(text.str()));
	for (size_t i = 0; i < library.globals.size(); i++)
		program->globals.insert(program->globals.begin() + i, std::move(library.globals[i]));
	for (size_t i = 0; i < library.functions.size(); i++)
		program->functions.push_back(std::move(library.functions[i]));

	vector<string> none;
	vector<string> frameRegisters;
	frameRegisters.push_back("s");
	frameRegisters.push_back("r");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_frame", xgsTileFrameSource(columns, rows, tileLines), frameRegisters));
	addXgsHardware(program);
}

static string trim(const string& text)
{
	size_t first = text.find_first_not_of(" \t\r");
	if (first == string::npos)
		return "";
	return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool includes(const string& source, const string& name)
{
	istringstream in(source);
	string line;
	while (getline(in, line))
	{
		line = trim(line);
		if (line.compare(0, 1, "#") != 0)
			continue;
		line = trim(line.substr(1));
		if (line.compare(0, 7, "include") != 0)
			continue;
		line = trim(line.substr(7));
		if (line == "<" + name + ">" || line == "\"" + name + "\"")
			return true;
	}
	return false;
}

bool definedNumber(const string& source, const string& name, long* value)
{
	istringstream in(source);
	string line;
	int number = 0;
	while (getline(in, line))
	{
		number++;
		line = trim(line);
		if (line.compare(0, 1, "#") != 0)
			continue;
		line = trim(line.substr(1));
		if (line.compare(0, 6, "define") != 0)
			continue;
		line = trim(line.substr(6));
		if (line.compare(0, name.size(), name) != 0 || line.size() == name.size() || (line[name.size()] != ' ' && line[name.size()] != '\t'))
			continue;
		string text = trim(line.substr(name.size()));
		size_t comment = text.find("//");
		if (comment != string::npos)
			text = trim(text.substr(0, comment));
		char* end = NULL;
		long result = strtol(text.c_str(), &end, 0);
		if (text.empty() || *end != '\0')
			throw CompileError(number, name + " must be a number");
		*value = result;
		return true;
	}
	return false;
}
