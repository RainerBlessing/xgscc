#include <stdint.h>

#define LIMIT 10

uint8_t total;

uint8_t square_sum(uint8_t a, uint8_t b)
{
    return (a << 1) + b;
}

void add(uint8_t value)
{
    total += value;
}

void main(void)
{
    for (uint8_t i = 0; i < LIMIT; i++) {
        add(square_sum(i, 1));
    }
    while (1) {
    }
}
