#include "optimizer.h"
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

using namespace std;

namespace
{
	struct Line
	{
		string text;
		bool protectedLine;		// inside an assembler routine
	};

	string trim(const string& text)
	{
		size_t first = text.find_first_not_of(" \t");
		if (first == string::npos)
			return "";
		return text.substr(first, text.find_last_not_of(" \t") - first + 1);
	}

	bool isLabel(const Line& line)
	{
		return !line.text.empty() && line.text[0] != ' ' && line.text[0] != '\t' && line.text[line.text.size() - 1] == ':';
	}

	string labelName(const Line& line)
	{
		return line.text.substr(0, line.text.size() - 1);
	}

	bool isOptimizerLabel(const Line& line)
	{
		return isLabel(line) && line.text.compare(0, 2, "_L") == 0;
	}

	// the words of an instruction line: "mov w, x" -> mnemonic "mov", operands "w, x"
	string mnemonic(const Line& line)
	{
		string text = trim(line.text);
		return text.substr(0, text.find_first_of(" \t"));
	}

	string operands(const Line& line)
	{
		string text = trim(line.text);
		size_t space = text.find_first_of(" \t");
		return space == string::npos ? "" : trim(text.substr(space));
	}

	bool isInstruction(const Line& line)
	{
		return !line.text.empty() && (line.text[0] == ' ' || line.text[0] == '\t') && !trim(line.text).empty();
	}

	// directives and markers that the assembler sees: they end a run of code
	bool isDirective(const Line& line)
	{
		string m = mnemonic(line);
		return m == "org" || m == "firsthalf" || m == "samepage" || m == "autopage" || m == "dw" || m == "equ";
	}

	bool skips(const Line& line)
	{
		string m = mnemonic(line);
		return m == "sb" || m == "snb" || m == "decsz" || m == "incsz";
	}

	vector<Line> split(const string& text)
	{
		vector<Line> lines;
		istringstream in(text);
		string line;
		bool inRoutine = false;
		while (getline(in, line))
		{
			Line entry = { line, false };
			string t = trim(line);
			if (t == "autopage off")
				inRoutine = true;
			entry.protectedLine = inRoutine;
			if (t == "autopage on")
				inRoutine = false;
			lines.push_back(entry);
		}
		return lines;
	}

	string join(const vector<Line>& lines)
	{
		string text;
		for (size_t i = 0; i < lines.size(); i++)
			text += lines[i].text + "\n";
		return text;
	}

	// the index of the next line that is not a label, or lines.size()
	size_t afterLabels(const vector<Line>& lines, size_t i)
	{
		while (i < lines.size() && isLabel(lines[i]))
			i++;
		return i;
	}

	// whether the instruction before i might skip the instruction i (looking back over page and bank)
	bool skippedByBefore(const vector<Line>& lines, size_t i)
	{
		while (i > 0)
		{
			i--;
			if (!isInstruction(lines[i]) || lines[i].protectedLine)
				return false;
			string m = mnemonic(lines[i]);
			if (skips(lines[i]))
				return true;
			if (m != "page" && m != "bank")
				return false;
		}
		return false;
	}

