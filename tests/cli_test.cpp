// xgscc is run as a program, the way a user runs it.
#ifndef _WIN32
#include <gtest/gtest.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fstream>
#include <sstream>
#include <string>

namespace {

struct Result {
    int exitCode;
    std::string output;     // stdout and stderr
};

Result run(const std::string& command) {
    Result result = { -1, "" };
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == NULL) {
        return result;
    }
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        result.output.append(buffer, count);
    }
    int status = pclose(pipe);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

std::string quoted(const std::string& path) {
    return "'" + path + "'";
}

std::string readFile(const std::string& path) {
    std::ifstream file(path.c_str());
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream(path.c_str()) << text;
}

}

class CliTest : public ::testing::Test {
protected:
    std::string dir = testing::TempDir();

    Result xgscc(const std::string& arguments) {
        return run(quoted(XGSCC_PATH) + " " + arguments);
    }
};

TEST_F(CliTest, WritesAHexFile) {
    Result result = xgscc(quoted(EXAMPLES_DIR "/counter.c") + " -o " + quoted(dir + "/counter.hex"));
    EXPECT_EQ(0, result.exitCode) << result.output;

    std::string hex = readFile(dir + "/counter.hex");
    EXPECT_EQ(0u, hex.find(":10000000")) << hex;
    EXPECT_NE(std::string::npos, hex.find(":00000001FF")) << hex;
}

TEST_F(CliTest, TheResetVectorIsInTheLastRecord) {
    xgscc(quoted(EXAMPLES_DIR "/counter.c") + " -o " + quoted(dir + "/counter.hex"));
    EXPECT_NE(std::string::npos, readFile(dir + "/counter.hex").find(":101FF000")) << "no record for 0xFF8-0xFFF";
}

TEST_F(CliTest, DashSWritesTheAssembler) {
    Result result = xgscc("-S " + quoted(EXAMPLES_DIR "/counter.c") + " -o " + quoted(dir + "/counter.asm"));
    EXPECT_EQ(0, result.exitCode) << result.output;
    std::string assembly = readFile(dir + "/counter.asm");
    EXPECT_NE(std::string::npos, assembly.find("jmp _start")) << assembly;
    EXPECT_NE(std::string::npos, assembly.find("inc v_counter")) << assembly;
}

TEST_F(CliTest, DashDashMapWritesTheMapNextToTheOutput) {
    Result result = xgscc("--map " + quoted(EXAMPLES_DIR "/functions.c") + " -o " + quoted(dir + "/game.hex"));
    EXPECT_EQ(0, result.exitCode) << result.output;
    std::string map = readFile(dir + "/game.map");
    EXPECT_NE(std::string::npos, map.find("RAM")) << map;
    EXPECT_NE(std::string::npos, map.find("square_sum")) << map;
    EXPECT_NE(std::string::npos, map.find("total")) << map;
    EXPECT_FALSE(readFile(dir + "/game.hex").empty());
}

TEST_F(CliTest, NoMapWithoutTheOption) {
    std::remove((dir + "/nomap.map").c_str());
    xgscc(quoted(EXAMPLES_DIR "/counter.c") + " -o " + quoted(dir + "/nomap.hex"));
    EXPECT_EQ("", readFile(dir + "/nomap.map"));
}

TEST_F(CliTest, MapWithTheAssemblerOutputToo) {
    Result result = xgscc("-S --map " + quoted(EXAMPLES_DIR "/counter.c") + " -o " + quoted(dir + "/c.asm"));
    EXPECT_EQ(0, result.exitCode) << result.output;
    EXPECT_NE(std::string::npos, readFile(dir + "/c.map").find("counter"));
}

TEST_F(CliTest, ErrorsNameFileAndLineAndFail) {
    writeFile(dir + "/bad.c", "uint8_t a;\nvoid main(void) {\n  a = b;\n}\n");
    Result result = xgscc(quoted(dir + "/bad.c") + " -o " + quoted(dir + "/bad.hex"));
    EXPECT_EQ(1, result.exitCode);
    EXPECT_NE(std::string::npos, result.output.find("bad.c:3: unknown variable 'b'")) << result.output;
}

TEST_F(CliTest, NoHexFileAfterAnError) {
    writeFile(dir + "/bad2.c", "void main(void) { a = 1; }\n");
    std::remove((dir + "/bad2.hex").c_str());
    xgscc(quoted(dir + "/bad2.c") + " -o " + quoted(dir + "/bad2.hex"));
    EXPECT_EQ("", readFile(dir + "/bad2.hex"));
}

TEST_F(CliTest, MissingSourceFileFails) {
    EXPECT_EQ(1, xgscc(quoted(dir + "/missing.c") + " -o " + quoted(dir + "/x.hex")).exitCode);
}

TEST_F(CliTest, WrongArgumentsShowTheUsage) {
    EXPECT_EQ(2, xgscc("").exitCode);
    EXPECT_EQ(2, xgscc(quoted(EXAMPLES_DIR "/counter.c")).exitCode);
    EXPECT_EQ(2, xgscc("-x " + quoted(EXAMPLES_DIR "/counter.c") + " -o out.hex").exitCode);
}

#endif
