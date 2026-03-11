#pragma once

#include "QWControls/Base/ControlBase.h"
#include "QG/Image.h"
#include "QCVector.h"

namespace QW
{
    namespace Controls
    {
        class ImageView : public ControlBase
        {
        public:
            ImageView();
            ImageView(Window *window, Rect bounds);
            ~ImageView() override = default;

            void setImage(const QG::ImageSurface *surface);
            const QG::ImageSurface *image() const { return m_surface; }

            void setScaleMode(QG::ImageScaleMode mode);
            QG::ImageScaleMode scaleMode() const { return m_scaleMode; }

            // Decorative view by default; do not intercept mouse hit tests.
            bool hitTest(int, int) const override { return false; }

            void paint(const PaintContext &context) override;

        private:
            const QG::ImageSurface *m_surface;
            QG::ImageScaleMode m_scaleMode;
            QC::Vector<QC::u32> m_scratchRow;
        };
    }
} // namespace QW
