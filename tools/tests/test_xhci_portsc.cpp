#include <cassert>
#include <iostream>

#include "../../kernel/Drivers/XHCI/xhci_regs.h"

using namespace QKDrv::XHCI;

static QC::u32 applyPortWrite(QC::u32 portsc, QC::u32 value)
{
    const QC::u32 persistent = PORTSC_PP | (3u << 14) | (7u << 25);
    portsc = (portsc & ~persistent) | (value & persistent);
    portsc &= ~(value & PORTSC_CHANGE_MASK);
    if (value & PORTSC_PED)
        portsc &= ~PORTSC_PED;
    return portsc | (value & PORTSC_PR);
}

int main()
{
    const QC::u32 connected = PORTSC_CCS | PORTSC_PED | PORTSC_PP |
                             (2u << 10) | (1u << 14) | (5u << 25);
    const QC::u32 pending = connected | PORTSC_CSC | PORTSC_PRC;

    // Acknowledge a port event without disabling the connected device.
    const QC::u32 ack = portscWriteBase(pending) | (pending & PORTSC_CHANGE_MASK);
    assert(applyPortWrite(pending, ack) == connected);
    assert(!(applyPortWrite(pending, pending | PORTSC_CHANGE_MASK) & PORTSC_PED));

    // Reset must not acknowledge pending changes or write the disable bit.
    const QC::u32 reset = portscWriteBase(pending) | PORTSC_PR;
    assert(!(reset & (PORTSC_PED | PORTSC_CHANGE_MASK)));
    assert(applyPortWrite(pending, reset) == (pending | PORTSC_PR));

    // Clearing reset completion must preserve enablement and other changes.
    const QC::u32 clearReset = portscWriteBase(pending) | PORTSC_PRC;
    assert(applyPortWrite(pending, clearReset) == (connected | PORTSC_CSC));

    // A snapshot taken during reset must not reassert reset when acknowledging.
    const QC::u32 duringReset = portscWriteBase(pending | PORTSC_PR) | PORTSC_CSC;
    assert(!(duringReset & (PORTSC_PR | PORTSC_PED)));
    assert(applyPortWrite(pending, duringReset) == (connected | PORTSC_PRC));

    std::cout << "xHCI port register tests passed\n";
}
