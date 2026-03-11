#include "QWControls/Leaf/ImageView.h"

#include "QGPainter.h"

namespace QW
{
    namespace Controls
    {
        ImageView::ImageView()
            : ControlBase(),
              m_surface(nullptr),
              m_scaleMode(QG::ImageScaleMode::Stretch)
        {
        }

        ImageView::ImageView(Window *window, Rect bounds)
            : ControlBase(window, bounds),
              m_surface(nullptr),
              m_scaleMode(QG::ImageScaleMode::Stretch)
        {
        }

        void ImageView::setImage(const QG::ImageSurface *surface)
        {
            m_surface = surface;
            invalidate();
        }

        void ImageView::setScaleMode(QG::ImageScaleMode mode)
        {
            if (m_scaleMode == mode)
                return;
            m_scaleMode = mode;
            invalidate();
        }

        void ImageView::paint(const PaintContext &context)
        {
            if (!m_visible || !context.painter || !m_surface || !m_surface->isValid())
                return;

            QC::Rect rect = absoluteBounds();
            QG::blitImage(context.painter, *m_surface, rect, m_scaleMode, m_scratchRow);
        }

    } // namespace Controls
} // namespace QW
