#include "ProfileManager.h"
#include "ProfileExchange.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
bool readProfileFile(const QString& path, Profile& result, QString* error)
{
    return ProfileExchange::readFile(path, ProfileExchange::Format::NativeJson, result, nullptr, error);
}
bool writeProfileFile(const QString& path, const Profile& candidate, QString* error)
{
    if (path.isEmpty()) return fail(error, QStringLiteral("No profile file path has been loaded"));
    QByteArray contents;
    if (!ProfileExchange::serialize(candidate, ProfileExchange::Format::NativeJson, contents, nullptr, error)) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(error, QStringLiteral("Cannot create profile directory"));
    return ProfileExchange::writeFile(path, contents, error);
}
} // namespace

ProfileManager& ProfileManager::instance()
{
    static ProfileManager manager;
    return manager;
}

bool ProfileManager::load(const QString& filePath, QString* error)
{
    if (error) error->clear();
    if (filePath.trimmed().isEmpty()) return fail(error, QStringLiteral("Profile file path is empty"));
    const QString path = QFileInfo(filePath).absoluteFilePath();
    if (!QFileInfo::exists(path)) {
        profile_ = Profile{};
        filePath_ = path;
        return true;
    }

    Profile loaded;
    if (!readProfileFile(path, loaded, error)) return false;
    profile_ = std::move(loaded);
    filePath_ = path;
    return true;
}

bool ProfileManager::save(QString* error) const
{
    return writeProfile(profile_, error);
}

bool ProfileManager::commit(const Profile& candidate, QString* error)
{
    Profile committed = candidate;
    if (!writeProfile(committed, error)) return false;
    profile_ = std::move(committed);
    return true;
}

bool ProfileManager::selectWeekSchedule(const QString& id, QString* error)
{
    if (error) error->clear();
    if (!id.isEmpty()) {
        bool found = false;
        for (const auto& week : profile_.schedules) if (week.id == id) { found = true; break; }
        if (!found) return fail(error, QStringLiteral("Selected week schedule does not exist"));
    }
    if (profile_.activeWeekScheduleId == id) return true;
    Profile candidate = profile_;
    candidate.activeWeekScheduleId = id;
    return commit(candidate, error);
}

bool ProfileManager::writeProfile(const Profile& candidate, QString* error) const
{
    return writeProfileFile(filePath_, candidate, error);
}

QString ProfileManager::profilesDirectory() const
{
    return filePath_.isEmpty() ? QString() : QFileInfo(filePath_).absolutePath();
}

QString ProfileManager::managedPath(const QString& id, QString* error) const
{
    if (filePath_.isEmpty()) {
        fail(error, QStringLiteral("No profile file path has been loaded"));
        return {};
    }
    if (id.trimmed().isEmpty() || id == QStringLiteral(".") || id == QStringLiteral("..") ||
        id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\')) || id.contains(QLatin1Char(':'))) {
        fail(error, QStringLiteral("Invalid profile file name"));
        return {};
    }
    const QString path = QDir(profilesDirectory()).absoluteFilePath(id + QStringLiteral(".json"));
    if (QFileInfo(path).isSymLink()) {
        fail(error, QStringLiteral("Profile symbolic links are not supported"));
        return {};
    }
    return path;
}

QList<ProfileManager::Entry> ProfileManager::listProfiles(QString* error) const
{
    if (error) error->clear();
    QList<Entry> entries;
    if (filePath_.isEmpty()) {
        fail(error, QStringLiteral("No profile file path has been loaded"));
        return entries;
    }
    const QDir directory(profilesDirectory());
    if (!directory.exists() || !QFileInfo(directory.absolutePath()).isReadable()) {
        fail(error, QStringLiteral("Profile directory is missing or unreadable"));
        return entries;
    }
    const auto files = directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const auto& file : files) {
        Entry entry;
        entry.id = file.completeBaseName();
        entry.filePath = file.absoluteFilePath();
        entry.current = file.absoluteFilePath().compare(filePath_, Qt::CaseInsensitive) == 0;
        Profile data;
        if (readProfile(entry.id, data, &entry.error)) entry.name = data.name;
        if (entry.name.trimmed().isEmpty()) entry.name = entry.id;
        entries.append(std::move(entry));
    }
    return entries;
}

