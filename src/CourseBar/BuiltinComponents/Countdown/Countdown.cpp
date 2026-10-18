#include "Countdown.h"
#include "../../CourseBarLabel.h"

#include "../../../Core/Logger/Logger.h"

#include <QFontMetrics>
#include <QLabel>
#include <QVBoxLayout>
#include <utility>

Countdown::Countdown(QWidget* parent) : Countdown(CourseRefreshService::Clock{}, parent)
{
}

Countdown::Countdown(CourseRefreshService::Clock clock, QWidget* parent) : Countdown(std::move(clock), nullptr, parent) {}
Countdown::Countdown(CourseRefreshService* service, QWidget* parent) : Countdown(CourseRefreshService::Clock{}, service, parent) {}
Countdown::Countdown(CourseRefreshService::Clock clock, CourseRefreshService* service, QWidget* parent)
    : ICourseBarComponent(parent)
{
    setObjectName(QStringLiteral("countdown"));
    label_ = new CourseBarLabel(this);
    label_->setObjectName(QStringLiteral("remainingTime"));
    label_->setTextFormat(Qt::PlainText);
    label_->setAlignment(Qt::AlignCenter);
    label_->setStyleSheet(QStringLiteral("color: black; background: transparent;"));
    label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(label_);
    updateMetrics();

    service_ = service ? service : new CourseRefreshService(std::move(clock), this);
    connect(service_, &CourseRefreshService::stateChanged, this, &Countdown::applyState);
    connect(service_, &CourseRefreshService::errorOccurred, this, [](const QString& message) {
        Logger::instance().log(Logger::Level::Error, QStringLiteral("Countdown: %1").arg(message));
        Logger::instance().flush();
    });
    if (service_->isRunning()) { applyState(service_->state()); }
    else service_->start();
}

QSize Countdown::sizeHint() const
{
    return QSize(hasCountdown_ ? preferredWidth_ : 0, metrics().height);
}

QSize Countdown::minimumSizeHint() const
{
    return QSize(0, 0);
}

void Countdown::applyState(const CourseRefreshService::State& state)
{
    if (state.tableRevision != service_->table().revision) return;
    using Phase = CourseRefreshService::Phase;
    const bool available = state.countdownTarget.isValid() &&
        (state.phase == Phase::BeforeFirstClass || state.phase == Phase::InClass || state.phase == Phase::Break);
    const qint64 seconds = qMax<qint64>(0, state.remainingSeconds);
    label_->setText(available ? QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0')) : QString());
    if (available == hasCountdown_) return;
    hasCountdown_ = available;
    notifyPreferredSizeChanged();
}

void Countdown::updateMetrics()
{
    label_->setFont(metrics().font());
    label_->setContentsMargins(metrics().textGuard(), 0, metrics().textGuard(), 0);
    // Reserve the widest visible numeric edges, without counting invisible
    // advance bearings as extra padding. The width remains stable every second.
    int inkWidth = 0;
    for (int first = 0; first <= 2; ++first) {
        for (int last = 0; last <= 9; ++last) {
            const QString sample = QStringLiteral("%1:00:%2")
                .arg(first * 10, 2, 10, QLatin1Char('0')).arg(last, 2, 10, QLatin1Char('0'));
            inkWidth = qMax(inkWidth, CourseBarMetrics::inkWidth(label_->font(), sample));
        }
    }
    preferredWidth_ = inkWidth + 2 * metrics().textGuard();
}
