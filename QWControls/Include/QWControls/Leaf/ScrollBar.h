#pragma once

// QWControls ScrollBar - Scrollbar control
// Namespace: QW::Controls

#include "QCTypes.h"
#include "QWControls/Base/ControlBase.h"
#include "QKEventTypes.h"

namespace QW
{
    class Window;

    namespace Controls
    {

        /// ScrollBar orientation
        enum class ScrollOrientation : QC::u8
        {
            Horizontal,
            Vertical
        };

        /// Scroll change callback
        class ScrollBar;
        using ScrollChangeHandler = void (*)(ScrollBar *scrollBar, void *userData);

        /// ScrollBar - Allows scrolling through content
        class ScrollBar : public ControlBase
        {
        public:
            ScrollBar();
            ScrollBar(Window *window, Rect bounds, ScrollOrientation orientation = ScrollOrientation::Vertical);
            virtual ~ScrollBar();

            // ==================== Properties ====================

            ScrollOrientation orientation() const { return m_orientation; }
            void setOrientation(ScrollOrientation orientation);

            /// Current scroll value (0 to max)
            QC::i32 value() const { return m_value; }
            void setValue(QC::i32 value);

            /// Minimum scroll value (usually 0)
            QC::i32 minimum() const { return m_minimum; }
            void setMinimum(QC::i32 minimum);

            /// Maximum scroll value
            QC::i32 maximum() const { return m_maximum; }
            void setMaximum(QC::i32 maximum);

            /// Page size (for proportional thumb sizing)
            QC::u32 pageSize() const { return m_pageSize; }
            void setPageSize(QC::u32 size);

            /// Small step when clicking arrows
            QC::i32 smallStep() const { return m_smallStep; }
            void setSmallStep(QC::i32 step) { m_smallStep = step; }

            /// Large step when clicking track
            QC::i32 largeStep() const { return m_largeStep; }
            void setLargeStep(QC::i32 step) { m_largeStep = step; }

            /// If enabled, any left-click inside the control toggles value between minimum and maximum.
            /// Default: false (normal scrollbar behavior).
            bool clickToMax() const { return m_clickToMax; }
            void setClickToMax(bool enabled) { m_clickToMax = enabled; }

            // ==================== Appearance ====================

            Color trackColor() const { return m_trackColor; }
            void setTrackColor(Color color) { m_trackColor = color; }

            Color thumbColor() const { return m_thumbColor; }
            void setThumbColor(Color color) { m_thumbColor = color; }

            Color arrowColor() const { return m_arrowColor; }
            void setArrowColor(Color color) { m_arrowColor = color; }

            Color backgroundColor() const { return m_bgColor; }
            void setBackgroundColor(Color color) { m_bgColor = color; }

            /// Minimum thumb size in pixels
            QC::u32 minThumbSize() const { return m_minThumbSize; }
            void setMinThumbSize(QC::u32 size) { m_minThumbSize = size; }

            // ==================== Events ====================

            void setScrollChangeHandler(ScrollChangeHandler handler, void *userData);

            // ==================== Rendering ====================

            void paint(const PaintContext &context) override;

            // ==================== Event Handlers ====================

            bool onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY) override;
            bool onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;
            bool onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button) override;

        private:
            /// Calculate thumb rectangle
            Rect calculateThumbRect() const;

            /// Calculate up/left arrow rectangle
            Rect calculateArrowUpRect() const;

            /// Calculate down/right arrow rectangle
            Rect calculateArrowDownRect() const;

            /// Calculate track rectangle (between arrows)
            Rect calculateTrackRect() const;

            /// Hit test areas
            enum class HitArea : QC::u8
            {
                None,
                ArrowUp,
                ArrowDown,
                TrackBefore,
                TrackAfter,
                Thumb
            };

            HitArea hitTestArea(QC::i32 x, QC::i32 y) const;

            ScrollOrientation m_orientation;
            QC::i32 m_value;
            QC::i32 m_minimum;
            QC::i32 m_maximum;
            QC::u32 m_pageSize;
            QC::i32 m_smallStep;
            QC::i32 m_largeStep;
            QC::u32 m_minThumbSize;

            Color m_trackColor;
            Color m_thumbColor;
            Color m_arrowColor;
            Color m_bgColor;

            ScrollChangeHandler m_changeHandler;
            void *m_changeUserData;

            // Drag state
            bool m_dragging;
            QC::i32 m_dragOffset;
            HitArea m_pressedArea;

            bool m_clickToMax;
        };

    } // namespace Controls
} // namespace QW
