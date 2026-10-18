#ifndef LISTING_H_
#define LISTING_H_
#include <map>
#include <string>
#include <vector>
#include "lexer.h"
#include "memorymap.h"

/**
 * The listing: the program words in address order, each group under the line of the C
 * source that it was made from:
 *
 *   ; game.c:12: score++;
 *   0031  c01  mov w, #1
 *
 * labels holds the "_C<line>_<n>" labels that the code generator wrote in front of the
 * statements (with the file in the upper bits of the line, see lexer.h); code that has
 * none (start code, library, runtime) is listed under its name from the map.
 * source is the main file, the other files are read from the paths in info.
 */
std::string listingText(const std::string& fileName, const std::string& source, const LexInfo& info, const std::map<std::string, int>& labels,
	const std::vector<unsigned short>& rom, const MemoryMap& map);

#endif /*LISTING_H_*/
