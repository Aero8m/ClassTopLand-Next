#include <QtTest>
#include <ElaApplication.h>
#include <ElaTheme.h>
#include <QImage>
#include "Core/ProfileManager/ProfileManager.h"
#include "Core/ConfigManager/ConfigManager.h"
#include "Core/CourseRefreshService/CourseRefreshService.h"
#include "ProfileEditWindow/ProfileEditSession.h"
#include "ProfileEditWindow/ProfileEditWindow.h"
#include "CourseBar/CourseBar.h"
#include "CourseBar/BuiltinComponents/CourseView/CourseView.h"
#include "TaskbarTrayMenu/TaskbarTrayMenu.h"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <memory>
#include <utility>

namespace {
Profile sample()
{
    Profile profile;
    profile.name = QStringLiteral("测试档案");
    profile.subjects = {Subject(QStringLiteral("数学")), Subject(QStringLiteral("语文"))};
    for (int i = 0; i < 2; ++i) {
        WeekSchedule week(i ? QStringLiteral("单周课表") : QStringLiteral("常规课表"),
                          i ? WeekScheduleMode::Odd : WeekScheduleMode::All);
        week.initDaySchedules();
        for (auto& day : week.daySchedules)
            day.classes.append(Class(i ? QStringLiteral("语文") : QStringLiteral("数学"), QTime(8, 0), QTime(8, 45)));
        profile.schedules.append(week);
    }
    return profile;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QAbstractButton* cardFor(TaskbarTrayMenu& menu, const QString& id)
{
    for (auto* button : menu.findChildren<QAbstractButton*>())
        if (button->property("scheduleId").isValid() && button->property("scheduleId").toString() == id) return button;
    return nullptr;
}

QList<TaskbarTrayMenu::ScheduleChoice> choicesFor(const Profile& profile)
{
    QList<TaskbarTrayMenu::ScheduleChoice> choices;
    for (const auto& week : profile.schedules) choices.append({week.id, week.name, QStringLiteral("全部周 · 今日 1 节")});
    return choices;
}
}

class ScheduleSwitchTests : public QObject
{
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> directory_;
    QString path_;
    bool writeJson(const QJsonObject& object)
    {
        QFile file(path_);
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) > 0;
    }

private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        // The offscreen Windows plugin has no system-font discovery.
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc")) >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf")) >= 0);
        }
