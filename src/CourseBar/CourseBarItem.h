#pragma once

#include "CourseBarMetrics.h"
#include <QLayout>
#include <QWidget>

class CourseBarLabel;
class QProgressBar;

// Shared presentation for built-in course blocks and plugin display items.
class CourseBarItem : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)
    Q_PROPERTY(bool active READ active WRITE setActive)
    Q_PROPERTY(int progressValue READ progressValue WRITE setProgressValue)
    Q_PROPERTY(int leftMargin READ leftMargin WRITE setLeftMargin)
    Q_PROPERTY(int rightMargin READ rightMargin WRITE setRightMargin)
  public:
    explicit CourseBarItem(QWidget *parent = nullptr);
    QString text() const;
    void setText(const QString &text);
    bool active() const { return active_; }
    void setActive(bool active);
    int progressValue() const;
    void setProgressValue(int value);
    int leftMargin() const { return leftMargin_; }
    int rightMargin() const { return rightMargin_; }
    void setLeftMargin(int value);
    void setRightMargin(int value);
    void setPresentation(const QString &text, bool active);
    void setMetrics(const CourseBarMetrics &metrics);
    void refreshThemeColor();
    int textWidth() const;
    void setHorizontalExtension(int left, int right);
    QSize sizeHint() const override;
    QFont textFont() const;
  signals:
    void preferredSizeChanged();
  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
  private:
    void updateWidth();
    void layoutText();
    CourseBarLabel *label_;
    QProgressBar *progress_;
    CourseBarMetrics metrics_;
    bool active_ = false;
    int left_ = 0, right_ = 0;
    // -1 inherits the host's share of the adjacent gap; explicit values use 49px bar units.
    int leftMargin_ = -1, rightMargin_ = -1;
};

// Returns the full preferred width, including the built-in active-edge extensions.
int layoutCourseBarItems(const QList<CourseBarItem *> &items, const CourseBarMetrics &metrics,
                         const QPoint &origin = {});

class CourseBarRowLayout final : public QLayout
{
    Q_OBJECT
  public:
    explicit CourseBarRowLayout(QWidget *parent = nullptr);
    ~CourseBarRowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override { return items_.size(); }
    QLayoutItem *itemAt(int index) const override { return items_.value(index); }
    QLayoutItem *takeAt(int index) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override { return {0, 0}; }
    void setGeometry(const QRect &rect) override;
    void insertWidget(int index, CourseBarItem *item);
    void setMetrics(const CourseBarMetrics &metrics);
  private:
    QList<CourseBarItem *> visibleItems() const;
    QList<QLayoutItem *> items_;
    CourseBarMetrics metrics_;
};
