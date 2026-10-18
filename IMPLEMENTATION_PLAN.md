# xgscc: a C compiler for the XGameStation Micro Edition

```
xgscc game.c -o game.hex
xgsemu game.hex
```

A cross compiler from a small subset of C to the SX52 of the XGS ME. It is a
separate project, not part of the emulator repository. It builds on what
[xgsemu](https://github.com/RainerBlessing/xgsemu) and
[sxcpu](https://github.com/RainerBlessing/sxcpu) already have:

| Have | Where |
|---|---|
| Assembler for whole sources with labels, `equ`, `dw`, `autopage` | `sxcpu/asm.h`: `assembleSource()` |
| Disassembler, `sxdis -s` turns a `.hex` back into source | `sxcpu/disasm.h`, `xgsemu/tools` |
| `.hex` writer | `xgsemu/src/hex_file.*` |
| The CPU for tests, also without screen | `sxcpu/cpu.h`, `Cpu(NULL)` |
| Real programs as test material | `xgsemu/examples/*.hex` |

Missing: the front end (lexer, parser, types), the code generator, a runtime
library and the XGS library. That is this plan.

## Decisions made

- **Own small compiler, not SDCC or LLVM.** The SX52 is odd (banked RAM, an
  8 level return stack, 9 bit jumps) and the first goal is a vertical slice that
  runs in the emulator. A new SDCC or LLVM backend costs far more before the
  first program runs. Revisit at the end of Stage 3, when it is clear how much of
  C is wanted.
- **C, not C++.** C++ would need a subset without exceptions, RTTI and `new`;
  that is only worth it once C works.
- **Output is assembler text.** The compiler writes source for `assembleSource()`
  and links `sxcpu` directly, no external process. The assembly is readable and
  can be checked with the tools that exist.
- **Written in C++11 with CMake and GoogleTest**, like the emulator.
  `sxcpu` is a git submodule.
- **No intermediate representation at first.** Syntax tree straight to assembler.
  An IR is added in Stage 3 only if the code generator needs it (16 bit, pointers).
- **Tests run the generated program.** The harness compiles a C snippet,
  assembles it, runs it on `Cpu(NULL)` and looks at the RAM. Compiler output is
  only compared as text where the text itself is the point.
- **No interrupts.** The emulator does not implement them either.

## What the hardware demands

Taken from the emulator (`sxcpu/cpu.cpp`); everything marked *check* has to be
confirmed in the SX52 datasheet before it is relied on.

- Reset: `pc = 0xFFF`, so the word at `0xFFF` is a `jmp`. The status register is
  0 then, the program starts in page 0 (the examples do `jmp 67`).
- ROM: 4096 words, 8 pages of 512. `jmp` reaches the current page. `call` reaches
  the first 256 words of the current page. The page comes from STATUS bits 7:5,
  set by `page n`. `retp` restores the page of the caller. A skipped instruction
  also skips `page` and `bank` in front of the next one.
- Return stack: 8 entries, the ninth call overwrites the oldest (*check*). No
  data stack: locals are static, **no recursion**.
- RAM: registers `0x00`-`0x0F` are direct (`0x00` is INDF, indirect through FSR),
  `0x10`-`0x1F` is a 16 byte window into the bank selected by FSR. Bank 0 as the
  first window is easy, anything else costs `bank n` or FSR writes (*check*
  the exact size and layout of the banks).
- Everything is 8 bit. 16 bit and 32 bit are several instructions.
- Constants in ROM are read with `retw #k` tables or `iread` (*check* timing).
- The video is made by the program: exact port writes at exact cycles. A C
  compiler cannot hold that timing, so the video is an assembler kernel (Stage 4).
- Every added `page` costs a cycle. The code generator must say where it adds
  them.

## Stage 1: a vertical slice

**Goal**: `counter++` in an endless loop runs in the emulator. Lexer, parser and
code generator for the smallest useful subset, plus the test harness and the
start code.
**Subset**: `uint8_t` globals with constant initializers, `main`, assignment,
`+ - & | ^ ~ << >>` (shifts by constants), `++ --`, comparisons `== != < > <= >=`
(unsigned), `if`/`else`, `while`, `for`, blocks, comments, `#include <stdint.h>`
(accepted, no real preprocessor yet).
**Success Criteria**:
- `xgscc counter.c -o counter.hex` writes a hex file that `xgsemu` runs.
- Start code: `org fff` / `jmp _start`, `_start` in page 0 selects bank 1,
  initializes the globals and falls into the code of `main` (a real call comes with
  functions in Stage 2).
- Errors carry file and line (`counter.c:7: unknown name cnt`).
**Tests**:
- Lexer: tokens, numbers (decimal, hex), comments, line numbers.
- Parser: precedence of the operators, `if`/`else` binding, error messages.
- Run tests (compile, assemble, execute N instructions, read RAM): increment,
  every operator with a few values including wraparound at 255, a countdown
  loop, `if`/`else`, nested loops, comparisons at the edges (0, 1, 127, 128, 255).
- `counter.c` from `doc/compiler.txt` as the first end-to-end test.
**Status**: Complete

## Stage 2: functions and static memory

**Goal**: Several functions with parameters and return values, no stack.
**Subset adds**: `void` and `uint8_t` functions, parameters, locals, `return`,
`break`, `continue`, `do`/`while`, `static`, `const`, `#define` for constants.
**Design**:
- Parameters, locals and the return value live at fixed addresses. The call graph
  decides which locals can share an address (functions that never run at the
  same time overlay each other).
- Recursion is an error. So is a call depth above the return stack (8), counted
  over the call graph, with the chain in the message.
- `autopage on` in generated code outside of cycle-critical routines; every callee
  ends with `retp`. The assembler got `firsthalf` (a call reaches only the first
  256 words of a page, so a callee starts there) and puts a `page` where code
  runs on into the next page.
**Success Criteria**:
- Programs with up to 10 functions run correctly in the emulator.
- Recursion and a too deep call chain are refused with a clear message.
**Tests**:
- Parameters in every position, return values, nested calls, the same local name
  in two functions, overlay of locals gives the right result, a function that
  lands on another page.
- Call depth: exactly 8 passes, 9 is an error; recursion is an error.
**Status**: Complete

## Stage 3: bigger types, pointers and the RAM layout

**Goal**: What a game needs: more RAM, 16 bit values, arrays, pointers, constant
tables, multiply and divide.
**Subset adds**: `int8_t`, `uint16_t`, `int16_t`, arrays, pointers (through
FSR/INDF), `const` arrays in ROM, `switch`, `*`, `/`, `%`, shifts by a variable,
struct without bit fields.
**Steps** (each one is committed with its tests, in this order because each needs
the one before):
1. **3a RAM in all banks.** Banks 1-7 plus the six direct registers: 118 bytes
   instead of 22. The code uses `@bank name` markers; after the addresses are
   chosen a pass writes `bank n` only where the bank changes (unknown after a
   label, a call or an indirect access). The most used variables (loops count
   more) get the direct registers, then the first bank, and so on.
   Status: Complete. The banks 8-15 (FSR bit 7) are not used yet.
2. **3b Types.** `int8_t`, `uint16_t`, `int16_t`, casts, usual conversions
   (the larger size wins, unsigned wins a tie), 16 bit values in functions
   (result in W and a register for the high byte), signed compares.
   Status: Complete
3. **3c Pointers and arrays** of RAM through FSR/INDF. A pointer is the 8 bit RAM
   address. Indexing, `&x`, `*p`, pointer arguments, strings as `const` data
   in ROM come with 3d.
   Status: Complete. Pointer and array reads and writes run through FSR/INDF with
   only the direct registers `acc_lo` and `acc_hi` in between, because a `bank`
   instruction would change FSR.
4. **3d Constant tables in ROM.** `const uint8_t t[] = { ... }` read with
   `mov m,#hi / iread` (needs the new assembler directive `align`; the table
   sits on a 256 word boundary, an index is 8 bit). Reading changes only the low
   nibble of MODE, bit 4 (the port control) stays. The compiler places the tables at
   the top of the ROM itself, so no `align` directive was needed; string literals
   also initialize RAM arrays.
   Status: Complete
5. **3e `*`, `/`, `%` and variable shifts** as routines in the runtime, in the
   program only when used. They are written in C (`src/runtime.cpp`) and compiled by
   the compiler itself, which was simpler than assembler and is good enough for now;
   the product is the same for signed and unsigned values, division and remainder
   have signed versions. Faster hand written versions are a task for stage 5.
   Status: Complete
6. **3f `switch` and `struct`.** `switch` is a comparison chain over a hidden
   variable; a struct is one run of RAM whose members are variables at fixed
   places (direct access) or reached through FSR. No struct values (assignment,
   parameters), no nesting by value, no bit fields.
   Status: Complete
7. **3g `--map`**: where every variable, function and table is, how much RAM
   and ROM is used. `xgscc --map game.c -o game.hex` writes `game.map`. The
   assembler gives the addresses of the labels for it (`assembleSource` got an
   optional output for them).
   Status: Complete
- **Decision after stage 3: no IR, the own back end stays, SDCC is not needed.**
  Syntax tree to assembler worked for everything in this stage; the odd parts of
  the SX52 (banks, FSR with INDF, the return stack, `iread`, pages) are handled
  where the code is made, and the tests run every program on the emulated chip, which
  is what an IR would have to be checked against anyway. What an IR would help with,
  better code (registers kept in W across statements, constants folded further,
  common sub expressions), is stage 5 and can be done on the assembler text, where
  the cost of an instruction is visible. SDCC would still need a new back end for the
  banked RAM, the 8 entry return stack and the page registers, and it would not
  know the XGS hardware.
**Success Criteria**:
- A sort of 16 bytes, a 16 bit counter with carry, a string copy and a table
  lookup run correctly.
- The RAM and ROM use are reported (`--map`).
**Tests**:
- Boundaries of every type, signed and unsigned compare, 16 bit add/sub across the
  byte boundary, multiply and divide against C computed in the test.
- Pointer to a variable in each bank, array indexed by a variable, ROM table lookup
  at the page boundaries.
- Out of RAM and out of ROM give a message with the numbers.
**Status**: Complete

## Stage 4: the XGS library and the video kernel

**Goal**: A game written in C: joystick, sound, SRAM and a picture.
**Study** (done, `doc/video.md`): the picture is a program that writes port RE; a line
is 5080 cycles, a frame about 1.33 million (60 Hz); the example programs have 20 to
70 % of the frame free, in pieces of about 4200 cycles in the blank lines.
**Design** (from the study):
- The kernel is cycle-exact assembler that the compiler never touches. C cannot be
  interrupted (no interrupts), so the frame has two parts: `xgs_picture()` makes the
  vertical sync and the picture lines, then C (`update()`) runs in the rest of the
  frame, then `xgs_wait_frame()` pads to the exact frame period with the RTCC. With
  192 picture lines C has about 330 000 cycles per frame. The vertical blank has no
  line syncs; a later mode keeps them with `xgs_yield()`.
- The picture is a tile map (small map in RAM, tiles as tables in ROM read with
  `iread`, 15 cycles per cell like Tetris), then sprites from a list in RAM.
- The compiler gets functions written in assembler: a prototype in C, arguments in
  fixed named places, the text goes into the ROM.
- `xgs.h`: joystick, sound, SRAM, `xgs_tile()`, the frame functions.
**Steps**:
1. **4a** the kernel: vertical sync, N lines of one colour, `xgs_wait_frame`. Tests
   headless: every line 5080 cycles, the prefix at the offsets of `doc/video.md`, the
   frame within 20 cycles of 1 330 956, the picture on `Video`. The kernel is one
   function `xgs_frame()` (wait for the frame period with the RTCC, vertical sync,
   the lines) so that the game does not have to hit the start of a frame. Measured:
   every line exactly 5080 cycles, the period within -4..+1 cycles of 1 330 956 for a
   game of up to 65 000 cycles (see `doc/video.md`, section 8). The picture on
   `Video` itself is not tested because `xgscc` has no SDL; the tests apply the same
   rules for sync, burst and lines.
   Status: Complete
2. **4b** assembler functions in the compiler, the kernel of 4a is the first user.
   Status: Not Started
3. **4c** the tile map kernel and `xgs.h`.
   Status: Not Started
4. **4d** joystick, sound and SRAM as C functions.
   Status: Not Started
5. **4e** sprites, and Pong in C.
   Status: Not Started
**Success Criteria**:
- A Pong written in C runs in `xgsemu` at 60 frames with a stable picture and
  sound.
**Tests**:
- Library functions against the emulator ports (joystick bits, sound writes, SRAM
  addressing) with `Cpu(NULL)`.
- The kernel's cycle count per line asserted against the numbers from the
  examples.
- A frame of the C game compared with a stored picture (checksum of the 560x525
  image from `Video`).
**Status**: In Progress (4b next)

## Stage 5: size, speed and use

**Goal**: Programs that fit into 4096 words and a compiler people can use.
**Contents**:
- Peephole optimizer on the generated assembler (redundant `mov`, `page`, `bank`,
  jump to jump, dead code), measured against the Stage 3 programs.
- Bank tracking so that a `bank` is only written when the bank changes.
- A real preprocessor (`#include`, `#define` with parameters, `#if`).
- Output `.obj` besides `.hex`, a listing with C line numbers, `-O0`/`-O1`.
- README with the supported C subset, the memory model and the limits.
**Success Criteria**:
- The Stage 4 game is at least 20 % smaller with `-O1` than with `-O0`, with the
  same behavior (checked by the tests).
- All earlier tests pass at both levels.
**Tests**:
- Every earlier run test runs at `-O0` and `-O1` and gives the same RAM.
- Each peephole rule has a test with the assembler before and after.
**Status**: Not Started

## Open questions

- SX52 datasheet: exact depth of the return stack, RAM banks, timing of `iread`,
  what a call from the second half of a page does on the chip. Until this is
  checked, the emulator is the only reference.
- How much of the frame is left for C code once the video kernel runs (Stage 4).
- Whether the `sxemu` debugger should show C line numbers (needs the listing
  from Stage 5).
- Whether `xgscc` should read the assembler's `page` insertions into its cycle
  counts, or whether cycle-critical code is always hand written.

## Rules for this project

- Every commit compiles and passes all tests.
- Each stage ends with the Success Criteria met and the Status updated here.
- This file is removed when all stages are done.