#endif
        eApp->init();
        eApp->setWindowDisplayMode(ElaApplicationType::Normal);
        qApp->setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
    }

    void init()
    {
        directory_ = std::make_unique<QTemporaryDir>();
        QVERIFY(directory_->isValid());
        path_ = directory_->filePath(QStringLiteral("profile.json"));
        QVERIFY(ProfileManager::instance().load(path_));
        ConfigManager::instance().config() = Config{};
    }

    void legacyMigrationDoesNotWriteUntilSave()
    {
        auto& manager = ProfileManager::instance();
        QVERIFY(manager.commit(sample()));
        auto root = QJsonDocument::fromJson(readFile(path_)).object();
        auto profile = root["profile"].toObject();
        profile.remove("activeWeekScheduleId");
        auto schedules = profile["schedules"].toArray();
        for (int i = 0; i < schedules.size(); ++i) {
            auto week = schedules[i].toObject(); week.remove("id"); schedules[i] = week;
        }
        profile["schedules"] = schedules; root["profile"] = profile;
        QVERIFY(writeJson(root));
        const auto original = readFile(path_);
        QVERIFY(manager.load(path_));
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
        const auto id = manager.profile().schedules[0].id;
        QVERIFY(!id.isEmpty());
        QVERIFY(id != manager.profile().schedules[1].id);
        QCOMPARE(readFile(path_), original);
        QVERIFY(manager.save());
        QVERIFY(manager.load(path_));
        QCOMPARE(manager.profile().schedules[0].id, id);
        QCOMPARE(QJsonDocument::fromJson(readFile(path_)).object()["version"].toInt(), 1);
    }

    void duplicateAndEmptyIdsAreRepaired()
    {
        auto& manager = ProfileManager::instance();
        QVERIFY(manager.commit(sample()));
        auto root = QJsonDocument::fromJson(readFile(path_)).object();
        auto profile = root["profile"].toObject();
        auto schedules = profile["schedules"].toArray();
        auto first = schedules[0].toObject(); first["id"] = "duplicate";
        auto second = schedules[1].toObject(); second["id"] = "duplicate";
        auto third = first; third["id"] = "";
        schedules = QJsonArray{first, second, third};
        profile["schedules"] = schedules; profile["activeWeekScheduleId"] = "duplicate";
        root["profile"] = profile; QVERIFY(writeJson(root));
        QVERIFY(manager.load(path_));
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
        QSet<QString> ids;
        for (const auto& week : manager.profile().schedules) { QVERIFY(!week.id.isEmpty()); ids.insert(week.id); }
        QCOMPARE(ids.size(), 3);
        QVERIFY(!ids.contains(QStringLiteral("duplicate")));
    }

    void missingSelectionFallsBackAndInvalidTypesFail()
    {
        auto& manager = ProfileManager::instance();
        QVERIFY(manager.commit(sample()));
        auto root = QJsonDocument::fromJson(readFile(path_)).object();
        auto profile = root["profile"].toObject(); profile["activeWeekScheduleId"] = "deleted";
        root["profile"] = profile; QVERIFY(writeJson(root)); QVERIFY(manager.load(path_));
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
        profile["activeWeekScheduleId"] = 1; root["profile"] = profile;
        QVERIFY(writeJson(root)); QString error; QVERIFY(!manager.load(path_, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
    }

    void selectionSurvivesRenameReorderAndRestart()
    {
        auto& manager = ProfileManager::instance();
        auto profile = sample();
        profile.schedules[1].name = profile.schedules[0].name; // Names are not identity.
        QVERIFY(manager.commit(profile));
        const auto selected = profile.schedules[1].id;
        QVERIFY(manager.selectWeekSchedule(selected));
        profile = manager.profile();
        profile.schedules[1].name = QStringLiteral("改名后的课表");
        profile.schedules.swapItemsAt(0, 1);
        QVERIFY(manager.commit(profile));
        QVERIFY(manager.load(path_));
        QCOMPARE(manager.profile().activeWeekScheduleId, selected);
        QDateTime now(QDate(2026, 10, 5), QTime(8, 10));
        CourseRefreshService service([&] { return now; }); service.start(); service.stop();
        QCOMPARE(service.table().weekScheduleIndex, 0);
        QCOMPARE(service.table().name, QStringLiteral("改名后的课表"));
        QCOMPARE(service.table().classes[0].subject, QStringLiteral("语文"));
        QCOMPARE(service.state().remainingSeconds, qint64(35 * 60));
    }

    void failedSaveAndRepeatedSelectionDoNotPublish()
    {
        auto& manager = ProfileManager::instance();
        auto profile = sample(); profile.activeWeekScheduleId = profile.schedules[0].id;
        // A directory at the target filename deterministically prevents QSaveFile.
        QVERIFY(QDir().mkdir(path_)); manager.profile() = profile;
        QString error;
        QVERIFY(!manager.selectWeekSchedule(profile.schedules[1].id, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(manager.profile().activeWeekScheduleId, profile.activeWeekScheduleId);
        QVERIFY(QFileInfo(path_).isDir());
        QVERIFY(manager.selectWeekSchedule(profile.activeWeekScheduleId, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!manager.selectWeekSchedule(QStringLiteral("missing"), &error));
        QCOMPARE(manager.profile().activeWeekScheduleId, profile.activeWeekScheduleId);
    }

    void editorDraftKeepsEditsAndLatestTraySelection()
    {
        auto& manager = ProfileManager::instance(); const auto profile = sample(); QVERIFY(manager.commit(profile));
        ProfileEditSession session; session.reset(manager.profile());
        session.edit().name = QStringLiteral("尚未保存的名称"); session.notifyChanged();
        QVERIFY(manager.selectWeekSchedule(profile.schedules[1].id));
        session.syncActiveWeekSchedule(profile.schedules[1].id);
        QVERIFY(session.isDirty()); QCOMPARE(session.draft().name, QStringLiteral("尚未保存的名称"));
        QCOMPARE(manager.profile().name, profile.name);
        QCOMPARE(session.draft().activeWeekScheduleId, profile.schedules[1].id);
        QVERIFY(manager.commit(session.draft())); session.markSaved();
        QCOMPARE(manager.profile().activeWeekScheduleId, profile.schedules[1].id);
        session.edit().name = QStringLiteral("放弃这个名称"); session.notifyChanged();
        QVERIFY(manager.selectWeekSchedule(profile.schedules[0].id)); session.syncActiveWeekSchedule(profile.schedules[0].id);
        session.discard();
        QVERIFY(!session.isDirty()); QCOMPARE(session.draft().name, QStringLiteral("尚未保存的名称"));
        QCOMPARE(session.draft().activeWeekScheduleId, profile.schedules[0].id);
        session.syncActiveWeekSchedule({}); QVERIFY(!session.isDirty());
    }

    void deletingSelectedDraftIsDeferredUntilSave()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample();
        profile.activeWeekScheduleId = profile.schedules[1].id; QVERIFY(manager.commit(profile));
        ProfileEditSession session; session.reset(manager.profile());
        session.edit().schedules.removeAt(1); session.notifyChanged();
        QVERIFY(session.draft().activeWeekScheduleId.isEmpty());
        QCOMPARE(manager.profile().activeWeekScheduleId, profile.activeWeekScheduleId);
        session.syncActiveWeekSchedule(profile.activeWeekScheduleId);
        QVERIFY(session.draft().activeWeekScheduleId.isEmpty()); QVERIFY(session.isDirty());
        session.discard(); QCOMPARE(session.draft().activeWeekScheduleId, profile.activeWeekScheduleId);
        session.edit().schedules.removeAt(1); session.notifyChanged();
        QVERIFY(manager.commit(session.draft())); session.markSaved();
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
    }

    void manualSelectionBypassesWeekModeAndTracksWeekday()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample();
        profile.schedules[1].daySchedules[1].classes.clear();
        profile.activeWeekScheduleId = profile.schedules[1].id; QVERIFY(manager.commit(profile));
        QDateTime now(QDate(2026, 10, 5), QTime(8, 10));
        CourseRefreshService service([&] { return now; }); service.start(); service.stop();
        QVERIFY(service.table().valid); QCOMPARE(service.table().weekScheduleIndex, 1);
        now = now.addDays(1); service.refresh();
        QVERIFY(service.table().valid); QVERIFY(service.table().classes.isEmpty());
        QCOMPARE(service.table().weekScheduleIndex, 1);
        QCOMPARE(service.state().phase, CourseRefreshService::Phase::NoClasses);
        service.setWeekScheduleIndex(0); QCOMPARE(service.table().weekScheduleIndex, 0);
        service.setWeekScheduleIndex(-1); QCOMPARE(service.table().weekScheduleIndex, 1);
    }

    void automaticErrorsAndUniqueMatch()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample(); QVERIFY(manager.commit(profile));
        QDateTime now(QDate(2026, 10, 5), QTime(8, 10));
        CourseRefreshService service([&] { return now; }); service.start(); service.stop();
        QVERIFY(!service.table().valid); QVERIFY(service.table().error.contains(QStringLiteral("reference date")));
        profile.schedules[1].mode = WeekScheduleMode::All; QVERIFY(manager.commit(profile)); service.reloadTable();
        QVERIFY(!service.table().valid); QVERIFY(service.table().error.contains(QStringLiteral("Multiple")));
        QVERIFY(manager.selectWeekSchedule(profile.schedules[1].id)); service.reloadTable(); QVERIFY(service.table().valid);
        QVERIFY(manager.selectWeekSchedule({})); service.reloadTable(); QVERIFY(!service.table().valid);
        profile.schedules.removeAt(1); QVERIFY(manager.commit(profile)); service.reloadTable();
        QVERIFY(service.table().valid); QCOMPARE(service.table().weekScheduleIndex, 0);
    }

    void switchingDoesNotReplayBoundaryEvents()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample();
        profile.activeWeekScheduleId = profile.schedules[0].id; QVERIFY(manager.commit(profile));
        QDateTime now(QDate(2026, 10, 5), QTime(7, 59, 59));
        CourseRefreshService service([&] { return now; }); QSignalSpy events(&service, &CourseRefreshService::eventOccurred);
        service.start(); service.stop();
        now = now.addSecs(1); QVERIFY(manager.selectWeekSchedule(profile.schedules[1].id)); service.reloadTable();
        QCOMPARE(events.count(), 0); QCOMPARE(service.state().phase, CourseRefreshService::Phase::InClass);
        now = now.addSecs(1); service.refresh(); QCOMPARE(events.count(), 0);
        QCOMPARE(service.table().classes[0].subject, QStringLiteral("语文"));
    }

    void allCourseViewsReloadAndOldNotificationsDisappear()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample();
        profile.activeWeekScheduleId = profile.schedules[0].id; QVERIFY(manager.commit(profile));
        ConfigManager::instance().config().courseBarConfig.components = {
            {CourseBarComponentType::CourseView, true}, {CourseBarComponentType::CourseView, true}};
        CourseBar bar;
        const auto views = bar.findChildren<CourseView*>(); QCOMPARE(views.size(), 2);
        const auto previousRevision = views[0]->refreshService()->table().revision;
        const auto old = views[0]->refreshService()->table();
        CourseRefreshService::Event event;
        event.tableRevision = old.revision; event.subject = QStringLiteral("数学");
        event.type = CourseRefreshService::EventType::ClassStarted;
        event.scheduledTime = QDateTime::currentDateTime();
        views[0]->refreshService()->eventOccurred(event);
        auto* notification = bar.findChild<QWidget*>(QStringLiteral("notificationLayer")); QVERIFY(notification);
        QVERIFY(!notification->isHidden());
        QVERIFY(manager.selectWeekSchedule(profile.schedules[1].id)); bar.reloadProfile();
        QVERIFY(notification->isHidden());
        for (auto* view : views) {
            QCOMPARE(view->refreshService()->table().weekScheduleIndex, 1);
            QVERIFY(view->refreshService()->table().revision > previousRevision);
        }
        QVERIFY(bar.hasCourseViews());
        QVERIFY(bar.scheduleStatus().contains(QStringLiteral("单周课表")));
        ConfigManager::instance().config().courseBarConfig.components.clear();
        CourseBar disabled; QVERIFY(!disabled.hasCourseViews()); QCOMPARE(disabled.scheduleStatus(), QStringLiteral("课程栏未启用"));
    }

    void menuCommitsBeforeShowingSelectionAndSupportsKeyboard()
    {
        auto& manager = ProfileManager::instance(); const auto profile = sample(); QVERIFY(manager.commit(profile));
        TaskbarTrayMenu menu; menu.setScheduleChoices(choicesFor(profile), {});
        QSignalSpy requests(&menu, &TaskbarTrayMenu::weekScheduleRequested);
        connect(&menu, &TaskbarTrayMenu::weekScheduleRequested, &menu, [&](const QString& id) {
            QString error;
            if (manager.selectWeekSchedule(id, &error)) menu.setScheduleChoices(choicesFor(manager.profile()), id);
            else menu.showScheduleError(error);
        });
        auto* first = cardFor(menu, profile.schedules[0].id); QVERIFY(first);
        QSignalSpy openings(&menu, &TaskbarTrayMenu::scheduleListRefreshRequested);
        menu.showMenu(); QTest::qWait(200); QVERIFY(menu.isVisible()); QCOMPARE(openings.count(), 1);
        first->click(); QVERIFY(first->isChecked()); QCOMPARE(requests.count(), 1);
        QTest::qWait(150); QVERIFY(menu.isVisible());
        first->click(); QCOMPARE(requests.count(), 1);
        auto* second = cardFor(menu, profile.schedules[1].id); QVERIFY(second);
        QTest::keyClick(second, Qt::Key_Return); QVERIFY(second->isChecked()); QVERIFY(!first->isChecked());
        QTest::keyClick(cardFor(menu, {}), Qt::Key_Space); QVERIFY(cardFor(menu, {})->isChecked());
        QVERIFY(manager.profile().activeWeekScheduleId.isEmpty());
        menu.setScheduleStatus(QStringLiteral("多张课表匹配，请手动选择"), true);
        QVERIFY(cardFor(menu, {})->toolTip().contains(QStringLiteral("多张课表")));
        menu.setScheduleStatus(QStringLiteral("课程栏未启用"), false);
        QVERIFY(cardFor(menu, {})->toolTip().contains(QStringLiteral("课程栏未启用")));
    }

    void menuFailureKeepsOldSelection()
    {
        auto& manager = ProfileManager::instance(); auto profile = sample();
        profile.activeWeekScheduleId = profile.schedules[0].id; manager.profile() = profile; QVERIFY(QDir().mkdir(path_));
        TaskbarTrayMenu menu; menu.setScheduleChoices(choicesFor(profile), profile.activeWeekScheduleId);
        connect(&menu, &TaskbarTrayMenu::weekScheduleRequested, &menu, [&](const QString& id) {
            QString error;
            if (!manager.selectWeekSchedule(id, &error)) menu.showScheduleError(error);
        });
        cardFor(menu, profile.schedules[1].id)->click();
        QVERIFY(cardFor(menu, profile.schedules[0].id)->isChecked());
        QVERIFY(!cardFor(menu, profile.schedules[1].id)->isChecked());
        QVERIFY(!menu.findChild<QWidget*>(QStringLiteral("scheduleError"))->isHidden());
    }

    void popupSlideAndFadeSurviveScheduleRefresh()
    {
        TaskbarTrayMenu menu;
        const QList<TaskbarTrayMenu::ScheduleChoice> initial = {
            {QStringLiteral("1"), QStringLiteral("常规课表"), QStringLiteral("全部周 · 今日 8 节")}};
        menu.setScheduleChoices(initial, QStringLiteral("1"));
        menu.showMenu();
        auto* animation = menu.findChild<QParallelAnimationGroup*>(QStringLiteral("visibilityAnimation"));
        QVERIFY(animation);
        auto* slide = qobject_cast<QPropertyAnimation*>(animation->animationAt(0));
        auto* fade = qobject_cast<QPropertyAnimation*>(animation->animationAt(1));
        QVERIFY(slide); QVERIFY(fade);
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        const QPoint start = slide->startValue().toPoint();
        const QPoint end = slide->endValue().toPoint();
        QVERIFY2(start != end, "The screen boundary must not erase the popup slide");
        QCOMPARE(fade->startValue().toReal(), qreal(0));
        QCOMPARE(fade->endValue().toReal(), qreal(1));
        animation->setCurrentTime(60);
        const QPoint framePosition = menu.pos();
        const qreal frameOpacity = menu.windowOpacity();
        QVERIFY(framePosition != start); QVERIFY(framePosition != end);
        QVERIFY(frameOpacity > 0 && frameOpacity < 1);
        menu.setScheduleChoices(initial, QStringLiteral("1"));
        QCOMPARE(menu.pos(), framePosition);
        QCOMPARE(slide->endValue().toPoint(), end);
        QCOMPARE(animation->currentTime(), 60);
        const int openingHeight = menu.height();
        auto expanded = initial;
        for (int i = 2; i <= 5; ++i)
            expanded.append({QString::number(i), QStringLiteral("备用课表"), QStringLiteral("全部周")});
        menu.setScheduleChoices(expanded, QStringLiteral("1"));
        QCOMPARE(menu.height(), openingHeight); // Resize waits for the slide to finish.
        QCOMPARE(menu.pos(), framePosition);
        animation->setCurrentTime(animation->duration());
        QVERIFY(menu.height() > openingHeight);
        QVERIFY(menu.height() <= 480);
        QCOMPARE(menu.windowOpacity(), qreal(1));
        QVERIFY(menu.isVisible());
        menu.hideMenu();
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->setCurrentTime(60);
        const qreal closingOpacity = menu.windowOpacity();
        QVERIFY(closingOpacity > 0 && closingOpacity < 1);
        menu.showMenu(); // Reopening during dismissal continues from the current frame.
        QCOMPARE(fade->startValue().toReal(), closingOpacity);
        QCOMPARE(fade->endValue().toReal(), qreal(1));
        animation->setCurrentTime(animation->duration());
        QVERIFY(menu.isVisible()); QCOMPARE(menu.windowOpacity(), qreal(1));
        menu.hide();
    }

    void layoutAndThemePreviews()
    {
        TaskbarTrayMenu menu;
        const QList<TaskbarTrayMenu::ScheduleChoice> choices = {
            {QStringLiteral("1"), QStringLiteral("常规课表"), QStringLiteral("全部周 · 今日 8 节")},
            {QStringLiteral("2"), QStringLiteral("单周课表"), QStringLiteral("单周 · 今日 6 节")},
            {QStringLiteral("3"), QStringLiteral("这是一个需要省略显示的很长很长的周课表名称"), QStringLiteral("双周 · 今日无课程")},
            {QStringLiteral("4"), QStringLiteral("考试周课表"), QStringLiteral("全部周 · 今日 4 节")},
            {QStringLiteral("5"), QStringLiteral("备用课表"), QStringLiteral("全部周 · 今日无课程")}};
        menu.setScheduleChoices(choices, QStringLiteral("1")); menu.setScheduleStatus(QStringLiteral("常规课表 · 今日 8 节"), true);
        auto* list = menu.findChild<QScrollArea*>(QStringLiteral("scheduleListScroll")); QVERIFY(list);
        QCOMPARE(list->height(), 2 * 56 + 8);
        menu.setFixedSize(menu.sizeHint()); menu.show(); QTest::qWait(30);
        auto* footer = menu.findChild<QWidget*>(QStringLiteral("restartButton"))->parentWidget();
        QCOMPARE(menu.width(), 380); QCOMPARE(footer->height(), 48);
        QVERIFY(menu.height() <= 480);
        QCOMPARE(footer->geometry().bottom(), menu.height() - 1);
        auto* outer = menu.findChild<QScrollArea*>(QStringLiteral("trayBodyScroll"));
        QVERIFY2(outer->widget()->width() == outer->viewport()->width(),
                 qPrintable(QStringLiteral("Body width %1 exceeds viewport %2; min width %3")
                     .arg(outer->widget()->width()).arg(outer->viewport()->width()).arg(outer->widget()->minimumWidth())));
        QVERIFY(list->widget()->width() == list->viewport()->width());
        QVERIFY(list->verticalScrollBar()->maximum() > 0);
        QCOMPARE(cardFor(menu, QStringLiteral("1"))->height(), 56);
        const auto automaticRect = cardFor(menu, {})->geometry();
        const auto firstRect = cardFor(menu, QStringLiteral("1"))->geometry();
        const auto secondRect = cardFor(menu, QStringLiteral("2"))->geometry();
        const auto thirdRect = cardFor(menu, QStringLiteral("3"))->geometry();
        QCOMPARE(automaticRect.top(), firstRect.top());
        QVERIFY(firstRect.left() > automaticRect.right());
        QCOMPARE(secondRect.top(), thirdRect.top());
        QVERIFY(secondRect.top() > automaticRect.bottom());
        QVERIFY(cardFor(menu, QStringLiteral("3"))->toolTip().contains(choices[2].name));
        auto* automatic = cardFor(menu, {}); automatic->setFocus();
        QTest::keyClick(automatic, Qt::Key_Tab);
        QCOMPARE(menu.focusWidget(), cardFor(menu, QStringLiteral("1")));
        for (int i = 1; i < choices.size(); ++i) {
            QTest::keyClick(menu.focusWidget(), Qt::Key_Tab);
            QCOMPARE(menu.focusWidget(), cardFor(menu, choices[i].id));
        }
        QVERIFY(list->verticalScrollBar()->value() > 0);
        QTest::keyClick(menu.focusWidget(), Qt::Key_Tab);
        QCOMPARE(menu.focusWidget()->objectName(), QStringLiteral("restartButton"));
        automatic->setFocus(); list->verticalScrollBar()->setValue(0); QTest::qWait(20);
        const QString output = qEnvironmentVariable("SCHEDULE_PREVIEW_DIR");
        const QString scale = qEnvironmentVariable("QT_SCALE_FACTOR", "1");
        const auto savePreview = [&](const QString& name) {
            return output.isEmpty() || menu.grab().save(output + QLatin1Char('/') + name + QLatin1Char('-') + scale + QStringLiteral(".png"));
        };
        eTheme->setThemeMode(ElaThemeType::Dark); QTest::qWait(20);
        const auto dark = menu.grab().toImage();
        const qreal ratio = dark.devicePixelRatio();
        QCOMPARE(dark.pixelColor(qRound(12 * ratio), qRound(300 * ratio)).rgb(), ElaThemeColor(ElaThemeType::Dark, WindowBase).rgb());
        QVERIFY(savePreview(QStringLiteral("tray-dark")));
        eTheme->setThemeMode(ElaThemeType::Light); QTest::qWait(20); QVERIFY(savePreview(QStringLiteral("tray-light")));
        menu.setFixedSize(380, 300); QTest::qWait(20);
        auto* body = menu.findChild<QScrollArea*>(QStringLiteral("trayBodyScroll")); QVERIFY(body);
        QVERIFY(body->verticalScrollBar()->maximum() > 0);
        QCOMPARE(footer->geometry().bottom(), menu.height() - 1);
        body->verticalScrollBar()->setValue(body->verticalScrollBar()->maximum()); QTest::qWait(20);
        QVERIFY(savePreview(QStringLiteral("tray-small")));
        menu.setScheduleChoices({}, {});
        QVERIFY(menu.findChild<QWidget*>(QStringLiteral("scheduleEmpty"))->isVisible());
        QVERIFY(list->isVisible()); // The automatic card remains in the grid.
        QCOMPARE(list->height(), 56);
        menu.hide();
    }
};

QTEST_MAIN(ScheduleSwitchTests)
#include "ScheduleSwitchTests.moc"
