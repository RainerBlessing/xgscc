#include "hexwriter.h"
#include <cstdio>
#include <fstream>

using namespace std;

static const int WORDS_PER_RECORD = 8;
static const unsigned short EMPTY = 0xfff;

static void appendByte(string* text, int byte, int* checksum)
{
	char digits[3];
	snprintf(digits, sizeof(digits), "%02X", byte & 0xff);
	*text += digits;
	*checksum += byte & 0xff;
}

string hexText(const unsigned short* rom, int words)
{
	string text;
	for (int first = 0; first < words; first += WORDS_PER_RECORD)
	{
		bool empty = true;
		for (int i = first; i < first + WORDS_PER_RECORD && i < words; i++)
			empty = empty && rom[i] == EMPTY;
		if (empty)
			continue;

		int count = min(WORDS_PER_RECORD, words - first);
		int checksum = 0;
		text += ":";
		appendByte(&text, count * 2, &checksum);
		appendByte(&text, (first * 2) >> 8, &checksum);
		appendByte(&text, (first * 2) & 0xff, &checksum);
		appendByte(&text, 0, &checksum);
		for (int i = first; i < first + count; i++)
		{
			appendByte(&text, rom[i] & 0xff, &checksum);
			appendByte(&text, rom[i] >> 8, &checksum);
		}
		int ignored = 0;
		appendByte(&text, 256 - (checksum & 0xff), &ignored);
		text += "\n";
	}
	text += ":00000001FF\n";
	return text;
}

bool writeHexFile(const string& path, const unsigned short* rom, int words)
{
	ofstream file(path.c_str());
	file << hexText(rom, words);
	return file.good();
}
