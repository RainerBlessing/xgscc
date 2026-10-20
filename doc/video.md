# How the XGS ME makes its picture, and what that means for C

The study for the video library, with the results of building it. All numbers were **measured in the
emulator** (xgsemu with the example programs of the XGS ME disc, run headless, every
write to port RE recorded with its clock cycle). Nothing was checked on real hardware.

The tools that produced them were throw-away programs on top of `sxcpu`
(`Cpu` with a `VideoPort` that records `changeVideo(cycle, colour)`); the numbers can
be reproduced that way. The programs analysed: `kj_pong_01`, `rem_tetris_01`,
`ms_pac_man`, `racer_01`, `starfield_01`, `mic_plasma_01`, `kj_3d_01`,
`mic_flags_01`, `rem_pac_01`.

## 1. The picture is a program

There is no video chip. Port RE goes to a resistor network that makes a composite
NTSC signal. **The program writes the signal.** One byte to RE is one level:

| bits | meaning |
|---|---|
| 3..0 | luminance. 0 = sync tip, about 6 = black (blanking) level, 15 = white |
| 7..4 | chroma: the phase of the colour against the colour burst (0..14); 15 means no colour |

The emulator (`src/Video.cpp`) turns the writes back into a picture: it measures how
long each level is held, a sync (luminance 0) longer than 360 cycles starts a line,
longer than 4000 cycles starts a frame, a 40 to 50 cycle level after the sync is the
colour burst, everything else is drawn as a fragment of the line. Time is counted in
clock cycles of the SX52 (80 MHz, 12.5 ns); a fragment is drawn with a resolution of 8
cycles.

So a C program does not "draw". It has to **write RE at exactly the right cycles,
all the time**: about 15 750 lines per second, each with its sync and burst, plus the
pixels in them.

## 2. The signal

### One line: 5080 cycles (63.5 µs)

Measured in `starfield_01` (the other examples have the same prefix):

| offset | RE | held | what |
|---|---|---|---|
| +0 | `f6` | 120 | front porch (blanking level) |
| +120 | `f0` | 376 | horizontal sync (4.7 µs) |
| +496 | `f6` | 48 | back porch |
| +544 | `05` | 200 | colour burst (2.5 µs) |
| +744 | `f6` | 128 | back porch |
| +872 | the picture | 4208 | the visible part, until the next line starts at +5080 |

A line without a picture holds one level for the 4208 cycles (`ff` in `starfield_01`,
`f6` in the others). Some programs have lines of 5084 to 5091 cycles; the sync is
generated again every line, so such a small error does not add up.

### One frame: about 1.33 million cycles (60.1 Hz)

257 ordinary lines, then a longer last line: after the white level the program writes
`f0` and holds it for about **19 300 cycles** (the vertical sync, a sync "pulse" that
is much longer than a line). In `starfield_01` that last line has 25 396 cycles. The
frame is 257 × 5080 + 25 396 = 1 330 956 cycles. The examples measure 1 328 752 to
1 340 561 cycles (59.7 to 60.2 Hz).

The emulator takes the number of line syncs since the vertical sync as the row of the
picture, so **blank lines at the top are not needed to place the picture**: the first
picture line is the first line after the vertical sync.

### What the programs show

| program | picture lines | smallest gap between writes | usual gap |
|---|---|---|---|
| `kj_pong_01` | 189 (lines 39 to 228) | 8 | 10 |
| `rem_tetris_01`, `ms_pac_man`, `rem_pac_01` | 200 (33 to 232) | 9 | 15 |
| `racer_01` | 165 | 9 | 20 and 23 |
| `starfield_01` | 32 (the stars) | 26 | 26 |
| `mic_plasma_01` | 208 | 2 | 28 |
| `kj_3d_01` | 191 | 14 | 14 |
| `mic_flags_01` | 136 | 2 | 51 |

A picture line has about 70 to 190 writes. The 15 cycles of Tetris and Pac-Man are one cell
per `iread` of a tile row from the ROM; the display cannot resolve less than 8
cycles.

