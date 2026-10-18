#ifndef OBJWRITER_H_
#define OBJWRITER_H_
#include <string>

/**
 * The ROM as a binary image, the way xgsemu reads a .obj file: two bytes per
 * word, low byte first, empty words are 0xFFF.
 */
std::string objBytes(const unsigned short* rom, int words);

/** Writes objBytes() to a file. Returns false if the file cannot be written. */
bool writeObjFile(const std::string& path, const unsigned short* rom, int words);

#endif /*OBJWRITER_H_*/
