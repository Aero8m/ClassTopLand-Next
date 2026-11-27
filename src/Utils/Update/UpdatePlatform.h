#pragma once

#include <QString>
#include <QStringList>

struct UpdateInstaller;

namespace UpdatePlatform {
bool installed();
bool installationBusy();
QString installationError(const QString& stageName);
void cleanCompleted();

class Handoff final {
public:
    Handoff() = default;
    ~Handoff();
    Handoff(const Handoff&) = delete;
    Handoff& operator=(const Handoff&) = delete;

    bool prepare(const UpdateInstaller& installer, const QStringList& arguments,
                 const QString& workingDirectory, bool restartAfterInstall, QString* error);
    void commit();

private:
    void* commit_ = nullptr;
};
}
