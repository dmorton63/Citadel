#include <cassert>
#include <iostream>

#include "../../kernel/Boot/TPM/QKBootTPMProbePolicy.h"
#include "../../kernel/Boot/Config/QKBootStartupConfig.h"

int main()
{
    using QK::Boot::Tpm::IsStartupResponseSuccessful;
    using QK::Boot::Config::StartupMode;
    using QK::Boot::Config::ShouldUseInteractiveTerminalFallbackForStartupMode;

    assert(IsStartupResponseSuccessful(0x00000000u));
    assert(IsStartupResponseSuccessful(0x00000100u));
    assert(!IsStartupResponseSuccessful(0x00000101u));
    assert(!IsStartupResponseSuccessful(0xFFFFFFFFu));

    assert(!ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Desktop));
    assert(!ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Network));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Terminal));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Installer));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Recovery));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Safe));

    std::cout << "tpm probe policy tests passed" << std::endl;
    return 0;
}
