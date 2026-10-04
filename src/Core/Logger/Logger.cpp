#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const char* levelName(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Debug: return "DEBUG";
    case Logger::Level::Info: return "INFO";
    case Logger::Level::Warning: return "WARNING";
    case Logger::Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

const char* levelColor(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Debug: return "\x1b[36m";
    case Logger::Level::Info: return "\x1b[32m";
    case Logger::Level::Warning: return "\x1b[33m";
    case Logger::Level::Error: return "\x1b[31m";
    }
    return "\x1b[0m";
}

bool prepareColor(std::ostream& stream)
{
#ifdef _WIN32
    DWORD handleId;
    if (&stream == &std::cout) {
        handleId = STD_OUTPUT_HANDLE;
    } else if (&stream == &std::cerr || &stream == &std::clog) {
        handleId = STD_ERROR_HANDLE;
    } else {
        // Explicitly enabled custom streams receive ANSI escape codes.
        return true;
    }

    const HANDLE handle = GetStdHandle(handleId);
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    return SetConsoleMode(handle, mode | ENABLE_PROCESSED_OUTPUT |
                                     ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    (void)stream;
    return true;
#endif
}

std::string formatMessage(Logger::Level level, std::string_view message)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    const bool validTime = localtime_s(&localTime, &time) == 0;
#else
    const bool validTime = localtime_r(&time, &localTime) != nullptr;
#endif

    std::ostringstream output;
    output << '[';
    if (validTime) {
        output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    } else {
        output << "unknown-time";
    }
    output << "] [" << levelName(level) << "] ";
    // A default-constructed string_view may have a null data pointer.
    if (!message.empty()) output << message;
    return output.str();
}
} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::addOutputStream(std::ostream& stream, bool enableColor)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool useColor = enableColor && prepareColor(stream);
    for (Output& output : outputs_) {
        if (output.stream == &stream) {
            output.enableColor = useColor;
            return;
        }
    }
    outputs_.push_back({&stream, useColor});
}

void Logger::removeOutputStream(std::ostream& stream)
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputs_.erase(std::remove_if(outputs_.begin(), outputs_.end(),
                                 [&stream](const Output& output) {
                                     return output.stream == &stream;
                                 }),
                   outputs_.end());
}

void Logger::clearOutputStreams()
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputs_.clear();
}

void Logger::addConsoleOutput(bool enableColor)
{
    addOutputStream(std::cout, enableColor);
}

void Logger::setMinimumLevel(Level level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    minimumLevel_ = level;
}

bool Logger::log(Level level, std::string_view message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < minimumLevel_) return true;
    if (outputs_.empty()) return false;

    const std::string text = formatMessage(level, message);
    const std::string plain = text + '\n';
    const std::string colored = std::string(levelColor(level)) + text + "\x1b[0m\n";
    return writeToOutputs(plain, colored);
}

bool Logger::writeToOutputs(std::string_view plain, std::string_view colored)
{
    bool success = true;
    for (const Output& output : outputs_) {
        const std::string_view record = output.enableColor ? colored : plain;
        try {
            if (!record.empty()) {
                output.stream->write(record.data(), static_cast<std::streamsize>(record.size()));
            }
            if (!*output.stream) success = false;
        } catch (const std::ios_base::failure&) {
            success = false;
        }
    }
    return success;
}

bool Logger::log(Level level, const QString& message)
{
    const QByteArray utf8 = message.toUtf8();
    return log(level, std::string_view(utf8.constData(),
                                     static_cast<std::size_t>(utf8.size())));
}

bool Logger::log(Level level, const char* message)
{
    return log(level, std::string_view(message));
}

bool Logger::print(Level level, std::string_view message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < minimumLevel_) return true;
    if (outputs_.empty()) return false;

    std::string colored = levelColor(level);
    if (!message.empty()) colored.append(message.data(), message.size());
    colored += "\x1b[0m";
    return writeToOutputs(message, colored);
}

bool Logger::print(Level level, const QString& message)
{
    const QByteArray utf8 = message.toUtf8();
    return print(level, std::string_view(utf8.constData(),
                                       static_cast<std::size_t>(utf8.size())));
}

bool Logger::print(Level level, const char* message)
{
    return print(level, std::string_view(message));
}

bool Logger::flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (outputs_.empty()) return false;

    bool success = true;
    for (const Output& output : outputs_) {
        try {
            output.stream->flush();
            if (!*output.stream) success = false;
        } catch (const std::ios_base::failure&) {
            success = false;
        }
    }
    return success;
}
