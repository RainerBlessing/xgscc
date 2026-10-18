#ifndef OPTIMIZER_H_
#define OPTIMIZER_H_
#include <string>

/**
 * The peephole optimizer for the assembler text that the code generator writes
 * (after the bank pass, before the assembler). Rules, each applied until nothing
 * changes any more:
 *
 *   - "or w, #0" right after "mov w, register" (which sets the zero flag, too)
 *   - labels "_L..." that nothing jumps to
 *   - a jump to the next instruction
 *   - a jump to a jump goes to the end of the chain
 *   - code behind an unconditional jump or return up to the next label
 *   - the same "bank n" twice in a row
 *
 * Routines in assembler (between "autopage off" and "autopage on") are not touched.
 * The result does the same as the text before.
 */
std::string optimize(const std::string& assembly);

#endif /*OPTIMIZER_H_*/
