#ifndef LIBRARY_H_
#define LIBRARY_H_
#include <string>
#include "ast.h"

/**
 * The XGS library, available with #include <xgs.h>:
 *
 *   void xgs_init(void)    sets up the video; call it once
 *   void xgs_frame(void)   waits for the end of the frame, makes the vertical sync
 *                          and the picture lines; the game runs after it returns
 *
 * Adds these functions, written in assembler, to the program.
 * lines is the number of picture lines (#define XGS_LINES, 1 to 255), colour the
 * byte for RE (#define XGS_COLOR).
 */
void addXgsLibrary(Program* program, int lines, int colour);

/** Whether the source has the line #include <name> or #include "name". */
bool includes(const std::string& source, const std::string& name);

/**
 * The number in the line "#define name number" of the source, decimal or 0x hex.
 * Returns false if there is no such line; throws CompileError if it is no number.
 */
bool definedNumber(const std::string& source, const std::string& name, long* value);

#endif /*LIBRARY_H_*/