bool ProfileManager::readProfile(const QString& id, Profile& result, QString* error) const
{
    if (error) error->clear();
    const QString path = managedPath(id, error);
    if (path.isEmpty()) return false;
    return readProfileFile(path, result, error);
}

bool ProfileManager::createProfile(const QString& name, bool copyCurrent, QString& createdId, QString* error) const
{
    if (error) error->clear();
    createdId.clear();
    if (name.trimmed().isEmpty()) return fail(error, QStringLiteral("档案名称不能为空"));
    if (filePath_.isEmpty()) return fail(error, QStringLiteral("No profile file path has been loaded"));
    Profile candidate;
    if (copyCurrent && !readProfile(QFileInfo(filePath_).completeBaseName(), candidate, error)) return false;
    candidate.name = name.trimmed();
    return createProfile(candidate, createdId, error);
}

bool ProfileManager::createProfile(const Profile& candidate, QString& createdId, QString* error) const
{
    if (error) error->clear();
    createdId.clear();
    if (candidate.name.trimmed().isEmpty()) return fail(error, QStringLiteral("档案名称不能为空"));
    if (filePath_.isEmpty()) return fail(error, QStringLiteral("No profile file path has been loaded"));
    if (!QDir().mkpath(profilesDirectory())) return fail(error, QStringLiteral("Cannot create profile directory"));
    QString id, path;
    do {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        path = managedPath(id, error);
        if (path.isEmpty()) return false;
    } while (QFileInfo::exists(path));
    // Reserve the new name exclusively before the atomic write, never overwriting
    // another existing profile, even if a file appeared after the existence check.
    QFile reservation(path);
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail(error, reservation.errorString());
    reservation.close();
    if (!writeProfileFile(path, candidate, error)) {
        QFile::remove(path);
        return false;
    }
    createdId = id;
    return true;
}

bool ProfileManager::removeProfile(const QString& id, QString* error) const
{
    if (error) error->clear();
    const QString path = managedPath(id, error);
    if (path.isEmpty()) return false;
    if (path.compare(filePath_, Qt::CaseInsensitive) == 0)
        return fail(error, QStringLiteral("当前档案不能删除，请先切换到其他档案"));
    QFile file(path);
    if (!file.remove()) return fail(error, file.errorString());
    return true;
}

bool ProfileManager::isManagedExportPath(const QString& path) const
{
    const QFileInfo target(path);
    const QString absolute = QDir::cleanPath(target.absoluteFilePath());
    if (absolute.compare(QDir::cleanPath(filePath_), Qt::CaseInsensitive) == 0) return true;
    const QFileInfo parent(target.absolutePath());
    const QFileInfo managed(profilesDirectory());
    const QString parentPath = parent.canonicalFilePath().isEmpty() ? parent.absoluteFilePath() : parent.canonicalFilePath();
    const QString managedDirectory = managed.canonicalFilePath().isEmpty() ? managed.absoluteFilePath() : managed.canonicalFilePath();
    if (parentPath.compare(managedDirectory, Qt::CaseInsensitive) == 0 &&
        target.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) return true;
    if (!target.canonicalFilePath().isEmpty()) {
        const QFileInfo resolved(target.canonicalFilePath());
        return resolved.absoluteFilePath().compare(QFileInfo(filePath_).canonicalFilePath(), Qt::CaseInsensitive) == 0 ||
            (resolved.absolutePath().compare(managedDirectory, Qt::CaseInsensitive) == 0 &&
             resolved.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0);
    }
    return false;
}
