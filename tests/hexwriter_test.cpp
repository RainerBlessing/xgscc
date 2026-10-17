#include <gtest/gtest.h>
#include <vector>
#include "hexwriter.h"

namespace {
std::vector<unsigned short> emptyRom() {
    return std::vector<unsigned short>(4096, 0xfff);
}
}

TEST(HexWriterTest, WritesAnEightWordRecordLowByteFirst) {
    std::vector<unsigned short> rom = emptyRom();
    rom[0] = 0xc05;
    EXPECT_EQ(
        ":10000000050CFF0FFF0FFF0FFF0FFF0FFF0FFF0F7D\n"
        ":00000001FF\n",
        hexText(&rom[0], 4096));
}

TEST(HexWriterTest, ByteAddressIsTwiceTheWordAddress) {
    std::vector<unsigned short> rom = emptyRom();
    rom[0xfff] = 0xa67;
    EXPECT_EQ(
        ":101FF000FF0FFF0FFF0FFF0FFF0FFF0FFF0F670A0E\n"
        ":00000001FF\n",
        hexText(&rom[0], 4096));
}

TEST(HexWriterTest, EmptyRomHasOnlyTheEndRecord) {
    std::vector<unsigned short> rom = emptyRom();
    EXPECT_EQ(":00000001FF\n", hexText(&rom[0], 4096));
}
