#ifndef CLASSTOPLAND_NEXT_SUBJECT_H
#define CLASSTOPLAND_NEXT_SUBJECT_H
#include<QString>

struct Subject
{
    Subject(QString name, QString simplifiedName="", QString teacher="")
    {
        this->name = name;
        if (simplifiedName.isEmpty() && !this->name.isEmpty())
        {
            simplifiedName = this->name.at(0);
        }
        this->simplifiedName = simplifiedName;
        this->teacher = teacher;
    }
    QString name;
    QString simplifiedName;
    QString teacher;
};

#endif //CLASSTOPLAND_NEXT_SUBJECT_H
