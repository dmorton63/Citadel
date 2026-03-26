#pragma once

#include "QCTypes.h"
#include "QCVector.h"

namespace QG
{
    struct ImageSurface;

    struct SVGDecodeOptions
    {
        // When an SVG uses stroke="currentColor", this is the fallback color.
        // Feather icons expect this to be the UI text color; white is a good default for Citadel's dark UI.
        QC::u32 currentColorARGB = 0xFFFFFFFFu;

        // Safety caps (avoid pathological SVGs consuming memory/CPU).
        QC::u32 maxOutputPixels = 512u * 512u;
        QC::u32 maxPathSegments = 8192u;
    };

    // Minimal SVG decoder intended for simple UI icons.
    // Supported elements: svg, path (subset), line, polyline, circle.
    // Supported styles: stroke, stroke-width, stroke-linecap/linejoin (best-effort), fill="none".
    bool decodeSVG(const QC::u8 *data, QC::usize size, ImageSurface &outSurface, const SVGDecodeOptions &options = {});
    bool decodeSVG(const QC::Vector<QC::u8> &buffer, ImageSurface &outSurface, const SVGDecodeOptions &options = {});
}
