// QCommon Logger - Implementation
// Namespace: QC

#include "QCLogger.h"
#include "QCBuiltins.h"
#include <cstdarg>

namespace QC
{

    // Serial port for debug output
    constexpr u16 SERIAL_COM1 = 0x3F8;

    Logger &Logger::instance()
    {
        static Logger instance;
        return instance;
    }

    Logger::Logger() : m_level(LogLevel::Info)
    {
        // Initialize serial port for logging
        outb(SERIAL_COM1 + 1, 0x00); // Disable interrupts
        outb(SERIAL_COM1 + 3, 0x80); // Enable DLAB
        outb(SERIAL_COM1 + 0, 0x03); // Set divisor to 3 (38400 baud)
        outb(SERIAL_COM1 + 1, 0x00);
        outb(SERIAL_COM1 + 3, 0x03); // 8 bits, no parity, one stop bit
        outb(SERIAL_COM1 + 2, 0xC7); // Enable FIFO
        outb(SERIAL_COM1 + 4, 0x0B); // IRQs enabled, RTS/DSR set
    }

    void Logger::setLevel(LogLevel level)
    {
        m_level = level;
    }

    LogLevel Logger::getLevel() const
    {
        return m_level;
    }

    void Logger::outputChar(char c)
    {
        // Wait for transmit buffer to be empty
        while ((inb(SERIAL_COM1 + 5) & 0x20) == 0)
            ;
        outb(SERIAL_COM1, static_cast<u8>(c));
    }

    void Logger::outputString(const char *str)
    {
        while (*str)
        {
            outputChar(*str++);
        }
    }

    // Helper: output an unsigned integer in a given base
    void Logger::outputNumber(u64 value, int base, int minWidth, char padChar, bool uppercase)
    {
        static const char digitsLower[] = "0123456789abcdef";
        static const char digitsUpper[] = "0123456789ABCDEF";
        const char *digits = uppercase ? digitsUpper : digitsLower;

        char buffer[24]; // Enough for 64-bit number in binary
        int pos = 0;

        // Handle zero
        if (value == 0)
        {
            buffer[pos++] = '0';
        }
        else
        {
            while (value > 0)
            {
                buffer[pos++] = digits[value % base];
                value /= base;
            }
        }

        // Pad if needed
        while (pos < minWidth)
        {
            buffer[pos++] = padChar;
        }

        // Output in reverse order
        while (pos > 0)
        {
            outputChar(buffer[--pos]);
        }
    }

    // Helper: output a signed integer
    void Logger::outputSigned(i64 value, int minWidth, char padChar)
    {
        if (value < 0)
        {
            outputChar('-');
            outputNumber(static_cast<u64>(-value), 10, minWidth > 0 ? minWidth - 1 : 0, padChar, false);
        }
        else
        {
            outputNumber(static_cast<u64>(value), 10, minWidth, padChar, false);
        }
    }

    void Logger::vlog(LogLevel level, const char *module, const char *format, va_list args)
    {
        if (level < m_level)
            return;

        // Print log level prefix
        const char *levelStr;
        switch (level)
        {
        case LogLevel::Trace:
            levelStr = "[TRACE]";
            break;
        case LogLevel::Debug:
            levelStr = "[DEBUG]";
            break;
        case LogLevel::Info:
            levelStr = "[INFO ]";
            break;
        case LogLevel::Warning:
            levelStr = "[WARN ]";
            break;
        case LogLevel::Error:
            levelStr = "[ERROR]";
            break;
        case LogLevel::Fatal:
            levelStr = "[FATAL]";
            break;
        default:
            levelStr = "[?????]";
            break;
        }

        outputString(levelStr);
        outputString(" ");
        outputString(module);
        outputString(": ");

        // Parse format string and handle specifiers
        while (*format)
        {
            if (*format != '%')
            {
                outputChar(*format++);
                continue;
            }

            format++; // Skip '%'

            if (*format == '%')
            {
                outputChar('%');
                format++;
                continue;
            }

            // Parse width and flags
            char padChar = ' ';
            int minWidth = 0;
            bool longLong = false;
            bool isLong = false;

            // Check for '0' padding
            if (*format == '0')
            {
                padChar = '0';
                format++;
            }

            // Parse width
            while (*format >= '0' && *format <= '9')
            {
                minWidth = minWidth * 10 + (*format - '0');
                format++;
            }

            // Check for length modifiers
            if (*format == 'l')
            {
                isLong = true;
                format++;
                if (*format == 'l')
                {
                    longLong = true;
                    format++;
                }
            }
            else if (*format == 'z')
            {
                // size_t - treat as long on 64-bit
                isLong = true;
                format++;
            }

            // Handle format specifier
            switch (*format)
            {
            case 'd':
            case 'i':
                if (longLong || isLong)
                    outputSigned(va_arg(args, i64), minWidth, padChar);
                else
                    outputSigned(va_arg(args, int), minWidth, padChar);
                break;

            case 'u':
                if (longLong || isLong)
                    outputNumber(va_arg(args, u64), 10, minWidth, padChar, false);
                else
                    outputNumber(va_arg(args, unsigned int), 10, minWidth, padChar, false);
                break;

            case 'x':
                if (longLong || isLong)
                    outputNumber(va_arg(args, u64), 16, minWidth, padChar, false);
                else
                    outputNumber(va_arg(args, unsigned int), 16, minWidth, padChar, false);
                break;

            case 'X':
                if (longLong || isLong)
                    outputNumber(va_arg(args, u64), 16, minWidth, padChar, true);
                else
                    outputNumber(va_arg(args, unsigned int), 16, minWidth, padChar, true);
                break;

            case 'p':
                outputString("0x");
                outputNumber(reinterpret_cast<u64>(va_arg(args, void *)), 16, 16, '0', false);
                break;

            case 's':
            {
                const char *str = va_arg(args, const char *);
                if (str)
                    outputString(str);
                else
                    outputString("(null)");
                break;
            }

            case 'c':
                outputChar(static_cast<char>(va_arg(args, int)));
                break;

            default:
                // Unknown specifier - output as-is
                outputChar('%');
                outputChar(*format);
                break;
            }

            format++;
        }

        outputString("\n");
    }

    void Logger::log(LogLevel level, const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(level, module, format, args);
        va_end(args);
    }

    void Logger::trace(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Trace, module, format, args);
        va_end(args);
    }

    void Logger::debug(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Debug, module, format, args);
        va_end(args);
    }

    void Logger::info(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Info, module, format, args);
        va_end(args);
    }

    void Logger::warning(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Warning, module, format, args);
        va_end(args);
    }

    void Logger::error(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Error, module, format, args);
        va_end(args);
    }

    void Logger::fatal(const char *module, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vlog(LogLevel::Fatal, module, format, args);
        va_end(args);
    }

} // namespace QC
