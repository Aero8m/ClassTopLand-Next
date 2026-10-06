#pragma once

#include "../Core/DateSchedule/DateSchedule.h"
#include <ElaWidget.h>
#include <ElaCalendarPicker.h>
#include <ElaDialog.h>

class ElaComboBox;
class ElaPushButton;
class ElaText;
class QButtonGroup;
class QCloseEvent;
class QListWidget;

class FutureDatePicker final : public ElaCalendarPicker {
    Q_OBJECT
public:
    explicit FutureDatePicker(QWidget* parent = nullptr);
    QDate date() const { return getSelectedDate(); }
    QDate minimumDate() const { return minimumDate_; }
    void setDate(const QDate& date);
    void refreshMinimum();
signals:
    void dateChanged(const QDate& date);
private:
    QDate minimumDate_;
    QDate publishedDate_;
};

class RescheduleWindow final : public ElaDialog {
    Q_OBJECT
public:
    explicit RescheduleWindow(QWidget* parent = nullptr);
    void refreshProfile(const Profile& profile);
    void showError(const QString& message);
    void reject() override;
signals:
    void rescheduleRequested(const QDate& date, const QString& weekId, int weekday);
    void restoreRequested(const QDate& date);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void updateActions();
    Profile profile_;
    FutureDatePicker* date_;
    ElaComboBox* source_;
    QButtonGroup* weekdays_;
    ElaText* status_;
    ElaText* error_;
    ElaPushButton* confirm_;
    ElaPushButton* restore_;
};

class SwapClassesWindow final : public ElaWidget {
    Q_OBJECT
public:
    explicit SwapClassesWindow(QWidget* parent = nullptr);
    void refreshProfile(const Profile& profile);
    void showError(const QString& message);
signals:
    void swapRequested(const QDate& date, const QString& weekId, int first, int second);
    void restoreRequested(const QDate& date);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void rebuildClasses();
    void updateSelection();
    Profile profile_;
    DateSchedule::Result table_;
    FutureDatePicker* date_;
    ElaComboBox* source_;
    ElaText* weekday_;
    ElaText* status_;
    ElaText* summary_;
    ElaText* error_;
    QListWidget* courses_;
    ElaPushButton* confirm_;
    ElaPushButton* restore_;
    QList<int> selected_;
};
