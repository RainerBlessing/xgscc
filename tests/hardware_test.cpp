// Joystick, sound and SRAM through <xgs.h>, run on the emulated SX52 with its ports.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "compiler.h"
#include "cpu.h"
#include "run_helper.h"

namespace {

class SoundRecorder : public SoundPort {
public:
    std::vector<unsigned char> writes;
    void portWrite(unsigned char ra) { writes.push_back(ra); }
};

struct Machine {
    CompileResult compiled;
    Cpu cpu;
    SoundRecorder sound;
    bool halted;

    Machine() : cpu(NULL), halted(false) {}

    // compiles and runs until main ends (a jump to itself)
    bool run(const std::string& source, unsigned short joystick = 0, long maxSteps = 3000000) {
        compiled = compile(source, "hw.c");
        if (!compiled.ok) {
            return false;
        }
        cpu.reset();
        cpu.setSound(&sound);
        *cpu.getJoystrickr() = joystick;
        for (size_t i = 0; i < compiled.rom.size(); i++) {
            cpu.getROM()[i] = compiled.rom[i];
        }
        for (long s = 0; s < maxSteps; s++) {
            unsigned short pc = *cpu.getPC();
            cpu.execute();
            if (*cpu.getPC() == pc) {
                halted = true;
                break;
            }
        }
        return halted;
    }

    int value(const std::string& name) {
        return cpu.getRAM()[compiled.variables.at(name)];
    }

    std::vector<int> commands() {
        return decodeSoundCommands(sound.writes);
    }
};

const std::string HEAD = "#include <stdint.h>\n#include <xgs.h>\n";

std::string program(const std::string& globals, const std::string& body) {
    return HEAD + globals + "\nvoid main(void) {\n xgs_init();\n" + body + "\n}\n";
}

}

// joystick: bit 0x01 up, 0x02 down, 0x04 left, 0x08 right, 0x10 fire; 1 = pressed

TEST(JoystickTest, NothingPressed) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t j;", "j = xgs_joystick();"), 0)) << m.compiled.error;
    EXPECT_EQ(0, m.value("j"));
}

TEST(JoystickTest, EachButtonOnItsOwn) {
    // the emulator's word has a 1 for a pressed button
    const unsigned short words[5] = { 1, 2, 4, 8, 0x10 };
    for (int i = 0; i < 5; i++) {
        Machine m;
        ASSERT_TRUE(m.run(program("uint8_t j;", "j = xgs_joystick();"), words[i])) << m.compiled.error;
        EXPECT_EQ(words[i], m.value("j")) << i;
    }
}

TEST(JoystickTest, SeveralButtonsAndNoneOfTheOtherBits) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t j;", "j = xgs_joystick();"), 0xffe5)) << m.compiled.error;
    EXPECT_EQ(0x05, m.value("j"));
}

TEST(JoystickTest, ReadTwiceInARow) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t a; uint8_t b;", "a = xgs_joystick(); b = xgs_joystick();"), 0x0a)) << m.compiled.error;
    EXPECT_EQ(0x0a, m.value("a"));
    EXPECT_EQ(0x0a, m.value("b"));
}

TEST(JoystickTest, ReadInTheConditionOfAnIf) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t x;", "if (xgs_joystick() & 8) x = 1; else x = 2;"), 0x08)) << m.compiled.error;
    EXPECT_EQ(1, m.value("x"));
}

TEST(JoystickTest, DoesNotDisturbTheOtherBitsOfRA) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t j;", "j = xgs_joystick();"), 0x1f)) << m.compiled.error;
    // the sound chip must not have been started: no strobe
    EXPECT_TRUE(m.commands().empty());
}

// sound

TEST(SoundTest, ACommandIsSentAsElevenBits) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sound(2, 0x45);"))) << m.compiled.error;
    std::vector<int> commands = m.commands();
    ASSERT_EQ(1u, commands.size());
    EXPECT_EQ((2 << 8) | 0x45, commands[0]);
}