## 3. The free time

The CPU is busy with the signal in the picture lines. Everything else the program wants
to do, the game, happens in the **lines that show nothing**, in the stretches where the
level is just held. Measured per frame (stretches of 1000 cycles or more):

| program | free cycles per frame | share of the frame | lines without picture |
|---|---|---|---|
| `starfield_01` | 951 000 | 71 % | 225 |
| `kj_pong_01` | 614 000 | 46 % | 68 |
| `mic_flags_01` | 565 000 | 42 % | 121 |
| `kj_3d_01` | 484 000 | 36 % | 65 |
| `racer_01` | 409 000 | 31 % | 92 |
| `rem_tetris_01` and the Pac-Man games | 268 000 | 20 % | 57 |
| `mic_plasma_01` | 262 000 | 20 % | 49 |

The SX52 needs on average about 1.7 cycles per instruction in these programs
(`xgsemu` runs about 47 million instructions per second at 80 MHz), so 270 000 free
cycles are roughly **150 000 instructions per frame** for the game, 9 million per second.
That is a lot for a game like Pong. The catch is that the time comes in pieces of
4208 cycles (one per blank line), in the example programs.

The programs keep the horizontal sync and burst going in the blank lines: a blank
line has the same six writes at the same offsets as above. The free time is the
`f6` or `ff` that is held.

## 4. The other hardware

All taken from `sxcpu/cpu.cpp` and `src/Bu8763.cpp`; this is what the emulator does,
which the example programs run on.

### Joystick (port RA)

The first joystick is a 16 bit shift register that the program reads serially:

- RA1 low: the read starts with bit 15.
- RA0: the clock. On every falling edge (after it was high) the next bit appears
  on RA2: **0 means pressed**, 1 means not pressed.
- The bits of the word: `0x01` up, `0x02` down, `0x04` left, `0x08` right, `0x10`
  fire (the others are not used by the emulator).

The program reads the joystick once per frame; its timing does not matter.

### Sound (port RA, BU8763)

Three channels, serial interface on RA: RA3 = SCLK, RA4 = SDATA, RA5 = SSTB,
RA6 = CKSEL (master clock). A command has 11 bits, MSB first: 3 bits address, 8 bits
data. SDATA is taken over with the rising edge of SCLK, the command is applied with the
rising edge of SSTB.

| address | register | data |
|---|---|---|
| 0 | OPER | which channels run |
| 1 | MODE | bits 7..6 channel, bit 5 sine (otherwise square), bit 4 envelope on, bit 3 fast envelope, bits 2..0 attenuation |
| 2 | FREQ | bits 7..6 channel, bits 5..0 the note (0 is A2, 110 Hz) |
| 3 | GAIN | master volume |

Port RA is shared with the joystick and the SRAM address bit 16: a function that
changes RA must keep the other bits.

### SRAM (ports RA7, RC, RD)

128 KB, 17 address bits: RA7 is bit 16, then a 12 bit page that is shifted in bit by
bit, and the low four bits on RC0..RC3.

- RC4: shifts RC5 into the page register (the page is cleared when the chip is
  deselected).
- RC6: chip enable, **low** selects the chip.
- RC7: 0 writes RD into the SRAM, 1 reads the SRAM into RD.
- RD: the 8 bit data bus.

Reading or writing a byte costs a number of port writes; the cycle count matters when
it happens in a picture line (a byte per pixel is not possible), it does not in the free
time.

## 5. What this means for the compiler

1. **C cannot make the picture.** Timing to the cycle needs instructions whose
   length is known (a 3 cycle `jmp` as a filler, `decsz` loops, `iread` as 4 cycles).
   The compiler's code has no promised timing and `autopage` adds `page` instructions.
   The kernel that produces the signal has to be **written in assembler and not touched
   by the compiler**.
2. **No interrupts.** The emulator has none (RTCC overflow is missing), so C code
   cannot be stopped at a line. Either C runs only where the kernel hands it control
   for a known time, or it hands the control back itself.
