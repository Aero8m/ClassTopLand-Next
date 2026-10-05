#include "TextTip.h"
TextTip::TextTip(QWidget* parent) : ICourseBarComponent(parent)
{
    // init ui
    textLabel = new QLabel;
    textLabel->setTextFormat(Qt::PlainText);
    textLabel->setFont(QFont("Microsoft YaHei UI",14));
    textLabel->setStyleSheet("color:black;");
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(textLabel);
}

TextTip::~TextTip()
{
}

void TextTip::resizeEvent(QResizeEvent* event)
{
    ICourseBarComponent::resizeEvent(event);
    const int pixelSize = qMax(
        1,
        qRound(contentsRect().height() * 14.0 / 35.0)
    );
    QFont font = textLabel->font();
    if (font.pixelSize() != pixelSize) {
        font.setPixelSize(pixelSize);
        textLabel->setFont(font);
    }
}
