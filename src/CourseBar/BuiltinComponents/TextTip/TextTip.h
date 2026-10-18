#ifndef CLASSTOPLAND_NEXT_TEXTTIP_H
#define CLASSTOPLAND_NEXT_TEXTTIP_H
#include"../../ICourseBarComponent.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include<QDate>
class TextTip : public ICourseBarComponent
{
    Q_OBJECT
public:
    TextTip(QWidget* parent);
    ~TextTip();
    QString getName() override { return "TextTip"; }
    void setText(QString text);
    QString getText() { return textLabel->text(); };
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(0, 0); }
private:
    void updateMetrics() override;
    QLabel* textLabel;
    QVBoxLayout* layout;
};


#endif //CLASSTOPLAND_NEXT_TEXTTIP_H
