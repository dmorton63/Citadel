#pragma once

// QEvent - Event Type Definitions
// Namespace: QK::Event

#include "QCTypes.h"

namespace QK
{
    namespace Event
    {

        /// Event categories for filtering and routing
        enum class Category : QC::u8
        {
            None = 0,
            Input = 1 << 0,       // Keyboard, mouse, touch
            System = 1 << 1,      // Timer, power, etc.
            Window = 1 << 2,      // Window events
            Application = 1 << 3, // App-level events
            Network = 1 << 4,     // Network events
            FileSystem = 1 << 5,  // File system events
            Custom = 1 << 6,      // User-defined events
            All = 0xFF
        };

        /// Specific event types
        enum class Type : QC::u16
        {
            None = 0,

            // Input events (100-199)
            KeyDown = 100,
            KeyUp = 101,
            KeyPress = 102,
            MouseMove = 110,
            MouseButtonDown = 111,
            MouseButtonUp = 112,
            MouseClick = 113,
            MouseDoubleClick = 114,
            MouseScroll = 115,
            TouchStart = 120,
            TouchMove = 121,
            TouchEnd = 122,

            // System events (200-299)
            Timer = 200,
            Tick = 201,
            Interrupt = 202,
            PowerStateChange = 210,
            MemoryLow = 220,
            MemoryCritical = 221,
            ShutdownRequest = 230,
            ShutdownPrepare = 231,
            ShutdownNow = 232,

            // Window events (300-399)
            WindowCreate = 300,
            WindowDestroy = 301,
            WindowResize = 302,
            WindowMove = 303,
            WindowFocus = 304,
            WindowBlur = 305,
            WindowMinimize = 306,
            WindowMaximize = 307,
            WindowRestore = 308,
            WindowPaint = 309,

            // Application events (400-499)
            AppStart = 400,
            AppQuit = 401,
            AppPause = 402,
            AppResume = 403,

            // Network events (500-599)
            NetConnect = 500,
            NetDisconnect = 501,
            NetDataReceived = 502,
            NetDataSent = 503,
            NetError = 504,

            // FileSystem events (600-699)
            FileOpened = 600,
            FileClosed = 601,
            FileRead = 602,
            FileWritten = 603,
            FileCreated = 604,
            FileDeleted = 605,
            FileModified = 606,

            // Custom events start at 1000
            CustomBase = 1000
        };

        /// Event priority levels
        enum class Priority : QC::u8
        {
            Low = 0,
            Normal = 1,
            High = 2,
            Critical = 3,
            Immediate = 4 // Bypass queue, process immediately
        };

        /// Mouse button identifiers
        enum class MouseButton : QC::u8
        {
            None = 0,
            Left = 1,
            Right = 2,
            Middle = 3,
            Button4 = 4,
            Button5 = 5
        };

        /// Modifier key flags
        enum class Modifiers : QC::u8
        {
            None = 0,
            Shift = 1 << 0,
            Ctrl = 1 << 1,
            Alt = 1 << 2,
            Super = 1 << 3, // Windows/Command key
            CapsLock = 1 << 4,
            NumLock = 1 << 5
        };

        // Bitwise operators for Modifiers
        inline Modifiers operator|(Modifiers a, Modifiers b)
        {
            return static_cast<Modifiers>(static_cast<QC::u8>(a) | static_cast<QC::u8>(b));
        }

        inline Modifiers operator&(Modifiers a, Modifiers b)
        {
            return static_cast<Modifiers>(static_cast<QC::u8>(a) & static_cast<QC::u8>(b));
        }

        inline bool hasModifier(Modifiers mods, Modifiers check)
        {
            return (static_cast<QC::u8>(mods) & static_cast<QC::u8>(check)) != 0;
        }

        // Bitwise operators for Category
        inline Category operator|(Category a, Category b)
        {
            return static_cast<Category>(static_cast<QC::u8>(a) | static_cast<QC::u8>(b));
        }

        inline Category operator&(Category a, Category b)
        {
            return static_cast<Category>(static_cast<QC::u8>(a) & static_cast<QC::u8>(b));
        }

        inline bool hasCategory(Category cats, Category check)
        {
            return (static_cast<QC::u8>(cats) & static_cast<QC::u8>(check)) != 0;
        }

