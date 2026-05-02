#pragma once

// QCMS Types - Shared constants and enums for Citadel Management Studio
// Namespace: QCMS

#include "QCTypes.h"

namespace QCMS
{

    enum class PanelId : QC::u8
    {
        DbBrowser    = 0,
        ThemeEditor  = 1,
        SysConfig    = 2,
        Security     = 3,
        ServiceMgr   = 4,
        Count
    };

    // Nav button geometry (left sidebar)
    constexpr QC::u32 kNavBarWidth    = 160;
    constexpr QC::u32 kNavButtonH     = 36;
    constexpr QC::u32 kNavPadding     = 8;

    // Main window default geometry
    constexpr QC::u32 kWindowW        = 920;
    constexpr QC::u32 kWindowH        = 580;

} // namespace QCMS
