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

    using QK::Boot::Tpm::ClassifyTransport;
    using QK::Boot::Tpm::StartMethodRequestsAcpiStart;
    using QK::Boot::Tpm::TransportKind;

    // Start method 6: interface ID decides between FIFO/TIS and CRB.
    assert(ClassifyTransport(6, false, true, 0x0) == TransportKind::Tis);
    assert(ClassifyTransport(6, false, true, 0xF) == TransportKind::Tis);
    assert(ClassifyTransport(6, true, true, 0x0) == TransportKind::Tis);
    assert(ClassifyTransport(6, true, true, 0x1) == TransportKind::Crb);
    assert(ClassifyTransport(6, false, true, 0x1) == TransportKind::None);
    assert(ClassifyTransport(6, true, true, 0x2) == TransportKind::None);
    assert(ClassifyTransport(6, true, false, 0) == TransportKind::Crb);
    assert(ClassifyTransport(6, false, false, 0) == TransportKind::Tis);

    // CRB start methods need a control area.
    for (unsigned sm : {2u, 7u, 8u})
    {
        assert(ClassifyTransport(sm, true, true, 0x1) == TransportKind::Crb);
        assert(ClassifyTransport(sm, false, false, 0) == TransportKind::None);
    }
    assert(ClassifyTransport(11, true, true, 0x1) == TransportKind::None);

    assert(StartMethodRequestsAcpiStart(2));
    assert(StartMethodRequestsAcpiStart(8));
    assert(!StartMethodRequestsAcpiStart(6));
    assert(!StartMethodRequestsAcpiStart(7));

    assert(!ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Desktop));
    assert(!ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Network));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Terminal));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Installer));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Recovery));
    assert(ShouldUseInteractiveTerminalFallbackForStartupMode(StartupMode::Safe));

    std::cout << "tpm probe policy tests passed" << std::endl;
    return 0;
}
