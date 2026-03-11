#pragma once

// QWindowing StyleRenderer - Style-aware drawing front-end
// Namespace: QW

#include "QGGraphicsBackend.h"
#include "QWStyleTypes.h"

namespace QW
{

    class StyleRenderer
    {
    public:
        StyleRenderer();
        explicit StyleRenderer(QG::GraphicsBackend *backend);

        void setBackend(QG::GraphicsBackend *backend);
        QG::GraphicsBackend *backend() const { return m_backend; }

        void setStyleSnapshot(const StyleSnapshot *snapshot);
        const StyleSnapshot *styleSnapshot() const { return m_snapshot; }

        bool beginFrame(const FrameContext &context);
        void endFrame();

        void drawWindowChrome(const WindowPaintArgs &args);
        void drawPanel(const PanelPaintArgs &args);
        void drawButton(const ButtonPaintArgs &args);

    private:
        const StyleSnapshot &style() const;
        void drawWindowBackground(const WindowPaintArgs &args, const StyleSnapshot &styleData);
        void drawWindowBorder(const WindowPaintArgs &args, const StyleSnapshot &styleData);

        QG::GraphicsBackend *m_backend;
        const StyleSnapshot *m_snapshot;
        FrameContext m_context;
        bool m_frameActive;
    };

} // namespace QW
