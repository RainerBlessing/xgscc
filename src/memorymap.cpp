#include "memorymap.h"
#include <cstdio>
#include <sstream>

using namespace std;

static const int ROM_WORDS = 4096;
static const int RETURN_STACK = 8;

static string pad(const string& text, size_t width)
{
	return text.size() >= width ? text : text + string(width - text.size(), ' ');
}

static string hex(int value, int digits)
{
	char text[16];
	snprintf(text, sizeof(text), "0x%0*X", digits, value);
	return text;
}

static string right(int value, size_t width)
{
	ostringstream out;
	out << value;
	string text = out.str();
	return text.size() >= width ? text : string(width - text.size(), ' ') + text;
}

string mapText(const MemoryMap& map, const string& fileName)
{
	ostringstream out;
	out << "Memory map of " << fileName << "\n\n";

	int ramUsed = 0;
	for (size_t i = 0; i < map.ram.size(); i++)
		ramUsed += map.ram[i].bytes;
	out << "RAM: " << ramUsed << " of " << map.ramAvailable << " bytes used, " << map.ramAvailable - ramUsed << " free\n";
	out << "  address  bytes  name\n";
	for (size_t i = 0; i < map.ram.size(); i++)
	{
		const RamEntry& entry = map.ram[i];
		out << "  " << pad(hex(entry.address, 2), 7) << right(entry.bytes, 7) << "  " << entry.name << "\n";
	}

	int romUsed = 0;
	for (size_t i = 0; i < map.rom.size(); i++)
		romUsed += map.rom[i].words;
	out << "\nROM: " << romUsed << " of " << ROM_WORDS << " words used, " << ROM_WORDS - romUsed << " free\n";
	out << "  address  words  kind       name\n";
	for (size_t i = 0; i < map.rom.size(); i++)
	{
		const RomEntry& entry = map.rom[i];
		out << "  " << pad(hex(entry.address, 3), 7) << right(entry.words, 7) << "  " << pad(entry.kind, 9) << "  " << entry.name << "\n";
	}

	out << "\ncall depth " << map.callDepth << " of " << RETURN_STACK << " (the return stack holds " << RETURN_STACK << " entries)\n";
	return out.str();
}