TEST(SoundTest, SeveralCommandsInOrder) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sound(0, 0x80); xgs_sound(1, 0x23); xgs_sound(3, 0x07); xgs_sound(2, 0xff);"))) << m.compiled.error;
    std::vector<int> commands = m.commands();
    ASSERT_EQ(4u, commands.size());
    EXPECT_EQ(0x080, commands[0]);
    EXPECT_EQ(0x123, commands[1]);
    EXPECT_EQ(0x307, commands[2]);
    EXPECT_EQ(0x2ff, commands[3]);
}

TEST(SoundTest, AllZeroAndAllOne) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sound(0, 0); xgs_sound(7, 0xff);"))) << m.compiled.error;
    std::vector<int> commands = m.commands();
    ASSERT_EQ(2u, commands.size());
    EXPECT_EQ(0x000, commands[0]);
    EXPECT_EQ(0x7ff, commands[1]);
}

TEST(SoundTest, ArgumentsFromVariables) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t a = 1; uint8_t d = 0x5a;", "xgs_sound(a, d); xgs_sound(a + 1, d ^ 0xff);"))) << m.compiled.error;
    std::vector<int> commands = m.commands();
    ASSERT_EQ(2u, commands.size());
    EXPECT_EQ(0x15a, commands[0]);
    EXPECT_EQ(0x2a5, commands[1]);
}

TEST(SoundTest, NoteSetsTheFrequencyOfAChannel) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_note(1, 12); xgs_note(3, 63);"))) << m.compiled.error;
    std::vector<int> commands = m.commands();
    ASSERT_EQ(2u, commands.size());
    EXPECT_EQ((2 << 8) | (1 << 6) | 12, commands[0]);
    EXPECT_EQ((2 << 8) | (3 << 6) | 63, commands[1]);
}

TEST(SoundTest, TheJoystickAndTheSoundShareRA) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t j;", "xgs_sound(2, 0x41); j = xgs_joystick(); xgs_sound(1, 0x02);"), 0x11)) << m.compiled.error;
    EXPECT_EQ(0x11, m.value("j"));
    std::vector<int> commands = m.commands();
    ASSERT_EQ(2u, commands.size());
    EXPECT_EQ(0x241, commands[0]);
    EXPECT_EQ(0x102, commands[1]);
}

TEST(SoundTest, TheGameKeepsItsVariablesAcrossTheCall) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t a = 7; uint8_t b = 9; uint8_t c;", "xgs_sound(a, b); c = a + b;"))) << m.compiled.error;
    EXPECT_EQ(16, m.value("c"));
}

// SRAM

TEST(SramTest, WriteThenRead) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t v;", "xgs_sram_write(0x1234, 0xab); v = xgs_sram_read(0x1234);"))) << m.compiled.error;
    EXPECT_EQ(0xab, m.value("v"));
    EXPECT_EQ(0xab, m.cpu.getSRAM()[0x1234]);
}

TEST(SramTest, TheAddressIsTheAddressOfTheByte) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sram_write(0, 1); xgs_sram_write(1, 2); xgs_sram_write(15, 3); xgs_sram_write(16, 4); xgs_sram_write(0xffff, 5); xgs_sram_write(0x8000, 6);"))) << m.compiled.error;
    EXPECT_EQ(1, m.cpu.getSRAM()[0]);
    EXPECT_EQ(2, m.cpu.getSRAM()[1]);
    EXPECT_EQ(3, m.cpu.getSRAM()[15]);
    EXPECT_EQ(4, m.cpu.getSRAM()[16]);
    EXPECT_EQ(5, m.cpu.getSRAM()[0xffff]);
    EXPECT_EQ(6, m.cpu.getSRAM()[0x8000]);
}

