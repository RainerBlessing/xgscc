#ifndef CODEGEN_H_
#define CODEGEN_H_
#include <map>
#include <string>
#include "ast.h"

/**
 * Translates the program into assembler source for assembleSource().
 * `variables` gets the RAM address of every global (of its low byte),
 * `highBytes` the address of the high byte of every 16 bit global.
 * Throws CompileError.
 */
std::string generateAssembly(const Program& program, std::map<std::string, int>* variables,
                             std::map<std::string, int>* highBytes);

#endif /*CODEGEN_H_*/
