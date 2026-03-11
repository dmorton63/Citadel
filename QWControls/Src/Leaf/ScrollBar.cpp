// QWControls ScrollBar - Scrollbar control implementation
// Namespace: QW::Controls

#include "QWControls/Leaf/ScrollBar.h"
#include "QWWindow.h"
#include "QGPainter.h"

namespace QW
{
    namespace Controls
    {

        ScrollBar::ScrollBar()
            : ControlBase(),
              m_orientation(ScrollOrientation::Vertical),
              m_value(0),
              m_minimum(0),
              m_maximum(100),
              m_pageSize(10),
              m_smallStep(1),
              m_largeStep(10),
              m_minThumbSize(16),
              m_trackColor(Color(220, 220, 220, 255)),
              m_thumbColor(Color(180, 180, 180, 255)),
              m_arrowColor(Color(100, 100, 100, 255)),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr),
              m_dragging(false),
              m_dragOffset(0),
              m_pressedArea(HitArea::None),
              m_clickToMax(false)
        {
            m_bgColor = Color(240, 240, 240, 255);
        }

        ScrollBar::ScrollBar(Window *window, Rect bounds, ScrollOrientation orientation)
            : ControlBase(window, bounds),
              m_orientation(orientation),
              m_value(0),
              m_minimum(0),
              m_maximum(100),
              m_pageSize(10),
              m_smallStep(1),
              m_largeStep(10),
              m_minThumbSize(16),
              m_trackColor(Color(220, 220, 220, 255)),
              m_thumbColor(Color(180, 180, 180, 255)),
              m_arrowColor(Color(100, 100, 100, 255)),
              m_changeHandler(nullptr),
              m_changeUserData(nullptr),
              m_dragging(false),
              m_dragOffset(0),
              m_pressedArea(HitArea::None),
              m_clickToMax(false)
        {
            m_bgColor = Color(240, 240, 240, 255);
        }

        ScrollBar::~ScrollBar()
        {
        }

        void ScrollBar::setOrientation(ScrollOrientation orientation)
        {
            m_orientation = orientation;
            invalidate();
        }

        void ScrollBar::setValue(QC::i32 value)
        {
            if (value < m_minimum)
                value = m_minimum;
            if (value > m_maximum)
                value = m_maximum;

            if (m_value != value)
            {
                m_value = value;
                invalidate();

                if (m_changeHandler)
                {
                    m_changeHandler(this, m_changeUserData);
                }
            }
        }

        void ScrollBar::setMinimum(QC::i32 minimum)
        {
            m_minimum = minimum;
            if (m_value < m_minimum)
                setValue(m_minimum);
        }

        void ScrollBar::setMaximum(QC::i32 maximum)
        {
            m_maximum = maximum;
            if (m_value > m_maximum)
                setValue(m_maximum);
        }

        void ScrollBar::setPageSize(QC::u32 size)
        {
            m_pageSize = size;
            invalidate();
        }

        void ScrollBar::setScrollChangeHandler(ScrollChangeHandler handler, void *userData)
        {
            m_changeHandler = handler;
            m_changeUserData = userData;
        }

        void ScrollBar::paint(const PaintContext &context)
        {
            if (!m_visible || !context.painter)
                return;

            Rect abs = absoluteBounds();
            auto *painter = context.painter;

            painter->fillRect(abs, m_bgColor);

            Rect trackRect = calculateTrackRect();
            painter->fillRect(trackRect, m_trackColor);

            Rect arrowUp = calculateArrowUpRect();
            Rect arrowDown = calculateArrowDownRect();
            painter->fillRect(arrowUp, m_bgColor);
            painter->fillRect(arrowDown, m_bgColor);
            painter->drawRect(arrowUp, m_arrowColor);
            painter->drawRect(arrowDown, m_arrowColor);

            Rect thumbRect = calculateThumbRect();
            Color thumbDrawColor = m_thumbColor;
            if (m_dragging || m_pressedArea == HitArea::Thumb)
                thumbDrawColor = Color(150, 150, 150, 255);

            painter->fillRect(thumbRect, thumbDrawColor);
            painter->drawRect(thumbRect, m_arrowColor);

            painter->drawRect(abs, Color(160, 160, 160, 255));
        }

        bool ScrollBar::onMouseMove(QC::i32 x, QC::i32 y, QC::i32 deltaX, QC::i32 deltaY)
        {
            (void)deltaX;
            (void)deltaY;

            if (m_dragging)
            {
                Rect trackRect = calculateTrackRect();
                QC::i32 trackSize, pos;

                if (m_orientation == ScrollOrientation::Vertical)
                {
                    trackSize = static_cast<QC::i32>(trackRect.height);
                    pos = y - trackRect.y - m_dragOffset;
                }
                else
                {
                    trackSize = static_cast<QC::i32>(trackRect.width);
                    pos = x - trackRect.x - m_dragOffset;
                }

                // Calculate thumb size
                QC::i32 range = m_maximum - m_minimum;
                if (range <= 0)
                    return true;

                QC::i32 thumbSize = static_cast<QC::i32>((m_pageSize * trackSize) / (range + m_pageSize));
                if (thumbSize < static_cast<QC::i32>(m_minThumbSize))
                    thumbSize = static_cast<QC::i32>(m_minThumbSize);

                QC::i32 scrollableTrack = trackSize - thumbSize;
                if (scrollableTrack > 0)
                {
                    QC::i32 newValue = m_minimum + (pos * range) / scrollableTrack;
                    setValue(newValue);
                }

                return true;
            }

            return hitTest(x, y);
        }

