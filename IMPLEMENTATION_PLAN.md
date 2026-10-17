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
   Status: Not Started
3. **3c Pointers and arrays** of RAM through FSR/INDF. A pointer is the 8 bit RAM
   address. Indexing, `&x`, `*p`, pointer arguments, strings as `const` data
   in ROM come with 3d.
   Status: Not Started
4. **3d Constant tables in ROM.** `const uint8_t t[] = { ... }` read with
   `mov m,#hi / iread` (needs the new assembler directive `align`; the table
   sits on a 256 word boundary, an index is 8 bit). Reading changes only the low
   nibble of MODE, bit 4 (the port control) stays.
   Status: Not Started
5. **3e `*`, `/`, `%` and variable shifts** as runtime routines in assembler, in
   the program only when used.
   Status: Not Started
6. **3f `switch` and `struct`.**
   Status: Not Started
7. **3g `--map`**: where every variable, function and table is, how much RAM
   and ROM is used.
   Status: Not Started
- Decide at the end whether an IR is needed and whether to keep the own back end
  (see *Decisions made*).
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
**Status**: In Progress (3b next)

## Stage 4: the XGS library and the video kernel

**Goal**: A game written in C: joystick, sound, SRAM and a picture.
**Needs a study first**: how the examples produce the video (line loop, port RE
timing, where a game can run between lines) and how much time is left for C.
`kj_pong_01` and `rem_tetris_01` are the references. Write the findings in
`doc/video.md` before any code.
**Design**:
- `xgs.h`: `joystick()`, `sound_write()`, SRAM read/write, `wait_frame()`, a
  small drawing interface on top of what the kernel can show.
- The kernel is cycle-exact assembler that the compiler never touches, called
  from C. How C and the kernel share the CPU (frame structure) is the result of
  the study.
- The code generator marks cycle-critical routines: no `autopage`, no inserted
  instructions, a cycle count per routine in the map.
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
**Status**: Not Started

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
