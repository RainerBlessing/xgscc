# xgscc

A C compiler for the XGameStation Micro Edition (Ubicom SX52). It writes a hex
file that [xgsemu](https://github.com/RainerBlessing/xgsemu) runs.

```bash
xgscc game.c -o game.hex      # compile
xgscc -S game.c -o game.asm   # the assembler source instead
xgscc --map game.c -o game.hex   # also writes game.map: where everything is
xgscc --listing game.c -o game.hex   # also writes game.lst: the words under their C lines
xgscc game.c -o game.obj       # a binary image (two bytes per word) instead of hex
xgscc -O0 game.c -o game.hex   # without the optimizer (-O1 is the default)
xgsemu game.hex
```

A subset of C with functions, arrays, pointers, structs, 16 bit types and a preprocessor.

## The C that is supported

```c
#include <stdint.h>

#define LIMIT 10                // names for constants (macros with parameters work, too)

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
- **Preprocessor**: `#define NAME tokens`, `#define NAME(a, b) tokens` (the arguments are
  expanded first, a macro is not expanded inside itself; no `#` and `##`), `#undef`, `#if`,
  `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif` (the operators of C, `defined(X)`,
  unknown names are 0), `#error`, and `#include`: `<stdint.h>` and `<xgs.h>` are built
  in, `"file.h"` reads the file relative to the directory of the file that includes it (a
  file that includes itself is refused after 16
  levels). Not there: `#pragma`, `#line`, `-D` on the command line. An error in an
  included file is reported with that file's name and line.
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

- The RAM has 246 bytes: the six direct registers `0x0A`-`0x0F`, the banks 1-7
  (`0x10`-`0x7F`) and the banks 8-15 (`0x80`-`0xFF`, FSR bit 7; the example programs
  of the XGS ME use them, too). The variables, parameters and temporaries (118 bytes) live
  in the lower part, because a bank is selected with `bank n`, which does not touch bit
  7 of FSR. A banked byte is reached through the bank that FSR selects; the compiler
  writes `bank n` only where the bank is not already selected (see "The optimizer").
  The most used variables (uses in loops count eight times more per level) get the
  direct registers, which need no `bank`; the others are put into the banks so that
  variables that are used one after the other share a bank.
- **Arrays** are only reached through FSR and fill the RAM from the top: first the
  upper half (127 bytes, the last byte stays free so that the address behind the end of
  an array fits into a pointer), then on into the lower half. A pointer into the upper
  half sets bit 7 of FSR, so after every access through FSR the code clears it, in
  programs that use the upper half (one cycle).
- Globals, static variables and the parameters of a function have an address of
  their own. `const` variables take none.
- The local variables and the temporaries of an expression form the frame of a
  function. Functions that cannot be active at the same time (neither calls the
  other, directly or through others) share their frames; so a function that calls
  another one keeps its locals.
- Arrays have a run of addresses of their own (also local ones and static ones; a local
  array is written again from its initializer every time the function runs, otherwise it
  is not touched). A 16 bit variable whose address is taken is placed the same way, so
  that its two bytes are next to each other, but in the lower half. A struct variable is
  one run too, also in the lower half; `s.x` is a variable at its place (direct, as fast
  as any other), `p->x` and `s[i].x` go through FSR. The element of an array of structs is
  found with its size: shifts for 1, 2, 4, 8 bytes, otherwise the multiplication of the
  runtime.
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
the video code is written without it. A routine in assembler that has to stay on one page (the
sprite kernel is nearly 500 words) gets a `samepage n` in front, n being its length in
words: it moves to the next page if it would cross the border of one.

The tables sit at the top of the ROM, below the reset vector, each inside one block
of 256 words: `iread` takes the high bits of the address from MODE and the low byte
from W. A read sets MODE (`mov m, #page`; bit 4, which the port control uses, is kept)
and leaves its low bits 0 afterwards. If the code grows into the tables the assembler
reports it as a program that is too big for the ROM.

The start code (`org fff`, `jmp _start`) initializes the globals and the static
variables and falls into the code of `main`. When `main` ends the
program loops on a `jmp` to itself.

## The optimizer

`-O1` (the default) works on the assembler text before it is assembled; `-O0` leaves
it as the code generator wrote it (`examples/pong.c` is 1198 words at `-O0`, 1166 at
`-O1`; the C code of it, without the video kernel and the tile table, 685 and 653).
The rules, each with a test:

- `or w, #0` right after `mov w, reg` (which sets the zero flag itself);
- jumps to the next instruction, jumps to jumps, code behind a jump or `retp`, labels
  of the code generator that nothing uses;
- a `bank n` where every way to it has selected bank `n` already. This follows the
  control flow: at a label the banks of all jumps and of the fall-through must agree,
  a function starts in the bank of all its calls and leaves the bank in which it
  returns (so a call does not forget the bank), and a write to FSR or a routine in
  assembler makes it unknown.

