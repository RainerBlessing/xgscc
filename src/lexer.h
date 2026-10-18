#ifndef LEXER_H_
#define LEXER_H_
#include <map>
#include <set>
#include <string>
#include <vector>

struct Token
{
	enum Type { END, IDENT, NUMBER, OPERATOR, STRING };
	Type type;
	std::string text;	// as written; for a character literal the literal itself; for a STRING its characters
	int value;		// NUMBER
	int line;
};

/**
 * The line number of a token (and of everything made from it) holds the file in its
 * upper bits: lineInFile() is the number in that file, fileOfLine() the index in
 * LexInfo::files (0 is the main source).
 */
const int LINE_FILE_SHIFT = 20;
inline int lineInFile(int line) { return line & ((1 << LINE_FILE_SHIFT) - 1); }
inline int fileOfLine(int line) { return line >> LINE_FILE_SHIFT; }

/** What the source defined and included, for the library. */
struct LexInfo
{
	std::vector<std::string> files;				// the paths of the files that were read; index 0 (the main source) is empty
	std::set<std::string> includes;				// the names of all #include lines that were read: "stdint.h", "xgs.h", ...
	std::map<std::string, std::vector<Token> > macros;	// the macros that are defined at the end: name -> replacement
};

/**
 * Cuts C source into tokens, the last one is END. Comments are dropped and the
 * preprocessor is carried out: #define (with and without parameters), #undef, #if,
 * #ifdef, #ifndef, #elif, #else, #endif, #error and #include. #include <name> is built
 * in (stdint.h, xgs.h), #include "file" reads the file from includeDir (without a
 * directory it is ignored). The line number of a token in an included file is the line
 * in that file. info, if given, gets the includes and macros. Throws CompileError.
 */
std::vector<Token> tokenize(const std::string& source, const std::string& includeDir, LexInfo* info);

/** tokenize(source, "", NULL) */
std::vector<Token> tokenize(const std::string& source);

#endif /*LEXER_H_*/
