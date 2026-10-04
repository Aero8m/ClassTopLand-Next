#include "Date.h"
Date::Date(QWidget* parent) : ICourseBarComponent(parent)
{
    // init ui
    dateLabel = new QLabel;
    dateLabel->setFont(QFont("Microsoft YaHei UI",14));
    dateLabel->setStyleSheet("color:black;");
    dateLabel->setAlignment(Qt::AlignCenter);
    dateLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(dateLabel);

    // init timer
    timer = new QTimer;
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
    dateLabel->setText(text);
}

void Date::resizeEvent(QResizeEvent* event)
{
    ICourseBarComponent::resizeEvent(event);
    const int pixelSize = qMax(
        1,
        qRound(contentsRect().height() * 14.0 / 35.0)
    );
    QFont font = dateLabel->font();
    if (font.pixelSize() != pixelSize) {
        font.setPixelSize(pixelSize);
        dateLabel->setFont(font);
    }
}