Every program of the tests runs at both levels and has to leave the same RAM behind.
The routines in assembler (the video kernel) are not touched. What is not done:
inlining, constant propagation, anything across statements except the banks; the
generated code keeps the shape of the C source.

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
before the include change the picture (one colour). `examples/xgs_blank.c` is the
smallest program.

### Joystick, sound and SRAM

All three are functions of `<xgs.h>`; `xgs_init()` sets the ports up first. They are
assembler routines, so they work in every picture mode.

| function | what it does |
|---|---|
| `uint8_t xgs_joystick()` | the buttons of the first joystick: `0x01` up, `0x02` down, `0x04` left, `0x08` right, `0x10` fire; 1 = pressed |
| `void xgs_sound(a, d)` | one command to the sound chip (BU8763): `a` is the register (0 OPER, 1 MODE, 2 FREQ, 3 GAIN), `d` the 8 bits of data |
| `void xgs_note(channel, note)` | `xgs_sound(2, channel << 6 \| note)`: the pitch of a channel (note 0 is A2, 110 Hz, a semitone per step) |
| `uint8_t xgs_sram_read(addr)` | one byte of the 128 KB SRAM, `addr` is 16 bits |
| `void xgs_sram_write(addr, value)` | writes one |
| `void xgs_sram_bank(b)` | 0 or 1: which 64 KB half the two functions above use |

Each call takes a few hundred cycles (the 16 bits of the joystick, the 11 bits of a
sound command and the 12 bits of the SRAM page are clocked out one by one): too long for
a picture line, but nothing for the time of the game. `examples/xgs_joystick.c` moves a
tile and plays a note.

### Tiles

With `XGS_COLUMNS`, `XGS_ROWS` or `XGS_TILE_LINES` defined the picture is a map of
tiles:

```c
#include <stdint.h>
#define XGS_COLUMNS 16          // 1 to 255 (the cells are 4090 / columns cycles wide)
#define XGS_ROWS 12
#define XGS_TILE_LINES 16       // 1 to 255; rows * tile lines <= 255 (240 lines fill a television)
#include <xgs.h>

// one byte for the video port per line of every tile: 256 / XGS_TILE_LINES tiles
const uint8_t xgs_tiles[256] = { ... };

void main(void) {
    xgs_init();
    xgs_tile(3, 2, 5);      // column 3, row 2 shows tile 5
    for (;;) xgs_frame();
}
```

The map is the array `xgs_map` (one byte per cell, columns times rows bytes, the tile
number times `XGS_TILE_LINES`); `xgs_tile(x, y, tile)` writes it, `xgs_get_tile(x, y)`
reads the number back. A cell is one write to the port per line: the kernel takes the
byte at `xgs_tiles[tile * XGS_TILE_LINES + line]` with `iread` (13 cycles per cell,
the rest of the cell is waiting), so the colour can change once per cell and line. The
kernel takes `acc_hi`, `acc_lo` and two more of the direct registers.
`examples/xgs_tiles.c` shows a moving pattern, `examples/pong.c` is a complete game: Pong with a title, started with fire, on
the tile map with the joystick, a computer opponent, sound and a score.

The kernel is an assembler function: the compiler puts its text in the ROM as it is,
without `page` instructions, on one page, and gives it two of the direct registers
(`0x0A-0x0F`), because a `bank` in the middle of it would change the timing. With the
two registers of `acc_hi` and `acc_lo` that leaves two direct registers for fast
variables.

### Sprites

With `XGS_SPRITES` as well, a few sprites are drawn over the tile map, the way the Atari 2600
draws its players: a sprite is a bitmap of 8 x 8 pixels with a colour for every pixel row, at a
position that is exact to a pixel (an eighth of a cell), with copies at a fixed distance and a
mask of which copies are there. A formation of invaders is one sprite.

```c
#include <stdint.h>
#define XGS_COLUMNS 15          // 2 to 15 (a cell has 8 pixels)
#define XGS_ROWS 3              // a small map: every sprite takes 24 + 3 * XGS_SPRITE_ROWS bytes of the RAM
#define XGS_TILE_LINES 80       // rows * tile lines <= 255: 3 rows of 80 lines, 3 tiles
#define XGS_SPRITES 3           // 1 to 4
#define XGS_SPRITE_ROWS 3       // rows of copies below each other, 1 to 5 (default 1)
#define XGS_SPRITE_STEP 2       // cells from one copy to the next, 2 to 15 (default 2)
#define XGS_SPRITE_LINES 4      // lines of a pixel row: 1, 2, 4 or 8 (default 1)
#include <xgs.h>

const uint8_t xgs_tiles[256] = { ... };             // as for the tile map
// 16 bytes for an image: the shape (the highest bit is the pixel on the left) and the colour of
// every one of its 8 pixel rows, from the top; at most 16 images
const uint8_t xgs_sprite_images[32] = { 0x18, 0x0f, 0x3c, 0x0e, ... };

void main(void) {
    xgs_init();
    xgs_sprite(0, 20, 30, 1);          // sprite 0: x 20, y 30, image 1
    xgs_sprite_copies(0, 0, 0x1f);     // row 0 of its copies: the 5 copies (bit n is copy n)
    for (;;) xgs_frame();
}
```