TEST(SramTest, ReadsWhatIsThere) {
    Machine m;
    m.cpu.reset();
    CompileResult c = compile(program("uint8_t a; uint8_t b;", "a = xgs_sram_read(0x0abc); b = xgs_sram_read(0xfedc);"), "hw.c");
    ASSERT_TRUE(c.ok) << c.error;
    m.compiled = c;
    for (size_t i = 0; i < c.rom.size(); i++) {
        m.cpu.getROM()[i] = c.rom[i];
    }
    m.cpu.getSRAM()[0x0abc] = 0x5c;
    m.cpu.getSRAM()[0xfedc] = 0xa3;
    for (long s = 0; s < 3000000; s++) {
        unsigned short pc = *m.cpu.getPC();
        m.cpu.execute();
        if (*m.cpu.getPC() == pc) break;
    }
    EXPECT_EQ(0x5c, m.value("a"));
    EXPECT_EQ(0xa3, m.value("b"));
}

TEST(SramTest, WritesDoNotTouchTheNeighbours) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sram_write(0x0100, 0x11); xgs_sram_write(0x0101, 0x22); xgs_sram_write(0x0100, 0x33);"))) << m.compiled.error;
    EXPECT_EQ(0x33, m.cpu.getSRAM()[0x100]);
    EXPECT_EQ(0x22, m.cpu.getSRAM()[0x101]);
    EXPECT_EQ(0, m.cpu.getSRAM()[0x102]);
    EXPECT_EQ(0, m.cpu.getSRAM()[0x0f0 + 15]);
}

TEST(SramTest, AFillAndASum) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint16_t sum; uint8_t i;",
        "for (i = 0; i < 40; i++) xgs_sram_write(0x300 + i, i * 3);"
        "for (i = 0; i < 40; i++) sum += xgs_sram_read(0x300 + i);"))) << m.compiled.error;
    int sum = 0;
    for (int i = 0; i < 40; i++) {
        sum += (i * 3) & 0xff;
    }
    int at = m.compiled.variables.at("sum");
    EXPECT_EQ(sum, m.cpu.getRAM()[at] | (m.cpu.getRAM()[at + 1] << 8));
}

TEST(SramTest, TheUpperHalfIsReachedWithTheBank) {
    Machine m;
    ASSERT_TRUE(m.run(program("uint8_t a; uint8_t b;",
        "xgs_sram_write(0x0042, 1); xgs_sram_bank(1); xgs_sram_write(0x0042, 2);"
        "a = xgs_sram_read(0x0042); xgs_sram_bank(0); b = xgs_sram_read(0x0042);"))) << m.compiled.error;
    EXPECT_EQ(1, m.cpu.getSRAM()[0x42]);
    EXPECT_EQ(2, m.cpu.getSRAM()[0x10042]);
    EXPECT_EQ(2, m.value("a"));
    EXPECT_EQ(1, m.value("b"));
}

TEST(SramTest, TheBankDoesNotStartTheSoundChip) {
    Machine m;
    ASSERT_TRUE(m.run(program("", "xgs_sram_bank(1); xgs_sram_write(5, 5); xgs_sram_bank(0);"))) << m.compiled.error;
    EXPECT_TRUE(m.commands().empty());
}

TEST(SramTest, WorksNextToTheTileKernel) {
    // the registers of the kernel and of the SRAM functions share direct registers
    std::string table = "const uint8_t xgs_tiles[256] = {1};\n";
    Machine m;
    ASSERT_TRUE(m.run("#include <stdint.h>\n#define XGS_COLUMNS 4\n#define XGS_ROWS 4\n#define XGS_TILE_LINES 4\n#include <xgs.h>\n" + table +
        "uint8_t v;\nvoid main(void) { xgs_init(); xgs_sram_write(0x77, 0x99); v = xgs_sram_read(0x77); xgs_sound(1, 2); v += xgs_joystick(); }\n", 1)) << m.compiled.error;
    EXPECT_EQ(0x9a, m.value("v"));
}
