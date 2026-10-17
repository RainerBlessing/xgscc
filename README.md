# xgscc

A C compiler for the XGameStation Micro Edition (Ubicom SX52). It writes a hex
file that [xgsemu](https://github.com/RainerBlessing/xgsemu) runs.

```bash
xgscc game.c -o game.hex      # compile
xgscc -S game.c -o game.asm   # the assembler source instead
xgsemu game.hex
```

This is the first stage of `IMPLEMENTATION_PLAN.md`: a small subset of C.

## The C that is supported

```c
#include <stdint.h>             // accepted and ignored, other directives are errors

uint8_t counter;                // global variables, optionally with a constant
uint8_t limit = 10;             // initializer; "unsigned char" is the same type

void main(void)                 // the only function
{
    uint8_t i;                  // local variables (also in blocks and in for)

    for (i = 0; i < limit; i++) {
        counter += 3;
    }
    while (1) {
        if (counter > 100 && !(counter & 1))
            counter >>= 1;
        else
            counter = counter + 1;
    }
}
```

- **Type**: `uint8_t` only. All arithmetic is 8 bit unsigned, constant
  expressions too: `2 - 3` is 255, and a shift by 8 or more gives 0.
- **Expressions**: `+ - & | ^ ~ ! << >>`, unary `-`, the comparisons, `&&`, `||`
  and parentheses. Shift counts must be constants.
- **Statements**: assignment with `= += -= &= |= ^= <<= >>=`, `++` and `--`
  (as statements only), `if`/`else`, `while`, `for`, blocks, declarations.
- **Not yet**: `* / %`, other types, pointers, arrays, functions besides `main`,
  `return`, `break`, `continue`, `do`, `switch`, `#define`. The compiler says so
  with the line: `game.c:7: operator '*' is not supported yet`.

## Memory

- The program must fit in the first 512 words (one page).
- 18 variables: 6 of RAM `0x0E`-`0x1F` after four temporaries at `0x0A`-`0x0D`.
  Expressions with more than four nested temporaries are refused.
- The start code (`org fff`, `jmp _start`) selects bank 1 so that `0x10`-`0x1F` is
  RAM, clears or initializes the globals and falls into the code of `main`.
  When `main` ends the program loops on a `jmp` to itself.

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