3. **The RTCC counts cycles.** With the prescaler off (`PSA` in OPTION) it counts
   every cycle, so a kernel can wait "until cycle n of the frame" without counting
   instructions, and measure how long C code took.
4. **The kernel and C talk through RAM.** The data of the picture (what colours where)
   must be in RAM, ROM or SRAM in a form the kernel reads with a fixed cost per pixel.
   RAM has 118 bytes, so a picture of any size comes from the ROM (tiles, via `iread`)
   or the SRAM, with only a small map or a list of sprites in RAM.
5. **The compiler needs a way to call assembler.** The kernel is a function that C calls
   (`xgs_frame()`), with arguments, written in assembler. The code generator has to
   (a) know such a function by its prototype, (b) store the arguments in fixed named
   places the assembler text can use, (c) allocate the RAM those names need and
   (d) put the text into the ROM. The runtime of stage 3e already does most of this
   for functions written in C.

## 6. The proposal

### Frame structure: the kernel draws, C computes in between

```c
for (;;) {
    xgs_picture();        // assembler: the vertical sync, then the picture lines
    update();             // C: the game; the signal is a held blanking level
    xgs_wait_frame();     // assembler: waits (RTCC) until the frame is 1 330 956 cycles old
}
```

- `xgs_picture()` makes the vertical sync and then P picture lines, each of them
  5080 cycles, from the data in RAM and ROM.
- `update()` runs after the last picture line. With **P = 192 picture lines the C code
  has about 1 330 956 − 25 396 − 192 × 5080 ≈ 330 000 cycles** per frame: more than
  the Tetris and Pac-Man programs have. With P = 160 it is about 490 000.
- `xgs_wait_frame()` pads to the exact frame period, so the refresh rate does not
  depend on how long `update()` took; it reports (a flag) if `update()` took too long.

The vertical blank then has **no horizontal syncs**. The emulator does not need them
(it places the picture by the vertical sync and the syncs of the picture lines). Real
sets and capture devices mostly cope with a few dozen missing line syncs, but this is
not checked. A second mode, later, keeps the syncs alive with `xgs_yield()`: C code
that calls it at least once per 4000 cycles finishes the line that is open and starts
the next blank line; a late call only makes one blank line longer, which is harmless
in the vertical blank.

### The pixels

Start with a **tile map**, the way Tetris and Pac-Man do:

- the map is small and lives in RAM (for example 20 × 12 tile numbers),
- the tiles are in the ROM as tables of colour bytes, one table per tile row,
- the kernel reads the number of a tile, `iread`s the colour of the row, writes RE:
  15 cycles per cell are known to work (Tetris).

