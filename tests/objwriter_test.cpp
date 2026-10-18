#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "objwriter.h"

TEST(ObjWriterTest, TwoBytesPerWordLowByteFirst) {
    unsigned short rom[4] = { 0xc0a, 0x001, 0xfff, 0x0ab };
    std::string bytes = objBytes(rom, 4);
    ASSERT_EQ(8u, bytes.size());
    EXPECT_EQ('\x0a', bytes[0]);
    EXPECT_EQ('\x0c', bytes[1]);
    EXPECT_EQ('\x01', bytes[2]);
    EXPECT_EQ('\x00', bytes[3]);
    EXPECT_EQ('\xff', bytes[4]);
    EXPECT_EQ('\x0f', bytes[5]);
    EXPECT_EQ('\xab', bytes[6]);
    EXPECT_EQ('\x00', bytes[7]);
}

TEST(ObjWriterTest, TheWholeRomIsWritten) {
    std::vector<unsigned short> rom(4096, 0xfff);
    EXPECT_EQ(8192u, objBytes(&rom[0], 4096).size());
}

TEST(ObjWriterTest, WritesAFileThatReadsBack) {
    std::string path = testing::TempDir() + "/test.obj";
    unsigned short rom[3] = { 0x123, 0xfff, 0x456 };
    ASSERT_TRUE(writeObjFile(path, rom, 3));
    std::ifstream file(path.c_str(), std::ios::binary);
    std::stringstream content;
    content << file.rdbuf();
    EXPECT_EQ(objBytes(rom, 3), content.str());
}

TEST(ObjWriterTest, AFileInANonexistentDirectoryFails) {
    unsigned short rom[1] = { 0 };
    EXPECT_FALSE(writeObjFile("/nonexistent_dir_xgscc/x.obj", rom, 1));
}
