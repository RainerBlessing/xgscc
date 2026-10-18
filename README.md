# xgscc

A C compiler for the XGameStation Micro Edition (Ubicom SX52). It writes a hex
file that [xgsemu](https://github.com/RainerBlessing/xgsemu) runs.

```bash
xgscc game.c -o game.hex      # compile
xgscc -S game.c -o game.asm   # the assembler source instead
xgscc --map game.c -o game.hex   # also writes game.map: where everything is
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

- **Types**: `uint8_t`, `int8_t`, `uint16_t`, `int16_t` (also `unsigned char`,
  `signed char`), and `void` as a result. A value of 16 bits is returned in W and
  the register `acc_hi`. Casts convert: `(uint16_t)x`, `(uint8_t)w`.
- **Arithmetic**: an operation has the larger of the types of its operands
  (unsigned if one is and the sizes are equal, like C). Unlike C, 8 bit stays 8 bit:
  `200 + 100` in `uint8_t` is 44, even if it is stored in a `uint16_t`. Cast one
  operand to get 300. A constant takes the type of the other operand if it fits, and
  widens the operation if not; `x < 300` for a `uint8_t x` compares as 16 bit.
  Constants are computed exactly, and a shift by the size of the type or more gives 0
  (for `>>` of a signed value 0 or -1).
- **Expressions**: `+ - * / % & | ^ ~ ! << >>`, unary `-`, the comparisons, `&&`,
  `||`, parentheses, calls, casts, and with pointers `*p`, `&x`, `a[i]`. `*`, `/`, `%`
  and shifts by a variable amount are calls of small routines that are written in C
  (`src/runtime.cpp`) and only reach the ROM if the program uses them; they cost
  from 44 words (`uint8_t` product) to 383 (signed 16 bit quotient), and several
  hundred cycles. Division is truncating like in C, the remainder has the sign of the
  dividend; dividing by zero gives no error at run time (the quotient is the
  largest value, the remainder the dividend). A shift by the number of bits or more
  gives 0 (the sign for `>>` of a signed value).
- **Statements**: assignment with `= += -= *= /= %= &= |= ^= <<= >>=` (also to `*p` and
  `a[i]`), `++` and `--` (as statements only, also `(*p)++` and `a[i]--`), calls,
  `if`/`else`, `while`, `do`/`while`, `for`, `switch` with `case` constants (also
  stacked, falling through and with `default`; a comparison chain, one test per
  case), `break`, `continue`, `return`, blocks and declarations (also `static` and
  `const`).
- **Arrays and pointers**: `uint8_t a[16];`, `int16_t w[4] = { 1, 2, 3 };`,
  `uint8_t b[] = { 1, 2 };`, and pointers to the four integer types. A pointer is
  the RAM address, one byte. `a` in an expression is the address of its first
  element, `p + n` and `p++` count elements (a step of 2 bytes for 16 bit
  elements), pointers compare like numbers. An array or pointer can be passed to a
  function and returned. Not supported: pointers to pointers, arrays of arrays,
  arrays as parameters (use a pointer), the difference of two pointers, `*p++`
  (write `*p` and `p++` separately).
- **Preprocessor**: `#include` is ignored, `#define NAME tokens` and `#undef NAME`
  work. Nothing else (no `#if`, no macros with parameters) yet.
- **Tables in ROM**: a `const` array is a table in the program memory and takes no
  RAM: `const uint8_t sine[64] = { ... };`, `const uint16_t steps[] = { 1000, 800 };`,
  `const uint8_t text[] = "Hello";` (the string ends with a 0). It is read with an
  index, `sine[i]`, and only that: it has no address, it cannot be passed to a function
  and not changed. A table holds at most 256 bytes (128 elements of 16 bits), each
  byte takes one ROM word. A string also initializes a RAM array:
  `uint8_t line[] = "ready";`.
- **Structs**: `struct Sprite { uint8_t x; int8_t dx; uint16_t score; uint8_t name[4]; struct Sprite *next; };`
  and `typedef struct { ... } Point;`. Members are integers, pointers (also to the
  struct itself) and arrays of them, they follow each other without gaps. Variables
  (also `static`, local, arrays `struct Sprite s[8];`) and pointers work: `s.x`,
  `p->x`, `s[i].x`, `(*p).x`, `&s.x`, `&s[i]`, `p++` steps over a whole struct. Initializer
  lists give the members in order, nested for array members and arrays of structs:
  `struct Sprite t[] = { { 1, 2 }, { 3, 4 } };`. A struct is not a value: no
  assignment of a whole struct, no passing or returning one (use a pointer), no
  struct inside a struct (use a pointer), no bit fields, no `const` struct.
- **Not yet**: `int`/`long`, `goto`. The compiler
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
- The local variables and the temporaries of an expression form the frame of a
  function. Functions that cannot be active at the same time (neither calls the
  other, directly or through others) share their frames; so a function that calls
  another one keeps its locals.
- Arrays have a run of addresses of their own at the end of the banks (up to 112
  bytes in all, also local ones and static ones; a local array is written again
  from its initializer every time the function runs, otherwise it is not touched).
  A 16 bit variable whose address is taken is placed the same way, so that its two
  bytes are next to each other. A struct variable is one run too; `s.x` is a variable
  at its place (direct, as fast as any other), `p->x` and `s[i].x` go through FSR.
  The element of an array of structs is found with its size: shifts for 1, 2, 4,
  8 bytes, otherwise the multiplication of the runtime.
- Reading and writing through a pointer sets FSR and uses INDF. `bank` changes the
  bits of FSR, so while FSR points to data the code touches only the direct
  registers `acc_lo` and `acc_hi` (two of the six, taken only when a program needs
  them); the compiler refuses to generate anything else there.
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

The tables sit at the top of the ROM, below the reset vector, each inside one block
of 256 words: `iread` takes the high bits of the address from MODE and the low byte
from W. A read sets MODE (`mov m, #page`; bit 4, which the port control uses, is kept)
and leaves its low bits 0 afterwards. If the code grows into the tables the assembler
reports it as a program that is too big for the ROM.

The start code (`org fff`, `jmp _start`) initializes the globals and the static
variables and falls into the code of `main`. When `main` ends the
program loops on a `jmp` to itself.

## The video

The picture of the XGS ME is a signal that the program writes to port RE, to the clock
cycle (`doc/video.md` explains it). A C program cannot time that, so the library has a
kernel in assembler:

```c
#include <stdint.h>
#include <xgs.h>            // only with this line the library is part of the program

void main(void)
{
    xgs_init();             // once
    for (;;) {
        xgs_frame();        // waits for the end of the frame, then 192 lines of picture
        // the game runs here, until the next xgs_frame()
    }
}
```

`xgs_frame()` keeps the frame period at 1 330 956 cycles (60.1 Hz) whatever the game
does, as long as the game needs less than 65 000 cycles per frame and less than the time
the picture leaves (336 000 cycles for 192 lines); if it takes longer, one frame is
longer, the lines stay exact. `#define XGS_LINES n` (1 to 255) and `#define XGS_COLOR c`
before the include change the picture; for now it is one colour, tiles come next.
`examples/xgs_blank.c` is the smallest program.

The kernel is an assembler function: the compiler puts its text in the ROM as it is,
without `page` instructions, on one page, and gives it two of the direct registers
(`0x0A-0x0F`), because a `bank` in the middle of it would change the timing. With the
two registers of `acc_hi` and `acc_lo` that leaves two direct registers for fast
variables.

## The map

`--map` writes a text file next to the output (`game.hex` gives `game.map`) with the
place of every variable, array and struct in the RAM (names that share a place are
listed together), the address and size in words of the start code, every function,
the runtime routines and the tables in the ROM, how much of both is free, and the
deepest nesting of calls against the 8 entries of the return stack:

```
RAM: 24 of 118 bytes used, 94 free
  address  bytes  name
  0x0A         1  acc_hi (high byte of 16 bit results)
  0x13         1  score (low byte)
  0x74        12  sprites[4]

ROM: 131 of 4096 words used, 3965 free
  address  words  kind      name
  0x000       73  start     start code and main
  0x049        8  function  scale
  0x051       33  runtime   __mul8u
  0xFEF       16  table     sine
  0xFFF        1  vector    reset vector
```

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
