#include "Core/ConfigManager/ConfigManager.h"
#include "Core/DateSchedule/DateSchedule.h"
#include "Core/ProfileManager/ProfileExchange.h"
#include "Core/ProfileManager/ProfileManager.h"
#include "test_profile.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

DateScheduleOverride adjustment(const Profile& profile, const QDate& date, int weekday)
{
    const auto& classes = profile.schedules.first().daySchedules.at(weekday - 1).classes;
    return {date, TestProfiles::weekId, weekday, classes, {}};
}
}

class PersistenceTest : public QObject
{
    Q_OBJECT

private slots:
    void configDefaultsCommitAndCorruptLoad();
    void migrateLegacyConfig();
    void profileDefaultsCommitAndCorruptLoad();
    void writeFailureKeepsPublishedState();
    void cleanupKeepsTodayAndFuture();
    void sourceChangesCancelOnlyAffectedDates();
};

void PersistenceTest::configDefaultsCommitAndCorruptLoad()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto path = directory.filePath(QStringLiteral("MainConfig.json"));
    auto& manager = ConfigManager::instance();
    QString error;
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.config().profileName, QStringLiteral("Default"));
    QVERIFY(!manager.config().automaticUpdates);
    QVERIFY(!QFile::exists(path));

    QSignalSpy committed(&manager, &ConfigManager::committed);
    QSignalSpy courseBarChanged(&manager, &ConfigManager::courseBarChanged);
    const auto revision = manager.revision();
    Config candidate = manager.config();
    candidate.profileName = QStringLiteral("Classroom");
    candidate.automaticUpdates = true;
    candidate.courseBarConfig.enable = false;
    candidate.appearanceConfig.themeColor = QStringLiteral("#ABCDEF");
    candidate.reminderConfig.advanceMinutes = 5;
    QVERIFY2(manager.commit(candidate, &error), qPrintable(error));
    QCOMPARE(manager.revision(), revision + 1);
    QCOMPARE(committed.size(), 1);
    QCOMPARE(committed.first().first().toULongLong(), manager.revision());
    QCOMPARE(courseBarChanged.size(), 1);

    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.config().profileName, candidate.profileName);
    QVERIFY(manager.config().automaticUpdates);
    QVERIFY(!manager.config().courseBarConfig.enable);
    QCOMPARE(manager.config().appearanceConfig.themeColor, QStringLiteral("#abcdef"));
    QCOMPARE(manager.config().reminderConfig.advanceMinutes, 5);

    const QByteArray broken("{broken");
    const auto corruptPath = directory.filePath(QStringLiteral("corrupt.json"));
    QVERIFY(writeBytes(corruptPath, broken));
    const auto beforeCorruptLoad = manager.revision();
    committed.clear();
    courseBarChanged.clear();
    QVERIFY(!manager.load(corruptPath, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(manager.revision(), beforeCorruptLoad);
    QCOMPARE(manager.filePath(), path);
    QCOMPARE(manager.config().profileName, candidate.profileName);
    QVERIFY(committed.isEmpty());
    QVERIFY(courseBarChanged.isEmpty());
    QCOMPARE(readBytes(corruptPath), broken);
}

void PersistenceTest::migrateLegacyConfig()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto path = directory.filePath(QStringLiteral("MainConfig.json"));
    QVERIFY(writeBytes(path, R"json({
        "version": 1,
        "config": {
            "profileName": "Legacy",
            "courseBarConfig": {"spacing": 21, "components": [{"type": "courseView", "enabled": false}]}
        }
    })json"));
    auto& manager = ConfigManager::instance();
    QString error;
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.config().profileName, QStringLiteral("Legacy"));
    QCOMPARE(manager.config().reminderConfig.advanceMinutes, 2);
    QCOMPARE(manager.config().courseBarConfig.components.size(), 1);
    const auto component = manager.config().courseBarConfig.components.first();
    QCOMPARE(component.type, CourseBarComponentType::CourseView);
    QCOMPARE(component.courseSpacing, 21);
    QVERIFY(!component.enabled);
    QVERIFY(!component.instanceId.isEmpty());

    const auto saved = QJsonDocument::fromJson(readBytes(path)).object();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 2);
    const auto components = saved.value(QStringLiteral("config")).toObject()
        .value(QStringLiteral("courseBarConfig")).toObject().value(QStringLiteral("components")).toArray();
    QCOMPARE(components.size(), 1);
    const auto savedComponent = components.first().toObject();
    QCOMPARE(savedComponent.value(QStringLiteral("typeId")).toString(), QStringLiteral("builtin/courseView"));
    QCOMPARE(savedComponent.value(QStringLiteral("instanceId")).toString(), component.instanceId);
    QCOMPARE(savedComponent.value(QStringLiteral("courseSpacing")).toInt(), 21);
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.config().courseBarConfig.components.size(), 1);
    QCOMPARE(manager.config().courseBarConfig.components.first().instanceId, component.instanceId);
}

