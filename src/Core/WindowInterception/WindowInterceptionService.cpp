#include "WindowInterceptionService.h"
#include "../ConfigManager/ConfigManager.h"

#include <QCoreApplication>
#include <QPointer>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <string>
#endif

namespace {
class NativeWindowBackend final : public WindowInterceptionBackend
{
public:
    ~NativeWindowBackend() override { unwatch(); }
    bool supported() const override
    {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }
    QVector<InterceptedWindow> enumerate(QString* error) override
    {
        if (error) error->clear();
        QVector<InterceptedWindow> result;
#ifdef Q_OS_WIN
        struct Context { NativeWindowBackend* backend; QVector<InterceptedWindow>* result; } context{this, &result};
        if (!EnumWindows([](HWND handle, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<Context*>(data);
            if (auto window = context->backend->inspect(reinterpret_cast<quintptr>(handle)))
                context->result->append(*window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context)) && error)
            *error = QObject::tr("无法枚举窗口（错误 %1）。").arg(GetLastError());
#endif
        return result;
    }
    std::optional<InterceptedWindow> inspect(quintptr id) override
    {
#ifdef Q_OS_WIN
        const auto handle = reinterpret_cast<HWND>(id);
        if (!IsWindow(handle) || GetAncestor(handle, GA_ROOT) != handle) return std::nullopt;
        DWORD pid = 0;
        if (!GetWindowThreadProcessId(handle, &pid) || pid == GetCurrentProcessId()) return std::nullopt;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return std::nullopt;
        std::wstring path(32768, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool found = QueryFullProcessImageNameW(process, 0, path.data(), &size)
            && GetProcessTimes(process, &created, &exited, &kernel, &user);
        CloseHandle(process);
        if (!found) return std::nullopt;
        wchar_t className[256]{};
        if (!GetClassNameW(handle, className, 256)) return std::nullopt;
        const int length = GetWindowTextLengthW(handle);
        std::wstring title(static_cast<size_t>(qBound(1, length + 1, 32768)), L'\0');
        const int count = GetWindowTextW(handle, title.data(), static_cast<int>(title.size()));
        return InterceptedWindow{id, static_cast<quint32>(pid),
            (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime,
            QString::fromWCharArray(title.data(), count),
            normalizedWindowPath(QString::fromWCharArray(path.data(), size)),
            QString::fromWCharArray(className), IsWindowVisible(handle) != FALSE};
#else
        Q_UNUSED(id);
        return std::nullopt;
#endif
    }
    bool setVisible(quintptr id, bool visible, QString* error) override
    {
#ifdef Q_OS_WIN
        const auto handle = reinterpret_cast<HWND>(id);
        ShowWindow(handle, visible ? SW_SHOWNA : SW_HIDE);
        // ShowWindow's return value is the *previous* visibility, not success.
        if (IsWindow(handle) && (IsWindowVisible(handle) != FALSE) == visible) return true;
        if (error) *error = QObject::tr("无法%1窗口，窗口可能已关闭或权限不足。")
                                .arg(visible ? QObject::tr("恢复") : QObject::tr("隐藏"));
#else
        Q_UNUSED(id); Q_UNUSED(visible);
        if (error) *error = QObject::tr("窗口拦截仅支持 Windows。");
#endif
        return false;
    }
    bool watch(std::function<void(quintptr)> shown, QString* error) override
    {
#ifdef Q_OS_WIN
        if (active_ && active_ != this) {
            if (error) *error = QObject::tr("窗口监听已被另一个服务占用。");
            return false;
        }
        shown_ = std::move(shown);
        active_ = this;
        hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &event,
            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook_) return true;
        if (error) *error = QObject::tr("无法监听新出现的窗口（错误 %1）。").arg(GetLastError());
        active_ = nullptr;
        shown_ = {};
#else
        Q_UNUSED(shown);
        if (error) *error = QObject::tr("窗口拦截仅支持 Windows。");
#endif
        return false;
    }
    void unwatch() override
    {
#ifdef Q_OS_WIN
        if (hook_) { UnhookWinEvent(hook_); hook_ = nullptr; }
        if (active_ == this) active_ = nullptr;
        shown_ = {};
#endif
    }
private:
#ifdef Q_OS_WIN
    static void CALLBACK event(HWINEVENTHOOK, DWORD, HWND handle, LONG object, LONG child, DWORD, DWORD)
    {
        if (active_ && object == OBJID_WINDOW && child == CHILDID_SELF && active_->shown_)
            active_->shown_(reinterpret_cast<quintptr>(handle));
    }
    inline static NativeWindowBackend* active_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    std::function<void(quintptr)> shown_;
#endif
};
}

WindowInterceptionService::WindowInterceptionService(QObject* parent)
    : WindowInterceptionService(std::make_unique<NativeWindowBackend>(), parent) {}

WindowInterceptionService::WindowInterceptionService(std::unique_ptr<WindowInterceptionBackend> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend))
{
    connect(qApp, &QCoreApplication::aboutToQuit, this, &WindowInterceptionService::stop);
}

WindowInterceptionService::~WindowInterceptionService() { stop(); }
bool WindowInterceptionService::supported() const { return backend_->supported(); }

bool WindowInterceptionService::fail(QString* error, const QString& message)
{
    lastError_ = message;
    if (error) *error = message;
    emit errorOccurred(message);
    return false;
}

bool WindowInterceptionService::start(QString* error)
{
    if (error) error->clear();
    if (running_) {
        if (error) *error = monitoringError_;
        return monitoringError_.isEmpty();
    }
    rules_ = ConfigManager::instance().config().windowInterceptionConfig.rules;
    if (!supported()) return fail(error, tr("窗口拦截仅支持 Windows，当前系统暂不可用。"));
    running_ = true;
    lastError_.clear();
    const auto generation = ++generation_;
    const QPointer<WindowInterceptionService> self(this);
    monitoringError_.clear();
    backend_->watch([self, generation](quintptr handle) {
        if (!self) return;
        // Always queue, including events received while ShowWindow is on the stack.
        QMetaObject::invokeMethod(self, [self, generation, handle] {
            if (self && self->running_ && generation == self->generation_) self->windowShown(handle);
        }, Qt::QueuedConnection);
    }, &monitoringError_);
    QString scanError;
    const bool scanned = applyRules(&scanError);
    if (!monitoringError_.isEmpty()) return fail(error, monitoringError_);
    if (!scanned) { if (error) *error = scanError; return false; }
    return true;
}

bool WindowInterceptionService::sameWindow(const InterceptedWindow& a, const InterceptedWindow& b)
{
    return a.handle == b.handle && a.processId == b.processId && a.processStarted == b.processStarted
        && normalizedWindowPath(a.exePath).compare(normalizedWindowPath(b.exePath), Qt::CaseInsensitive) == 0
        && a.className == b.className;
}

bool WindowInterceptionService::matches(const WindowInterceptionRule& rule, const InterceptedWindow& window)
{
    return normalizedWindowPath(rule.exePath).compare(normalizedWindowPath(window.exePath), Qt::CaseInsensitive) == 0
        && rule.className == window.className
        && (rule.titleContains.isEmpty() || window.title.contains(rule.titleContains, Qt::CaseInsensitive));
}

bool WindowInterceptionService::owned(const InterceptedWindow& window) const
{
    const auto it = hidden_.constFind(window.handle);
    return it != hidden_.cend() && sameWindow(*it, window);
}

bool WindowInterceptionService::blocked(const InterceptedWindow& window) const
{
    for (const auto& rule : rules_) if (rule.enabled && matches(rule, window)) return true;
    return false;
}

std::optional<InterceptedWindow> WindowInterceptionService::checked(const InterceptedWindow& expected, QString* error)
{
    if (!running_) { fail(error, tr("窗口拦截服务未运行。")); return std::nullopt; }
    auto current = backend_->inspect(expected.handle);
    if (!current || !sameWindow(expected, *current)) {
        fail(error, tr("所选窗口已关闭、发生变化或无法访问，请刷新列表。"));
        return std::nullopt;
    }
    return current;
}

QVector<InterceptedWindow> WindowInterceptionService::windows(QString* error)
{
    if (error) error->clear();
    QVector<InterceptedWindow> result;
    if (!supported()) return result;
    QString message;
    const auto all = backend_->enumerate(&message);
    if (!message.isEmpty()) { fail(error, message); return result; }
    for (auto window : all) {
        window.hiddenByUs = owned(window);
        bool known = false;
        for (const auto& rule : rules_) if (matches(rule, window)) { known = true; break; }
        const auto recovery = recovery_.constFind(window.handle);
        if (window.visible || window.hiddenByUs || known
            || (recovery != recovery_.cend() && sameWindow(*recovery, window))) result.append(window);
    }
    // Destroyed windows must not accumulate in a long-running tray process.
    for (auto* tracked : {&hidden_, &recovery_}) {
        for (auto it = tracked->begin(); it != tracked->end();) {
            const auto current = backend_->inspect(it.key());
            if (!current || !sameWindow(*it, *current)) it = tracked->erase(it);
            else ++it;
        }
    }
    return result;
}

bool WindowInterceptionService::hide(const InterceptedWindow& expected, QString* error)
{
    auto current = checked(expected, error);
    if (!current) return false;
    if (!current->visible) return true;
    QString message;
    if (!backend_->setVisible(current->handle, false, &message)) return fail(error, message);
    hidden_.insert(current->handle, *current);
    return true;
}

bool WindowInterceptionService::show(const InterceptedWindow& expected, QString* error)
{
    auto current = checked(expected, error);
    if (!current) return false;
    QString message;
    if (!current->visible && !backend_->setVisible(current->handle, true, &message)) return fail(error, message);
    hidden_.remove(current->handle);
    recovery_.remove(current->handle);
    return true;
}

bool WindowInterceptionService::hideOnce(const InterceptedWindow& window, QString* error)
{
    if (error) error->clear();
    const bool result = hide(window, error);
    emit windowsChanged();
    return result;
}

bool WindowInterceptionService::persist(const QVector<WindowInterceptionRule>& rules, QString* error)
{
    if (!running_) return fail(error, tr("窗口拦截服务未运行。"));
    Config candidate = ConfigManager::instance().config();
    candidate.windowInterceptionConfig.rules = rules;
    QString message;
    if (!ConfigManager::instance().commit(candidate, &message))
        return fail(error, tr("无法保存窗口规则：%1").arg(message));
    rules_ = ConfigManager::instance().config().windowInterceptionConfig.rules;
    emit rulesChanged();
    return true;
}

bool WindowInterceptionService::hideAndSave(const InterceptedWindow& expected, const QString& title, QString* error)
{
    if (error) error->clear();
    const auto current = checked(expected, error);
    if (!current) return false;
    WindowInterceptionRule rule{current->exePath, current->className, title.trimmed(), true};
    if (!matches(rule, *current)) return fail(error, tr("所填标题条件与当前窗口不匹配。"));
    auto updated = rules_;
    bool found = false;
    for (auto& existing : updated) {
        if (normalizedWindowPath(existing.exePath).compare(normalizedWindowPath(rule.exePath), Qt::CaseInsensitive) == 0
            && existing.className == rule.className && existing.titleContains == rule.titleContains) {
            existing.enabled = true; found = true; break;
        }
    }
    if (!found) updated.append(rule);
    if (!persist(updated, error)) return false;
    QString message;
    const bool result = hide(*current, &message);
    if (!result) fail(error, tr("规则已保存，但未能隐藏所选窗口：%1").arg(message));
    emit windowsChanged();
    return result;
}

bool WindowInterceptionService::restore(const InterceptedWindow& expected, QString* error)
{
    if (error) error->clear();
    const auto current = checked(expected, error);
    if (!current) return false;
    if (blocked(*current)) return fail(error, tr("该窗口有启用中的规则，请先选择“恢复并停用”。"));
    const bool result = show(*current, error);
    emit windowsChanged();
    return result;
}

bool WindowInterceptionService::applyRules(QString* error)
{
    QString message;
    const auto all = backend_->enumerate(&message);
    if (!message.isEmpty()) return fail(error, message);
    bool result = true;
    for (const auto& window : all) if (blocked(window) && !hide(window, error)) result = false;
    emit windowsChanged();
    return result;
}

bool WindowInterceptionService::restoreMatches(const WindowInterceptionRule& rule, QString* error)
{
    QString message;
    const auto all = backend_->enumerate(&message);
    if (!message.isEmpty()) return fail(error, message);
    bool result = true;
    for (const auto& window : all) {
        if (window.visible || !matches(rule, window) || blocked(window)) continue;
        if (owned(window)) {
            if (!show(window, error)) result = false;
        } else recovery_.insert(window.handle, window);
    }
    return result;
}

bool WindowInterceptionService::setRuleEnabled(int index, bool enabled, QString* error)
{
    if (error) error->clear();
    if (index < 0 || index >= rules_.size()) return fail(error, tr("所选规则不存在，请刷新列表。"));
    auto updated = rules_;
    updated[index].enabled = enabled;
    if (!persist(updated, error)) return false;
    const bool result = enabled ? applyRules(error) : restoreMatches(updated[index], error);
    if (!result && error) *error = tr("规则已%1，但部分窗口操作失败：%2")
        .arg(enabled ? tr("启用") : tr("停用"), *error);
    emit windowsChanged();
    return result;
}

bool WindowInterceptionService::deleteRule(int index, QString* error)
{
    if (error) error->clear();
    if (index < 0 || index >= rules_.size()) return fail(error, tr("所选规则不存在，请刷新列表。"));
    auto updated = rules_;
    const auto removed = updated.takeAt(index);
    if (!persist(updated, error)) return false;
    const bool result = restoreMatches(removed, error);
    if (!result && error) *error = tr("规则已删除，但部分窗口恢复失败：%1").arg(*error);
    emit windowsChanged();
    return result;
}

void WindowInterceptionService::windowShown(quintptr handle)
{
    const auto current = backend_->inspect(handle);
    if (current && current->visible && blocked(*current)) {
        hide(*current, nullptr);
        emit windowsChanged();
    }
}

void WindowInterceptionService::stop()
{
    if (!running_) return;
    running_ = false;
    ++generation_;
    backend_->unwatch();
    for (const auto& expected : std::as_const(hidden_)) {
        const auto current = backend_->inspect(expected.handle);
        if (!current || !sameWindow(expected, *current) || current->visible) continue;
        QString error;
        if (!backend_->setVisible(current->handle, true, &error))
            fail(nullptr, tr("退出时恢复窗口失败：%1").arg(error));
    }
    hidden_.clear();
    recovery_.clear();
}
