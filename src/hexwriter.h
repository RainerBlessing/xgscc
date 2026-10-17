#ifndef HEXWRITER_H_
#define HEXWRITER_H_
#include <string>

/**
 * The ROM as Intel HEX text, the way xgsemu and the XGS tools read it: eight
 * 12 bit words (16 bytes, low byte first) per record, the byte address is
 * twice the word address. Rows that hold only empty words (0xFFF) are left out.
 */
std::string hexText(const unsigned short* rom, int words);

/** Writes hexText() to a file. Returns false if the file cannot be written. */
bool writeHexFile(const std::string& path, const unsigned short* rom, int words);

#endif /*HEXWRITER_H_*/
