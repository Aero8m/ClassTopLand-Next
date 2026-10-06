#include "../src/ScheduleAdjustments/DateScheduleWidgets.h"
#include "../src/TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "../src/Core/AppComposer/AppComposer.h"
#include "../src/Core/ProfileManager/ProfileExchange.h"
#include "../src/ProfileEditWindow/ProfileEditSession.h"
#include "../src/ProfileEditWindow/ProfileEditWindow.h"
#include <ElaApplication.h>
#include <ElaComboBox.h>
#include <ElaCalendar.h>
#include <ElaPushButton.h>
#include <ElaTheme.h>
#include <ElaText.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

template<class T> T* widget(const QString& name)
{
    for (auto* item : QApplication::allWidgets())
        if (item->objectName() == name) if (auto* typed = qobject_cast<T*>(item)) return typed;
    return nullptr;
}

Profile sample()
{
    Profile profile;
    profile.name = QStringLiteral("测试档案");
    WeekSchedule week(QStringLiteral("默认课表"), WeekScheduleMode::All);
    week.initDaySchedules();
    for (auto& day : week.daySchedules)
        day.classes = {Class(QStringLiteral("音乐"), QTime(8, 20), QTime(9, 0)),
                       Class(QStringLiteral("音乐"), QTime(9, 15), QTime(9, 55)),
                       Class(QStringLiteral("化学"), QTime(10, 5), QTime(10, 45)),
                       Class(QStringLiteral("信息技术"), QTime(10, 55), QTime(11, 35)),
                       Class(QStringLiteral("生物"), QTime(11, 45), QTime(12, 25)),
                       Class(QStringLiteral("生物"), QTime(12, 35), QTime(13, 15))};
    profile.schedules.append(week); profile.activeWeekScheduleId = week.id;
    return profile;
}

void choose(QListWidget* list, int row)
{
    auto* item = list->item(row); CHECK(item);
    list->scrollToItem(item);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(item).center());
}

