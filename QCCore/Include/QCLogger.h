#pragma once

// QCommon Logger - Kernel logging system
// Namespace: QC

#include "QCTypes.h"
#include <cstdarg>

namespace QC
{

    enum class LogLevel : u8
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Fatal = 5
    };

    class Logger
    {
    public:
        static Logger &instance();

        void setLevel(LogLevel level);
        LogLevel getLevel() const;

        void log(LogLevel level, const char *module, const char *format, ...);

        void trace(const char *module, const char *format, ...);
        void debug(const char *module, const char *format, ...);
        void info(const char *module, const char *format, ...);
        void warning(const char *module, const char *format, ...);
        void error(const char *module, const char *format, ...);
        void fatal(const char *module, const char *format, ...);

    private:
        Logger();
        ~Logger() = default;
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

        void outputChar(char c);
        void outputString(const char *str);
        void outputNumber(u64 value, int base, int minWidth, char padChar, bool uppercase);
        void outputSigned(i64 value, int minWidth, char padChar);
        void vlog(LogLevel level, const char *module, const char *format, va_list args);

        LogLevel m_level;
    };

// Convenience macros
#define QC_LOG_TRACE(module, fmt, ...) QC::Logger::instance().trace(module, fmt, ##__VA_ARGS__)
#define QC_LOG_DEBUG(module, fmt, ...) QC::Logger::instance().debug(module, fmt, ##__VA_ARGS__)
#define QC_LOG_INFO(module, fmt, ...) QC::Logger::instance().info(module, fmt, ##__VA_ARGS__)
#define QC_LOG_WARN(module, fmt, ...) QC::Logger::instance().warning(module, fmt, ##__VA_ARGS__)
#define QC_LOG_ERROR(module, fmt, ...) QC::Logger::instance().error(module, fmt, ##__VA_ARGS__)
#define QC_LOG_FATAL(module, fmt, ...) QC::Logger::instance().fatal(module, fmt, ##__VA_ARGS__)

} // namespace QC
