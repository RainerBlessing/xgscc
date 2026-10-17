#ifndef PARSER_H_
#define PARSER_H_
#include <vector>
#include "ast.h"
#include "lexer.h"

/**
 * Reads the C subset: global uint8_t variables, void functions without
 * parameters, and in them declarations, assignments, ++ and --, if/else,
 * while, for and blocks. Throws CompileError.
 */
Program parse(const std::vector<Token>& tokens);

#endif /*PARSER_H_*/