void uiCases(const QString& output)
{
    const auto profile = sample();
    SwapClassesWindow swap;
    swap.refreshProfile(profile); swap.show(); QTest::qWait(80);
    auto* courses = swap.findChild<QListWidget*>(QStringLiteral("swapCourses"));
    auto* confirm = swap.findChild<ElaPushButton*>(QStringLiteral("confirmSwap"));
    auto* restore = swap.findChild<ElaPushButton*>(QStringLiteral("restoreSwap"));
    auto* source = swap.findChild<ElaComboBox*>(QStringLiteral("swapSource"));
    auto* date = swap.findChild<FutureDatePicker*>(QStringLiteral("swapDate"));
    CHECK(courses && confirm && restore && source && date);
    CHECK(courses->count() == 6 && !confirm->isEnabled() && !restore->isEnabled());
    choose(courses, 0); choose(courses, 1); CHECK(!confirm->isEnabled());
    choose(courses, 1); choose(courses, 3); CHECK(confirm->isEnabled());
    choose(courses, 2); CHECK(courses->item(2)->data(Qt::CheckStateRole).toInt() == Qt::Unchecked);
    QSignalSpy requests(&swap, &SwapClassesWindow::swapRequested);
    confirm->click(); CHECK(requests.size() == 1);
    CHECK(requests[0][2].toInt() == 0 && requests[0][3].toInt() == 3);
    QSignalSpy dates(date, &FutureDatePicker::dateChanged);
    date->setDate(QDate::currentDate().addDays(1)); CHECK(!confirm->isEnabled());
    CHECK(dates.size() == 1);
    CHECK(date->minimumDate() == QDate::currentDate());
    auto* calendar = date->findChild<ElaCalendar*>(); CHECK(calendar);
    calendar->setSelectedDate(QDate::currentDate().addDays(-1));
    CHECK(date->date() == QDate::currentDate().addDays(1) && dates.size() == 1);
    CHECK(calendar->getMaximumDate() > QDate::currentDate().addDays(365));
    choose(courses, 0); choose(courses, 3);
    for (const auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode); QTest::qWait(30);
        const auto suffix = mode == ElaThemeType::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        CHECK(swap.grab().save(output + QStringLiteral("/swap-") + suffix + QStringLiteral(".png")));
    }
    swap.resize(430, 380); QTest::qWait(30);
    CHECK(swap.grab().save(output + QStringLiteral("/swap-small.png")));
    Profile adjusted = profile;
    adjusted.dateOverrides.append({date->date(), profile.schedules[0].id, 1,
                                  profile.schedules[0].daySchedules[0].classes, profile.schedules[0].daySchedules[0].classes});
    swap.refreshProfile(adjusted);
    CHECK(!source->isEnabled() && restore->isEnabled());
    swap.refreshProfile(profile); CHECK(source->isEnabled() && !restore->isEnabled());
    auto ambiguous = profile; ambiguous.activeWeekScheduleId.clear();
    auto second = ambiguous.schedules[0]; second.id = QStringLiteral("second"); ambiguous.schedules.append(second);
    SwapClassesWindow ambiguousWindow; ambiguousWindow.refreshProfile(ambiguous);
    CHECK(ambiguousWindow.findChild<ElaComboBox*>(QStringLiteral("swapSource"))->currentIndex() == 0);
    CHECK(ambiguousWindow.findChild<QListWidget*>(QStringLiteral("swapCourses"))->count() == 0);
    swap.close(); CHECK(!swap.isVisible());

    TaskbarTrayMenu tray;
    tray.setScheduleChoices({{profile.schedules[0].id, profile.schedules[0].name, QStringLiteral("全部周")}}, profile.activeWeekScheduleId);
    tray.showMenu(); QTest::qWait(220);
    QSignalSpy rescheduleRequests(&tray, &TaskbarTrayMenu::rescheduleWindowRequested);
    auto* shortcut = tray.findChild<QAbstractButton*>(QStringLiteral("rescheduleButton")); CHECK(shortcut);
    shortcut->click(); CHECK(rescheduleRequests.size() == 1);
    CHECK(tray.findChildren<RescheduleWindow*>().isEmpty());
    tray.hideMenu(); QTest::qWait(160);

    RescheduleWindow reschedule;
    reschedule.refreshProfile(profile); reschedule.show(); QTest::qWait(50);
    CHECK(reschedule.isWindow() && reschedule.parentWidget() == nullptr);
    CHECK(reschedule.windowModality() == Qt::NonModal);
    CHECK(reschedule.findChild<ElaPushButton*>(QStringLiteral("confirmReschedule"))->isEnabled());
    for (const auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode); QTest::qWait(30);
        const auto suffix = mode == ElaThemeType::Dark ? QStringLiteral("dark") : QStringLiteral("light");
        CHECK(reschedule.grab().save(output + QStringLiteral("/reschedule-") + suffix + QStringLiteral(".png")));
    }
    const auto normalSize = reschedule.size();
    reschedule.resize(360, 320); QTest::qWait(30);
    auto* footer = reschedule.findChild<QWidget*>(QStringLiteral("rescheduleFooter")); CHECK(footer);
    CHECK(footer->rect().contains(reschedule.findChild<ElaPushButton*>(QStringLiteral("confirmReschedule"))->geometry()));
    CHECK(footer->rect().contains(reschedule.findChild<ElaPushButton*>(QStringLiteral("cancelReschedule"))->geometry()));
    reschedule.showError(QStringLiteral("调休保存失败：档案暂时无法写入。请检查文件是否被其他程序占用后重试，当前选择已保留。"));
    QTest::qWait(30);
    CHECK(reschedule.grab().save(output + QStringLiteral("/reschedule-small.png")));
    reschedule.resize(normalSize); reschedule.refreshProfile(profile); QTest::qWait(30);
    auto* datePicker = reschedule.findChild<FutureDatePicker*>(); CHECK(datePicker);
    QTest::mouseClick(datePicker, Qt::LeftButton, Qt::NoModifier, QPoint(datePicker->width() - 8, datePicker->height() / 2));
    QTest::qWait(50);
    CHECK(reschedule.isVisible() && QApplication::activePopupWidget());
    QApplication::activePopupWidget()->hide();
    reschedule.activateWindow(); QTest::qWait(20);
    QTest::keyClick(datePicker, Qt::Key_Escape); QTest::qWait(30);
    CHECK(!reschedule.isVisible());
}

