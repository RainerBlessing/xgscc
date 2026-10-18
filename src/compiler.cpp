#include "compiler.h"
#include <algorithm>
#include <map>
#include <sstream>
#include "asm.h"
#include "codegen.h"
#include "errors.h"
#include "lexer.h"
#include "library.h"
#include "parser.h"
#include "runtime.h"

using namespace std;

static const int ROM_WORDS = 4096;
static const int RESET_VECTOR = 0xfff;

static bool lessAddress(const RomEntry& a, const RomEntry& b)
{
	return a.address < b.address;
}
// Where everything is: the RAM from the code generator, the ROM from the addresses
// of the labels. A function ends where the next one begins; only the programmed
// words count, the gaps in front of a function that has to start in the first half
// of a page do not.
static void buildMap(const CodeInfo& info, const map<string, int>& labels, CompileResult& result)
{
	MemoryMap& map = result.map;
	map.ram = info.ram;
	map.ramAvailable = 246;
	map.callDepth = info.callDepth;

	vector<pair<int, const CodeSymbol*> > code;
	int lowestTable = RESET_VECTOR;
	for (size_t i = 0; i < info.symbols.size(); i++)
	{
		const CodeSymbol& symbol = info.symbols[i];
		int address = labels.at(symbol.label);
		if (symbol.kind == "table")
		{
			RomEntry entry = { address, symbol.words, symbol.name, "table" };
			map.rom.push_back(entry);
			lowestTable = min(lowestTable, address);
		}
		else
			code.push_back(make_pair(address, &symbol));
	}
	sort(code.begin(), code.end());
	for (size_t i = 0; i < code.size(); i++)
	{
		int end = i + 1 < code.size() ? code[i + 1].first : lowestTable;
		int words = 0;
		for (int address = code[i].first; address < end; address++)
		{
			if (result.rom[address] != 0xfff)
				words++;
		}
		if (code[i].second->kind == "assembler")
		{
			// timed to the cycle: it has to be callable (first half of a page) and on one page
			int last = code[i].first;
			for (int address = code[i].first; address < end; address++)
			{
				if (result.rom[address] != 0xfff)
					last = address;
			}
			if (code[i].first % 512 >= 256 || code[i].first / 512 != last / 512)
				throw CompileError(0, "the assembler routine " + code[i].second->name + " does not fit on one page of the ROM");
		}
		RomEntry entry = { code[i].first, words, code[i].second->name, code[i].second->kind };
		map.rom.push_back(entry);
	}
	RomEntry vector = { RESET_VECTOR, 1, "reset vector", "vector" };
	map.rom.push_back(vector);
	sort(map.rom.begin(), map.rom.end(), lessAddress);
}

CompileResult compile(const string& source, const string& fileName)
{
	CompileResult result;
	result.ok = false;
	try
	{
		Program program = parse(tokenize(source));
		if (includes(source, "xgs.h"))
		{
			long lines = 192;
			long colour = 0xe6;
			definedNumber(source, "XGS_LINES", &lines);
			definedNumber(source, "XGS_COLOR", &colour);
			if (lines < 1 || lines > 255)
				throw CompileError(1, "XGS_LINES has to be a number from 1 to 255");
			if (colour < 0 || colour > 255)
				throw CompileError(1, "XGS_COLOR has to be a number from 0 to 255");
			addXgsLibrary(&program, (int) lines, (int) colour);
		}
		// the runtime comes behind the program, only what is called gets into the ROM
		Program runtime = parse(tokenize(runtimeSource()));
		for (size_t i = 0; i < runtime.functions.size(); i++)
			program.functions.push_back(std::move(runtime.functions[i]));
		CodeInfo info;
		result.assembly = generateAssembly(program, &info);
		result.variables = info.variables;
		result.highBytes = info.highBytes;

		result.rom.assign(ROM_WORDS, 0xfff);
		string error;
		map<string, int> labels;
		if (!assembleSource(result.assembly, &result.rom[0], ROM_WORDS, &error, &labels))
		{
			// the only mistake of the program that only the assembler sees
			if (error.find("beyond the end of the ROM") != string::npos || error.find("is used twice") != string::npos)
				throw CompileError(0, fileName + ": the program is too big for the ROM (4096 words)");
			throw CompileError(0, fileName + ": internal error in the generated code: " + error);
		}
		buildMap(info, labels, result);
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
