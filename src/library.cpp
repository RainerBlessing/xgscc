#include "library.h"
#include <cstdlib>
#include <sstream>
#include "errors.h"
#include "kernel.h"

using namespace std;

static Function assemblerFunction(const string& name, const string& text, const vector<string>& registers)
{
	Function function;
	function.name = name;
	function.line = 0;
	function.returnsValue = false;
	function.body = StmtPtr(new Stmt(Stmt::BLOCK, 0));
	function.assembly = text;
	function.registers = registers;
	return function;
}

void addXgsLibrary(Program* program, int lines, int colour)
{
	vector<string> none;
	vector<string> frameRegisters;
	frameRegisters.push_back("a");
	frameRegisters.push_back("n");
	program->functions.push_back(assemblerFunction("xgs_init", xgsInitSource(), none));
	program->functions.push_back(assemblerFunction("xgs_frame", xgsFrameSource(lines, colour), frameRegisters));
}

static string trim(const string& text)
{
	size_t first = text.find_first_not_of(" \t\r");
	if (first == string::npos)
		return "";
	return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool includes(const string& source, const string& name)
{
	istringstream in(source);
	string line;
	while (getline(in, line))
	{
		line = trim(line);
		if (line.compare(0, 1, "#") != 0)
			continue;
		line = trim(line.substr(1));
		if (line.compare(0, 7, "include") != 0)
			continue;
		line = trim(line.substr(7));
		if (line == "<" + name + ">" || line == "\"" + name + "\"")
			return true;
	}
	return false;
}

bool definedNumber(const string& source, const string& name, long* value)
{
	istringstream in(source);
	string line;
	int number = 0;
	while (getline(in, line))
	{
		number++;
		line = trim(line);
		if (line.compare(0, 1, "#") != 0)
			continue;
		line = trim(line.substr(1));
		if (line.compare(0, 6, "define") != 0)
			continue;
		line = trim(line.substr(6));
		if (line.compare(0, name.size(), name) != 0 || line.size() == name.size() || (line[name.size()] != ' ' && line[name.size()] != '\t'))
			continue;
		string text = trim(line.substr(name.size()));
		size_t comment = text.find("//");
		if (comment != string::npos)
			text = trim(text.substr(0, comment));
		char* end = NULL;
		long result = strtol(text.c_str(), &end, 0);
		if (text.empty() || *end != '\0')
			throw CompileError(number, name + " must be a number");
		*value = result;
		return true;
	}
	return false;
}
