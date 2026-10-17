#ifndef MEMORYMAP_H_
#define MEMORYMAP_H_
#include <string>
#include <vector>

/** A place in the RAM and what lives there. Names that share a place are joined with ", ". */
struct RamEntry
{
	int address;
	int bytes;
	std::string name;
};

/** A part of the ROM. kind: "start", "function", "runtime", "table" or "vector". */
struct RomEntry
{
	int address;
	int words;		// code: the words that are programmed, not the gaps between functions
	std::string name;
	std::string kind;
};

/** Where everything is: the RAM, the ROM and the deepest nesting of calls. */
struct MemoryMap
{
	MemoryMap() : ramAvailable(0), callDepth(0) {}

	std::vector<RamEntry> ram;		// sorted by address
	std::vector<RomEntry> rom;		// sorted by address
	int ramAvailable;
	int callDepth;			// calls inside each other; the return stack holds 8
};

/** The map as text, for a file next to the hex file. */
std::string mapText(const MemoryMap& map, const std::string& fileName);

#endif /*MEMORYMAP_H_*/
