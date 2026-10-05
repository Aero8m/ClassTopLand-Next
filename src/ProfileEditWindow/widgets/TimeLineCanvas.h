#pragma once

#include <ElaScrollPageArea.h>
#include "../../Core/Model/TimeLine.h"
#include <QRectF>

class ElaToolTip;
class QTimer;

class TimeLineCanvas final : public ElaScrollPageArea {
    Q_OBJECT
public:
    explicit TimeLineCanvas(QWidget* parent = nullptr);
    void setTimePoints(const QList<TimeLinePoint>& points);
    void setSelectedIndex(int index);
    int selectedIndex() const { return selected_; }
    void setZoom(int percent);
    int zoom() const { return zoom_; }
    qreal yForSeconds(int seconds) const;
    int secondsAtY(qreal y) const;
    QRectF pointRect(int sourceIndex) const;
    bool isDragging() const;
    void cancelDrag();
signals:
    void selectionRequested(int sourceIndex);
    void timeChangeRequested(int sourceIndex, QTime start, QTime end);
    void addRequested(QTime start);
    void invalidChange(const QString& error);
    void previewChanged(QTime start, QTime end);
    void previewFinished();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void hideEvent(QHideEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    enum class Drag { None, Move, Start, End };
    int hitTest(QPointF position) const;
    QRectF rangeRect(int start, int end) const;
    QString previewError() const;
    void updatePreview(qreal y);
    void updateHover(QPointF position);
    qreal pixelsPerMinute() const { return 2.0 * zoom_ / 100.0; }
    QList<TimeLinePoint> points_;
    int selected_ = -1;
    int zoom_ = 100;
    Drag drag_ = Drag::None;
    qreal pressY_ = 0;
    int originalStart_ = 0, originalEnd_ = 0;
    int previewStart_ = 0, previewEnd_ = 0;
    bool moved_ = false;
    ElaToolTip* tip_;
    QTimer* hoverTimer_;
    int hovered_ = -1;
};