        /// Base event structure
        struct EventData
        {
            Type type = Type::None;
            Category category = Category::None;
            Priority priority = Priority::Normal;
            QC::u64 timestamp = 0; // Timestamp in ticks
            QC::u32 sourceId = 0;  // Source identifier (window, device, etc.)
            bool handled = false;  // Set to true to stop propagation

            EventData() = default;
            EventData(Type t, Category c, Priority p = Priority::Normal)
                : type(t), category(c), priority(p) {}
        };

        /// Keyboard event data
        struct KeyEventData : EventData
        {
            QC::u8 scancode = 0; // Hardware scan code
            QC::u8 keycode = 0;  // Virtual key code
            char character = 0;  // ASCII character (if applicable)
            Modifiers modifiers = Modifiers::None;
            bool isRepeat = false;

            KeyEventData() : EventData(Type::KeyDown, Category::Input) {}
        };

        /// Mouse event data
        struct MouseEventData : EventData
        {
            QC::i32 x = 0;           // Current X position
            QC::i32 y = 0;           // Current Y position
            QC::i32 deltaX = 0;      // Relative X movement
            QC::i32 deltaY = 0;      // Relative Y movement
            QC::i32 scrollDelta = 0; // Scroll wheel delta
            MouseButton button = MouseButton::None;
            Modifiers modifiers = Modifiers::None;

            MouseEventData() : EventData(Type::MouseMove, Category::Input) {}
        };

        /// Timer event data
        struct TimerEventData : EventData
        {
            QC::u32 timerId = 0;    // Timer identifier
            QC::u64 elapsedMs = 0;  // Elapsed time in milliseconds
            QC::u64 intervalMs = 0; // Timer interval

            TimerEventData() : EventData(Type::Timer, Category::System) {}
        };

        /// Window event data
        struct WindowEventData : EventData
        {
            QC::u32 windowId = 0;
            QC::i32 x = 0;
            QC::i32 y = 0;
            QC::u32 width = 0;
            QC::u32 height = 0;

            WindowEventData() : EventData(Type::WindowCreate, Category::Window) {}
        };

        /// Generic custom event data
        struct CustomEventData : EventData
        {
            QC::u64 param1 = 0;
            QC::u64 param2 = 0;
            void *userData = nullptr;

            CustomEventData() : EventData(Type::CustomBase, Category::Custom) {}
        };

        /// Shutdown lifecycle event data
        struct ShutdownEventData : EventData
        {
            QC::u32 reasonCode = 0;  // Encoded source-specific reason
            void *context = nullptr; // Optional context pointer (dialog, window, etc.)

            ShutdownEventData() : EventData(Type::ShutdownRequest, Category::System) {}
        };

        /// Union of all event types for efficient storage
        union EventUnion
        {
            EventData base;
            KeyEventData key;
            MouseEventData mouse;
            TimerEventData timer;
            WindowEventData window;
            CustomEventData custom;
            ShutdownEventData shutdown;

            EventUnion() : base() {}
        };

        /// Event wrapper that holds any event type
        struct Event
        {
            EventUnion data;

            Event() = default;

            // Convenience accessors
            Type type() const { return data.base.type; }
            Category category() const { return data.base.category; }
            Priority priority() const { return data.base.priority; }
            QC::u64 timestamp() const { return data.base.timestamp; }
            bool isHandled() const { return data.base.handled; }
            void setHandled(bool h = true) { data.base.handled = h; }

            // Type-safe accessors
            EventData &base() { return data.base; }
            const EventData &base() const { return data.base; }

            KeyEventData &asKey() { return data.key; }
            const KeyEventData &asKey() const { return data.key; }

            MouseEventData &asMouse() { return data.mouse; }
            const MouseEventData &asMouse() const { return data.mouse; }

            TimerEventData &asTimer() { return data.timer; }
            const TimerEventData &asTimer() const { return data.timer; }

            WindowEventData &asWindow() { return data.window; }
            const WindowEventData &asWindow() const { return data.window; }

            CustomEventData &asCustom() { return data.custom; }
            const CustomEventData &asCustom() const { return data.custom; }

            ShutdownEventData &asShutdown() { return data.shutdown; }
            const ShutdownEventData &asShutdown() const { return data.shutdown; }

            // Check if event is of a certain category
            bool isInput() const { return hasCategory(category(), Category::Input); }
            bool isSystem() const { return hasCategory(category(), Category::System); }
            bool isWindow() const { return hasCategory(category(), Category::Window); }
        };

    } // namespace Event
} // namespace QK
