#include "TextTip.h"
#include "../../CourseBarLabel.h"
TextTip::TextTip(QWidget* parent) : ICourseBarComponent(parent)
{
    // init ui
    textLabel = new CourseBarLabel(this);
    textLabel->setObjectName(QStringLiteral("tipText"));
    textLabel->setTextFormat(Qt::PlainText);
    textLabel->setStyleSheet("color:black;");
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->addWidget(textLabel);
    updateMetrics();
}

TextTip::~TextTip()
{
}

void TextTip::setText(QString text)
{
    if (textLabel->text() == text) return;
    textLabel->setText(text);
    notifyPreferredSizeChanged();
}

QSize TextTip::sizeHint() const
{
    return QSize(metrics().textWidth(textLabel->text(), textLabel->font()), metrics().height);
}

void TextTip::updateMetrics()
{
    textLabel->setFont(metrics().font());
    textLabel->setContentsMargins(metrics().textGuard(), 0, metrics().textGuard(), 0);
}
