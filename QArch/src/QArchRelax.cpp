// QArch Relax - CPU spin-wait hint
// Provides C linkage cpu_relax() used by low-level drivers.

extern "C" void cpu_relax()
{
    asm volatile("pause");
}
