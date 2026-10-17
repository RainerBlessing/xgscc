#ifndef PARSER_H_
#define PARSER_H_
#include <vector>
#include "ast.h"
#include "lexer.h"

/**
 * Reads the C subset: uint8_t variables (static, const), functions of uint8_t
 * parameters returning void or uint8_t, and in them declarations, assignments,
 * calls, ++ and --, if/else, while, do, for, break, continue, return and
 * blocks. Throws CompileError.
 */
Program parse(const std::vector<Token>& tokens);

#endif /*PARSER_H_*/
