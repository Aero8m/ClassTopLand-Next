#include "Date.h"
#include "../../CourseBarLabel.h"
Date::Date(QWidget* parent) : ICourseBarComponent(parent)
{
    // init ui
    dateLabel = new CourseBarLabel(this);
    dateLabel->setObjectName(QStringLiteral("dateText"));
    dateLabel->setStyleSheet("color:black;");
    dateLabel->setAlignment(Qt::AlignCenter);
    dateLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(dateLabel);
    updateMetrics();

    // init timer
    timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, &Date::refreshDate);

    refreshDate();
    timer->start();
}

Date::~Date()
{
    delete timer;
}

void Date::refreshDate()
{
    static QString weekdays = QStringLiteral("一二三四五六日");
    QDate date = QDate::currentDate();
    QString text = QStringLiteral("周%1 %2")
        .arg(weekdays.at(date.dayOfWeek() - 1))
        .arg(date.toString("MM/dd"));
    if (dateLabel->text() != text) {
        dateLabel->setText(text);
        notifyPreferredSizeChanged();
    }
}

QSize Date::sizeHint() const
{
    return QSize(metrics().textWidth(dateLabel->text(), dateLabel->font()), metrics().height);
}

void Date::updateMetrics()
{
    dateLabel->setFont(metrics().font());
    dateLabel->setContentsMargins(metrics().textGuard(), 0, metrics().textGuard(), 0);
}
