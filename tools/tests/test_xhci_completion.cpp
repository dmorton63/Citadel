#include <cassert>
#include <iostream>

#include "../../kernel/Drivers/XHCI/xhci_trb.h"

using namespace QKDrv::XHCI;

int main()
{
    // SET_CONFIGURATION and HID SET_PROTOCOL have no data stage.
    const QC::u32 setConfigStatus = makeControlStatusTRB(0x00, 0, true);
    const QC::u32 setProtocolStatus = makeControlStatusTRB(0x21, 0, false);
    assert(setConfigStatus == makeTRBControl(TRBType::StatusStage, TRB_IOC | TRB_CYCLE | TRB_DIR_IN));
    assert(setProtocolStatus == makeTRBControl(TRBType::StatusStage, TRB_IOC | TRB_DIR_IN));
    assert(makeControlStatusTRB(0x80, 0, false) & TRB_DIR_IN);
    // GET_DESCRIPTOR reads data, whereas a control write sends data.
    assert(!(makeControlStatusTRB(0x80, 18, true) & TRB_DIR_IN));
    assert(makeControlStatusTRB(0x21, 8, true) & TRB_DIR_IN);

    TRB event{};
    event.parameter = 0x1010;
    assert(commandCompletionMatches(true, 0x1010, event));
    assert(!commandCompletionMatches(true, 0x1020, event));
    assert(!commandCompletionMatches(false, 0x1010, event));

    event.control = (2u << 24) | (1u << 16);
    assert(transferCompletionMatches(true, 2, 1, event));
    assert(!transferCompletionMatches(true, 3, 1, event));
    assert(!transferCompletionMatches(true, 2, 3, event));
    assert(!transferCompletionMatches(false, 2, 1, event));

    bool pending = true;
    QC::u64 now = 0;
    auto clock = [&]() { return now; };
    auto advance = [&]() { ++now; };
    auto completeAt4999 = [&]()
    {
        if (now == 4999)
            pending = false;
    };
    assert(waitForCompletion(pending, 5000, true, completeAt4999, clock, advance, 20) ==
           CompletionWaitResult::Completed);
    assert(now == 4999);
    pending = true;
    now = 0;
    assert(waitForCompletion(pending, 5000, true, []() {}, clock, advance, 20) ==
           CompletionWaitResult::Timeout);
    assert(now == 5000);
    assert(waitForCompletion(pending, 5000, true, []() {}, clock, []() {}, 20) ==
           CompletionWaitResult::ClockStopped);
    assert(waitForCompletion(pending, 5000, false, []() {}, clock, []() {}, 20) ==
           CompletionWaitResult::SpinLimit);

    std::cout << "xHCI completion matching tests passed\n";
}
