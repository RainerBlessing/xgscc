#include "run_helper.h"
#include <string.h>
#include <gtest/gtest.h>
#include "cpu.h"

int ProgramRun::value(const std::string& name) const
{
	std::map<std::string, int>::const_iterator found = compiled.variables.find(name);
	if (found == compiled.variables.end())
		return -1;
	return ram[found->second];
}

int ProgramRun::value16(const std::string& name) const
{
	std::map<std::string, int>::const_iterator high = compiled.highBytes.find(name);
	if (high == compiled.highBytes.end())
		return -1;
	return value(name) | ram[high->second] << 8;
}

std::vector<int> decodeSoundCommands(const std::vector<unsigned char>& raWrites)
{
	std::vector<int> result;
	int shift = 0;
	bool sclk = false;
	bool sstb = false;
	for (size_t i = 0; i < raWrites.size(); i++)
	{
		const unsigned char ra = raWrites[i];
		const bool nowSclk = (ra & 8) != 0;
		const bool nowSstb = (ra & 0x20) != 0;
		if (nowSclk && !sclk)
			shift = ((shift << 1) | ((ra & 0x10) ? 1 : 0)) & 0x7ff;
		if (nowSstb && !sstb)
			result.push_back(shift);
		sclk = nowSclk;
		sstb = nowSstb;
	}
	return result;
}

// one run at the given optimization level
static ProgramRun runAt(const std::string& source, long maxSteps, int level)
{
	ProgramRun run;
	run.halted = false;
	run.steps = 0;
	memset(run.ram, 0, sizeof(run.ram));
	run.mode = 0;
	run.compiled = compile(source, "test.c", level);
	if (!run.compiled.ok)
		return run;

	Cpu cpu(NULL);
	cpu.reset();
	memcpy(cpu.getROM(), &run.compiled.rom[0], run.compiled.rom.size() * sizeof(unsigned short));
	while (run.steps < maxSteps)
	{
		unsigned short before = *cpu.getPC();
		cpu.execute();
		run.steps++;
		if (*cpu.getPC() == before)
		{
			run.halted = true;
			break;
		}
	}
	memcpy(run.ram, cpu.getRAM(), sizeof(run.ram));
	run.mode = *cpu.getMode();
	return run;
}

// Every program runs without and with the optimizer: the RAM has to be the same.
ProgramRun runProgram(const std::string& source, long maxSteps)
{
	ProgramRun optimized = runAt(source, maxSteps, 1);
	if (!optimized.compiled.ok || !optimized.halted)
		return optimized;
	ProgramRun plain = runAt(source, maxSteps * 2, 0);
	if (!plain.compiled.ok || !plain.halted)
	{
		ADD_FAILURE() << "the program does not end without the optimizer, or does not compile";
		return optimized;
	}
	for (int address = 0x0a; address < 256; address++)
	{
		if (plain.ram[address] != optimized.ram[address])
			ADD_FAILURE() << "RAM " << address << " differs: " << (int) plain.ram[address] << " at -O0, " << (int) optimized.ram[address] << " at -O1";
	}
	if (plain.steps < optimized.steps)
		ADD_FAILURE() << "the optimized program takes more steps";
	return optimized;
}