void PersistenceTest::profileDefaultsCommitAndCorruptLoad()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto path = directory.filePath(QStringLiteral("profile.json"));
    auto& manager = ProfileManager::instance();
    QString error;
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QVERIFY(manager.profile().name.isEmpty());
    QVERIFY(manager.profile().schedules.isEmpty());
    QVERIFY(!QFile::exists(path));

    QSignalSpy committed(&manager, &ProfileManager::committed);
    const auto revision = manager.revision();
    const Profile candidate = TestProfiles::regular();
    QVERIFY2(manager.commit(candidate, &error), qPrintable(error));
    QCOMPARE(manager.revision(), revision + 1);
    QCOMPARE(committed.size(), 1);
    QCOMPARE(committed.first().first().toULongLong(), manager.revision());
    QVERIFY2(manager.selectWeekSchedule(TestProfiles::weekId, &error), qPrintable(error));
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.profile().name, candidate.name);
    QCOMPARE(manager.profile().activeWeekScheduleId, TestProfiles::weekId);
    QCOMPARE(manager.profile().subjects.size(), 3);
    QCOMPARE(manager.profile().subjects.first().teacher, candidate.subjects.first().teacher);

    const QByteArray broken(R"json({"version":1,"profile":[]})json");
    const auto corruptPath = directory.filePath(QStringLiteral("corrupt.json"));
    QVERIFY(writeBytes(corruptPath, broken));
    const auto beforeCorruptLoad = manager.revision();
    committed.clear();
    QVERIFY(!manager.load(corruptPath, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(manager.revision(), beforeCorruptLoad);
    QCOMPARE(manager.filePath(), path);
    QCOMPARE(manager.profile().name, candidate.name);
    QCOMPARE(manager.profile().activeWeekScheduleId, TestProfiles::weekId);
    QVERIFY(committed.isEmpty());
    QCOMPARE(readBytes(corruptPath), broken);
}

void PersistenceTest::writeFailureKeepsPublishedState()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto configPath = directory.filePath(QStringLiteral("storage/MainConfig.json"));
    const auto profilePath = directory.filePath(QStringLiteral("storage/profile.json"));
    auto& config = ConfigManager::instance();
    auto& profiles = ProfileManager::instance();
    QString error;
    QVERIFY2(config.load(configPath, &error), qPrintable(error));
    QVERIFY2(profiles.load(profilePath, &error), qPrintable(error));
    Config initial = config.config();
    initial.profileName = QStringLiteral("Saved");
    QVERIFY2(config.commit(initial, &error), qPrintable(error));
    const Profile initialProfile = TestProfiles::regular();
    QVERIFY2(profiles.commit(initialProfile, &error), qPrintable(error));
    const auto configBytes = readBytes(configPath);
    const auto profileBytes = readBytes(profilePath);
    QVERIFY(!configBytes.isEmpty());
    QVERIFY(!profileBytes.isEmpty());

    // A file in place of the parent directory forces a write failure without relying on ACLs.
    QVERIFY(QDir(directory.path()).rename(QStringLiteral("storage"), QStringLiteral("backup")));
    const auto blockedPath = directory.filePath(QStringLiteral("storage"));
    const QByteArray blocker("parent is a file");
    QVERIFY(writeBytes(blockedPath, blocker));

    QSignalSpy configCommits(&config, &ConfigManager::committed);
    QSignalSpy courseBarChanged(&config, &ConfigManager::courseBarChanged);
    QSignalSpy profileCommits(&profiles, &ProfileManager::committed);
    const auto configRevision = config.revision();
    const auto profileRevision = profiles.revision();
    Config candidate = initial;
    candidate.profileName = QStringLiteral("Unsaved");
    candidate.courseBarConfig.enable = false;
    Profile candidateProfile = initialProfile;
    candidateProfile.name = QStringLiteral("Unsaved profile");

    QVERIFY(!config.commit(candidate, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!profiles.commit(candidateProfile, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(config.config().profileName, initial.profileName);
    QCOMPARE(config.config().courseBarConfig.enable, initial.courseBarConfig.enable);
    QCOMPARE(profiles.profile().name, initialProfile.name);
    QCOMPARE(config.revision(), configRevision);
    QCOMPARE(profiles.revision(), profileRevision);
    QVERIFY(configCommits.isEmpty());
    QVERIFY(courseBarChanged.isEmpty());
    QVERIFY(profileCommits.isEmpty());
    QCOMPARE(readBytes(blockedPath), blocker);
    QCOMPARE(readBytes(directory.filePath(QStringLiteral("backup/MainConfig.json"))), configBytes);
    QCOMPARE(readBytes(directory.filePath(QStringLiteral("backup/profile.json"))), profileBytes);
}

void PersistenceTest::cleanupKeepsTodayAndFuture()
{
    const QDate today(2026, 9, 7);
    const auto yesterday = today.addDays(-1);
    const auto tomorrow = today.addDays(1);
    Profile profile = TestProfiles::regular();
    profile.dateOverrides = {adjustment(profile, yesterday, 1), adjustment(profile, today, 1),
                             adjustment(profile, tomorrow, 2)};

    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto path = directory.filePath(QStringLiteral("profile.json"));
    QString error;
    QByteArray contents;
    using ProfileExchange::Format;
    QVERIFY2(ProfileExchange::serialize(profile, Format::NativeJson, contents, nullptr, &error), qPrintable(error));
    QVERIFY(writeBytes(path, contents));
    auto& manager = ProfileManager::instance();
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.profile().dateOverrides.size(), 3);
    const auto revision = manager.revision();

    QVERIFY2(manager.cleanPastDateOverrides(today, &error), qPrintable(error));
    QCOMPARE(manager.revision(), revision + 1);
    QVERIFY2(manager.cleanPastDateOverrides(today, &error), qPrintable(error));
    QCOMPARE(manager.revision(), revision + 1);
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.profile().dateOverrides.size(), 2);
    QVERIFY(!DateSchedule::find(manager.profile(), yesterday));
    QVERIFY(DateSchedule::find(manager.profile(), today));
    QVERIFY(DateSchedule::find(manager.profile(), tomorrow));
}

void PersistenceTest::sourceChangesCancelOnlyAffectedDates()
{
    const auto firstDate = QDate::currentDate().addDays(14);
    const auto secondDate = firstDate.addDays(1);
    Profile profile = TestProfiles::regular();
    profile.dateOverrides = {adjustment(profile, firstDate, 1), adjustment(profile, secondDate, 2)};

    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const auto path = directory.filePath(QStringLiteral("profile.json"));
    auto& manager = ProfileManager::instance();
    QString error;
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QVERIFY2(manager.commit(profile, &error), qPrintable(error));

    QList<QDate> canceled;
    auto& monday = profile.schedules[0].getDaySchedule(0);
    monday.name = QStringLiteral("Renamed Monday");
    std::reverse(monday.classes.begin(), monday.classes.end());
    QVERIFY2(manager.commit(profile, &error, &canceled), qPrintable(error));
    QVERIFY(canceled.isEmpty());
    QCOMPARE(manager.profile().dateOverrides.size(), 2);

    monday.classes.last().endTime = QTime(8, 46);
    QVERIFY2(manager.commit(profile, &error, &canceled), qPrintable(error));
    QCOMPARE(canceled, QList<QDate>{firstDate});
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QCOMPARE(manager.profile().dateOverrides.size(), 1);
    QCOMPARE(manager.profile().dateOverrides.first().date, secondDate);
}

QTEST_GUILESS_MAIN(PersistenceTest)
#include "persistence.moc"
