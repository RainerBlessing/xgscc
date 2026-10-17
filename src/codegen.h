#ifndef CODEGEN_H_
#define CODEGEN_H_
#include <map>
#include <string>
#include "ast.h"

/**
 * Translates the program into assembler source for assembleSource().
 * `variables` gets the RAM address of every global. Throws CompileError.
 */
std::string generateAssembly(const Program& program, std::map<std::string, int>* variables);

#endif /*CODEGEN_H_*/