| | |
|---|---|
| `xgs_sprite(n, x, y, image)` | place sprite `n`: `x` counts pixels from the left edge of the picture (0 to 8 * columns - 1), `y` is the line of its first pixel row, `image` the number in `xgs_sprite_images` |
| `xgs_sprite_copies(n, row, mask)` | which copies of the row `row` are there: bit 0 is the copy at `x`, bit 1 the one `XGS_SPRITE_STEP` cells to the right, and so on. A sprite without a bit is not there: this is how to hide it. |
| `xgs_sprites[]` | the table behind them, one record for every sprite: `x`, `y`, image * 16, the masks of the rows, then work space of the kernel (the patterns of the copies, the state of a line, the 8 pixel rows of the image shifted to `x`); the masks can be read there, too |

`xgs_frame()` first works out the patterns of the copies of the sprites and the shifted bitmaps (2 800 to 4 500
cycles for 3 sprites of 3 rows, depending on `x & 7`; 540 to 1 100 for one sprite of 1 row; 5 400 to 7 700 for
4 sprites of 5 rows) and waits for the end of the frame, then draws the picture;
the game runs after it returns, as before. In short:

- A cell has 8 pixels, `4090 / (8 * columns)` cycles apart (34 with 15 columns). A pixel has the
  colour of the sprite where its shape has a bit, else the colour of the tile. The sprites do not change
  the map. With 4 lines per pixel row the pixels of the sprite are about square.
- A sprite is `8 * XGS_SPRITE_LINES` lines high, and so is every further row of copies (the rows of
  the image that are 0 make a gap): `8 * rows * XGS_SPRITE_LINES` lines at most, 255 in all. In the
  other lines the sprite is not there.
- The sprites reach both edges of the picture; at the right edge they are cut off. `y` is a line number
  of 8 bits, the lines of a sprite count modulo 256: one that reaches past line 255 comes in again at
  the top.
- Sprites are not told apart in a cell: all their pixels in it get the colour of the sprite with the
  highest number that has a copy there. A sprite has a colour of its own for every pixel row, a
  cell for every line, so this only shows where two sprites touch.
- A sprite takes 116 to 121 cycles of the start of every line (1, 2, 4 or 8 lines per pixel row: 121, 116,
  118, 120), the kernel has room for 4: three in the 374 cycles between the sync writes, the fourth in the
  198 cycles behind them. It is 411 to 479 words long and takes all six direct registers (`o`, `bg`, `col`,
  `no`, `acc_hi`, `acc_lo`). A page of the ROM has 512 words; 1 to 4 sprites fit with every number of
  columns and every `XGS_SPRITE_LINES`. The compiler reports it, with the number of words, if a setting does not.
- The kernel is placed at the start of a page of the ROM (`samepage`), and so is
  `xgs_sprite_update`, so up to about 500 words in front of each can stay empty, which matters
  if the ROM is nearly full. RAM: `24 + 3 * XGS_SPRITE_ROWS` bytes for a sprite (16 of them are the shifted
  bitmap that the update makes, so that the kernel does not have to shift), 8 for the kernel,
  `columns * rows` for the map. The RAM is shared between the map and the sprites, so the size of the map is a
  choice of the program, not a limit of the compiler: `XGS_ROWS * XGS_TILE_LINES` only has to give the lines of
  the picture (at most 255), and a map with fewer rows has taller tiles (`256 / XGS_TILE_LINES` of them).
  A 15 x 10 map (150 bytes) with 2 sprites of 1 row of copies takes 218 of the 246 bytes in an empty program;
  4 sprites of 3 rows take 132 bytes, so there (`examples/xgs_sprites.c`: 3 rows of 80 lines, 45 bytes) the map is
  small. The tile map gets more rows when there are fewer sprites or fewer rows of copies.

`examples/xgs_sprites.c` is a small invaders scene with 4 sprites: the formation (one sprite, 5 copies in 3
rows), a cannon that the joystick moves, a shot that removes a copy from the formation and the bombs of the
invaders, which start below the lowest invader nearest to the cannon, fall, and start the game again when they hit it
(213 of the 246 bytes of RAM).
`doc/video.md` has the numbers of the kernel and how it was tested: a model of the picture that is
compared with every write to the video port of a frame.

## The map

`--map` writes a text file next to the output (`game.hex` gives `game.map`) with the
place of every variable, array and struct in the RAM (names that share a place are
listed together), the address and size in words of the start code, every function,
the runtime routines and the tables in the ROM, how much of both is free, and the
deepest nesting of calls against the 8 entries of the return stack:

```
RAM: 24 of 246 bytes used, 222 free
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
