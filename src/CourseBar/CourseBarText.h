#ifndef CLASSTOPLAND_NEXT_COURSEBARTEXT_H
#define CLASSTOPLAND_NEXT_COURSEBARTEXT_H

#include <QCache>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QtMath>

struct CourseBarTextImage
{
    QImage image;
    int lineHeight = 0;
    qreal top = 0;
};

inline qreal courseBarTextRatio()
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    return screen ? screen->devicePixelRatio() : 1.0;
}

// Cache the actual painted glyph bounds. Font advance and outline metrics can
// differ from the Windows rasterizer by a few pixels, especially for digits.
inline CourseBarTextImage courseBarTextImage(const QFont& font, const QString& text,
                                           qreal ratio = courseBarTextRatio())
{
    constexpr int cacheBytes = 4 * 1024 * 1024;
    static QCache<QString, CourseBarTextImage> cache(cacheBytes);
    const QString key = font.key() + QChar(0x1f) + QString::number(ratio, 'g', 12) + QChar(0x1f) + text;
    if (const auto* cached = cache.object(key)) return *cached;
    const QFontMetrics metrics(font);
    const QStringList lines = text.split(QLatin1Char('\n'));
    int left = 0;
    int right = 0;
    for (const QString& line : lines) {
        const QRect bounds = metrics.boundingRect(line);
        left = qMin(left, bounds.left());
        right = qMax(right, qMax(bounds.right() + 1, metrics.horizontalAdvance(line)));
    }
    constexpr int padding = 4;
    CourseBarTextImage result;
    result.lineHeight = metrics.height() + (lines.size() - 1) * metrics.lineSpacing();
    QImage canvas(QSize(qMax(1, qCeil((right - left + 2 * padding) * ratio)),
                       qMax(1, qCeil((result.lineHeight + 2 * padding) * ratio))),
                  QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(ratio);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setFont(font);
    painter.setPen(Qt::black);
    int baseline = padding + metrics.ascent();
    for (const QString& line : lines) {
        const int offset = lines.size() > 1 ? (right - left - metrics.horizontalAdvance(line)) / 2 : 0;
        painter.drawText(padding - left + offset, baseline, line);
        baseline += metrics.lineSpacing();
    }
    painter.end();
    QRect ink;
    for (int y = 0; y < canvas.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(canvas.constScanLine(y));
        for (int x = 0; x < canvas.width(); ++x)
            if (qAlpha(row[x]) > 0) ink = ink.united(QRect(x, y, 1, 1));
    }
    if (!ink.isEmpty()) {
        result.image = canvas.copy(ink);
        result.image.setDevicePixelRatio(ratio);
        result.top = ink.top() / ratio - padding;
    }
    const int cost = static_cast<int>(qMin<qsizetype>(cacheBytes + 1,
        qMax<qsizetype>(1, result.image.sizeInBytes())));
    cache.insert(key, new CourseBarTextImage(result), cost);
    return result;
}

#endif // CLASSTOPLAND_NEXT_COURSEBARTEXT_H
