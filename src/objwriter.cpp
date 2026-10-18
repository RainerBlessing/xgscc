#include "objwriter.h"
#include <fstream>

using namespace std;

string objBytes(const unsigned short* rom, int words)
{
	string bytes;
	for (int i = 0; i < words; i++)
	{
		bytes += (char) (rom[i] & 0xff);
		bytes += (char) (rom[i] >> 8);
	}
	return bytes;
}

bool writeObjFile(const string& path, const unsigned short* rom, int words)
{
	ofstream file(path.c_str(), ios::binary);
	string bytes = objBytes(rom, words);
	file.write(bytes.data(), bytes.size());
	file.close();
	return !file.fail();
}
