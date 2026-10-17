#ifndef LEXER_H_
#define LEXER_H_
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
 * Cuts C source into tokens, the last one is END. Comments and #include lines
 * are dropped, #define and #undef are carried out (names only, no parameters).
 * Throws CompileError.
 */
std::vector<Token> tokenize(const std::string& source);

#endif /*LEXER_H_*/
