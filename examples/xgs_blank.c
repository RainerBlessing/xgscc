// A picture of one colour with the video kernel. The game goes where "frames++" is:
// it runs after every frame, in the time that is left of the 1.33 million cycles.
// The number of lines and the colour can be changed with
//     #define XGS_LINES 160
//     #define XGS_COLOR 0x9a
// in front of the include.
#include <stdint.h>
#include <xgs.h>

uint8_t frames;

void main(void)
{
    xgs_init();
    for (;;) {
        xgs_frame();
        frames++;
    }
}