Sprites (Pong's ball and paddles) are a list in RAM that the kernel checks per line;
that is the next step. A bitmap from the SRAM is possible but a byte costs several port
writes, which fits only a coarse picture.

### Steps

1. **4a** the kernel in assembler: vertical sync, N lines with a single colour,
   `xgs_wait_frame`. Tested headless: every line 5080 cycles, the prefix at the offsets
   of section 2, the frame within 20 cycles of 1 330 956, and the picture on the `Video`
   of xgsemu (a stored checksum).
2. **4b** assembler functions in the compiler (point 5 above), with the kernel of
   4a as the first user.
3. **4c** the tile map kernel and `xgs.h`: `xgs_tile(x, y, tile)`, the tile tables.
4. **4d** the joystick, the sound and the SRAM as C functions.
5. **4e** sprites, and Pong in C.

## 7. Open questions

- The size of the line syncs in the vertical blank for real hardware (section 6).
- Whether `P` should be fixed for a program or change per frame.
- How the tile tables are given: as `const uint8_t` tables in C (they already live in
  the ROM and are read with `iread`) or in a separate asset file. The first needs
  nothing new.
- The NTSC details (burst phase, the colour wheel of the XGS ME board) are as the
  emulator has them; the real hardware could differ.

## 8. Result of step 4a: the kernel

`src/kernel.cpp` makes `xgsKernelSource(lines, colour)`: the assembler of `xgs_init` and
`xgs_frame`, tested in `tests/kernel_test.cpp` by running it on the emulated SX52 and
recording every write to RE.

- **Lines**: every line has the six writes at the offsets of section 2 (0, 120, 496,
  544, 744, 872 cycles) and is exactly 5080 cycles long, in every line of every frame.
- **Frame**: the vertical sync is `f0` for exactly 19 283 cycles, then the lines, then the
  blanking level is held while the game runs. The frame period is 1 330 956 cycles; the
  measured error is **−4 to +1 cycles** (the wait ends when a loop sees the RTCC tick,
  which takes between 0 and 5 cycles).
- **The time of the game**: `xgs_frame()` waits until the frame is over, so the rate
  does not depend on the game as long as the game takes less than the slack, which is
  `1 330 956 − 19 284 − 5080 × lines` cycles (336 000 for 192 lines, 16 000 for 255)
  **and** less than 65 536 cycles, what the RTCC can count with its largest prescaler
  (1:256). Longer makes that one frame longer, by 65 536 cycles when the game overran
  the counter; the lines stay exact. A game that needs more time per frame will call a
  `xgs_tick()` that counts the wraps (not written yet).
- **Size and place**: about 190 words, in the first half of a page, on one page, no
  `page` instruction inside (the text has no `autopage`).
- **Calling it**: `call xgs_frame` and `call xgs_init`; they end with `retp`.
  They use four registers, `xgs_count`, `xgs_n`, `xgs_hi`, `xgs_lo`, that the caller
  defines. How C gets to call them is step 4b.

## 9. Result of step 4b: the kernel is called from C

`#include <xgs.h>` adds `xgs_init()` and `xgs_frame()` to the program. They are
**assembler functions** (`Function::assembly`): the code generator writes their text
into the output as it is, between `autopage off` and `autopage on`, gives the registers
that they ask for (`{a}`, `{n}`) the direct registers 0x0A to 0x0F and checks after
the assembly that each of them is on one page and callable (first half of a page).
The tests of `tests/xgs_test.cpp` run C programs on the emulated SX52 and check lines,
frame period and the game that runs once per frame; the kernel is called from another
page, too.

The picture was also checked with the `Video` class of xgsemu itself (a program that
runs the hex file on `Cpu` with a real `Video`): 192 lines, drawn twice, in one colour,
and the colour for `0xe6` is the same as the background of `starfield_01`. That check
needs SDL, which `xgscc` does not use, so it is not part of the tests.

## 10. Result of step 4c: the tile map

`XGS_COLUMNS`, `XGS_ROWS` and `XGS_TILE_LINES` switch `xgs_frame()` to a picture of
tiles (`xgsTileFrameSource` in `src/kernel.cpp`). The prefix of every line is the same
as before (writes at 0, 120, 496, 544, 744 cycles), then the cells come from 872 on,
`4090 / columns` cycles apart, and a last write to the blanking level ends the line
(`columns + 6` writes per line). The tests record them and check each cell's value
and cycle against the map and the table, for 1 to 32 columns and tiles of 1 to 16 lines.

- **A cell** (13 cycles of work): `mov w, indf` (the map byte, tile number times tile
  lines), `add w, s` (the line in the tile), `mov m, #page`, `iread` (4 cycles), `mov re,
  w`, `inc fsr`, then a wait; `decsz`/`jmp` for the next cell. The table has 256 entries
  and sits on one 256 word block, so its page goes to MODE and the offset is W.
- **The end of a line** counts the line in the tile (`s`); at the end of the tile it
  takes the row start (`r`) from FSR, which has walked over the row, and ends the frame
  when `r` is the end of the map (`v_xgs_map_end`, an `equ` that the code generator now
  writes for every array). All three paths (same row, next row, last line) take the
  same number of cycles, so every line is 5080 cycles.
- **Registers**: `s` and `r` are direct registers of the kernel, the counters use `acc_hi`
  (waits) and `acc_lo` (cells); an assembler function can ask for `{acc_hi}`/`{acc_lo}`.
  FSR points into the map, so bit 7 is cleared before the kernel returns.
- **The map** is limited by the RAM: 246 bytes with the upper half (the arrays now use
  banks 8-15, FSR bit 7). A bigger picture would need another way: 16 x 12 is 192.
- **The frame** has the same period as before; the kernel adds nothing to the wait.

## 11. Result of step 4d: joystick, sound and SRAM

Five more assembler functions (`src/kernel.cpp`, added by `library.cpp` to every
program that includes `<xgs.h>`): `xgs_joystick`, `xgs_sound`, `xgs_sram_read`,
`xgs_sram_write`, `xgs_sram_bank`; `xgs_note` is C on top of `xgs_sound`. The tests
(`tests/hardware_test.cpp`) run them on the emulated SX52 with the sound port recorded
(commands decoded like the BU8763 does) and the joystick word and the SRAM set from
outside.

- `xgs_init()` writes the direction registers (RA2 input, RC output, RD input) and the
  idle levels of RA and RC. Writes to the direction registers need the MODE register
  at `0xF`, so `mov m, #f` comes first.
- **Joystick**: RA1 low then high starts the word, then 16 times RA0 high and low; after
  each falling edge RA2 is the next bit (0 = pressed). The last five bits are kept and
  turned around.
- **Sound**: the 11 bits come out of a 16 bit rotation of address and data (shifted 5
  up first), SDATA is set, SCLK pulsed; SSTB pulsed at the end.
- **SRAM**: the emulator looks at the SRAM only when `setb`/`clrb` change a bit of RC
  (a `mov rc, w` is not seen), so the routines use bit instructions for RC4, RC5 and
  RC6, and `mov rc, w` only for the low four address bits while the chip is deselected.
  The access is the moment RC6 goes low; RC6 high again clears the page register.
- Parameters of assembler functions can be in the banks: the lines that name one get a
  `@bank` marker, which the bank pass resolves (the routines do not use FSR).


## 12. Result of step 4e: sprites

`XGS_SPRITES` adds sprites to the tile map (`xgsSpriteFrameSource` in `src/kernel.cpp`, the library part
in `src/library.cpp`, `README.md` has the C side). The model is the one of the Atari 2600, whose TIA
draws two "players" of 8 pixels with a colour for every line, with copies at a fixed distance and a
mask for them: **a few sprites with a lot of copies**, not a long list. A formation of 15 invaders is one
sprite with 5 copies in 3 rows.

### How the picture is made

- A line is 8 pixels in every cell of the map, each `4090 / (8 * columns)` cycles wide (34 with 15
  columns, so a cell is the same 272 cycles as in the tile kernel). A pixel is `rl o; mov w, bg; snb c; mov w, col;
  mov re, w` (5 cycles): the colour of the sprite where the mask `o` of the cell has a bit, else the
  colour of the tile.
- Only 5 of the 34 cycles are the write: **between two pixels 29 cycles are free**, 8 times in a
  cell. The masks of the next cell are put together in these gaps: one gap for every sprite (23
  cycles), one for the tile of the next cell and the loop (27 cycles), 1 to set up. So a cell
  takes the 272 cycles, whatever there is to draw: every write of every line is at a cycle that is
  known at build time.
- The sprite is not drawn pixel by pixel but as bytes: its shape is shifted to the right by `x & 7` into a pair of
  bytes (the pixels in the cell of `x`, and in the next one) for each of its 8 pixel rows, **once before the frame**
  by `xgs_sprite_update` (16 bytes in the record of the sprite). For every cell the bits of the pattern of the copies
  (16 bits, a bit for every cell where a copy starts, made from the mask at the same time) say whether the first
  byte, the second byte or both of a sprite go into the mask of the next cell. The first cell of a line has no cell
  in front of it to be made in: the sprite routine puts the shape and colour of a copy that starts there into
  two bytes of RAM (`xgs_m0`, `xgs_k0`), from which the start of the line takes the first mask. Overlapping
  sprites in a cell share its colour. A line of a sprite is outside of the sprite where its pattern is 0, so the
  shifted bytes of such a line need no mask.
- The work for the sprites of a line (the line in the sprite, the shifted bytes of the pixel row from the record,
  the colour of the row of the image with one `iread`, the pattern of the row of copies) is one routine for all
  sprites, `xgs_sub`, called from a place for each and left with `retp`: 116 to 121 cycles per sprite (1, 2, 4,
  8 lines per pixel row: 121, 116, 118, 120), the call and the return (6 cycles) included.
  A `call` reaches only the first 256 words of a page (SX book, 1.15.1.3), so the routine is at the
  start of the kernel, behind a `jmp` that skips it. It runs in the stretches of the start of a line that
  are free (118, 374, 46, 198, 123 cycles between the sync, burst and porch writes), and the
  generator puts the sprites there where they fit: three of them in the stretch of 374 cycles (3 * 121 = 363),
  the fourth in the one of 198. Two additions in it have no `clrb status.0` in front (there is no cycle left for it;
  the carry is 0 there); `SpritesTest.TheCarryIsZeroWhereTheKernelDependsOnIt` looks at the carry in front of both.
- The generator counts every instruction (class `Piece`) and pads with delays to the planned
  cycle; a configuration that does not fit is an error of the compiler with the number of cycles.

### Numbers

| | |
|---|---|
| kernel | 411 words for 1 sprite, 435 for 2, 455 for 3, 479 for 4 (1 row of copies, 1 line per pixel row; 455 for the example: 3 sprites, 3 rows, 4 lines); a page has 512 |
| RAM | `24 + 3 * rows` bytes per sprite (16 of them the shifted bitmap), 8 for the kernel |
| time before the frame | `xgs_sprite_update` makes the patterns of the copies and the shifted bitmaps, inside the time of the game, before the wait for the frame: 540 to 1 100 cycles (1 sprite, 1 row; the offset `x & 7` 0 to 7), 2 800 to 4 500 (3 sprites, 3 rows), 3 800 to 6 100 (4 sprites, 3 rows), 5 400 to 7 700 (4 sprites, 5 rows). The example (3 sprites, a joystick that moves and shoots, collisions) leaves 88 100 of the 92 473 cycles that 240 lines leave: game and update take 4 300 (`SpritesTest.TheExampleRunsAndTheShotFlies`). The rest of the wait for the frame is done by the same routine (the wraps of the RTCC), the kernel does the last part, which makes it shorter |
| frame | 1 330 956 cycles ±5 and the vertical sync of 19 283 cycles, like the tile kernel (checked in the tests) |

With 1 to 4 sprites every number of columns (2 to 15) and every number of lines per pixel row fits (checked by
`SpritesTest.FourSpritesWithEveryNumberOfColumnsAndLinesPerPixelRow`). The history: the routine was entered with
a `jmp` and left through a chain of tests (167 to 176 cycles a sprite, 3 sprites needed 13 to 15 columns); `call` and
`retp` made it 156 cycles; the shift in the line (30 cycles) became the shifted bitmap of the update, which, with a
cheaper computation of the pixel row, made the 4th sprite fit. The price is 16 bytes of RAM for every sprite.
The kernel and `xgs_sprite_update` each begin on a page of the ROM (`samepage`), which can leave up to about 500
words in front of them unused.

### Test

`tests/sprites_test.cpp` runs programs on the emulated SX52 and compares **every write to RE of a frame**,
value and cycle, with a model of the picture that is written separately: the tile map, the image tables, and
for every line the copies, the shift, the pixel rows (1 to 8 lines), the rows of copies, the priority of
up to 4 sprites, the edges of the picture (also the cell on the left and the top, where a sprite that is below the picture comes in again). It also moves a sprite and changes
a mask from frame to frame, and checks that errors of the setup are said.

### Not done

More than 4 sprites (a fifth routine of 116 to 121 cycles does not fit what is left of the stretches), shots and bombs as
cheaper objects, a colour for every pixel (the colour is one per
row of the image and cell), collision in the kernel (the C code can compare the positions), a
combination with the hi-res tiles.
