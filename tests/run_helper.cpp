#include "run_helper.h"
#include <string.h>
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

ProgramRun runProgram(const std::string& source, long maxSteps)
{
	ProgramRun run;
	run.halted = false;
	run.steps = 0;
	memset(run.ram, 0, sizeof(run.ram));
	run.compiled = compile(source, "test.c");
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
	return run;
}
