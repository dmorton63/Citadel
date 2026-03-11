// QCommon UIStyle - Global UI style implementation
// Namespace: QC

#include "QCUIStyle.h"

namespace QC
{

    // Global style state - default to our unique QWStyle
    static UIStyle g_currentUIStyle = UIStyle::QWStyle;

    UIStyle currentUIStyle()
    {
        return g_currentUIStyle;
    }

    void setUIStyle(UIStyle style)
    {
        g_currentUIStyle = style;
    }

} // namespace QC
