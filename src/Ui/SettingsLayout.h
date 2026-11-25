#pragma once
#include <QPointer>
#include <QList>
#include <QWidget>

class ElaPushButton;
class ElaScrollPageArea;
class ElaText;
class QBoxLayout;
class QHBoxLayout;
class QVBoxLayout;

namespace AppUi {
QVBoxLayout* pageContent(QWidget* parent, const QString& title = {}, int titleSize = 24);
void configureCard(ElaScrollPageArea* area);
void configureCardLayout(QVBoxLayout* layout);
void sizeActionButton(ElaPushButton* button);
void configureActionButton(ElaPushButton* button, bool primary = false);

class SettingsSection : public QWidget {
public:
    explicit SettingsSection(QWidget* parent = nullptr);
    QString title() const;
    void setTitle(const QString& title);
    QVBoxLayout* bodyLayout() const { return body_; }
private:
    ElaText* heading_;
    QVBoxLayout* body_;
};

class SettingsRow : public QWidget {
public:
    explicit SettingsRow(QWidget* parent = nullptr);
    QString title() const;
    void setTitle(const QString& value);
    QString description() const;
    void setDescription(const QString& value);
    QWidget* editor() const { return editor_; }
    bool attachEditor(QWidget* editor);
    void detachEditor(QWidget* editor);
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void arrange();
    ElaText *title_, *description_;
    QWidget* labels_;
    QBoxLayout* body_;
    QPointer<QWidget> editor_;
    int editorWidth_ = 0;
    int editorMinimumWidth_ = 0;
    int editorMaximumWidth_ = QWIDGETSIZE_MAX;
    bool expanding_ = false;
};

class SettingsActions : public QWidget {
public:
    explicit SettingsActions(QWidget* parent = nullptr);
    QString message() const;
    void setMessage(const QString& value);
    bool canSetPrimary(ElaPushButton* button, bool primary) const;
    bool insertAction(ElaPushButton* button, int index = -1);
    void removeAction(ElaPushButton* button);
    void setPrimary(ElaPushButton* button, bool primary);
    int actionCount() const;
private:
    ElaText* message_;
    QHBoxLayout* buttons_;
    QList<QPointer<ElaPushButton>> actions_;
};
}
