#include "library.h"
#include <cstdlib>
#include <map>
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
	function.library = true;
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
	{
		library.functions[i].library = true;
		program->functions.push_back(std::move(library.functions[i]));
	}
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

	// the map holds tile * lines: a shift if the lines are a power of two, else a multiplication
	int shift = 0;
	while ((1 << shift) < tileLines)
		shift++;
	bool powerOfTwo = (1 << shift) == tileLines;
	ostringstream text;
	text << "typedef unsigned char uint8_t;\n"
		"uint8_t xgs_map[" << columns * rows << "];\n"
		"void xgs_tile(uint8_t x, uint8_t y, uint8_t tile) { xgs_map[y * " << columns << " + x] = tile " << (powerOfTwo ? "<< " : "* ") << (powerOfTwo ? shift : tileLines) << "; }\n"
		"uint8_t xgs_get_tile(uint8_t x, uint8_t y) { return xgs_map[y * " << columns << " + x] " << (powerOfTwo ? ">> " : "/ ") << (powerOfTwo ? shift : tileLines) << "; }\n";
	Program library = parse(tokenize(text.str()));
	for (size_t i = 0; i < library.globals.size(); i++)
		program->globals.insert(program->globals.begin() + i, std::move(library.globals[i]));
	for (size_t i = 0; i < library.functions.size(); i++)
	{
		library.functions[i].library = true;
		program->functions.push_back(std::move(library.functions[i]));
	}

	vector<string> none;
	vector<string> frameRegisters;
	frameRegisters.push_back("s");
	frameRegisters.push_back("r");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_frame", xgsTileFrameSource(columns, rows, tileLines), frameRegisters));
	addXgsHardware(program);
}

void addXgsSpriteLibrary(Program* program, const SpriteConfig& c)
{
	xgsCheckSprites(c);
	bool tiles = false;
	bool images = false;
	for (size_t i = 0; i < program->globals.size(); i++)
	{
		const Stmt& declaration = *program->globals[i];
		if (declaration.name == "xgs_tiles")
		{
			tiles = true;
			if (declaration.arraySize != 256 || !declaration.isConst || declaration.type.size != 1)
				throw CompileError(declaration.line, "xgs_tiles has to be declared as const uint8_t xgs_tiles[256]: one byte for every line of every tile, "
					"in all 256 / XGS_TILE_LINES tiles");
		}
		if (declaration.name == "xgs_sprite_images")
		{
			images = true;
			if (!declaration.isConst || declaration.type.size != 1 || declaration.arraySize % 16 != 0 || declaration.arraySize < 16 || declaration.arraySize > 256)
				throw CompileError(declaration.line, "xgs_sprite_images has to be declared as const uint8_t xgs_sprite_images[16 * images] (at most 256 bytes): "
					"the shape and the colour of the 8 pixel rows of every image");
		}
	}
	if (!tiles)
		throw CompileError(1, "the sprites need the table of the tile graphics: const uint8_t xgs_tiles[256] = { ... } (one colour for every line of every tile)");
	if (!images)
		throw CompileError(1, "the sprites need the table of their images: const uint8_t xgs_sprite_images[16 * images] = { ... } "
			"(shape and colour of 8 pixel rows for every image)");

	int shift = 0;
	while ((1 << shift) < c.tileLines)
		shift++;
	bool powerOfTwo = (1 << shift) == c.tileLines;
	const int record = xgsSpriteRecord(c.spriteRows);
	// n * record as shifts and additions: no call of the multiplication routine
	string product;
	for (int bit = 0; (1 << bit) <= record; bit++)
	{
		if (record & (1 << bit))
			product += string(product.empty() ? "" : " + ") + "(n << " + to_string(bit) + ")";
	}
	ostringstream text;
	text << "typedef unsigned char uint8_t;\n"
		"uint8_t xgs_map[" << c.columns * c.rows << "];\n"
		"uint8_t xgs_sprites[" << c.players * record << "];\n"
		"uint8_t xgs_ln[1];\nuint8_t xgs_s[1];\nuint8_t xgs_r[1];\nuint8_t xgs_mp[1];\nuint8_t xgs_cnt[1];\nuint8_t xgs_up[1];\nuint8_t xgs_ret[1];\nuint8_t xgs_m0[1];\nuint8_t xgs_k0[1];\n"
		"void xgs_tile(uint8_t x, uint8_t y, uint8_t tile) { xgs_map[y * " << c.columns << " + x] = tile " << (powerOfTwo ? "<< " : "* ") << (powerOfTwo ? shift : c.tileLines) << "; }\n"
		"uint8_t xgs_get_tile(uint8_t x, uint8_t y) { return xgs_map[y * " << c.columns << " + x] " << (powerOfTwo ? ">> " : "/ ") << (powerOfTwo ? shift : c.tileLines) << "; }\n"
		"void xgs_sprite(uint8_t n, uint8_t x, uint8_t y, uint8_t image) {\n"
		"  uint8_t b = " << product << ";\n"
		"  xgs_sprites[b] = x; xgs_sprites[b + 1] = y; xgs_sprites[b + 2] = image << 4;\n"
		"}\n"
		"void xgs_sprite_copies(uint8_t n, uint8_t row, uint8_t mask) { xgs_sprites[" << product << " + 3 + row] = mask; }\n"
		"void xgs_frame(void) { xgs_sprite_update(); xgs_sprite_frame(); }\n";
	Program library = parse(tokenize(text.str()));
	for (size_t i = 0; i < library.globals.size(); i++)
		program->globals.insert(program->globals.begin() + i, std::move(library.globals[i]));
	for (size_t i = 0; i < library.functions.size(); i++)
	{
		library.functions[i].library = true;
		program->functions.push_back(std::move(library.functions[i]));
	}

	vector<string> none;
	vector<string> registers;
	registers.push_back("o");
	registers.push_back("bg");
	registers.push_back("col");
	registers.push_back("no");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_sprite_frame", xgsSpriteFrameSource(c), registers));
	program->functions.push_back(assemblerFunction("xgs_sprite_update", xgsSpriteUpdateSource(c), registers));
	addXgsHardware(program);
}

