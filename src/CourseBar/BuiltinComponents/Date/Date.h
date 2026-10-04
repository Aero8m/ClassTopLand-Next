#ifndef CLASSTOPLAND_NEXT_DATE_H
#define CLASSTOPLAND_NEXT_DATE_H
#include"../../ICourseBarComponent.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include<QDate>
class Date : public ICourseBarComponent
{
    Q_OBJECT
public:
    Date(QWidget* parent);
    ~Date();
    QString getName() override { return "Date"; }
private slots:
    void refreshDate();
private:
    void resizeEvent(QResizeEvent* event) override;
    QTimer* timer;
    QLabel* dateLabel;
    QVBoxLayout* layout;
};


#endif //CLASSTOPLAND_NEXT_DATE_H