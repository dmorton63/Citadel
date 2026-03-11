#pragma once

// QCommon Geometry - Core geometry types
// Namespace: QC
//
// These are foundational types used throughout the system.
// They live in QCommon so all modules can depend on them
// without circular dependencies.

#include "QCTypes.h"

namespace QC
{

    /// 2D Point with signed coordinates
    struct Point
    {
        i32 x;
        i32 y;

        Point() : x(0), y(0) {}
        Point(i32 px, i32 py) : x(px), y(py) {}

        bool operator==(const Point &other) const
        {
            return x == other.x && y == other.y;
        }

        bool operator!=(const Point &other) const
        {
            return !(*this == other);
        }

        Point operator+(const Point &other) const
        {
            return Point(x + other.x, y + other.y);
        }

        Point operator-(const Point &other) const
        {
            return Point(x - other.x, y - other.y);
        }

        Point &operator+=(const Point &other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }

        Point &operator-=(const Point &other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }
    };

    /// 2D Size with unsigned dimensions
    struct Size
    {
        u32 width;
        u32 height;

        Size() : width(0), height(0) {}
        Size(u32 w, u32 h) : width(w), height(h) {}

        bool operator==(const Size &other) const
        {
            return width == other.width && height == other.height;
        }

        bool operator!=(const Size &other) const
        {
            return !(*this == other);
        }

        /// Total area
        u64 area() const
        {
            return static_cast<u64>(width) * static_cast<u64>(height);
        }

        /// Check if size is empty (zero area)
        bool isEmpty() const
        {
            return width == 0 || height == 0;
        }
    };

    /// 2D Rectangle with position and size
    struct Rect
    {
        i32 x;
        i32 y;
        u32 width;
        u32 height;

        Rect() : x(0), y(0), width(0), height(0) {}
        Rect(i32 px, i32 py, u32 w, u32 h) : x(px), y(py), width(w), height(h) {}
        Rect(Point origin, Size size) : x(origin.x), y(origin.y), width(size.width), height(size.height) {}

        bool operator==(const Rect &other) const
        {
            return x == other.x && y == other.y &&
                   width == other.width && height == other.height;
        }

        bool operator!=(const Rect &other) const
        {
            return !(*this == other);
        }

        /// Get origin point
        Point origin() const { return Point(x, y); }

        /// Get size
        Size size() const { return Size(width, height); }

        /// Right edge (x + width)
        i32 right() const { return x + static_cast<i32>(width); }

        /// Bottom edge (y + height)
        i32 bottom() const { return y + static_cast<i32>(height); }

        /// Center point
        Point center() const
        {
            return Point(x + static_cast<i32>(width / 2),
                         y + static_cast<i32>(height / 2));
        }

        /// Check if rectangle is empty
        bool isEmpty() const
        {
            return width == 0 || height == 0;
        }

        /// Check if point is inside rectangle
        bool contains(Point p) const
        {
            return p.x >= x && p.x < right() &&
                   p.y >= y && p.y < bottom();
        }

        /// Check if point (x,y) is inside rectangle
        bool contains(i32 px, i32 py) const
        {
            return contains(Point(px, py));
        }

        /// Check if this rectangle fully contains another
        bool contains(const Rect &other) const
        {
            return other.x >= x && other.right() <= right() &&
                   other.y >= y && other.bottom() <= bottom();
        }

        /// Check if this rectangle intersects with another
        bool intersects(const Rect &other) const
        {
            return !(other.x >= right() || other.right() <= x ||
                     other.y >= bottom() || other.bottom() <= y);
        }

        /// Get intersection of two rectangles
        Rect intersection(const Rect &other) const
        {
            i32 nx = (x > other.x) ? x : other.x;
            i32 ny = (y > other.y) ? y : other.y;
            i32 nr = (right() < other.right()) ? right() : other.right();
            i32 nb = (bottom() < other.bottom()) ? bottom() : other.bottom();

            if (nr <= nx || nb <= ny)
            {
                return Rect(); // Empty intersection
            }

            return Rect(nx, ny, static_cast<u32>(nr - nx), static_cast<u32>(nb - ny));
        }

        /// Get bounding rectangle of two rectangles (union)
        Rect united(const Rect &other) const
        {
            if (isEmpty())
                return other;
            if (other.isEmpty())
                return *this;

            i32 nx = (x < other.x) ? x : other.x;
            i32 ny = (y < other.y) ? y : other.y;
            i32 nr = (right() > other.right()) ? right() : other.right();
            i32 nb = (bottom() > other.bottom()) ? bottom() : other.bottom();

            return Rect(nx, ny, static_cast<u32>(nr - nx), static_cast<u32>(nb - ny));
        }

        /// Offset rectangle by delta
        Rect offset(i32 dx, i32 dy) const
        {
            return Rect(x + dx, y + dy, width, height);
        }

        /// Offset rectangle by point
        Rect offset(Point delta) const
        {
            return offset(delta.x, delta.y);
        }

        /// Inset rectangle (shrink by amount on all sides)
        Rect inset(i32 amount) const
        {
            return inset(amount, amount, amount, amount);
        }

        /// Inset rectangle with different amounts per side
        Rect inset(i32 left, i32 top, i32 r, i32 bot) const
        {
            i32 nx = x + left;
            i32 ny = y + top;
            i32 nw = static_cast<i32>(width) - left - r;
            i32 nh = static_cast<i32>(height) - top - bot;

            if (nw < 0)
                nw = 0;
            if (nh < 0)
                nh = 0;

            return Rect(nx, ny, static_cast<u32>(nw), static_cast<u32>(nh));
        }
    };

    /// Margins/Insets for padding around elements
    struct Margins
    {
        u32 left;
        u32 top;
        u32 right;
        u32 bottom;

        Margins() : left(0), top(0), right(0), bottom(0) {}
        Margins(u32 all) : left(all), top(all), right(all), bottom(all) {}
        Margins(u32 horizontal, u32 vertical)
            : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
        Margins(u32 l, u32 t, u32 r, u32 b) : left(l), top(t), right(r), bottom(b) {}

        /// Total horizontal margin
        u32 horizontal() const { return left + right; }

        /// Total vertical margin
        u32 vertical() const { return top + bottom; }
    };

} // namespace QC