void addXgsHiresLibrary(Program* program, int columns, int rows, int tileLines, int pixels)
{
	if (tileLines % 8 != 0 || tileLines / 8 > 14)
		throw CompileError(1, "XGS_TILE_LINES has to be 8 to 112 and a multiple of 8 with XGS_PIXELS: a tile has 8 pixel rows");
	if (pixels < 1 || pixels > 8)
		throw CompileError(1, "XGS_PIXELS has to be a number from 1 to 8");
	if (XGS_TILE_AREA / (columns * pixels) < XGS_HIRES_MIN_PITCH)
		throw CompileError(1, "XGS_COLUMNS times XGS_PIXELS is " + to_string(columns * pixels) + ": the pixels would be less than " +
			to_string(XGS_HIRES_MIN_PITCH) + " cycles apart, at most " + to_string(XGS_TILE_AREA / XGS_HIRES_MIN_PITCH) + " pixels fit into a line");

	// the table of the tile graphics is the program's own: 16 bytes for every tile
	bool found = false;
	for (size_t i = 0; i < program->globals.size(); i++)
	{
		const Stmt& declaration = *program->globals[i];
		if (declaration.name != "xgs_tiles")
			continue;
		found = true;
		if (!declaration.isConst || declaration.type.size != 1 || declaration.arraySize % 16 != 0 || declaration.arraySize > 4096)
			throw CompileError(declaration.line, "xgs_tiles has to be declared as const uint8_t xgs_tiles[16 * tiles]: for every tile the shape and "
				"the colour of its 8 pixel rows, from the top");
	}
	if (!found)
		throw CompileError(1, "the hi-res tiles need the table of the tile graphics: const uint8_t xgs_tiles[16 * tiles] = { ... } "
			"(shape and colour of 8 pixel rows for every tile)");

	// the map holds the tile numbers
	ostringstream text;
	text << "typedef unsigned char uint8_t;\n"
		"uint8_t xgs_map[" << columns * rows << "];\n"
		"void xgs_tile(uint8_t x, uint8_t y, uint8_t tile) { xgs_map[y * " << columns << " + x] = tile; }\n"
		"uint8_t xgs_get_tile(uint8_t x, uint8_t y) { return xgs_map[y * " << columns << " + x]; }\n";
	Program library = parse(tokenize(text.str()));
	for (size_t i = 0; i < library.globals.size(); i++)
		program->globals.insert(program->globals.begin() + i, std::move(library.globals[i]));
	for (size_t i = 0; i < library.functions.size(); i++)
	{
		library.functions[i].library = true;
		program->functions.push_back(std::move(library.functions[i]));
	}

	vector<string> none;
	vector<string> frameRegisters;
	frameRegisters.push_back("s");
	frameRegisters.push_back("sh");
	frameRegisters.push_back("col");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_frame", xgsHiresFrameSource(columns, rows, tileLines, pixels), frameRegisters));
	addXgsHardware(program);
}

bool includes(const LexInfo& info, const string& name)
{
	return info.includes.count(name) != 0;
}

bool definedNumber(const LexInfo& info, const string& name, long* value)
{
	map<string, vector<Token> >::const_iterator found = info.macros.find(name);
	if (found == info.macros.end())
		return false;
	const vector<Token>& body = found->second;
	if (body.size() != 1 || body[0].type != Token::NUMBER)
		throw CompileError(body.empty() ? 1 : body[0].line, name + " must be a number");
	*value = body[0].value;
	return true;
}
