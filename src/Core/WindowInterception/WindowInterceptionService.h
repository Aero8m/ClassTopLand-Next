#pragma once

#include "../Model/WindowInterceptionConfig.h"
#include <QObject>
#include <QHash>
#include <functional>
#include <memory>
#include <optional>

struct InterceptedWindow
{
    quintptr handle = 0;
    quint32 processId = 0;
    quint64 processStarted = 0;
    QString title;
    QString exePath;
    QString className;
    bool visible = false;
    bool hiddenByUs = false;
};

// Platform boundary for native window enumeration, visibility and event hooks.
class WindowInterceptionBackend
{
public:
    virtual ~WindowInterceptionBackend() = default;
    virtual bool supported() const = 0;
    virtual QVector<InterceptedWindow> enumerate(QString* error) = 0;
    virtual std::optional<InterceptedWindow> inspect(quintptr handle) = 0;
    virtual bool setVisible(quintptr handle, bool visible, QString* error) = 0;
    virtual bool watch(std::function<void(quintptr)> shown, QString* error) = 0;
    virtual void unwatch() = 0;
};

class WindowInterceptionService final : public QObject
{
    Q_OBJECT
public:
    explicit WindowInterceptionService(QObject* parent = nullptr);
    explicit WindowInterceptionService(std::unique_ptr<WindowInterceptionBackend> backend,
                                       QObject* parent = nullptr);
    ~WindowInterceptionService() override;
    bool supported() const;
    bool start(QString* error = nullptr);
    void stop();
    const QString& monitoringError() const { return monitoringError_; }
    const QString& lastError() const { return lastError_; }
    const QVector<WindowInterceptionRule>& rules() const { return rules_; }
    QVector<InterceptedWindow> windows(QString* error = nullptr);
    bool hideOnce(const InterceptedWindow& window, QString* error = nullptr);
    bool hideAndSave(const InterceptedWindow& window, const QString& titleContains,
                     QString* error = nullptr);
    bool restore(const InterceptedWindow& window, QString* error = nullptr);
    bool setRuleEnabled(int index, bool enabled, QString* error = nullptr);
    bool deleteRule(int index, QString* error = nullptr);
    static bool matches(const WindowInterceptionRule& rule, const InterceptedWindow& window);
    static bool sameWindow(const InterceptedWindow& a, const InterceptedWindow& b);
signals:
    void rulesChanged();
    void windowsChanged();
    void errorOccurred(const QString& message);
private:
    bool fail(QString* error, const QString& message);
    std::optional<InterceptedWindow> checked(const InterceptedWindow& expected, QString* error);
    bool blocked(const InterceptedWindow& window) const;
    bool owned(const InterceptedWindow& window) const;
    bool hide(const InterceptedWindow& window, QString* error);
    bool show(const InterceptedWindow& window, QString* error);
    bool persist(const QVector<WindowInterceptionRule>& rules, QString* error);
    bool applyRules(QString* error);
    bool restoreMatches(const WindowInterceptionRule& rule, QString* error);
    void windowShown(quintptr handle);
    std::unique_ptr<WindowInterceptionBackend> backend_;
    QVector<WindowInterceptionRule> rules_;
    QHash<quintptr, InterceptedWindow> hidden_;
    QHash<quintptr, InterceptedWindow> recovery_;
    QString monitoringError_;
    QString lastError_;
    quint64 generation_ = 0;
    bool running_ = false;
};
