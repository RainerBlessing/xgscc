#include <stdint.h>

uint8_t counter;

void main(void)
{
    counter = 0;

    while (1) {
        counter++;
    }
}
