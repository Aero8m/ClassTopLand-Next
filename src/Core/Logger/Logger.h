#ifndef CLASSTOPLAND_NEXT_LOGGER_H
#define CLASSTOPLAND_NEXT_LOGGER_H

#include <QString>

#include <mutex>
#include <ostream>
#include <string_view>
#include <vector>

class Logger final
{
public:
    enum class Level {
        Debug, Info, Warning, Error
    };

    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Streams are borrowed. Remove them before closing or destroying them.
    // Adding the same stream again updates its color setting.
    void addOutputStream(std::ostream& stream, bool enableColor = false);
    void removeOutputStream(std::ostream& stream);
    void clearOutputStreams();
    void addConsoleOutput(bool enableColor = true);

    void setMinimumLevel(Level level);

    // Filtered messages succeed without writing. No outputs means failure.
    // A failed stream does not prevent writing to the remaining streams.
    bool log(Level level, std::string_view message);
    // QString messages are written as UTF-8.
    bool log(Level level, const QString& message);
    // Keep string literals unambiguous with the QString overload.
    bool log(Level level, const char* message);

    // Write text directly, without a timestamp, level label, or added newline.
    // Uses the same level filtering, stream colors, and success rules as log().
    bool print(Level level, std::string_view message);
    bool print(Level level, const QString& message);
    bool print(Level level, const char* message);
    bool flush();

private:
    struct Output {
        std::ostream* stream;
        bool enableColor;
    };

    Logger() = default;
    ~Logger() = default;

    // Caller must hold mutex_.
    bool writeToOutputs(std::string_view plain, std::string_view colored);

    std::vector<Output> outputs_;
    Level minimumLevel_ = Level::Debug;
    std::mutex mutex_;
};

#endif // CLASSTOPLAND_NEXT_LOGGER_H
