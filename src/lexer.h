#ifndef LEXER_H_
#define LEXER_H_
#include <string>
#include <vector>

struct Token
{
	enum Type { END, IDENT, NUMBER, OPERATOR };
	Type type;
	std::string text;	// as written; for a character literal the literal itself
	int value;		// NUMBER
	int line;
};

/**
 * Cuts C source into tokens, the last one is END. Comments and #include lines
 * are dropped. Throws CompileError.
 */
std::vector<Token> tokenize(const std::string& source);

#endif /*LEXER_H_*/
