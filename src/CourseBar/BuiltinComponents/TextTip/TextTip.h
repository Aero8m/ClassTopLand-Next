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
    void setText(QString text) { textLabel->setText(text); };
    QString getText() { return textLabel->text(); };
private:
    void resizeEvent(QResizeEvent* event) override;
    QLabel* textLabel;
    QVBoxLayout* layout;
};


#endif //CLASSTOPLAND_NEXT_TEXTTIP_H