#ifndef PARSER_H_
#define PARSER_H_
#include <vector>
#include "ast.h"
#include "lexer.h"

/**
 * Reads the C subset: integer variables of 8 and 16 bits (static, const), functions of
 * such parameters returning void or such a value, casts, and in them declarations, assignments,
 * calls, ++ and --, if/else, while, do, for, break, continue, return and
 * blocks. Throws CompileError.
 */
Program parse(const std::vector<Token>& tokens);

#endif /*PARSER_H_*/
