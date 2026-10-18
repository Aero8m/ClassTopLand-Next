#pragma once

#include "../Core/Model/DesktopCountdownConfig.h"
#include <QWidget>
#include <functional>

class QLabel;
class QTimer;
class QScreen;

class DesktopCountdown final : public QWidget
{
    Q_OBJECT
public:
    using Clock = std::function<QDateTime()>;
    explicit DesktopCountdown(QWidget* parent = nullptr);
    explicit DesktopCountdown(Clock clock, QWidget* parent = nullptr);

public slots:
    void reloadConfig();
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void watchScreen(QScreen* screen);
    void updateGeometryAndFonts();
    QScreen* targetScreen() const;
    DesktopCountdownConfig config_;
    Clock clock_;
    QTimer* timer_;
    QLabel* title_;
    QLabel* prefix_;
    QLabel* number_;
    QLabel* unit_;
    QLabel* tail_;
    QLabel* english_;
    QWidget* accent_;
    QString layoutKey_;
    bool expired_ = false;
};
