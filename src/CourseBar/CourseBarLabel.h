#ifndef CLASSTOPLAND_NEXT_COURSEBARLABEL_H
#define CLASSTOPLAND_NEXT_COURSEBARLABEL_H

#include <QLabel>
#include <QPainter>
#include "CourseBarText.h"

// Center the visible glyphs rather than their advances. In particular, a date
// ending in a digit must have the same edge padding as a Chinese course name.
class CourseBarLabel final : public QLabel
{
public:
    explicit CourseBarLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const qreal ratio = devicePixelRatioF();
        const auto textImage = courseBarTextImage(font(), text(), ratio);
        if (textImage.image.isNull()) return;
        const qreal x = qRound((width() * ratio - textImage.image.width()) / 2) / ratio;
        const qreal y = qRound(((height() - textImage.lineHeight) / 2.0 + textImage.top) * ratio) / ratio;
        painter.drawImage(QPointF(x, y), textImage.image);
    }
};

#endif // CLASSTOPLAND_NEXT_COURSEBARLABEL_H
