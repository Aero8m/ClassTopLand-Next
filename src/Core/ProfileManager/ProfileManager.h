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
    // Write the candidate atomically before publishing it to readers.
    bool commit(const Profile& candidate, QString* error = nullptr);
    // Persist first; a failed write never changes the published selection.
    bool selectWeekSchedule(const QString& id, QString* error = nullptr);

    struct Entry {
        QString id; // File stem, independent of the editable display name.
        QString name;
        QString filePath;
        QString error;
        bool current = false;
        bool isValid() const { return error.isEmpty(); }
    };
    QString profilesDirectory() const;
    QList<Entry> listProfiles(QString* error = nullptr) const;
    // These operations never replace the running profile. Missing files are errors.
    bool readProfile(const QString& id, Profile& result, QString* error = nullptr) const;
    bool createProfile(const QString& name, bool copyCurrent, QString& createdId,
                       QString* error = nullptr) const;
    bool createProfile(const Profile& candidate, QString& createdId,
                       QString* error = nullptr) const;
    bool isManagedExportPath(const QString& path) const;
    bool removeProfile(const QString& id, QString* error = nullptr) const;

private:
    ProfileManager() = default;
    bool writeProfile(const Profile& candidate, QString* error) const;
    QString managedPath(const QString& id, QString* error) const;

    Profile profile_{};
    QString filePath_;
};

#endif // CLASSTOPLAND_NEXT_PROFILEMANAGER_H
