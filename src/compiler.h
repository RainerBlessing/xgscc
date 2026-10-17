#ifndef COMPILER_H_
#define COMPILER_H_
#include <map>
#include <string>

struct CompileResult
{
	bool ok;
	std::string error;		// "file:line: message" if !ok
	std::string assembly;		// the generated source for assembleSource()
	std::map<std::string, int> variables;	// name of a global -> RAM address
};

/** Compiles a C source into SX52 assembler. fileName is only used in messages. */
CompileResult compile(const std::string& source, const std::string& fileName);

#endif /*COMPILER_H_*/
