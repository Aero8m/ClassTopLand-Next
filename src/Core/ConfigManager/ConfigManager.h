#ifndef CLASSTOPLAND_NEXT_CONFIGMANAGER_H
#define CLASSTOPLAND_NEXT_CONFIGMANAGER_H

#include "../Model/Config.h"

class ConfigManager final
{
public:
    static ConfigManager& instance();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    Config& config() noexcept { return config_; }
    const Config& config() const noexcept { return config_; }
    const QString& filePath() const noexcept { return filePath_; }

    // A missing file means a new, default config. A malformed file is an error.
    // Missing course bar settings use the defaults from the model.
    // The supplied path is retained only when loading succeeds.
    bool load(const QString& filePath, QString* error = nullptr);
    bool save(QString* error = nullptr) const;

private:
    ConfigManager() = default;

    Config config_{};
    QString filePath_;
};

#endif // CLASSTOPLAND_NEXT_CONFIGMANAGER_H
