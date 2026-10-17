#ifndef ERRORS_H_
#define ERRORS_H_
#include <string>

/** A mistake in the C source. compile() turns it into "file:line: message". */
struct CompileError
{
	CompileError(int line, const std::string& message) : line(line), message(message) {}
	int line;
	std::string message;
};

#endif /*ERRORS_H_*/
