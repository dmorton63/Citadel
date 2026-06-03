#include "QDDesktopDocument.h"

namespace QD
{
    const char *desktopDocumentSourceKindName(DesktopDocumentSourceKind kind)
    {
        switch (kind)
        {
        case DesktopDocumentSourceKind::Cmms:
            return "cmms";
        case DesktopDocumentSourceKind::CuiMLImport:
            return "cuiml-import";
        case DesktopDocumentSourceKind::JsonImport:
            return "json-import";
        case DesktopDocumentSourceKind::NewDocument:
            return "new";
        default:
            return "unknown";
        }
    }

    const char *desktopDocumentFormatName(DesktopDocumentFormat format)
    {
        switch (format)
        {
        case DesktopDocumentFormat::CuiML:
            return "cuiml";
        case DesktopDocumentFormat::Json:
            return "json";
        default:
            return "unknown";
        }
    }

    const char *desktopControlKindName(DesktopControlKind kind)
    {
        switch (kind)
        {
        case DesktopControlKind::Panel:
            return "panel";
        case DesktopControlKind::Button:
            return "button";
        case DesktopControlKind::Label:
            return "label";
        case DesktopControlKind::Image:
            return "image";
        case DesktopControlKind::Slider:
            return "slider";
        case DesktopControlKind::Container:
            return "container";
        case DesktopControlKind::Clock:
            return "clock";
        case DesktopControlKind::LauncherTile:
            return "launcher-tile";
        case DesktopControlKind::Custom:
            return "custom";
        default:
            return "unknown";
        }
    }
}