void lifecycleCases()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/date-ui-XXXXXX")); CHECK(dir.isValid());
    CHECK(ConfigManager::instance().load(dir.filePath(QStringLiteral("MainConfig.json"))));
    auto config = ConfigManager::instance().config();
    config.courseBarConfig.enable = false; config.courseBarConfig.components.clear();
    CHECK(ConfigManager::instance().commit(config));
    auto profile = sample();
    for (int i = -1; i <= 1; ++i)
        profile.dateOverrides.append({QDate::currentDate().addDays(i), profile.schedules[0].id, 1,
                                      profile.schedules[0].daySchedules[0].classes, profile.schedules[0].daySchedules[0].classes});
    QByteArray bytes;
    CHECK(ProfileExchange::serialize(profile, ProfileExchange::Format::NativeJson, bytes));
    CHECK(QDir().mkpath(dir.filePath(QStringLiteral("profiles"))));
    const auto path = dir.filePath(QStringLiteral("profiles/Default.json"));
    CHECK(ProfileExchange::writeFile(path, bytes));
    AppComposer composer(dir.path()); CHECK(composer.startApp());
    auto& manager = ProfileManager::instance();
    CHECK(manager.profile().dateOverrides.size() == 2);
    Profile persisted;
    CHECK(ProfileExchange::readFile(path, ProfileExchange::Format::NativeJson, persisted));
    CHECK(persisted.dateOverrides.size() == 2);
    auto* timer = composer.findChild<QTimer*>(QStringLiteral("dateScheduleCleanupTimer"));
    CHECK(timer && timer->isActive() && timer->interval() == 60000);
    CHECK(QMetaObject::invokeMethod(&composer, "showProfileEditorWindow"));
    auto* editor = widget<ProfileEditWindow>(QString());
    if (!editor) for (auto* item : QApplication::allWidgets())
        if (auto* candidate = qobject_cast<ProfileEditWindow*>(item)) { editor = candidate; break; }
    CHECK(editor);
    editor->session()->edit().name = QStringLiteral("保留未保存修改"); editor->session()->notifyChanged();
    manager.profile().dateOverrides.prepend(profile.dateOverrides[0]); // Simulate expiry while the editor is open.
    editor->session()->syncDateOverrides(manager.profile().dateOverrides);
    CHECK(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    CHECK(manager.profile().dateOverrides.size() == 2);
    CHECK(editor->session()->draft().dateOverrides.size() == 2);
    CHECK(editor->session()->isDirty() && editor->session()->draft().name == QStringLiteral("保留未保存修改"));
    CHECK(QMetaObject::invokeMethod(&composer, "showSwapWindow"));
    auto* swap = widget<SwapClassesWindow>(QStringLiteral("SwapClassesWindow")); CHECK(swap && swap->isVisible());
    CHECK(QMetaObject::invokeMethod(&composer, "showSwapWindow"));
    CHECK(widget<SwapClassesWindow>(QStringLiteral("SwapClassesWindow")) == swap);
    auto* courses = swap->findChild<QListWidget*>(QStringLiteral("swapCourses")); CHECK(courses);
    auto* confirm = swap->findChild<ElaPushButton*>(QStringLiteral("confirmSwap")); CHECK(confirm);
    QTest::qWait(30); choose(courses, 0); choose(courses, 3); CHECK(confirm->isEnabled());
    QFile locked(path); CHECK(locked.open(QIODevice::ReadOnly));
    confirm->click();
    CHECK(swap->findChild<ElaText*>(QStringLiteral("swapError"))->isVisible());
    CHECK(confirm->isEnabled());
    CHECK(DateSchedule::find(manager.profile(), QDate::currentDate())->classes[0].subject == QStringLiteral("音乐"));
    locked.close(); confirm->click();
    CHECK(DateSchedule::find(manager.profile(), QDate::currentDate())->classes[0].subject == QStringLiteral("信息技术"));
    CHECK(editor->session()->draft().dateOverrides.size() == 2 && editor->session()->isDirty());
    swap->findChild<ElaPushButton*>(QStringLiteral("restoreSwap"))->click();
    CHECK(!DateSchedule::find(manager.profile(), QDate::currentDate()));
    CHECK(editor->session()->draft().dateOverrides.size() == 1);
    auto* tray = widget<TaskbarTrayMenu>(QStringLiteral("TaskbarTrayMenu")); CHECK(tray);
    tray->showMenu(); QTest::qWait(220);
    tray->findChild<QAbstractButton*>(QStringLiteral("rescheduleButton"))->click();
    QTest::qWait(160);
    auto* reschedule = widget<RescheduleWindow>(QStringLiteral("RescheduleWindow"));
    CHECK(reschedule && reschedule->isVisible() && !tray->isVisible());
    CHECK(QMetaObject::invokeMethod(&composer, "showRescheduleWindow"));
    CHECK(widget<RescheduleWindow>(QStringLiteral("RescheduleWindow")) == reschedule);
    reschedule->findChild<QAbstractButton*>(QStringLiteral("rescheduleWeekday7"))->click();
    reschedule->findChild<ElaPushButton*>(QStringLiteral("confirmReschedule"))->click();
    CHECK(!reschedule->isVisible());
    CHECK(DateSchedule::find(manager.profile(), QDate::currentDate())->sourceWeekday == 7);
    CHECK(editor->session()->draft().name == QStringLiteral("保留未保存修改"));
    // Save a draft that changes both source days; the committed editor state must
    // contain the pruned records rather than re-publishing its stale snapshots.
    editor->session()->edit().schedules[0].daySchedules[0].classes[0].subject = QStringLiteral("化学");
    editor->session()->edit().schedules[0].daySchedules[6].classes[0].subject = QStringLiteral("化学");
    editor->session()->notifyChanged();
    auto* save = editor->findChild<ElaPushButton*>(QStringLiteral("saveProfile")); CHECK(save);
    save->click();
    CHECK(manager.profile().dateOverrides.isEmpty());
    CHECK(editor->session()->draft().dateOverrides.isEmpty() && !editor->session()->isDirty());
    CHECK(manager.profile().name == QStringLiteral("保留未保存修改"));
    composer.shutdown(); CHECK(!timer->isActive());
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    // The offscreen backend does not enumerate Windows fonts automatically.
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/seguisym.ttf"));
#endif
    eApp->init();
    eApp->setWindowDisplayMode(ElaApplicationType::Normal);
    QApplication::setQuitOnLastWindowClosed(false);
    const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR");
    const auto output = QDir::currentPath() + QStringLiteral("/date-schedule-previews") +
                        (scale.isEmpty() ? QString() : QStringLiteral("-") + scale);
    QDir().mkpath(output);
    try {
        uiCases(output); lifecycleCases();
        std::cout << "Passed " << checks << " date schedule UI/integration checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
