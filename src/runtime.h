#ifndef RUNTIME_H_
#define RUNTIME_H_

/**
 * The C source of the routines that the compiler calls for what the SX52 has no
 * instruction for: multiplication, division, remainder and shifts by a variable
 * amount. They are compiled with every program but only the ones that are called
 * (directly or through each other) reach the ROM.
 */
const char* runtimeSource();

#endif /*RUNTIME_H_*/
