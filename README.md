# xgscc

A C compiler for the XGameStation Micro Edition (Ubicom SX52). It writes a hex
file that [xgsemu](https://github.com/RainerBlessing/xgsemu) runs.

```bash
xgscc game.c -o game.hex      # compile
xgscc -S game.c -o game.asm   # the assembler source instead
xgsemu game.hex
```

This is stage 2 of `IMPLEMENTATION_PLAN.md`: a small subset of C with functions.

## The C that is supported

```c
#include <stdint.h>

#define LIMIT 10                // names for constants (no macros with parameters)

const uint8_t STEP = 2;         // a constant, takes no RAM
uint8_t total;                  // global variables, optionally with a constant
uint8_t start = 5;              // initializer; "unsigned char" is the same type

uint8_t twice(uint8_t value)    // functions of uint8_t parameters, returning
{                               // uint8_t or void
    static uint8_t calls;       // static locals keep their value
    calls++;
    return value << 1;
}

void add(uint8_t value);        // a prototype; the order of definitions is free

void main(void)                 // main is void main(void) and starts the program
{
    for (uint8_t i = 0; i < LIMIT; i += STEP) {
        if (i == 6)
            continue;
        add(twice(i));
    }
    do {
        total--;
    } while (total > 100 && !(total & 1));
}

void add(uint8_t value)
{
    total += value;
}
```

- **Type**: `uint8_t` (and `unsigned char`) only, plus `void` as a result. All
  arithmetic is 8 bit unsigned, constant expressions too: `2 - 3` is 255, and a
  shift by 8 or more gives 0.
- **Expressions**: `+ - & | ^ ~ ! << >>`, unary `-`, the comparisons, `&&`, `||`,
  parentheses and calls. Shift counts must be constants.
- **Statements**: assignment with `= += -= &= |= ^= <<= >>=`, `++` and `--` (as
  statements only), calls, `if`/`else`, `while`, `do`/`while`, `for`, `break`,
  `continue`, `return`, blocks and declarations (also `static` and `const`).
- **Preprocessor**: `#include` is ignored, `#define NAME tokens` and `#undef NAME`
  work. Nothing else (no `#if`, no macros with parameters) yet.
- **Not yet**: `* / %`, other types, pointers, arrays, `switch`, `goto`. The compiler
  says so with the line: `game.c:7: operator '*' is not supported yet`.
- **Refused on purpose**: recursion (`recursion is not supported: f -> g -> f`) and
  calls nested more than 8 deep, which is the size of the return stack of the
  SX52.

## Memory model

There is no data stack. Every variable has a fixed RAM address, chosen when the
whole program is known:

- The RAM available is the six direct registers `0x0A`-`0x0F` and the banks 1-7
  (`0x10`-`0x7F`): 118 bytes. A banked byte is reached through the bank that FSR
  selects. The compiler writes `bank n` only where the bank is not already
  selected; it is unknown after a label and after a call. The most used variables
  (uses in loops count eight times more per level) get the direct registers, which
  need no `bank`, then the first bank, and so on.
- Globals, static variables and the parameters of a function have an address of
  their own. `const` variables take none.
- The local variables and the temporaries of an expression (at most four at a time)
  form the frame of a function. Functions that cannot be active at the same time
  (neither calls the other, directly or through others) share their frames; so a
  function that calls another one keeps its locals.
- A call stores the arguments into the parameters of the callee and returns the
  result in W. Only functions that main can reach are compiled.
- `out of RAM` says how many bytes of which kind are needed.

## Code and pages

The generated source starts with `autopage on` (see the README of xgsemu): the
assembler puts `page` where code and its jump or call target are on different
pages of 512 words, and where code runs on into the next page. A called function
starts at the beginning of a page or in the first half of one (a `call` reaches no
further) and ends with `retp`, which gives the page back. `page` costs a cycle;
the video code of stage 4 will be written without it.

The start code (`org fff`, `jmp _start`) initializes the globals and the static
variables and falls into the code of `main`. When `main` ends the
program loops on a `jmp` to itself.

## Build

Needs CMake, a C++11 compiler and, for the tests, a network connection once
(GoogleTest is downloaded).

```bash
git clone --recurse-submodules <repository>
mkdir build && cd build
cmake .. && cmake --build .
./unit_tests
```

The CPU, assembler and disassembler come from the submodule
[sxcpu](https://github.com/RainerBlessing/sxcpu). The tests compile programs,
run them on the emulated SX52 (without video) and look at the RAM.

## Known limits

- `xor w, #ff` has the opcode `0xFFF`, which sxcpu takes for an empty ROM word and
  cannot assemble. The compiler writes `mov w, /reg` for `~x` and `x ^ 255`.
