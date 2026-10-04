#ifndef CLASSTOPLAND_NEXT_PROFILEMANAGER_H
#define CLASSTOPLAND_NEXT_PROFILEMANAGER_H

#include "../Model/Profile.h"

class ProfileManager final
{
public:
    static ProfileManager& instance();

    ProfileManager(const ProfileManager&) = delete;
    ProfileManager& operator=(const ProfileManager&) = delete;

    Profile& profile() noexcept { return profile_; }
    const Profile& profile() const noexcept { return profile_; }
    const QString& filePath() const noexcept { return filePath_; }

    // A missing file means a new, empty profile. A malformed file is an error.
    // The supplied path is retained only when loading succeeds.
    bool load(const QString& filePath, QString* error = nullptr);
    bool save(QString* error = nullptr) const;

private:
    ProfileManager() = default;

    Profile profile_{};
    QString filePath_;
};

#endif // CLASSTOPLAND_NEXT_PROFILEMANAGER_H
