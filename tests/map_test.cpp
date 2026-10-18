#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include "compiler.h"
#include "memorymap.h"

namespace {

std::string number(int value) {
    return std::to_string(value);
}

CompileResult compiled(const std::string& program) {
    CompileResult result = compile("#include <stdint.h>\n" + program, "map.c");
    EXPECT_TRUE(result.ok) << result.error;
    return result;
}

const RamEntry* ramEntry(const MemoryMap& map, const std::string& name) {
    for (const RamEntry& entry : map.ram) {
        if (entry.name.find(name) != std::string::npos) {
            return &entry;
        }
    }
    return NULL;
}

const RomEntry* romEntry(const MemoryMap& map, const std::string& name) {
    for (const RomEntry& entry : map.rom) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return NULL;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int programmedWords(const CompileResult& result) {
    int count = 0;
    for (unsigned short word : result.rom) {
        if (word != 0xfff) {
            count++;
        }
    }
    return count;
}

const char* const PROGRAM =
    "const uint8_t sine[16] = { 0, 3, 6, 9, 12, 14, 15, 15, 15, 14, 12, 9, 6, 3, 0, 0 };\n"
    "uint8_t counter;\n"
    "uint16_t total;\n"
    "uint8_t buffer[10];\n"
    "struct P { uint8_t x; uint8_t y; }; struct P player;\n"
    "uint8_t twice(uint8_t v) { return v + v; }\n"
    "uint8_t quad(uint8_t v) { return twice(twice(v)); }\n"
    "void main(void) { counter = quad(3) * 2; total += sine[counter & 15]; buffer[1] = player.x; }\n";

}

TEST(MapTest, EveryVariableHasItsAddress) {
    CompileResult result = compiled(PROGRAM);
    const RamEntry* counter = ramEntry(result.map, "counter");
    ASSERT_TRUE(counter != NULL);
    EXPECT_EQ(result.variables.at("counter"), counter->address);
    EXPECT_EQ(1, counter->bytes);
}

TEST(MapTest, SixteenBitVariableHasTwoBytes) {
    CompileResult result = compiled(PROGRAM);
    const RamEntry* low = ramEntry(result.map, "total (low byte)");
    const RamEntry* high = ramEntry(result.map, "total (high byte)");
    ASSERT_TRUE(low != NULL);
    ASSERT_TRUE(high != NULL);
    EXPECT_EQ(result.variables.at("total"), low->address);
    EXPECT_EQ(result.highBytes.at("total"), high->address);
}

TEST(MapTest, ArraysAndStructsAreOneEntryWithTheirSize) {
    CompileResult result = compiled(PROGRAM);
    const RamEntry* buffer = ramEntry(result.map, "buffer");
    ASSERT_TRUE(buffer != NULL);
    EXPECT_EQ(10, buffer->bytes);
    EXPECT_EQ(result.variables.at("buffer"), buffer->address);
    const RamEntry* player = ramEntry(result.map, "player");
    ASSERT_TRUE(player != NULL);
    EXPECT_EQ(2, player->bytes);
}

TEST(MapTest, ParametersAndTemporariesAreNamedAfterTheirFunction) {
    CompileResult result = compiled(PROGRAM);
    EXPECT_TRUE(ramEntry(result.map, "twice.v (parameter)") != NULL);
    EXPECT_TRUE(ramEntry(result.map, "main temporary") != NULL);
}

TEST(MapTest, RamEntriesDoNotOverlapAndAddUpToTheRam) {
    CompileResult result = compiled(PROGRAM);
    std::set<int> used;
    int bytes = 0;
    for (const RamEntry& entry : result.map.ram) {
        for (int a = entry.address; a < entry.address + entry.bytes; a++) {
            EXPECT_TRUE(used.insert(a).second) << "address " << a << " twice: " << entry.name;
            EXPECT_TRUE((a >= 0x0a && a <= 0x0f) || (a >= 0x10 && a <= 0xff)) << a;
        }
        bytes += entry.bytes;
    }
    EXPECT_EQ(246, result.map.ramAvailable);
    EXPECT_LE(bytes, result.map.ramAvailable);
    EXPECT_EQ(static_cast<int>(used.size()), bytes);
}

TEST(MapTest, FunctionsHaveTheirAddress) {
    CompileResult result = compiled(PROGRAM);
    const RomEntry* twice = romEntry(result.map, "twice");
    const RomEntry* quad = romEntry(result.map, "quad");
    ASSERT_TRUE(twice != NULL);
    ASSERT_TRUE(quad != NULL);
    EXPECT_EQ("function", twice->kind);
    EXPECT_NE(0xfff, result.rom[twice->address]);
    EXPECT_NE(quad->address, twice->address);
    EXPECT_GT(twice->words, 0);
}

TEST(MapTest, StartCodeAndMainSitAtTheStart) {
    CompileResult result = compiled(PROGRAM);
    const RomEntry* start = romEntry(result.map, "start code and main");
    ASSERT_TRUE(start != NULL);
    EXPECT_EQ(0, start->address);
    EXPECT_EQ("start", start->kind);
}

TEST(MapTest, RuntimeRoutinesAreMarked) {
    CompileResult result = compiled(PROGRAM);
    const RomEntry* multiply = romEntry(result.map, "__mul8u");
    ASSERT_TRUE(multiply != NULL);
    EXPECT_EQ("runtime", multiply->kind);
    EXPECT_TRUE(romEntry(result.map, "__div8u") == NULL);
}

TEST(MapTest, TablesHaveAddressAndSize) {
    CompileResult result = compiled(PROGRAM);
    const RomEntry* sine = romEntry(result.map, "sine");
    ASSERT_TRUE(sine != NULL);
    EXPECT_EQ("table", sine->kind);
    EXPECT_EQ(16, sine->words);
    EXPECT_EQ(3, result.rom[sine->address + 1]);
    EXPECT_EQ(15, result.rom[sine->address + 6]);
}

TEST(MapTest, TheResetVectorIsListed) {
    CompileResult result = compiled(PROGRAM);
    const RomEntry* vector = romEntry(result.map, "reset vector");
    ASSERT_TRUE(vector != NULL);
    EXPECT_EQ(0xfff, vector->address);
    EXPECT_EQ(1, vector->words);
}

TEST(MapTest, RomEntriesAccountForEveryWord) {
    CompileResult result = compiled(PROGRAM);
    int words = 0;
    for (const RomEntry& entry : result.map.rom) {
        words += entry.words;
    }
    EXPECT_EQ(programmedWords(result), words);
}

TEST(MapTest, EveryWordIsInOneEntry) {
    CompileResult result = compiled(PROGRAM);
    for (int address = 0; address < 4096; address++) {
        if (result.rom[address] == 0xfff) {
            continue;
        }
        int owners = 0;
        for (const RomEntry& entry : result.map.rom) {
            // code entries count their programmed words, so only the range matters
            if (entry.kind == "table" || entry.kind == "vector") {
                if (address >= entry.address && address < entry.address + entry.words) {
                    owners++;
                }
            }
        }
        EXPECT_LE(owners, 1) << address;
    }
}

TEST(MapTest, BiggerProgramsAddUp) {
    std::string functions;
    std::string calls;
    for (int f = 0; f < 10; f++) {
        std::string body;
        for (int i = 0; i < 40; i++) {
            body += "x = x + 1;\n";
        }
        functions += "void f" + number(f) + "(void) {\n" + body + "}\n";
        calls += "f" + number(f) + "();\n";
    }
    CompileResult result = compiled("uint8_t x; const uint8_t t[200] = { 1 };\n" + functions + "void main(void) {\n" + calls + "}\n");
    int words = 0;
    for (const RomEntry& entry : result.map.rom) {
        words += entry.words;
    }
    EXPECT_EQ(programmedWords(result), words);
    EXPECT_TRUE(romEntry(result.map, "f9") != NULL);
}

TEST(MapTest, CallDepth) {
    CompileResult result = compiled(PROGRAM);
    EXPECT_EQ(2, result.map.callDepth);       // main -> quad -> twice
    CompileResult flat = compiled("uint8_t x; void main(void) { x = 1; }");
    EXPECT_EQ(0, flat.map.callDepth);
}

TEST(MapTest, UnusedFunctionsAreNotListed) {
    CompileResult result = compiled("uint8_t x; void unused(void) { uint8_t big[50]; big[0] = 1; } void main(void) { x = 2; }");
    EXPECT_TRUE(romEntry(result.map, "unused") == NULL);
    EXPECT_TRUE(ramEntry(result.map, "big") == NULL);
}

TEST(MapTest, SharedFrameLocationsListAllTheirNames) {
    CompileResult result = compiled("uint8_t r; void f(void) { uint8_t a = 1; r += a; } void g(void) { uint8_t b = 2; r += b; } void main(void) { f(); g(); }");
    const RamEntry* shared = ramEntry(result.map, "f.a");
    ASSERT_TRUE(shared != NULL);
    EXPECT_TRUE(contains(shared->name, "g.b")) << shared->name;
}

// the text

TEST(MapTextTest, HasASectionForRamAndRom) {
    CompileResult result = compiled(PROGRAM);
    std::string text = mapText(result.map, "map.c");
    EXPECT_TRUE(contains(text, "map.c")) << text;
    EXPECT_TRUE(contains(text, "RAM")) << text;
    EXPECT_TRUE(contains(text, "ROM")) << text;
    EXPECT_TRUE(contains(text, "free")) << text;
    EXPECT_TRUE(contains(text, "call depth 2 of 8")) << text;
}

TEST(MapTextTest, ListsNamesWithHexAddresses) {
    CompileResult result = compiled(PROGRAM);
    std::string text = mapText(result.map, "map.c");
    EXPECT_TRUE(contains(text, "counter")) << text;
    EXPECT_TRUE(contains(text, "sine")) << text;
    EXPECT_TRUE(contains(text, "0xFFF")) << text;
    EXPECT_TRUE(contains(text, "start code and main")) << text;
}

TEST(MapTextTest, SaysHowMuchIsLeft) {
    CompileResult result = compiled("uint8_t x; void main(void) { x = 1; }");
    std::string text = mapText(result.map, "map.c");
    int used = 0;
    for (const RamEntry& entry : result.map.ram) {
        used += entry.bytes;
    }
    EXPECT_TRUE(contains(text, number(used) + " of 246 bytes")) << text;
    EXPECT_TRUE(contains(text, number(246 - used) + " free")) << text;
    EXPECT_TRUE(contains(text, "of 4096 words")) << text;
}
