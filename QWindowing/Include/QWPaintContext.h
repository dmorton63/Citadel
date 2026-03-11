#pragma once

namespace QG
{
    class IPainter;
}

namespace QW
{

    class Window;
    class StyleRenderer;

    struct PaintContext
    {
        Window *window = nullptr;
        StyleRenderer *styleRenderer = nullptr;
        QG::IPainter *painter = nullptr;
    };

} // namespace QW