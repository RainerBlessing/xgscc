// The listing: program words under the C lines they were made from
#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "compiler.h"

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

// the words (address, text) that follow the header "; file:line:" up to the next header or blank line
std::vector<std::string> wordsUnder(const std::string& listing, const std::string& header) {
    std::vector<std::string> words;
    std::vector<std::string> lines = split(listing);
    bool inside = false;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].compare(0, 2, "; ") == 0) {
            inside = lines[i].compare(0, header.size(), header) == 0;
        } else if (inside && !lines[i].empty()) {
            words.push_back(lines[i]);
        }
    }
    return words;
}

const char* const PROGRAM =
    "#include <stdint.h>\n"
    "uint8_t x;\n"
    "uint8_t y;\n"
    "void main(void) {\n"
    "    x = 5;\n"
    "    y = x + 1;\n"
    "    while (y < 9) {\n"
    "        y++;\n"
    "    }\n"
    "}\n";

}

TEST(ListingTest, TheProgramIsTheSameWithAndWithoutAListing) {
    CompileResult plain = compile(PROGRAM, "t.c", 1, false);
    CompileResult listed = compile(PROGRAM, "t.c", 1, true);
    ASSERT_TRUE(plain.ok) << plain.error;
    ASSERT_TRUE(listed.ok) << listed.error;
    EXPECT_EQ(plain.rom, listed.rom);
}

TEST(ListingTest, TheProgramIsTheSameAtBothLevels) {
    CompileResult plain = compile(PROGRAM, "t.c", 0, false);
    CompileResult listed = compile(PROGRAM, "t.c", 0, true);
    EXPECT_EQ(plain.rom, listed.rom);
}

TEST(ListingTest, NoListingNoText) {
    EXPECT_TRUE(compile(PROGRAM, "t.c", 1, false).listing.empty());
}

TEST(ListingTest, EveryStatementHasItsLine) {
    CompileResult r = compile(PROGRAM, "t.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(contains(r.listing, "; t.c:5: x = 5;")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; t.c:6: y = x + 1;")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; t.c:7: while (y < 9) {")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; t.c:8: y++;")) << r.listing;
}

TEST(ListingTest, TheWordsAreListedUnderTheirLine) {
    CompileResult r = compile(PROGRAM, "t.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    std::vector<std::string> words = wordsUnder(r.listing, "; t.c:5:");
    ASSERT_EQ(2u, words.size()) << r.listing;
    EXPECT_NE(std::string::npos, words[0].find("mov w, #5"));
    EXPECT_NE(std::string::npos, words[1].find("mov "));
    // 4 hex digits of address, the opcode, the instruction
    EXPECT_EQ(' ', words[0][4]);
}

TEST(ListingTest, TheAddressesAndOpcodesAreThoseOfTheRom) {
    CompileResult r = compile(PROGRAM, "t.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    int checked = 0;
    for (const std::string& line : split(r.listing)) {
        if (line.size() > 10 && line[0] != ';' && line[0] != 'L') {
            int address = (int) strtol(line.substr(0, 4).c_str(), NULL, 16);
            int word = (int) strtol(line.substr(6, 3).c_str(), NULL, 16);
            EXPECT_EQ(r.rom[address], word) << line;
            checked++;
        }
    }
    EXPECT_GT(checked, 8);
}

TEST(ListingTest, TheAddressesGoUp) {
    CompileResult r = compile(PROGRAM, "t.c", 1, true);
    int last = -1;
    for (const std::string& line : split(r.listing)) {
        if (line.size() > 10 && line[0] != ';' && line[0] != 'L') {
            int address = (int) strtol(line.substr(0, 4).c_str(), NULL, 16);
            EXPECT_GT(address, last) << line;
            last = address;
        }
    }
}

TEST(ListingTest, StartCodeAndLibraryCodeHaveNoCLine) {
    CompileResult r = compile("#include <stdint.h>\n#include <xgs.h>\nvoid main(void) { xgs_init(); xgs_frame(); }\n", "g.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(contains(r.listing, "; start code")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; xgs_frame (assembler routine)")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; g.c:3: ")) << r.listing;
}

TEST(ListingTest, ARuntimeRoutineIsNamedNotAttributedToALine) {
    CompileResult r = compile("#include <stdint.h>\nuint8_t a = 3; uint8_t b = 4; uint8_t c;\nvoid main(void) { c = a * b; }\n", "m.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(contains(r.listing, "; __mul8u (library)")) << r.listing;
    EXPECT_FALSE(contains(r.listing, "m.c:1:")) << r.listing;
}

TEST(ListingTest, ForLoopsListTheirThreeParts) {
    CompileResult r = compile("#include <stdint.h>\nuint8_t s;\nvoid main(void) {\n for (uint8_t i = 0; i < 4; i++)\n  s += i;\n}\n", "f.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(contains(r.listing, "; f.c:5: s += i;")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; f.c:4: ")) << r.listing;
}

TEST(ListingTest, TheLinesOfAnIncludedFileNameThatFile) {
    char pattern[] = "/tmp/xgscc_ls_XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::ofstream((dir + "/inc.h").c_str()) << "void bump(void) {\n    x++;\n}\n";
    CompileResult r = compile("#include <stdint.h>\nuint8_t x;\n#include \"inc.h\"\nvoid main(void) { bump(); }\n", dir + "/main.c", 1, true);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(contains(r.listing, "; " + dir + "/inc.h:2: x++;")) << r.listing;
    EXPECT_TRUE(contains(r.listing, "; " + dir + "/main.c:4: void main(void) { bump(); }")) << r.listing;
}

TEST(ListingTest, AnErrorInAnIncludedFileNamesThatFile) {
    char pattern[] = "/tmp/xgscc_le_XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::ofstream((dir + "/bad.h").c_str()) << "uint8_t ok;\nvoid f(void) { nothing = 1; }\n";
    CompileResult r = compile("#include <stdint.h>\n#include \"bad.h\"\nvoid main(void) { f(); }\n", dir + "/main.c");
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(0u, r.error.find(dir + "/bad.h:2: ")) << r.error;
}

TEST(ListingTest, ATokenErrorInAnIncludedFileNamesThatFile) {
    char pattern[] = "/tmp/xgscc_lt_XXXXXX";
    std::string dir = mkdtemp(pattern);
    std::ofstream((dir + "/bad.h").c_str()) << "\n\n#error stop here\n";
    CompileResult r = compile("#include \"bad.h\"\nvoid main(void) {}\n", dir + "/main.c");
    ASSERT_FALSE(r.ok);
    EXPECT_EQ(dir + "/bad.h:3: #error stop here", r.error);
}

TEST(ListingTest, TheGameHasAListing) {
    std::ifstream file(EXAMPLES_DIR "/pong.c");
    std::stringstream text;
    text << file.rdbuf();
    CompileResult plain = compile(text.str(), "pong.c", 1, false);
    CompileResult listed = compile(text.str(), "pong.c", 1, true);
    ASSERT_TRUE(listed.ok) << listed.error;
    EXPECT_EQ(plain.rom, listed.rom);
    EXPECT_TRUE(contains(listed.listing, "; pong.c:")) << listed.listing.substr(0, 400);
    EXPECT_TRUE(contains(listed.listing, "xgs_tile"));
}
