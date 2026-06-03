#pragma once

#include "QCTypes.h"
#include "QCGeometry.h"
#include "QCVector.h"

namespace QD
{
    enum class DesktopDocumentSourceKind : QC::u8
    {
        Unknown = 0,
        Cmms,
        CuiMLImport,
        JsonImport,
        NewDocument
    };

    enum class DesktopDocumentFormat : QC::u8
    {
        Unknown = 0,
        CuiML,
        Json
    };

    enum class DesktopPublishState : QC::u8
    {
        Draft = 0,
        Validated,
        Published
    };

    enum class DesktopCanvasPolicy : QC::u8
    {
        RuntimeNative = 0,
        Fixed,
        Scalable
    };

    enum class DesktopBackgroundMode : QC::u8
    {
        None = 0,
        Solid,
        Gradient,
        Image
    };

    enum class DesktopAssetKind : QC::u8
    {
        Unknown = 0,
        Wallpaper,
        Icon,
        Illustration,
        Font,
        Import
    };

    enum class DesktopControlKind : QC::u8
    {
        Unknown = 0,
        Panel,
        Button,
        Label,
        Image,
        Slider,
        Container,
        Clock,
        LauncherTile,
        Custom
    };

    struct DesktopAssetRef
    {
        char path[192]{};
        DesktopAssetKind kind = DesktopAssetKind::Unknown;
        bool exists = false;
    };

    struct DesktopBinding
    {
        char event[32]{};
        char action[32]{};
        char argument[192]{};
    };

    struct DesktopProperty
    {
        char key[48]{};
        char value[192]{};
    };

    struct DesktopThemePreviewRef
    {
        char themeId[48]{};
        char variant[48]{};
        bool hasPreviewOverrides = false;
    };

    struct DesktopCanvasModel
    {
        DesktopCanvasPolicy widthPolicy = DesktopCanvasPolicy::RuntimeNative;
        DesktopCanvasPolicy heightPolicy = DesktopCanvasPolicy::RuntimeNative;
        QC::u32 referenceWidth = 0;
        QC::u32 referenceHeight = 0;
        QC::u32 insetLeft = 0;
        QC::u32 insetTop = 0;
        QC::u32 insetRight = 0;
        QC::u32 insetBottom = 0;
        DesktopBackgroundMode backgroundMode = DesktopBackgroundMode::None;
        char rootStyleClass[64]{};
    };

    struct DesktopControlModel
    {
        char id[64]{};
        DesktopControlKind kind = DesktopControlKind::Unknown;
        char name[64]{};
        char parentId[64]{};
        QC::Rect layout{};
        QC::i32 zIndex = 0;
        bool visible = true;
        bool enabled = true;
        char text[160]{};
        DesktopAssetRef iconRef{};
        char styleClass[64]{};
        QC::Vector<DesktopProperty> properties;
        QC::Vector<DesktopBinding> bindings;
    };

    struct DesktopDocumentMetadata
    {
        DesktopPublishState publishState = DesktopPublishState::Draft;
        char sourcePath[192]{};
        char author[64]{};
        char notes[160]{};
    };

    struct DesktopDocument
    {
        char documentId[48]{};
        char displayName[96]{};
        DesktopDocumentSourceKind sourceKind = DesktopDocumentSourceKind::Unknown;
        DesktopDocumentFormat format = DesktopDocumentFormat::Unknown;
        QC::u32 version = 1;
        DesktopCanvasModel canvas{};
        DesktopBackgroundMode backgroundMode = DesktopBackgroundMode::None;
        DesktopAssetRef backgroundAsset{};
        DesktopThemePreviewRef themeRef{};
        DesktopDocumentMetadata metadata{};
        QC::Vector<DesktopControlModel> controls;
        QC::Vector<DesktopBinding> bindings;
    };

    const char *desktopDocumentSourceKindName(DesktopDocumentSourceKind kind);
    const char *desktopDocumentFormatName(DesktopDocumentFormat format);
    const char *desktopControlKindName(DesktopControlKind kind);
}