	bool dropUnusedLabels(vector<Line>* lines)
	{
		set<string> used;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			string m = mnemonic(line);
			if (isInstruction(line) && (m == "jmp" || m == "call"))
				used.insert(operands(line));
			else if (line.protectedLine || isDirective(line))
			{
				// names in routines and data lines: keep what they might use
				string text = line.text;
				for (size_t at = text.find("_L"); at != string::npos; at = text.find("_L", at + 1))
				{
					size_t end = at;
					while (end < text.size() && (isalnum((unsigned char) text[end]) || text[end] == '_'))
						end++;
					used.insert(text.substr(at, end - at));
				}
			}
		}
		bool changed = false;
		vector<Line> result;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (!line.protectedLine && isOptimizerLabel(line) && !used.count(labelName(line)))
			{
				changed = true;
				continue;
			}
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}

	bool dropRedundantOr(vector<Line>* lines)
	{
		bool changed = false;
		vector<Line> result;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (!line.protectedLine && !result.empty() && isInstruction(line) && trim(line.text) == "or w, #0")
			{
				const Line& before = result.back();
				string o = operands(before);
				if (!before.protectedLine && isInstruction(before) && mnemonic(before) == "mov" && o.compare(0, 3, "w, ") == 0 &&
						o.compare(3, 1, "#") != 0 && !skippedByBefore(*lines, i))
				{
					changed = true;
					continue;
				}
			}
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}

	bool dropJumpToNext(vector<Line>* lines)
	{
		bool changed = false;
		vector<Line> result;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (!line.protectedLine && isInstruction(line) && mnemonic(line) == "jmp")
			{
				string target = operands(line);
				bool next = false;
				for (size_t j = i + 1; j < lines->size() && isLabel((*lines)[j]); j++)
				{
					if (labelName((*lines)[j]) == target)
						next = true;
				}
				if (next)
				{
					changed = true;
					continue;
				}
			}
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}

	// jmp A where A is "jmp B": jmp B
	bool threadJumps(vector<Line>* lines)
	{
		bool changed = false;
		for (size_t i = 0; i < lines->size(); i++)
		{
			Line& line = (*lines)[i];
			if (line.protectedLine || !isInstruction(line) || mnemonic(line) != "jmp")
				continue;
			string target = operands(line);
			for (int hops = 0; hops < 16; hops++)
			{
				// where is the target?
				size_t at = lines->size();
				for (size_t j = 0; j < lines->size(); j++)
				{
					if (!(*lines)[j].protectedLine && isLabel((*lines)[j]) && labelName((*lines)[j]) == target)
					{
						at = j;
						break;
					}
				}
				if (at == lines->size())
					break;
				size_t first = afterLabels(*lines, at + 1);
				if (first >= lines->size() || (*lines)[first].protectedLine || !isInstruction((*lines)[first]) || mnemonic((*lines)[first]) != "jmp")
					break;
				string next = operands((*lines)[first]);
				if (next == target)
					break;
				target = next;
				string indent = line.text.substr(0, line.text.find_first_not_of(" \t"));
				line.text = indent + "jmp " + target;
				changed = true;
			}
		}
		return changed;
	}

	// code behind an unconditional jmp, ret or retp, up to the next label or directive
	bool dropDeadCode(vector<Line>* lines)
	{
		bool changed = false;
		vector<Line> result;
		bool dead = false;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (line.protectedLine || isLabel(line) || isDirective(line) || !isInstruction(line))
			{
				dead = false;
				result.push_back(line);
				continue;
			}
			if (dead)
			{
				changed = true;
				continue;
			}
			string m = mnemonic(line);
			if ((m == "jmp" || m == "retp" || m == "ret") && !skippedByBefore(*lines, i))
				dead = true;
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}

	bool dropDoubleBank(vector<Line>* lines)
	{
		bool changed = false;
		vector<Line> result;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (!line.protectedLine && isInstruction(line) && mnemonic(line) == "bank" && !result.empty())
			{
				const Line& before = result.back();
				if (!before.protectedLine && isInstruction(before) && mnemonic(before) == "bank")
				{
					// the first one is the one that stays only if it cannot be skipped
					if (operands(before) == operands(line) && !skippedByBefore(result, result.size() - 1))
					{
						changed = true;
						continue;
					}
					if (operands(before) != operands(line) && !skippedByBefore(result, result.size() - 1))
					{
						// the later bank wins: the earlier one is useless
						result.pop_back();
						result.push_back(line);
						changed = true;
						continue;
					}
				}
			}
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}

	const int TOP = -2;		// not reached yet
	const int UNKNOWN = -1;

	int meet(int a, int b)
	{
		if (a == TOP)
			return b;
		if (b == TOP)
			return a;
		return a == b ? a : UNKNOWN;
	}

	// an instruction that changes the bank bits of FSR
	bool writesFsr(const Line& line)
	{
		string m = mnemonic(line);
		string o = operands(line);
		if (m == "bank")
			return false;
		if (o.compare(0, 3, "fsr") != 0)
			return false;
		if ((m == "setb" || m == "clrb") && o == "fsr.7")
			return false;
		return o == "fsr" || o.compare(0, 4, "fsr,") == 0 || o.compare(0, 4, "fsr.") == 0;
	}

	// Which bank is selected before every "bank n"? A "bank" is dropped where every way
	// to it has selected the same bank. After a call, after a write to FSR and in
	// assembler routines nothing is known. The code at "_start" runs with bank 0 (reset).
	bool dropKnownBanks(vector<Line>* lines)
	{
		map<string, int> in;
		in["_start"] = 0;
		vector<int> before(lines->size(), UNKNOWN);
		for (int pass = 0; pass < 60; pass++)
		{
			bool changed = false;
			int state = TOP;
			for (size_t i = 0; i < lines->size(); i++)
			{
				const Line& line = (*lines)[i];
				before[i] = state;
				if (line.protectedLine)
				{
					state = UNKNOWN;
					continue;
				}
				if (isLabel(line))
				{
					string name = labelName(line);
					int merged = meet(state, in.count(name) ? in[name] : TOP);
					if (!in.count(name) || in[name] != merged)
					{
						in[name] = merged;
						changed = true;
					}
					state = merged;
					before[i] = state;
					continue;
				}
				if (!isInstruction(line))
					continue;
				if (isDirective(line))
				{
					state = TOP;
					continue;
				}
				string m = mnemonic(line);
				bool skipped = skippedByBefore(*lines, i);
				if (m == "bank")
				{
					int bank = (int) strtol(operands(line).c_str(), NULL, 16);
					state = skipped ? meet(state, bank) : bank;
					if (state == TOP)
						state = bank;
				}
				else if (m == "jmp" || m == "call")
				{
					string target = operands(line);
					int merged = meet(in.count(target) ? in[target] : TOP, state);
					if (!in.count(target) || in[target] != merged)
					{
						in[target] = merged;
						changed = true;
					}
					if (m == "call")
						state = UNKNOWN;
					else if (!skipped)
						state = TOP;
				}
				else if (m == "retp" || m == "ret")
				{
					if (!skipped)
						state = TOP;
				}
				else if (writesFsr(line))
					state = UNKNOWN;
			}
			if (!changed)
				break;
		}

		bool changed = false;
		vector<Line> result;
		for (size_t i = 0; i < lines->size(); i++)
		{
			const Line& line = (*lines)[i];
			if (!line.protectedLine && isInstruction(line) && mnemonic(line) == "bank" && !skippedByBefore(*lines, i) &&
					before[i] >= 0 && before[i] == (int) strtol(operands(line).c_str(), NULL, 16))
			{
				changed = true;
				continue;
			}
			result.push_back(line);
		}
		*lines = result;
		return changed;
	}
}

string optimize(const string& assembly)
{
	vector<Line> lines = split(assembly);
	for (int round = 0; round < 100; round++)
	{
		bool changed = false;
		changed = dropRedundantOr(&lines) || changed;
		changed = threadJumps(&lines) || changed;
		changed = dropJumpToNext(&lines) || changed;
		changed = dropUnusedLabels(&lines) || changed;
		changed = dropDeadCode(&lines) || changed;
		changed = dropDoubleBank(&lines) || changed;
		changed = dropKnownBanks(&lines) || changed;
		if (!changed)
			break;
	}
	return join(lines);
}
