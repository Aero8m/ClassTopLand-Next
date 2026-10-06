#include "ProfileManager.h"
#include "ProfileExchange.h"
#include "../DateSchedule/DateSchedule.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>
#include <algorithm>

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

bool ProfileManager::commit(const Profile& candidate, QString* error, QList<QDate>* canceledDates)
{
    if (error) error->clear();
    if (canceledDates) canceledDates->clear();
    if (!DateSchedule::validateOverrides(candidate.dateOverrides, error)) return false;
    Profile committed = candidate;
    QList<QDate> canceled;
    for (qsizetype i = committed.dateOverrides.size(); i-- > 0;) {
        const auto& record = committed.dateOverrides[i];
        if (record.date.isValid() && record.date < QDate::currentDate()) {
            committed.dateOverrides.removeAt(i);
        } else if (record.date.isValid() && !DateSchedule::sourceMatches(committed, record)) {
            canceled.append(record.date);
            committed.dateOverrides.removeAt(i);
        }
    }
    if (!writeProfile(committed, error)) return false;
    profile_ = std::move(committed);
    std::sort(canceled.begin(), canceled.end());
    if (canceledDates) *canceledDates = canceled;
    return true;
}

bool ProfileManager::setDateReschedule(const QDate& date, const QString& weekId, int weekday, QString* error)
{
    if (!date.isValid() || date < QDate::currentDate()) return fail(error, QStringLiteral("请选择今天或未来日期"));
    const auto* day = DateSchedule::sourceDay(profile_, weekId, weekday);
    if (!day) return fail(error, QStringLiteral("请选择有效的来源课表和星期"));
    if (!DateSchedule::validateClasses(day->classes, error)) return false;
    Profile candidate = profile_;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
        if (candidate.dateOverrides[i].date == date) candidate.dateOverrides.removeAt(i);
    const auto courses = DateSchedule::sorted(day->classes);
    candidate.dateOverrides.append({date, weekId, weekday, courses, courses});
    return commit(candidate, error);
}

bool ProfileManager::swapDateClasses(const QDate& date, const QString& weekId, int first, int second, QString* error)
{
    if (!date.isValid() || date < QDate::currentDate()) return fail(error, QStringLiteral("请选择今天或未来日期"));
    auto table = DateSchedule::resolve(profile_, date, weekId);
    if (!table.valid) return fail(error, table.error);
    if (first < 0 || second < 0 || first == second || first >= table.classes.size() || second >= table.classes.size())
        return fail(error, QStringLiteral("请选择两节不同的课程"));
    if (table.classes[first].subject == table.classes[second].subject)
        return fail(error, QStringLiteral("两节课的科目相同，无需交换"));
    const auto* day = DateSchedule::sourceDay(profile_, table.sourceWeekScheduleId, table.sourceWeekday);
    if (!day) return fail(error, QStringLiteral("来源日课表不存在"));
    DateScheduleOverride record{date, table.sourceWeekScheduleId, table.sourceWeekday,
                               DateSchedule::sorted(day->classes), table.classes};
    std::swap(record.classes[first].subject, record.classes[second].subject);
    Profile candidate = profile_;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
        if (candidate.dateOverrides[i].date == date) candidate.dateOverrides.removeAt(i);
    candidate.dateOverrides.append(record);
    return commit(candidate, error);
}

bool ProfileManager::restoreDate(const QDate& date, QString* error)
{
    if (error) error->clear();
    if (!DateSchedule::find(profile_, date)) return true;
    Profile candidate = profile_;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
        if (candidate.dateOverrides[i].date == date) candidate.dateOverrides.removeAt(i);
    return commit(candidate, error);
}

bool ProfileManager::cleanPastDateOverrides(const QDate& today, QString* error, QList<QDate>* canceledDates)
{
    if (error) error->clear();
    if (canceledDates) canceledDates->clear();
    if (!today.isValid()) return fail(error, QStringLiteral("清理日期无效"));
    Profile candidate = profile_;
    QList<QDate> canceled;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;) {
        const auto& record = candidate.dateOverrides[i];
        if (record.date < today || !DateSchedule::sourceMatches(candidate, record)) {
            if (record.date >= today) canceled.append(record.date);
            candidate.dateOverrides.removeAt(i);
        }
    }
    if (candidate.dateOverrides.size() == profile_.dateOverrides.size()) return true;
    // Do not call commit: its wall-clock filter would defeat the injected date.
    if (!writeProfile(candidate, error)) return false;
    profile_ = std::move(candidate);
    std::sort(canceled.begin(), canceled.end());
    if (canceledDates) *canceledDates = canceled;
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
