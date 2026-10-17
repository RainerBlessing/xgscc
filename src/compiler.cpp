#include "compiler.h"

CompileResult compile(const std::string& source, const std::string& fileName)
{
	(void) source;
	CompileResult result;
	result.ok = false;
	result.error = fileName + ": the compiler does not translate anything yet";
	return result;
}
