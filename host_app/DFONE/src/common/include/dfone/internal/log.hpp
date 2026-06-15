#pragma once

#include <iostream>
#include <string_view>

namespace dfone {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

inline const char *to_string(LogLevel level)
{
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }

    return "UNKNOWN";
}

inline void log(LogLevel level, std::string_view message)
{
    std::ostream &os = (level == LogLevel::Error) ? std::cerr : std::cout;
    os << "[DFONE][" << to_string(level) << "] " << message << '\n';
}

}  // namespace dfone
