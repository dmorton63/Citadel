// QDesktop Accent - Implementation
// Namespace: QD

#include "QDAccent.h"

namespace QD
{

    // Global accent color state - default to Electric Blue
    static AccentColor g_currentAccent = AccentColor::ElectricBlue;

    AccentColor currentAccent()
    {
        return g_currentAccent;
    }

    void setAccent(AccentColor accent)
    {
        g_currentAccent = accent;
    }

} // namespace QD
