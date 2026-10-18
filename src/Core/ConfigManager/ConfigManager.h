#ifndef CLASSTOPLAND_NEXT_CONFIGMANAGER_H
#define CLASSTOPLAND_NEXT_CONFIGMANAGER_H

#include "../Model/Config.h"
#include <QObject>

class ConfigManager final : public QObject
{
    Q_OBJECT
public:
    static ConfigManager& instance();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    Config& config() noexcept { return config_; }
    const Config& config() const noexcept { return config_; }
    const QString& filePath() const noexcept { return filePath_; }

    // A missing file means a new, default config. A malformed file is an error.
    // Missing settings sections use the defaults from the model (including empty window rules).
    // The supplied path is retained only when loading succeeds.
    bool load(const QString& filePath, QString* error = nullptr);
    bool save(QString* error = nullptr) const;
    bool commit(const Config& candidate, QString* error = nullptr);
    quint64 revision() const { return revision_; }
signals:
    void committed(quint64 revision);
    void courseBarChanged();

private:
    ConfigManager() = default;
    bool writeConfig(const Config& candidate, QString* error) const;

    Config config_{};
    QString filePath_;
    quint64 revision_ = 0;
};

#endif // CLASSTOPLAND_NEXT_CONFIGMANAGER_H
