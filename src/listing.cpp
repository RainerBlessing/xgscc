#include "listing.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "disasm.h"

using namespace std;

namespace
{
	struct Marker
	{
		int address;
		int sequence;
		int line;		// 0 for code that is not from the C source
	};

	bool earlier(const Marker& a, const Marker& b)
	{
		return a.address != b.address ? a.address < b.address : a.sequence < b.sequence;
	}

	vector<string> linesOf(const string& text)
	{
		vector<string> lines;
		istringstream in(text);
		string line;
		while (getline(in, line))
			lines.push_back(line);
		return lines;
	}

	string trim(const string& text)
	{
		size_t first = text.find_first_not_of(" \t\r");
		if (first == string::npos)
			return "";
		return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
	}

	string hex(int value, int digits)
	{
		char buffer[16];
		snprintf(buffer, sizeof(buffer), "%0*X", digits, value);
		return buffer;
	}

	bool isCode(const RomEntry& entry)
	{
		return entry.kind != "table" && entry.kind != "vector";
	}
}

string listingText(const string& fileName, const string& source, const LexInfo& info, const map<string, int>& labels,
	const vector<unsigned short>& rom, const MemoryMap& map)
{
	vector<Marker> markers;
	for (std::map<string, int>::const_iterator l = labels.begin(); l != labels.end(); ++l)
	{
		const string& name = l->first;
		if (name.compare(0, 2, "_C") != 0)
			continue;
		size_t underscore = name.find('_', 2);
		if (underscore == string::npos)
			continue;
		Marker marker = { l->second, atoi(name.c_str() + underscore + 1), atoi(name.c_str() + 2) };
		markers.push_back(marker);
	}
	sort(markers.begin(), markers.end(), earlier);

	vector<vector<string> > files;		// the lines of the files, read when they are needed
	files.push_back(linesOf(source));

	ostringstream out;
	out << "Listing of " << fileName << "\n";
	size_t next = 0;
	for (size_t e = 0; e < map.rom.size(); e++)
	{
		const RomEntry& entry = map.rom[e];
		if (!isCode(entry))
			continue;
		for (int address = entry.address; address < entry.address + entry.words && address < (int) rom.size(); address++)
		{
			// the headers of the markers at this address (the first word of a group)
			bool header = false;
			while (next < markers.size() && markers[next].address <= address)
			{
				if (markers[next].address == address)
				{
					if (!header)
						out << "\n";
					header = true;
					int encoded = markers[next].line;
					if (encoded == 0)
						out << "; " << (entry.kind == "start" ? string("start code") : entry.name + " (" + (entry.kind == "assembler" ? "assembler routine" : "library") + ")") << "\n";
					else
					{
						int file = fileOfLine(encoded);
						int line = lineInFile(encoded);
						while ((int) files.size() <= file)
							files.push_back(vector<string>());
						if (files[file].empty() && file > 0 && file < (int) info.files.size())
						{
							ifstream in(info.files[file].c_str());
							stringstream content;
							content << in.rdbuf();
							files[file] = linesOf(content.str());
						}
						string path = file == 0 ? fileName : (file < (int) info.files.size() ? info.files[file] : "?");
						string text = line >= 1 && line <= (int) files[file].size() ? trim(files[file][line - 1]) : "";
						out << "; " << path << ":" << line << ": " << text << "\n";
					}
				}
				next++;
			}
			out << hex(address, 4) << "  " << hex(rom[address], 3) << "  " << disassemble(rom[address]) << "\n";
		}
	}
	return out.str();
}
