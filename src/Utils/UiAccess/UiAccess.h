#pragma once

#include <QString>
#include <QStringList>
#include <memory>

// No Windows types escape this interface. The broker runs before QApplication;
// a successor acknowledges Qt initialization before loading any business data.
namespace UiAccess {
bool supported();
bool enabled();
struct Handshake;
struct Startup {
    QStringList arguments; // User arguments only, without argv[0].
    QString dataDirectory;
    bool exitRequested = false;
    int exitCode = 0;
    std::shared_ptr<Handshake> handshake;
};
Startup inspectStartup(int argc, char** argv, const QString& defaultDataDirectory);
bool finishStartup(const Startup& startup, QString* error);

class Restart final {
public:
    Restart();
    ~Restart();
    Restart(const Restart&) = delete;
    Restart& operator=(const Restart&) = delete;
    bool prepare(bool enable, const QString& dataDirectory,
                 const QStringList& arguments, QString* error);
    void commit(); // Call after saving and destroying the old business objects.
    void cancel();
private:
    std::shared_ptr<Handshake> handshake_;
    bool committed_ = false;
};
}