        bool ScrollBar::onMouseDown(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            if (!m_enabled || button != QK::Event::MouseButton::Left)
                return false;

            // Optional slider-like behavior: toggle to max/min on click.
            if (m_clickToMax)
            {
                if (hitTest(x, y))
                {
                    m_dragging = false;
                    m_pressedArea = HitArea::None;
                    setValue((m_value >= m_maximum) ? m_minimum : m_maximum);
                    return true;
                }
                return false;
            }

            HitArea area = hitTestArea(x, y);
            m_pressedArea = area;

            switch (area)
            {
            case HitArea::ArrowUp:
                setValue(m_value - m_smallStep);
                break;

            case HitArea::ArrowDown:
                setValue(m_value + m_smallStep);
                break;

            case HitArea::TrackBefore:
                setValue(m_value - m_largeStep);
                break;

            case HitArea::TrackAfter:
                setValue(m_value + m_largeStep);
                break;

            case HitArea::Thumb:
            {
                m_dragging = true;
                Rect thumbRect = calculateThumbRect();
                if (m_orientation == ScrollOrientation::Vertical)
                {
                    m_dragOffset = y - thumbRect.y;
                }
                else
                {
                    m_dragOffset = x - thumbRect.x;
                }
                break;
            }

            default:
                return false;
            }

            invalidate();
            return true;
        }

        bool ScrollBar::onMouseUp(QC::i32 x, QC::i32 y, QK::Event::MouseButton button)
        {
            (void)x;
            (void)y;
            (void)button;

            if (m_dragging || m_pressedArea != HitArea::None)
            {
                m_dragging = false;
                m_pressedArea = HitArea::None;
                invalidate();
                return true;
            }

            return false;
        }

        Rect ScrollBar::calculateThumbRect() const
        {
            Rect trackRect = calculateTrackRect();
            QC::i32 range = m_maximum - m_minimum;

            if (range <= 0)
            {
                return trackRect;
            }

            QC::i32 trackSize, thumbSize, thumbPos;

            if (m_orientation == ScrollOrientation::Vertical)
            {
                trackSize = static_cast<QC::i32>(trackRect.height);
            }
            else
            {
                trackSize = static_cast<QC::i32>(trackRect.width);
            }

            thumbSize = static_cast<QC::i32>((m_pageSize * trackSize) / (range + m_pageSize));
            if (thumbSize < static_cast<QC::i32>(m_minThumbSize))
                thumbSize = static_cast<QC::i32>(m_minThumbSize);

            QC::i32 scrollableTrack = trackSize - thumbSize;
            thumbPos = (scrollableTrack > 0) ? ((m_value - m_minimum) * scrollableTrack) / range : 0;

            if (m_orientation == ScrollOrientation::Vertical)
            {
                return {trackRect.x, trackRect.y + thumbPos, trackRect.width, static_cast<QC::u32>(thumbSize)};
            }
            else
            {
                return {trackRect.x + thumbPos, trackRect.y, static_cast<QC::u32>(thumbSize), trackRect.height};
            }
        }

        Rect ScrollBar::calculateArrowUpRect() const
        {
            Rect abs = absoluteBounds();
            if (m_orientation == ScrollOrientation::Vertical)
            {
                return {abs.x, abs.y, abs.width, abs.width};
            }
            else
            {
                return {abs.x, abs.y, abs.height, abs.height};
            }
        }

        Rect ScrollBar::calculateArrowDownRect() const
        {
            Rect abs = absoluteBounds();
            if (m_orientation == ScrollOrientation::Vertical)
            {
                return {abs.x, abs.y + static_cast<QC::i32>(abs.height - abs.width), abs.width, abs.width};
            }
            else
            {
                return {abs.x + static_cast<QC::i32>(abs.width - abs.height), abs.y, abs.height, abs.height};
            }
        }

        Rect ScrollBar::calculateTrackRect() const
        {
            Rect abs = absoluteBounds();
            Rect arrowUp = calculateArrowUpRect();
            Rect arrowDown = calculateArrowDownRect();

            if (m_orientation == ScrollOrientation::Vertical)
            {
                QC::i32 y = abs.y + static_cast<QC::i32>(arrowUp.height);
                QC::u32 height = abs.height - arrowUp.height - arrowDown.height;
                return {abs.x, y, abs.width, height};
            }
            else
            {
                QC::i32 x = abs.x + static_cast<QC::i32>(arrowUp.width);
                QC::u32 width = abs.width - arrowUp.width - arrowDown.width;
                return {x, abs.y, width, abs.height};
            }
        }

        ScrollBar::HitArea ScrollBar::hitTestArea(QC::i32 x, QC::i32 y) const
        {
            if (!hitTest(x, y))
                return HitArea::None;

            Rect arrowUp = calculateArrowUpRect();
            if (arrowUp.contains({x, y}))
                return HitArea::ArrowUp;

            Rect arrowDown = calculateArrowDownRect();
            if (arrowDown.contains({x, y}))
                return HitArea::ArrowDown;

            Rect thumbRect = calculateThumbRect();
            if (thumbRect.contains({x, y}))
                return HitArea::Thumb;

            Rect trackRect = calculateTrackRect();
            if (trackRect.contains({x, y}))
            {
                if (m_orientation == ScrollOrientation::Vertical)
                {
                    return (y < thumbRect.y) ? HitArea::TrackBefore : HitArea::TrackAfter;
                }
                else
                {
                    return (x < thumbRect.x) ? HitArea::TrackBefore : HitArea::TrackAfter;
                }
            }

            return HitArea::None;
        }

    } // namespace Controls
} // namespace QW
