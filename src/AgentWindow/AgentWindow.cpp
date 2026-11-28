#include "AgentWindow.h"

#include "../Ui/MarkdownDocument.h"
#include "../Ui/UiWidgets.h"
#include "../Core/Plugins/PluginWorkspace.h"
#include "../Ui/Plugins/PluginHistoryDialog.h"
#include <QJsonDocument>
#include <ElaCheckBox.h>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaScrollBar.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToolButton.h>
#include <ElaWidgetToolsDef.h>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QDir>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleOptionToolButton>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

namespace
{
using namespace Qt::StringLiterals;
using Permission = AgentService::Permission;
constexpr int controlHeight = ElaWidgetToolsSize::DefaultControlHeight;
constexpr int conversationWidth = 780;
constexpr qsizetype inlineMarkdownLimit = 24 * 1024;

QString pageKey(const QWidget *page) { return page->property("ElaPageKey").toString(); }
QString workspaceName(const QString &path)
{
    const auto name = QFileInfo(path).fileName();
    return path.isEmpty() ? u"无工作区"_s : name.isEmpty() ? path : name;
}

QColor chatBackground(ElaThemeType::ThemeMode mode)
{
    return mode == ElaThemeType::Dark ? QColor(25, 25, 25) : QColor(250, 250, 250);
}

QColor warningColor(ElaThemeType::ThemeMode mode)
{
    return mode == ElaThemeType::Dark ? QColor(237, 148, 85) : QColor(166, 77, 19);
}

QString reasoningPreview(const QString &text)
{
    if (text.size() <= inlineMarkdownLimit) return text;
    const auto suffix = u"\n\n…（仅显示前 24 Ki 字符）"_s;
    auto head = text.left(inlineMarkdownLimit - suffix.size());
    if (head.at(head.size() - 1).isHighSurrogate()) head.chop(1);
    return head + suffix;
}

QString permissionLabel(Permission permission)
{
    switch (permission) {
    case Permission::ReadOnly: return u"只读"_s;
    case Permission::Ask: return u"审批"_s;
    case Permission::Yolo: return u"YOLO"_s;
    }
    return {};
}

QString effortLabel(const QString &effort)
{
    if (effort == "none"_L1) return u"关闭"_s;
    if (effort == "low"_L1) return u"低"_s;
    if (effort == "high"_L1) return u"高"_s;
    if (effort == "max"_L1) return u"最高"_s;
    return u"模型默认"_s;
}

class ComposerArea final : public ElaScrollPageArea
{
public:
    explicit ComposerArea(QWidget *parent) : ElaScrollPageArea(parent)
    {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = eTheme->getThemeMode() == ElaThemeType::Dark;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(dark ? QColor(51, 51, 51) : QColor(255, 255, 255));
        painter.setPen(dark ? QColor(66, 66, 66) : QColor(220, 220, 220));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 22, 22);
    }
};

class UserBubble final : public ElaScrollPageArea
{
public:
    explicit UserBubble(const QString &text, QWidget *parent) : ElaScrollPageArea(parent)
    {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMaximumWidth(620);
        auto textFont = font();
        textFont.setPixelSize(14);
        textFont.setLetterSpacing(QFont::AbsoluteSpacing, .5);
        const QFontMetrics metrics(textFont);
        for (const auto &line : text.split(u'\n'))
            preferredWidth_ = qMax(preferredWidth_, qMin(620, metrics.horizontalAdvance(line) + 34));
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
    QSize sizeHint() const override
    {
        auto size = ElaScrollPageArea::sizeHint();
        size.setWidth(preferredWidth_);
        return size;
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto mode = eTheme->getThemeMode();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(chatBackground(mode));
        painter.drawRoundedRect(QRectF(rect()), 17, 17);
        auto accent = ElaThemeColor(mode, PrimaryNormal);
        accent.setAlpha(mode == ElaThemeType::Dark ? 96 : 40);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(rect()), 17, 17);
    }
private:
    int preferredWidth_ = 60;
};

// Keep activity rows quiet; Ela still provides their focus, keyboard and click behavior.
class ActivityButton final : public ElaToolButton
{
public:
    explicit ActivityButton(QWidget *parent) : ElaToolButton(parent)
    {
        setFixedHeight(controlHeight);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto mode = eTheme->getThemeMode();
        const bool warning = property("activityWarning").toBool();
        auto color = warning ? warningColor(mode) : ElaThemeColor(mode, BasicText);
        if (!warning) color.setAlpha(underMouse() ? 230 : 155);
        painter.setPen(color);
        painter.setFont(font());
        const auto suffix = isCheckable() ? (isChecked() ? u" ⌄"_s : u" ›"_s) : QString();
        const auto label = fontMetrics().elidedText(text(), Qt::ElideRight, qMax(0, width() - 30 - fontMetrics().horizontalAdvance(suffix)));
        const auto icon = property("ElaIconType").toString();
        const int inset = icon.isEmpty() ? 0 : 23;
        painter.drawText(rect().adjusted(inset, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, label + suffix);
        if (!icon.isEmpty()) {
            QFont iconFont(u"ElaAwesome"_s);
            iconFont.setPixelSize(13);
            painter.setFont(iconFont);
            painter.drawText(QRect(0, 0, 17, height()), Qt::AlignCenter, icon);
        }
        if (hasFocus()) {
            painter.setPen(QPen(color, 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 3, 3);
        }
    }
};

// Shared by live updates and persisted tool events.
class ToolDetails final : public QWidget
{
public:
    explicit ToolDetails(QWidget *parent) : QWidget(parent)
    {
        setObjectName(u"agentToolDetails"_s);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);
        input_ = new QWidget(this);
        auto *inputLayout = new QVBoxLayout(input_);
        inputLayout->setContentsMargins(0, 0, 0, 0);
        auto *heading = new QHBoxLayout;
        location_ = new QLabel(input_);
        location_->setWordWrap(true);
        location_->setTextFormat(Qt::PlainText);
        heading->addWidget(location_, 1);
        copy_ = new ElaToolButton(input_);
        copy_->setText(u"复制"_s);
        copy_->setToolTip(u"复制完整原始命令"_s);
        copy_->setFixedSize(44, controlHeight);
        heading->addWidget(copy_, 0, Qt::AlignTop);
        inputLayout->addLayout(heading);
        command_ = new QLabel(input_);
        command_->setTextFormat(Qt::PlainText);
        command_->setWordWrap(true);
        command_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(13);
        command_->setFont(mono);
        command_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        inputLayout->addWidget(command_);
        layout->addWidget(input_);
        separator_ = new QFrame(this);
        separator_->setFrameShape(QFrame::HLine);
        layout->addWidget(separator_);
        output_ = new QPlainTextEdit(this);
        output_->setObjectName(u"agentToolOutput"_s);
        output_->setReadOnly(true);
        output_->setFont(mono);
        output_->setFrameShape(QFrame::NoFrame);
        output_->setLineWrapMode(QPlainTextEdit::NoWrap);
        output_->setFixedHeight(64);
        layout->addWidget(output_);
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        status_->setTextFormat(Qt::PlainText);
        layout->addWidget(status_);
        connect(copy_, &ElaToolButton::clicked, this, [this] { QApplication::clipboard()->setText(script_); });
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { refreshTheme(); });
        refreshTheme();
    }

    void setEvent(const QJsonObject &event)
    {
        const bool shell = event.value(u"name"_s).toString() == u"shell"_s || event.value(u"name"_s).toString() == u"powershell"_s;
        input_->setVisible(shell); separator_->setVisible(shell);
        const auto command = event.value(u"command"_s).toObject();
        script_ = command.value(u"script"_s).toString();
        if (shell) {
            location_->setText(command.value(u"cwd"_s).toString());
            command_->setText(script_.isEmpty() ? u"原始命令未记录"_s : script_);
            copy_->setEnabled(!script_.isEmpty());
        }
        auto text = event.value(u"output"_s).toString();
        if (!shell) {
            const auto detail = event.value(u"detail"_s).toString();
            text = detail + (detail.isEmpty() || text.isEmpty() ? QString() : u"\n\n"_s) + text;
        }
        const auto error = event.value(u"error"_s).toString();
        if (!error.isEmpty()) text += (text.isEmpty() ? QString() : u"\n\n"_s) + error;
        if (event.value(u"historyTruncated"_s).toBool()) text += u"\n…（历史输出已截断）"_s;
        if (text.isEmpty()) text = event.value(u"state"_s).toString() == u"正在执行"_s ? u"等待输出…"_s : u"（无输出）"_s;
        setOutput(text);
        const auto result = event.value(u"result"_s).toObject();
        QStringList parts{event.value(u"state"_s).toString()};
        if (result.contains(u"elapsed_ms"_s)) parts.append(u"用时 %1 秒"_s.arg(result.value(u"elapsed_ms"_s).toDouble() / 1000, 0, 'f', 1));
        if (result.contains(u"exit_code"_s)) parts.append(u"退出码 %1"_s.arg(result.value(u"exit_code"_s).toInteger()));
        if (result.value(u"timed_out"_s).toBool()) parts.append(u"已超时"_s);
        if (result.value(u"truncated"_s).toBool()) parts.append(u"输出已截断"_s);
        status_->setText(parts.join(u" · "_s));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = eTheme->getThemeMode() == ElaThemeType::Dark;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(dark ? QColor(30, 30, 30) : QColor(247, 247, 247));
        painter.setPen(dark ? QColor(57, 57, 57) : QColor(221, 221, 221));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 12, 12);
    }

private:
    void refreshTheme()
    {
        const auto mode = eTheme->getThemeMode();
        const bool dark = mode == ElaThemeType::Dark;
        auto palette = output_->palette();
        palette.setColor(QPalette::Base, dark ? QColor(30, 30, 30) : QColor(247, 247, 247));
        palette.setColor(QPalette::Text, ElaThemeColor(mode, BasicText));
        output_->setPalette(palette);
        output_->setStyleSheet(u"QPlainTextEdit { background-color:%1; color:%2; border:0; }"_s.arg(
            palette.color(QPalette::Base).name(), palette.color(QPalette::Text).name()));
        separator_->setStyleSheet(u"color:%1;"_s.arg(dark ? u"#393939"_s : u"#dddddd"_s));
        const auto style = u"color:%1;background:transparent;"_s.arg(ElaThemeColor(mode, BasicText).name());
        command_->setStyleSheet(style);
        const auto muted = u"color:%1;background:transparent;"_s.arg(ElaThemeColor(mode, BasicDetailsText).name());
        location_->setStyleSheet(muted); status_->setStyleSheet(muted);
        update();
    }
    void setOutput(QString text)
    {
        text.replace(u"\r\n"_s, u"\n"_s);
        text.replace(u'\r', u'\n');
        const auto previous = output_->toPlainText();
        if (previous == text) return;
        auto *bar = output_->verticalScrollBar();
        const auto position = bar->value();
        const auto selection = output_->textCursor();
        const bool follow = !selection.hasSelection() && position >= bar->maximum() - 3;
        qsizetype prefix = 0;
        while (prefix < previous.size() && prefix < text.size() && previous[prefix] == text[prefix]) ++prefix;
        if (prefix && prefix < previous.size() && previous[prefix - 1].isHighSurrogate()) --prefix;
        QTextCursor cursor(output_->document());
        cursor.setPosition(int(prefix));
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.insertText(text.mid(prefix));
        QTextCursor restored(output_->document());
        restored.setPosition(qMin(selection.anchor(), int(text.size())));
        restored.setPosition(qMin(selection.position(), int(text.size())), QTextCursor::KeepAnchor);
        output_->setTextCursor(restored);
        output_->setFixedHeight(qBound(64, output_->document()->blockCount() * output_->fontMetrics().lineSpacing() + 28, 320));
        bar->setValue(follow ? bar->maximum() : position);
    }
    QWidget *input_;
    QLabel *location_, *command_, *status_;
    ElaToolButton *copy_;
    QFrame *separator_;
    QPlainTextEdit *output_;
    QString script_;
};

class SendButton final : public ElaToolButton
{
public:
    explicit SendButton(QWidget *parent) : ElaToolButton(parent)
    {
        setFixedSize(controlHeight, controlHeight);
        setCursor(Qt::PointingHandCursor);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto mode = eTheme->getThemeMode();
        QColor fill, ink;
        if (!isEnabled()) {
            fill = ElaThemeColor(mode, BasicTextDisable);
            fill.setAlpha(40);
            ink = ElaThemeColor(mode, BasicTextDisable);
        } else {
            fill = isDown() ? ElaThemeColor(mode, PrimaryPress)
                 : underMouse() ? ElaThemeColor(mode, PrimaryHover)
                 : ElaThemeColor(mode, PrimaryNormal);
            ink = ElaThemeColor(mode, BasicTextInvert);
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const QRectF bounds(rect());
        const QPointF center = bounds.center();
        painter.drawEllipse(bounds.adjusted(1, 1, -1, -1));
        painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (property("stopping").toBool()) {
            painter.setBrush(ink);
            painter.drawRoundedRect(QRectF(center - QPointF(5, 5), QSizeF(10, 10)), 1, 1);
        } else {
            const QPointF tip = center - QPointF(0, 6);
            painter.drawLine(tip, center + QPointF(0, 6));
            painter.drawLine(tip, center - QPointF(5, 1));
            painter.drawLine(tip, center + QPointF(5, -1));
        }
        if (hasFocus()) {
            painter.setPen(QPen(ElaThemeColor(eTheme->getThemeMode(), BasicText), 1, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect()).adjusted(.5, .5, -.5, -.5));
        }
    }
};

class LatestButton final : public ElaToolButton
{
public:
    explicit LatestButton(QWidget *parent) : ElaToolButton(parent)
    {
        setIsTransparent(true);
        setAutoFillBackground(false);
        setFixedSize(controlHeight, controlHeight);
        setCursor(Qt::PointingHandCursor);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto ink = ElaThemeColor(eTheme->getThemeMode(), BasicText);
        ink.setAlpha(underMouse() || hasFocus() ? 255 : 180);
        painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF bounds(rect());
        const auto center = bounds.center();
        const auto tip = center + QPointF(0, 6);
        painter.drawEllipse(bounds.adjusted(1.5, 1.5, -1.5, -1.5));
        painter.drawLine(center - QPointF(0, 6), tip);
        painter.drawLine(tip, center + QPointF(-5, 1));
        painter.drawLine(tip, center + QPointF(5, 1));
    }
};

class AccessButton final : public ElaToolButton
{
public:
    explicit AccessButton(QWidget *parent) : ElaToolButton(parent) {}
protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (!property("fullAccess").toBool() || !isEnabled()) {
            ElaToolButton::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ElaThemeColor(eTheme->getThemeMode(), BasicHoverAlpha));
            painter.drawRoundedRect(rect(), 12, 12);
        }
        painter.setPen(warningColor(eTheme->getThemeMode()));
        QFont iconFont(u"ElaAwesome"_s);
        iconFont.setPixelSize(15);
        painter.setFont(iconFont);
        painter.drawText(QRect(6, 0, 20, height()), Qt::AlignCenter,
                         QString(QChar(static_cast<ushort>(ElaIconType::Shield))));
        painter.setFont(font());
        painter.drawText(rect().adjusted(32, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};
}

class ApprovalCard final : public ElaScrollPageArea
{
public:
    explicit ApprovalCard(QWidget *parent) : ElaScrollPageArea(parent)
    {
        setObjectName(u"agentApprovalCard"_s);
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(6);
        status_ = AppUi::text(u"等待审批"_s, this, 12);
        status_->setTextFormat(Qt::PlainText);
        layout->addWidget(status_);
        summary_ = AppUi::text({}, this, 15);
        summary_->setTextFormat(Qt::PlainText);
        summary_->setWordWrap(true);
        summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(summary_);
        description_ = new ElaPlainTextEdit(this);
        description_->setObjectName(u"agentApprovalDetails"_s);
        description_->setAccessibleName(u"待审批操作详情"_s);
        description_->setReadOnly(true);
        description_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        description_->setFrameShape(QFrame::NoFrame);
        description_->document()->setDocumentMargin(4);
        auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(13);
        description_->setFont(mono);
        layout->addWidget(description_);
        auto *actions = new QHBoxLayout;
        actions->setSpacing(8);
        stop_ = AppUi::button(u"停止"_s, this);
        stop_->setObjectName(u"agentApprovalStop"_s);
        actions->addWidget(stop_);
        actions->addStretch();
        deny_ = AppUi::button(u"拒绝"_s, this);
        deny_->setObjectName(u"agentApprovalDeny"_s);
        allow_ = AppUi::button(u"允许"_s, this, true);
        allow_->setObjectName(u"agentApprovalAllow"_s);
        actions->addWidget(deny_);
        actions->addWidget(allow_);
        layout->addLayout(actions);
        for (auto *button : {stop_, deny_, allow_}) button->setAutoDefault(false);
        applyTheme();
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { applyTheme(); update(); });
    }

    ElaPushButton *allowButton() const { return allow_; }
    ElaPushButton *denyButton() const { return deny_; }
    ElaPushButton *stopButton() const { return stop_; }

    void setActionsEnabled(bool enabled)
    {
        allow_->setEnabled(enabled);
        deny_->setEnabled(enabled);
        stop_->setEnabled(enabled);
    }

    void focusDetails()
    {
        if (description_->isHidden()) setFocus();
        else description_->setFocus();
    }

    void setContent(const QString &summary, const QString &payload, bool newRequest)
    {
        summary_->setText(summary);
        description_->setVisible(!payload.isEmpty());
        if (!newRequest && description_->toPlainText() == payload) return;
        description_->setPlainText(payload);
        description_->setFixedHeight(qBound(46, description_->document()->blockCount() * description_->fontMetrics().lineSpacing() + 20, 128));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto mode = eTheme->getThemeMode();
        auto border = warningColor(mode);
        border.setAlpha(mode == ElaThemeType::Dark ? 110 : 80);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(border, 1));
        painter.setBrush(mode == ElaThemeType::Dark ? QColor(31, 31, 31) : QColor(255, 250, 243));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 10, 10);
    }

private:
    void applyTheme()
    {
        const auto mode = eTheme->getThemeMode();
        status_->setStyleSheet(u"color:%1;background:transparent;"_s.arg(warningColor(mode).name()));
        description_->setStyleSheet(u"#agentApprovalDetails { color:%1;background-color:%2;border:0; }"_s.arg(
            ElaThemeColor(mode, BasicText).name(), mode == ElaThemeType::Dark ? u"#1f1f1f"_s : u"#fffaf3"_s));
    }
    ElaText *status_, *summary_;
    ElaPlainTextEdit *description_;
    ElaPushButton *allow_, *deny_, *stop_;
};

namespace
{
QString toolLabel(const QString &name)
{
    if (name == "list_directory"_L1) return u"列出目录"_s;
    if (name == "read_file"_L1 || name == "read_text"_L1) return u"读取文件"_s;
    if (name == "write_file"_L1) return u"写入文件"_s;
    if (name == "edit_file"_L1) return u"编辑文件"_s;
    if (name == "open_application"_L1) return u"启动应用"_s;
    if (name == "shell"_L1 || name == "powershell"_L1) return u"系统命令"_s;
    if (name == "get_plugin_draft"_L1) return u"读取插件草稿"_s;
    if (name == "preview_plugin"_L1) return u"打开插件预览"_s;
    if (name == "inspect_plugin_preview"_L1) return u"检查插件预览"_s;
    if (name == "stop_plugin_preview"_L1) return u"关闭插件预览"_s;
    if (name == "list_plugin_actions"_L1) return u"查看插件动作"_s;
    if (name == "call_plugin_action"_L1) return u"调用插件动作"_s;
    if (name == "get_plugin_sdk"_L1) return u"读取插件开发文档"_s;
    if (name == "write_plugin_draft"_L1) return u"更新插件草稿"_s;
    if (name == "update_plan"_L1) return u"更新任务步骤"_s;
    if (name == "list_plugins"_L1) return u"查看已安装插件"_s;
    if (name == "open_plugin"_L1) return u"选择插件"_s;
    if (name == "validate_plugin"_L1) return u"校验插件"_s;
    if (name == "apply_plugin"_L1) return u"应用插件修改"_s;
    if (name == "list_plugin_versions"_L1) return u"查看插件历史"_s;
    if (name == "rollback_plugin"_L1) return u"回滚插件"_s;
    if (name == "find_files"_L1) return u"搜索文件"_s;
    if (name == "search_files"_L1) return u"搜索文件内容"_s;
    if (name == "apply_patch"_L1) return u"批量修改文件"_s;
    if (name == "delete_file"_L1) return u"删除文件"_s;
    if (name == "rename_file"_L1) return u"重命名文件"_s;
    return name;
}

void setMarkdown(ElaText *label, const QString &text)
{
    label->setProperty("sourceMarkdown", text);
    if (auto *full = label->parentWidget()->findChild<ElaToolButton *>(u"agentFullMessage"_s))
        full->setVisible(text.size() > inlineMarkdownLimit);
    auto preview = text.left(inlineMarkdownLimit);
    if (!preview.isEmpty() && preview.at(preview.size() - 1).isHighSurrogate()) preview.chop(1);
    if (label->property("renderedPreview").toString() == preview && label->property("hasPreview").toBool()) return;
    label->setProperty("renderedPreview", preview);
    label->setProperty("hasPreview", true);
    AppUi::MarkdownDocument document;
    document.setDefaultFont(label->font());
    document.setDefaultStyleSheet(u"pre { white-space: pre-wrap; font-family: Consolas; } code { font-family: Consolas; }"_s);
    document.setSource(preview);
    const auto codeBlock = [](const QTextBlock &block) {
        if (!block.isValid()) return false;
        const auto format = block.blockFormat();
        return format.hasProperty(QTextFormat::BlockCodeFence) ||
            format.hasProperty(QTextFormat::BlockCodeLanguage) || format.nonBreakableLines();
    };
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        auto format = block.blockFormat();
        const bool code = codeBlock(block);
        format.setLineHeight(code ? 125 : 140, QTextBlockFormat::ProportionalHeight);
        format.setBottomMargin(code ? (codeBlock(block.next()) ? 0 : 10) : block.textList() ? 3 : 10);
        if (code) {
            format.setTopMargin(codeBlock(block.previous()) ? 0 : 8);
            format.setLeftMargin(10);
            format.setRightMargin(10);
            format.setBackground(eTheme->getThemeMode() == ElaThemeType::Dark ? QColor(40, 40, 40) : QColor(237, 239, 242));
            format.setNonBreakableLines(false);
            QTextCursor cursor(block);
            cursor.select(QTextCursor::BlockUnderCursor);
            QTextCharFormat characters;
            characters.setFontFamilies({u"Consolas"_s});
            cursor.mergeCharFormat(characters);
        }
        QTextCursor(block).setBlockFormat(format);
    }
    label->setText(document.toHtml().replace(u"white-space: pre;"_s, u"white-space: pre-wrap;"_s));
}

ElaToolButton *iconButton(ElaIconType::IconName icon, const QString &text, QWidget *parent)
{
    auto *button = new ElaToolButton(parent);
    button->setElaIcon(icon);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setFixedSize(controlHeight, controlHeight);
    return button;
}

ElaToolButton *composerButton(QWidget *parent, bool access = false)
{
    ElaToolButton *button = access ? new AccessButton(parent) : new ElaToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setIsTransparent(true);
    button->setBorderRadius(12);
    button->setFixedHeight(controlHeight);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

template <typename Handler>
QAction *addMenuAction(ElaMenu *menu, ElaIconType::IconName icon, const QString &text, QObject *context, Handler &&handler)
{
    auto *action = menu->addElaIconAction(icon, text);
    QObject::connect(action, &QAction::triggered, context, std::forward<Handler>(handler));
    return action;
}

ElaText *messageLabel(QWidget *parent, int size = 14)
{
    auto *label = AppUi::text({}, parent, size);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    // ElaText custom wrap painting omits QLabel selection; use the native text renderer.
    label->setIsWrapAnywhere(false);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QHBoxLayout *centeredColumn(QWidget *column)
{
    column->setMaximumWidth(conversationWidth);
    auto *layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch();
    layout->addWidget(column, 1);
    layout->addStretch();
    return layout;
}

class PreviewImage final : public ElaText
{
public:
    explicit PreviewImage(QPixmap image, QWidget *parent) : ElaText(parent), image_(std::move(image))
    {
        setObjectName(u"pluginPreviewScreenshot"_s);
        setAccessibleName(u"插件预览截图"_s);
        if (image_.isNull()) {
            setText(u"预览截图已不可用"_s);
            return;
        }
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto policy = sizePolicy();
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        setToolTip(accessibleName());
    }

    int heightForWidth(int width) const override
    {
        if (image_.isNull()) return ElaText::heightForWidth(width);
        return qRound(imageSize(width).height());
    }

    QSize sizeHint() const override
    {
        return image_.isNull() ? ElaText::sizeHint() : imageSize(640).toSize();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (image_.isNull()) {
            ElaText::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRectF(QPointF(), imageSize(width())), image_, image_.rect());
    }

private:
    QSizeF imageSize(int width) const
    {
        const auto original = image_.deviceIndependentSize();
        return original * (qMin(qreal(qBound(0, width, 640)), original.width()) / original.width());
    }

    QPixmap image_;
};
}

class ModelButton final : public ElaToolButton
{
public:
    explicit ModelButton(QWidget *parent) : ElaToolButton(parent)
    {
        setToolButtonStyle(Qt::ToolButtonTextOnly);
        setIsTransparent(true);
        setBorderRadius(12);
        setFixedHeight(controlHeight);
        setCursor(Qt::PointingHandCursor);
        setMaximumWidth(360);
    }

    void setSelection(const QString &label, const QString &effort)
    {
        label_ = label;
        effortSuffix_ = effort.isEmpty() ? QString() : u" · "_s + effort;
        setText(label_ + effortSuffix_);
    }

    QSize sizeHint() const override
    {
        return {qMin(maximumWidth(), fontMetrics().horizontalAdvance(text()) + horizontalPadding), controlHeight};
    }

    QSize minimumSizeHint() const override
    {
        return {qMin(96, sizeHint().width()), controlHeight};
    }

protected:
    void initStyleOption(QStyleOptionToolButton *option) const override
    {
        ElaToolButton::initStyleOption(option);
        const int textWidth = qMax(0, option->rect.width() - horizontalPadding);
        const auto &metrics = option->fontMetrics;
        const auto suffix = textWidth < metrics.horizontalAdvance(u"模型…"_s + effortSuffix_) ? QString() : effortSuffix_;
        option->text = metrics.elidedText(label_, Qt::ElideRight, textWidth - metrics.horizontalAdvance(suffix)) + suffix;
    }

private:
    static constexpr int horizontalPadding = 16;
    QString label_, effortSuffix_;
};

class ActivityGroup final : public QWidget
{
public:
    explicit ActivityGroup(QWidget *parent) : QWidget(parent)
    {
        setObjectName(u"agentActivities"_s);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        header_ = new ActivityButton(this);
        header_->setObjectName(u"agentActivitySummary"_s);
        header_->setElaIcon(ElaIconType::Code);
        header_->setCheckable(true);
        layout->addWidget(header_);
        auto *items = new QWidget(this);
        items_ = new QVBoxLayout(items);
        items_->setContentsMargins(13, 0, 0, 3);
        items_->setSpacing(2);
        layout->addWidget(items);
        items->hide();
        connect(header_, &ElaToolButton::toggled, items, &QWidget::setVisible);
    }

    void add(QWidget *item)
    {
        items_->addWidget(item);
        refreshSummary();
    }

    void refreshSummary()
    {
        bool waiting = false, running = false, thinking = false;
        int failed = 0, denied = 0, interrupted = 0;
        for (int i = 0; i < items_->count(); ++i) {
            const auto state = items_->itemAt(i)->widget()->property("activityState").toString();
            waiting |= state == u"待审批"_s;
            running |= state == u"正在执行"_s;
            thinking |= state == u"正在思考…"_s;
            failed += state == u"失败"_s;
            denied += state == u"已拒绝"_s;
            interrupted += state == u"已中断"_s || state == u"思考中断"_s;
        }
        QStringList parts{u"活动记录 · %1 项"_s.arg(items_->count())};
        if (failed) parts.append(u"%1 项失败"_s.arg(failed));
        if (denied) parts.append(u"%1 项拒绝"_s.arg(denied));
        if (interrupted) parts.append(u"%1 项中断"_s.arg(interrupted));
        if (waiting) parts.append(u"等待审批"_s);
        else if (running) parts.append(u"正在执行"_s);
        else if (thinking) parts.append(u"正在思考"_s);
        const auto text = parts.join(u" · "_s);
        header_->setText(text);
        header_->setToolTip(text);
        header_->setAccessibleName(text);
        header_->setProperty("activityWarning", failed || denied || interrupted || waiting);
        header_->update();
    }

private:
    ActivityButton *header_;
    QVBoxLayout *items_;
};

class AgentScrollArea : public ElaScrollArea
{
public:
    explicit AgentScrollArea(QWidget *parent) : ElaScrollArea(parent)
    {
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFrameShape(QFrame::NoFrame);
        viewport()->setAutoFillBackground(false);
        AppUi::enableTouchScrolling(this);

        auto *bar = static_cast<ElaScrollBar *>(verticalScrollBar());
        // Range changes stop Ela's wheel animation, so apply wheel input immediately during streaming.
        bar->setIsSmoothScroll(false);
        connect(bar, &QScrollBar::rangeChanged, this, [this] { scheduleFollow(); });
        connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
            // A shrinking range can clamp the position without user input.
            if (!restoringPosition_ && lastPosition_ <= bar->maximum())
                followingEnd_ = value == bar->maximum();
            lastPosition_ = value;
        });
    }

    void scrollToEnd()
    {
        followingEnd_ = true;
        auto *bar = verticalScrollBar();
        bar->setValue(bar->maximum());
        scheduleFollow();
    }

    void restorePosition(int position)
    {
        if (followingEnd_) return;
        QTimer::singleShot(0, this, [this, position] {
            if (followingEnd_) return;
            const QScopedValueRollback restoring(restoringPosition_, true);
            verticalScrollBar()->setValue(position);
        });
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (event->pixelDelta().isNull()) {
            ElaScrollArea::wheelEvent(event);
            return;
        }
        auto *bar = verticalScrollBar();
        const int position = bar->value();
        bar->setValue(position - event->pixelDelta().y());
        event->setAccepted(bar->value() != position);
    }

private:
    void scheduleFollow()
    {
        if (!followingEnd_ || followScheduled_) return;
        followScheduled_ = true;
        QTimer::singleShot(0, this, [this] {
            followScheduled_ = false;
            if (!followingEnd_) return;
            auto *bar = verticalScrollBar();
            bar->setValue(bar->maximum());
        });
    }

    int lastPosition_ = 0;
    bool followingEnd_ = true;
    bool followScheduled_ = false;
    bool restoringPosition_ = false;
};

class ReasoningScrollArea final : public AgentScrollArea
{
public:
    explicit ReasoningScrollArea(QWidget *parent) : AgentScrollArea(parent)
    {
        setObjectName(u"agentReasoningView"_s);
        setStyleSheet(u"#agentReasoningView { background: transparent; border: none; }"_s);
        text_ = messageLabel(this, 13);
        text_->setTextFormat(Qt::RichText);
        text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setWidget(text_);
        text_->setAutoFillBackground(false);
    }

    void setProcessText(const QString &text)
    {
        setMarkdown(text_, text);
        updateHeight();
    }

    void refreshTheme(const QString &text)
    {
        text_->setProperty("hasPreview", false);
        setProcessText(text);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        AgentScrollArea::resizeEvent(event);
        updateHeight();
    }

private:
    void updateHeight()
    {
        constexpr int maximumHeight = 200;
        // Measure at the full width to see whether the scrollbar is needed.
        const int width = maximumViewportSize().width();
        const int height = qBound(0, text_->heightForWidth(width), maximumHeight);
        if (minimumHeight() != height) setFixedHeight(height);
    }

    ElaText *text_;
};

AgentWindow::AgentWindow(AgentService *service, QWidget *parent)
    : ElaWindow(parent), service_(service), pendingWorkspace_(service->defaultWorkspace()),
      pendingModel_(service->defaultModel())
{
    setWindowTitle(u"AI Agent"_s);
    setIsDefaultClosed(false);
    setUserInfoCardVisible(false);
    // 会话标题较长，按窗口比例限制导航宽度，放大后允许显示更多内容。
    setNavigationBarMaximumWidthRatio(0.28);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setIsAllowPageOpenInNewWindow(false);
    setIsCentralStackedWidgetTransparent(true);
    setStackSwitchMode(ElaWindowType::Popup);
    for (const auto flag : {ElaAppBarType::StayTopButtonHint, ElaAppBarType::RouteBackButtonHint,
                            ElaAppBarType::RouteForwardButtonHint, ElaAppBarType::ThemeChangeButtonHint})
        setWindowButtonFlag(flag, false);
    resize(AppUi::defaultWindowSize);
    setMinimumSize(AppUi::defaultWindowSize);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });

    newPage_ = new QWidget(this);
    newPage_->installEventFilter(this);
    auto *pageLayout = new QVBoxLayout(newPage_);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    addPageNode(u"新对话"_s, newPage_, ElaIconType::Message);
    addFooterNode(u"设置"_s, settingsKey_, 0, ElaIconType::Gear);
    setupSidebarManagement();

    chat_ = new QWidget(newPage_);
    chat_->setObjectName(u"agentChat"_s);
    chat_->setStyleSheet(u"#agentChat { background: transparent; }"_s);
    navCentral_ = findChild<QWidget *>(u"ElaWindowNavigationCentralWidget"_s);
    if (navCentral_) {
        navStack_ = navCentral_->findChild<QWidget *>(u"ElaCentralStackedWidget"_s, Qt::FindDirectChildrenOnly);
        navCentral_->installEventFilter(this);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { navCentral_->update(); });
    }
    auto *layout = new QVBoxLayout(chat_);
    layout->setContentsMargins(16, 6, 16, 14);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    title_ = AppUi::text({}, chat_, 17);
    title_->setMinimumHeight(controlHeight);
    title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    header->addWidget(title_, 1);
    more_ = iconButton(ElaIconType::Ellipsis, u"会话管理"_s, chat_);
    auto morePolicy = more_->sizePolicy();
    morePolicy.setRetainSizeWhenHidden(true);
    more_->setSizePolicy(morePolicy);
    header->addWidget(more_);
    layout->addLayout(header);

    notice_ = AppUi::text({}, chat_, 13);
    notice_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notice_->hide();
    layout->addWidget(notice_);
    scroll_ = new AgentScrollArea(chat_);
    scroll_->setObjectName(u"agentConversationScroll"_s);
    scroll_->setStyleSheet(u"#agentConversationScroll { background: transparent; border: none; }"_s);
    auto *conversation = new QWidget(scroll_);
    auto *transcript = new QWidget(conversation);
    auto *conversationLayout = new QVBoxLayout(conversation);
    conversationLayout->setContentsMargins(0, 0, 0, 0);
    conversationLayout->addLayout(centeredColumn(transcript), 1);
    messages_ = new QVBoxLayout(transcript);
    messages_->setContentsMargins(0, 12, 0, 16);
    messages_->setSpacing(18);
    messages_->addStretch();
    scroll_->setWidget(conversation);
    conversation->setAutoFillBackground(false);
    auto *scrollBar = scroll_->verticalScrollBar();
    layout->addWidget(scroll_, 1);
    latest_ = new LatestButton(scroll_->viewport());
    latest_->setObjectName(u"agentLatest"_s);
    latest_->setToolTip(u"回到最新消息"_s);
    latest_->setAccessibleName(u"回到最新消息"_s);
    scroll_->viewport()->installEventFilter(this);
    latest_->hide();
    connect(latest_, &ElaToolButton::clicked, scroll_, &AgentScrollArea::scrollToEnd);
    const auto updateLatest = [this, scrollBar] {
        positionLatest();
        latest_->setVisible(scrollBar->maximum() - scrollBar->value() > 1);
        if (latest_->isVisible()) latest_->raise();
    };
    connect(scrollBar, &QScrollBar::valueChanged, this, updateLatest);
    connect(scrollBar, &QScrollBar::rangeChanged, this, updateLatest);

    auto *inputDock = new QWidget(chat_);
    auto *dockLayout = new QVBoxLayout(inputDock);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->setSpacing(0);
    composer_ = new ComposerArea(inputDock);
    composer_->setObjectName(u"agentComposer"_s);
    auto *composerLayout = new QVBoxLayout(composer_);
    composerLayout->setContentsMargins(12, 12, 10, 10);
    composerLayout->setSpacing(8);
    input_ = new ElaPlainTextEdit(composer_);
    input_->setObjectName(u"agentInput"_s);
    input_->setAccessibleName(u"消息输入"_s);
    input_->setPlaceholderText(u"向 Agent 发送消息"_s);
    input_->setToolTip(u"Enter 发送 · Ctrl+Enter 换行"_s);
    input_->setFrameShape(QFrame::NoFrame);
    input_->setStyleSheet(u"#agentInput { background: transparent; border: none; padding: 0px; }"_s);
    input_->viewport()->setAutoFillBackground(false);
    input_->document()->setDocumentMargin(1);
    input_->setFixedHeight(40);
    input_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(input_->document(), &QTextDocument::contentsChanged, this, [this] {
        const auto height = qRound(input_->document()->size().height() * input_->fontMetrics().lineSpacing()) + 4;
        input_->setFixedHeight(qBound(40, height, 112));
    });
    connect(input_, &ElaPlainTextEdit::textChanged, this, &AgentWindow::refreshSendAvailability);
    input_->installEventFilter(this);
    composerLayout->addWidget(input_);
    auto *options = new QHBoxLayout;
    options->setSpacing(4);
    context_ = composerButton(composer_, true);
    context_->setObjectName(u"agentContext"_s);
    context_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    context_->setElaIcon(ElaIconType::Folder);
    context_->setMaximumWidth(200);
    options->addWidget(context_);
    connect(context_, &ElaToolButton::clicked, this, &AgentWindow::contextMenu);
    model_ = new ModelButton(composer_);
    model_->setObjectName(u"agentModel"_s);
    permission_ = composerButton(composer_, true);
    permission_->setObjectName(u"agentPermission"_s);
    permission_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    permission_->setElaIcon(ElaIconType::Shield);
    send_ = new SendButton(composer_);
    send_->setObjectName(u"agentSend"_s);
    options->addWidget(permission_);
    options->addStretch();
    options->addWidget(model_);
    options->addWidget(send_, 0, Qt::AlignVCenter);
    composerLayout->addLayout(options);
    dockLayout->addWidget(composer_);
    approvalCard_ = new ApprovalCard(inputDock);
    approvalCard_->hide();
    dockLayout->addWidget(approvalCard_);
    layout->addWidget(inputDock);
    pageLayout->addWidget(chat_);

    connect(this, &ElaWindow::navigationNodeClicked, this, [this](auto, const QString &key) {
        if (navigating_) return;
        if (key == settingsKey_) {
            emit settingsRequested({});
            return;
        }
    });
    connect(model_, &ElaToolButton::clicked, this, &AgentWindow::modelMenu);
    connect(permission_, &ElaToolButton::clicked, this, &AgentWindow::permissionMenu);
    connect(more_, &ElaToolButton::clicked, this, &AgentWindow::sessionMenu);
    connect(send_, &ElaToolButton::clicked, this, [this] {
        if (service_->busy() && service_->activeSession() == session_) service_->stop();
        else send();
    });
    const auto decide = [this](bool allow) {
        if (approvalTool_.isEmpty() || service_->activeSession() != session_) return;
        approvalCard_->focusDetails();
        approvalCard_->setActionsEnabled(false);
        const auto tool = approvalTool_;
        service_->decide(tool, allow);
    };
    connect(approvalCard_->allowButton(), &ElaPushButton::clicked, this, [decide] { decide(true); });
    connect(approvalCard_->denyButton(), &ElaPushButton::clicked, this, [decide] { decide(false); });
    connect(approvalCard_->stopButton(), &ElaPushButton::clicked, this, [this] {
        if (approvalTool_.isEmpty() || service_->activeSession() != session_) return;
        approvalCard_->focusDetails();
        approvalCard_->setActionsEnabled(false);
        service_->stop();
    });
    streamTimer_.setSingleShot(true);
    streamTimer_.setInterval(80);
    connect(&streamTimer_, &QTimer::timeout, this, [this] { flushReasoning(); flushStream(); });
    connect(service_, &AgentService::configurationChanged, this, [this] {
        const auto current = service_->resolveModel(pendingModel_.id, pendingModel_.model);
        pendingModel_ = !pendingModelOverridden_ || current.id.isEmpty() ? service_->defaultModel() : current;
        if (pendingWorkspace_.isEmpty() || !service_->workspaces().contains(pendingWorkspace_))
            pendingWorkspace_ = service_->defaultWorkspace();
        refreshControls();
    });
    connect(service_, &AgentService::sessionsChanged, this, &AgentWindow::syncNavigation);
    connect(service_, &AgentService::titleGenerated, this, [this](const QString &id, const QString &error) {
        if (!error.isEmpty() && id == session_) notify(error);
    });
    connect(service_, &AgentService::storageError, this, &AgentWindow::notify);
    connect(service_, &AgentService::eventAdded, this, [this](const QString &id, const QJsonObject &event) {
        if (id == session_) addEvent(event);
    });
    connect(service_, &AgentService::textReceived, this, [this](const QString &id, const QString &chunk) {
        if (id != session_) return;
        stream_ += chunk;
        if (!streamTimer_.isActive()) streamTimer_.start();
    });
    connect(service_, &AgentService::reasoningReceived, this, [this](const QString &id, const QString &chunk) {
        if (id != session_) return;
        reasoning_ += chunk;
        if (!streamTimer_.isActive()) streamTimer_.start();
    });
    connect(service_, &AgentService::toolUpdated, this, [this](const QString &id, const QJsonObject &event) {
        if (id != session_) return;
        updateTool(event);
        refreshApproval();
    });
    connect(service_, &AgentService::busyChanged, this, [this](bool busy) {
        const auto *current = service_->session(session_);
        if (busy && service_->activeSession() == session_ && current && current->events.isEmpty()) render();
        if (!busy) {
            if (streamLabel_ || !stream_.isEmpty() || reasoningHeader_ || !reasoning_.isEmpty())
                render(false);
            toolCards_.clear();
        }
        refreshApproval();
        refreshResume();
        refreshControls();
        updateNavMultiSelectState();
    });
    syncNavigation();
    render();
    if (!service_->migrationError().isEmpty()) notify(u"供应商迁移暂未写入：%1"_s.arg(service_->migrationError()));
}

void AgentWindow::syncNavigation()
{
    const QScopedValueRollback guard(navigating_, true);
    const auto current = session_;
    QStringList remove;
    for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it) {
        const auto *item = service_->session(it.key());
        if (!item || item->workspace != it->workspace) remove.append(it.key());
    }
    if (remove.contains(current)) {
        newPage_->layout()->addWidget(chat_);
        chat_->show();
        setStackSwitchMode(ElaWindowType::None);
        navigation(pageKey(newPage_));
        setStackSwitchMode(ElaWindowType::Popup);
    }
    for (const auto &id : remove) {
        const auto node = nodes_.take(id);
        sessionOrder_[node.workspace].removeAll(id);
        removeNavigationNode(pageKey(node.page));
        node.page->deleteLater();
    }
    for (const auto &item : service_->sessions()) {
        if (nodes_.contains(item.id)) {
            const auto key = pageKey(nodes_.value(item.id).page);
            if (getNavigationNodeTitle(key) != item.title) setNavigationNodeTitle(key, item.title);
            continue;
        }
        if (!groups_.contains(item.workspace)) {
            QString key;
            addExpanderNode(workspaceName(item.workspace), key, ElaIconType::Folder);
            groups_.insert(item.workspace, key);
            expanderOrder_.append(item.workspace);
        }
        auto *page = new QWidget(this);
        page->installEventFilter(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        addPageNode(item.title, page, groups_.value(item.workspace), ElaIconType::Message);

        nodes_.insert(item.id, {page, item.workspace});
        sessionOrder_[item.workspace].append(item.id);
        if (!getNavigationNodeIsExpanded(groups_.value(item.workspace)))
            expandNavigationNode(groups_.value(item.workspace));
    }
    for (const auto &workspace : groups_.keys()) {
        bool used = false;
        for (const auto &node : std::as_const(nodes_)) used |= node.workspace == workspace;
        if (!used) {
            expanderOrder_.removeAll(workspace);
            sessionOrder_.remove(workspace);
            removeNavigationNode(groups_.take(workspace));
        }
    }
    if (remove.contains(current)) {
        if (nodes_.contains(current)) {
            nodes_.value(current).page->layout()->addWidget(chat_);
            chat_->show();
            setStackSwitchMode(ElaWindowType::None);
            navigation(pageKey(nodes_.value(current).page));
            setStackSwitchMode(ElaWindowType::Popup);
        } else {
            select({});
            unsent_.remove(current);
        }
    }
    updateNavMultiSelectState();
    refreshControls();
}

void AgentWindow::select(const QString &id)
{
    if (session_ == id) return;
    unsent_.insert(session_, input_->toPlainText());
    session_ = id;
    auto *page = nodes_.contains(id) ? nodes_.value(id).page : newPage_;
    page->layout()->addWidget(chat_);
    chat_->show();
    input_->setPlainText(unsent_.value(id));
    imeComposing_ = false;
    render();
    refreshControls();
    if (page->isVisible()) {
        if (auto *content = scroll_->widget(); content && content->layout())
            content->layout()->activate();
        if (chat_->layout()) chat_->layout()->activate();
        page->layout()->activate();
        if (approvalTool_.isEmpty()) input_->setFocus();
        else approvalCard_->focusDetails();
    }
}

void AgentWindow::refreshControls()
{
    const auto *item = service_->session(session_);
    auto connection = item ? service_->resolveModel(item->connection, item->model) : pendingModel_;
    connection.reasoningEffort = item ? item->reasoningEffort : pendingEffort_;
    const auto access = item ? item->permission : pendingPermission_;
    auto modelName = connection.model;
    for (const auto &provider : service_->providers())
        if (provider.id == connection.id)
            for (const auto &entry : provider.models)
                if (entry.id == connection.model && !entry.name.isEmpty()) modelName = entry.name;
    const bool busy = service_->busy();
    const bool active = busy && service_->activeSession() == session_;
    const auto heading = item ? item->title : u"新对话"_s;
    title_->setText(heading.left(58));
    title_->setToolTip(heading);
    const auto workspace = item ? item->workspace : pendingWorkspace_;
    const auto location = item && !item->plugin.isEmpty() ? item->plugin : workspaceName(workspace);
    context_->setText(context_->fontMetrics().elidedText(location, Qt::ElideRight, 130) + u" ⌄"_s);
    context_->setAccessibleName(u"工作区：%1"_s.arg(workspaceName(workspace)));
    context_->setToolTip(context_->accessibleName() + (workspace.isEmpty() ? QString() : u"\n"_s + QDir::toNativeSeparators(workspace)) +
                         (item && !item->plugin.isEmpty() ? u"\n当前插件："_s + item->plugin : QString()));
    context_->setEnabled(!busy);
    more_->setVisible(item != nullptr);
    more_->setEnabled(!active);
    const auto effort = connection.effectiveReasoningEffort();
    modelAvailable_ = !connection.id.isEmpty();
    const auto effortName = effort.isEmpty() ? QString() : effortLabel(effort);
    const auto selectionLabel = item && !item->connection.isEmpty() ? u"需重新选择模型"_s : u"选择模型"_s;
    model_->setSelection(modelAvailable_ ? modelName : selectionLabel, modelAvailable_ ? effortName : QString());
    model_->setAccessibleName(!modelAvailable_ ? selectionLabel : effortName.isEmpty() ? modelName
                              : u"%1，思考强度：%2"_s.arg(modelName, effortName));
    model_->setToolTip(!modelAvailable_ ? u"在设置中管理模型供应商"_s
                       : u"%1 · %2\n%3\n思考强度：%4"_s.arg(connection.name, modelName,
                           connection.endpoint().toDisplayString(), effortLabel(effort)));
    permission_->setText(access == Permission::Yolo ? u"完全访问"_s : permissionLabel(access));
    permission_->setAccessibleName(u"工具权限：%1"_s.arg(permission_->text()));
    permission_->setProperty("fullAccess", access == Permission::Yolo);
    permission_->update();
    auto permissionTip = access == Permission::Yolo ? u"完全访问仍需确认插件编辑授权、首次启用和新增权限"_s
                                                    : u"选择本会话的工具权限"_s;
    if (!AgentPlatform::canExecute())
        permissionTip += u"\n增强置顶或管理员模式下系统工具不可用"_s;
    permission_->setToolTip(permissionTip);
    model_->setEnabled(!busy);
    permission_->setEnabled(!busy);
    input_->setReadOnly(active);
    refreshSendAvailability();
}

void AgentWindow::refreshSendAvailability()
{
    const bool busy = service_->busy();
    const bool active = busy && service_->activeSession() == session_;
    send_->setElaIcon(active ? ElaIconType::Stop : ElaIconType::ArrowUp);
    send_->setProperty("stopping", active);
    send_->update();
    send_->setAccessibleName(active ? u"停止"_s : u"发送"_s);
    send_->setToolTip(send_->accessibleName());
    send_->setEnabled(active || (!busy && modelAvailable_ && !input_->toPlainText().trimmed().isEmpty()));
}

void AgentWindow::refreshApproval()
{
    const auto &tools = service_->liveTools();
    const auto current = service_->activeSession() == session_ && !tools.isEmpty() ? tools.constLast() : QJsonObject();
    const auto tool = current.value(u"state"_s).toString() == u"待审批"_s ? current.value(u"id"_s).toString() : QString();
    const auto *focus = QApplication::focusWidget();
    if (tool.isEmpty()) {
        if (approvalTool_.isEmpty()) return;
        const bool restoreFocus = focus && (focus == approvalCard_ || approvalCard_->isAncestorOf(focus));
        approvalTool_.clear();
        approvalCard_->hide();
        composer_->show();
        if (restoreFocus) input_->setFocus();
        return;
    }

    auto detail = current.value(u"detail"_s).toString();
    detail.replace(u"\r\n"_s, u"\n"_s);
    const auto split = detail.indexOf(u'\n');
    const auto headline = split < 0 ? detail : detail.left(split);
    const auto payload = split < 0 ? QString() : detail.mid(split + 1);
    const bool newRequest = approvalTool_ != tool;
    approvalCard_->setContent(headline.isEmpty() ? toolLabel(current.value(u"name"_s).toString()) : headline, payload, newRequest);
    if (newRequest) {
        const bool moveFocus = focus && composer_->isAncestorOf(focus);
        approvalTool_ = tool;
        approvalCard_->setActionsEnabled(true);
        composer_->hide();
        approvalCard_->show();
        if (moveFocus) approvalCard_->focusDetails();
    }
}

void AgentWindow::refreshResume()
{
    const auto *item = service_->session(session_);
    if (service_->busy() || !item || !item->resumable) {
        if (resumeRow_) {
            messages_->removeWidget(resumeRow_);
            resumeRow_->hide();
            resumeRow_->deleteLater();
            resumeRow_ = nullptr;
        }
        return;
    }
    if (resumeRow_) return;

    auto *card = new QWidget(scroll_->widget());
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *resume = AppUi::button(u"继续执行"_s, card);
    layout->addWidget(resume);
    layout->addStretch();
    connect(resume, &ElaPushButton::clicked, this, [this] {
        QString error;
        if (!service_->continueSession(session_, &error)) notify(error);
    });
    addRow(card);
    resumeRow_ = card->parentWidget();
}

void AgentWindow::render(bool resetScroll)
{
    const int scrollPosition = scroll_->verticalScrollBar()->value();
    streamTimer_.stop();
    streamLabel_ = nullptr;
    reasoningScroll_ = nullptr;
    reasoningHeader_ = nullptr;
    reasoningGroup_ = activities_ = nullptr;
    resumeRow_ = nullptr;
    reasoning_.clear();
    toolCards_.clear();
    while (messages_->count() > 1) {
        auto *row = messages_->takeAt(0);
        row->widget()->hide();
        row->widget()->deleteLater();
        delete row;
    }
    const auto *item = service_->session(session_);
    const bool active = service_->busy() && service_->activeSession() == session_;
    const auto live = active ? service_->liveTools() : QList<QJsonObject>();
    if (item) {
        QSet<qsizetype> legacyFinals;
        qsizetype lastAssistant = -1;
        for (qsizetype i = 0; i < item->events.size(); ++i) {
            const auto event = item->events[i].toObject();
            const auto kind = event.value(u"kind"_s).toString();
            if (kind == u"user"_s || kind == u"turn"_s) {
                if (lastAssistant >= 0) legacyFinals.insert(lastAssistant);
                lastAssistant = -1;
            } else if (kind == u"assistant"_s) lastAssistant = i;
        }
        if (!active && lastAssistant >= 0) legacyFinals.insert(lastAssistant);
        for (qsizetype i = 0; i < item->events.size(); ++i) {
            auto event = item->events[i].toObject();
            if (event.value(u"kind"_s).toString() == u"assistant"_s && !event.contains(u"final"_s))
                event.insert(u"final"_s, legacyFinals.contains(i));
            const auto id = event.value(u"toolId"_s).toString();
            const auto tool = std::find_if(live.cbegin(), live.cend(), [&](const QJsonObject &card) {
                return !id.isEmpty() && card.value(u"id"_s).toString() == id;
            });
            if (tool != live.cend()) updateTool(*tool);
            else addEvent(event);
        }
    }
    for (const auto &tool : live)
        if (!toolCards_.contains(tool.value(u"id"_s).toString())) updateTool(tool);
    stream_ = active ? service_->liveText() : QString();
    reasoning_ = active ? service_->liveReasoning() : QString();
    flushReasoning();
    flushStream();
    if ((!item || item->events.isEmpty()) && !active)
        addMessage(u"welcome"_s, pendingModel_.id.isEmpty()
            ? u"在「设置 → AI Agent」配置供应商和模型后开始对话。"_s
            : u"可以直接提问，也可以在对话中使用系统工具和开发应用插件。"_s);
    if (!service_->draftName(session_).isEmpty() && item && item->events.isEmpty())
        addMessage(u"draft"_s, u"当前会话已有插件草稿"_s);
    refreshApproval();
    refreshResume();
    if (resetScroll) scroll_->scrollToEnd();
    else scroll_->restorePosition(scrollPosition);
}

void AgentWindow::addEvent(const QJsonObject &event)
{
    const auto kind = event.value(u"kind"_s).toString();
    const auto text = event.value(u"text"_s).toString();
    if (kind == "turn"_L1) {
        auto *elapsed = AppUi::text(text, scroll_->widget(), 12);
        elapsed->setObjectName(u"agentTurnDuration"_s);
        addRow(elapsed);
        return;
    }
    if (kind == "preview"_L1) {
        addRow(new PreviewImage(QPixmap(text), scroll_->widget()));
        return;
    }
    if (kind == "plugin"_L1) {
        auto *card = new QWidget(chat_);
        auto *layout = new QVBoxLayout(card);
        auto *summary = AppUi::text(text, card, 13); summary->setWordWrap(true);
        summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(summary);
        auto *actions = new QHBoxLayout;
        auto *diff = AppUi::button(u"查看差异"_s, card);
        auto *undo = AppUi::button(u"撤销本轮修改"_s, card);
        auto *history = AppUi::button(u"版本历史"_s, card);
        actions->addWidget(diff); actions->addWidget(undo); actions->addWidget(history); actions->addStretch();
        layout->addLayout(actions);
        const auto plugin = event.value(u"plugin"_s).toString(), version = event.value(u"version"_s).toString();
        connect(diff, &ElaPushButton::clicked, this, [this, event] {
            auto *dialog = new AppUi::FormDialog(this, u"插件修改差异"_s);
            auto *view = new ElaPlainTextEdit(dialog); view->setReadOnly(true); view->setLineWrapMode(QPlainTextEdit::NoWrap);
            view->setMinimumSize(560, 300); view->setPlainText(event.value(u"output"_s).toString());
            dialog->field(u"本轮文件变化"_s, view); AppUi::run(dialog);
        });
        connect(undo, &ElaPushButton::clicked, this, [this, plugin, version] {
            if (!AppUi::confirm(this, u"撤销本轮修改"_s, u"将恢复该轮修改之前的完整代码和资源，后续代码修改也会撤销。保留当前私有数据，当前源文件会先备份。"_s)) return;
            QString error;
            const QPointer<AgentWindow> self(this);
            if (!service_->rollbackPlugin(plugin, version, [self](const QString &failure) {
                if (self) self->notify(failure.isEmpty() ? u"插件已回滚"_s : failure);
            }, &error)) notify(error);
        });
        connect(history, &ElaPushButton::clicked, this, [this, plugin] {
            AppUi::showPluginHistory(service_->pluginWorkspace(), plugin, this, [this] { return service_->busy(); });
        });
        addRow(card); return;
    }
    if (kind == "reasoning"_L1) {
        const auto process = service_->reasoningText(session_, event.value(u"reasoningId"_s).toString());
        if (reasoningHeader_) {
            const auto preview = reasoningPreview(process);
            reasoningHeader_->setText(text);
            reasoningHeader_->setProperty("activityWarning", text == u"思考中断"_s);
            reasoningHeader_->setProperty("processText", preview);
            if (reasoningHeader_->isChecked()) reasoningScroll_->setProcessText(preview);
            reasoningHeader_->parentWidget()->setProperty("activityState", text);
            reasoningGroup_->refreshSummary();
        } else {
            addReasoning(process, text);
        }
        reasoning_.clear();
        reasoningHeader_ = nullptr;
        reasoningScroll_ = nullptr;
        reasoningGroup_ = nullptr;
        return;
    }
    if (kind == "assistant"_L1) {
        streamTimer_.stop();
        stream_.clear();
        if (streamLabel_) {
            streamLabel_->setProperty("sourceMarkdown", text);
            setMarkdown(streamLabel_, text);
            if (event.value(u"final"_s).toBool()) addFinalCopy(streamLabel_);
            streamLabel_ = nullptr;
            return;
        }
    }
    if (kind == "tool"_L1 && toolCards_.contains(event.value(u"toolId"_s).toString())) return;
    if (kind == "tool"_L1) { addToolEvent(event); return; }
    addMessage(kind, text, event.value(u"final"_s).toBool());
}

void AgentWindow::addToolEvent(const QJsonObject &event)
{
    auto normalized = event;
    const auto text = event.value(u"text"_s).toString();
    if (!normalized.contains(u"name"_s)) normalized.insert(u"name"_s, text.section(u" · "_s, 0, 0));
    if (!normalized.contains(u"state"_s)) normalized.insert(u"state"_s, text.section(u" · "_s, 1, 1));
    if (!normalized.contains(u"error"_s) && text.contains(u" · 失败 · "_s)) normalized.insert(u"error"_s, text.section(u" · "_s, 2));
    normalized.insert(u"id"_s, event.value(u"toolId"_s).toString(u"history-%1"_s.arg(toolCards_.size())));
    updateTool(normalized);
}

void AgentWindow::addRow(QWidget *card, bool right)
{
    activities_ = nullptr;
    auto *row = new QWidget(messages_->parentWidget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(right ? 62 : 0, 0, 0, 0);
    if (right) layout->addStretch();
    layout->addWidget(card, right ? 0 : 1);
    messages_->insertWidget(messages_->count() - 1, row);
}

ActivityGroup *AgentWindow::addActivity(QWidget *widget)
{
    if (!activities_) {
        auto *group = new ActivityGroup(scroll_->widget());
        addRow(group);
        activities_ = group;
    }
    activities_->add(widget);
    return activities_;
}

void AgentWindow::addMessage(const QString &kind, const QString &text, bool final)
{
    if (kind == "plan"_L1) {
        auto *card = new QWidget(scroll_->widget());
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto *header = new ActivityButton(card);
        header->setText(u"任务步骤"_s);
        header->setCheckable(true);
        header->setElaIcon(ElaIconType::Code);
        layout->addWidget(header);
        auto *steps = new ElaPlainTextEdit(card);
        steps->setReadOnly(true);
        steps->setPlainText(text);
        steps->setFixedHeight(110);
        layout->addWidget(steps);
        steps->hide();
        connect(header, &ElaToolButton::toggled, steps, &QWidget::setVisible);
        addActivity(card);
        return;
    }
    const bool user = kind == "user"_L1;
    QWidget *card = user ? new UserBubble(text, scroll_->widget()) : new QWidget(scroll_->widget());
    card->setObjectName(user ? u"agentUserBubble"_s : u"agentMessage"_s);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(user ? 15 : 0, user ? 11 : 2, user ? 15 : 0, user ? 11 : 0);
    layout->setSpacing(6);
    auto *content = messageLabel(card);
    layout->addWidget(content);
    if (kind == "assistant"_L1) {
        auto *actions = new QHBoxLayout;
        actions->setObjectName(u"agentMessageActions"_s);
        auto *full = composerButton(card);
        full->setObjectName(u"agentFullMessage"_s);
        full->setText(u"长消息 · 查看完整内容"_s);
        actions->addWidget(full);
        actions->addStretch();
        layout->addLayout(actions);
        connect(full, &ElaToolButton::clicked, this, [this, content] {
            auto *dialog = new AppUi::FormDialog(this, u"完整消息"_s, u"关闭"_s);
            auto *text = new ElaPlainTextEdit(dialog);
            text->setReadOnly(true);
            text->setPlainText(content->property("sourceMarkdown").toString());
            text->setMinimumSize(450, 280);
            dialog->field(u"消息原文"_s, text);
            AppUi::run(dialog);
        });
    }
    if (kind == "assistant"_L1) {
        content->setIsWrapAnywhere(false);
        content->setWordWrap(true);
        content->setTextFormat(Qt::RichText);
        setMarkdown(content, text);
        if (final) addFinalCopy(content);
        connect(eTheme, &ElaTheme::themeModeChanged, content, [content] {
            content->setProperty("hasPreview", false);
            setMarkdown(content, content->property("sourceMarkdown").toString());
        });
        if (text.isEmpty()) streamLabel_ = content;
    } else {
        content->setTextFormat(Qt::PlainText);
        content->setText(text);
        if (user) {
            content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, card, [card, text](const QPoint &point) {
                auto *menu = new ElaMenu(card);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->addAction(u"复制消息"_s, card, [text] { QApplication::clipboard()->setText(text); });
                menu->popup(card->mapToGlobal(point));
            });
        }
    }
    if (kind == "draft"_L1) {
        auto *review = AppUi::button(u"前往审阅"_s, card);
        layout->addWidget(review, 0, Qt::AlignLeft);
        connect(review, &ElaPushButton::clicked, this, [this, id = session_] { emit settingsRequested(id); });
    }
    if (kind == "welcome"_L1 && pendingModel_.id.isEmpty()) {
        auto *configure = AppUi::button(u"配置模型"_s, card, true);
        layout->addWidget(configure, 0, Qt::AlignLeft);
        connect(configure, &ElaPushButton::clicked, this, [this] { emit settingsRequested({}); });
    }
    if (kind == "draft"_L1) addActivity(card);
    else addRow(card, user);
}

void AgentWindow::addFinalCopy(ElaText *content)
{
    auto *card = content->parentWidget();
    if (card->findChild<ElaToolButton *>(u"agentFinalCopy"_s)) return;
    auto *actions = card->findChild<QHBoxLayout *>(u"agentMessageActions"_s);
    if (!actions) return;
    auto *copy = iconButton(ElaIconType::Copy, u"复制最终答复"_s, card);
    copy->setObjectName(u"agentFinalCopy"_s);
    actions->insertWidget(0, copy);
    connect(copy, &ElaToolButton::clicked, card, [content] {
        QApplication::clipboard()->setText(content->property("sourceMarkdown").toString());
    });
}

void AgentWindow::positionLatest()
{
    const auto *viewport = scroll_->viewport();
    const int centeredX = (scroll_->width() - latest_->width()) / 2 - viewport->x();
    latest_->move(qBound(0, centeredX, qMax(0, viewport->width() - latest_->width())),
                  qMax(0, viewport->height() - latest_->height() - 12));
}

void AgentWindow::updateTool(const QJsonObject &event)
{
    const auto id = event.value(u"id"_s).toString();
    if (!toolCards_.contains(id)) {
        auto *card = new QWidget(scroll_->widget());
        card->setProperty("activityState", event.value(u"state"_s).toString());
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto *header = new ActivityButton(card);
        header->setObjectName(u"agentToolSummary"_s);
        header->setCheckable(true);
        header->setElaIcon(ElaIconType::Code);
        layout->addWidget(header);
        auto *details = new QWidget(card);
        auto *body = new QVBoxLayout(details);
        body->setContentsMargins(0, 0, 0, 0);
        auto *contents = new ToolDetails(details);
        body->addWidget(contents);
        layout->addWidget(details);
        auto *review = AppUi::button(u"前往审阅"_s, card);
        layout->addWidget(review, 0, Qt::AlignLeft);
        details->hide();
        review->hide();
        connect(header, &ElaToolButton::toggled, details, &QWidget::setVisible);
        connect(review, &ElaPushButton::clicked, this, [this, session = session_] { emit settingsRequested(session); });
        auto *group = addActivity(card);
        toolCards_.insert(id, {header, contents, review, group});
    }
    const auto card = toolCards_.value(id);
    const auto state = event.value(u"state"_s).toString();
    const auto name = event.value(u"name"_s).toString();
    card.header->setText(u"%1 · %2"_s.arg(toolLabel(name), state));
    card.header->setToolTip(card.header->text());
    card.header->setAccessibleName(card.header->text());
    card.header->setProperty("activityWarning", state == u"待审批"_s || state == u"失败"_s ||
                             state == u"已拒绝"_s || state == u"已中断"_s);
    card.header->update();
    static_cast<ToolDetails *>(card.contents)->setEvent(event);
    card.review->setVisible(name == "write_plugin_draft"_L1 && state != u"正在执行"_s &&
                            !service_->draftName(session_).isEmpty());
    auto *item = card.header->parentWidget();
    if (item->property("activityState").toString() != state) {
        item->setProperty("activityState", state);
        card.group->refreshSummary();
    }
}

void AgentWindow::flushStream()
{
    if (stream_.isEmpty()) return;
    if (!streamLabel_) addMessage(u"assistant"_s, {});
    setMarkdown(streamLabel_, stream_);
}

void AgentWindow::addReasoning(const QString &text, const QString &status)
{
    auto *card = new QWidget(scroll_->widget());
    card->setProperty("activityState", status);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    reasoningHeader_ = new ActivityButton(card);
    reasoningHeader_->setText(status);
    reasoningHeader_->setProperty("activityWarning", status == u"思考中断"_s);
    reasoningHeader_->setObjectName(u"agentReasoning"_s);
    reasoningHeader_->setCheckable(true);
    reasoningHeader_->setElaIcon(ElaIconType::Clock);
    reasoningHeader_->setProperty("processText", reasoningPreview(text));
    reasoningHeader_->setEnabled(!text.isEmpty());
    reasoningHeader_->setToolTip(text.isEmpty() ? u"思考过程仅在本次运行期间可用"_s : u"展开或收起模型返回的思考过程"_s);
    layout->addWidget(reasoningHeader_);
    auto *header = reasoningHeader_;
    auto *view = new ReasoningScrollArea(card);
    reasoningScroll_ = view;
    view->hide();
    layout->addWidget(view);
    connect(eTheme, &ElaTheme::themeModeChanged, view, [header, view] {
        if (header->isChecked()) view->refreshTheme(header->property("processText").toString());
    });
    connect(header, &ElaToolButton::toggled, view, [header, view](bool expanded) {
        if (expanded) view->refreshTheme(header->property("processText").toString());
        view->setVisible(expanded);
    });
    reasoningGroup_ = addActivity(card);
}

void AgentWindow::flushReasoning()
{
    if (reasoning_.isEmpty()) return;
    if (!reasoningHeader_) {
        addReasoning(reasoning_, u"正在思考…"_s);
        return;
    }
    const auto preview = reasoningPreview(reasoning_);
    reasoningHeader_->setProperty("processText", preview);
    if (reasoningHeader_->isChecked()) reasoningScroll_->setProcessText(preview);
}

void AgentWindow::modelMenu()
{
    auto *menu = new ElaMenu(model_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto *item = service_->session(session_);
    auto current = item ? service_->resolveModel(item->connection, item->model) : pendingModel_;
    current.reasoningEffort = item ? item->reasoningEffort : pendingEffort_;
    const auto efforts = current.reasoningEfforts();
    if (!current.id.isEmpty()) {
        auto *reasoning = new ElaMenu(menu);
        reasoning->setTitle(u"思考强度"_s);
        menu->addMenu(reasoning);
        if (efforts.isEmpty()) {
            auto *action = reasoning->addAction(u"由模型决定"_s);
            action->setEnabled(false);
        }
        for (const auto &effort : efforts) {
            auto *action = reasoning->addAction(effortLabel(effort));
            action->setCheckable(true);
            action->setChecked(effort == current.effectiveReasoningEffort());
            connect(action, &QAction::triggered, this, [this, effort] {
                if (session_.isEmpty()) pendingEffort_ = effort;
                else service_->setReasoningEffort(session_, effort);
                refreshControls();
            });
        }
        menu->addSeparator();
    }
    for (const auto &provider : service_->providers()) {
        if (provider.models.isEmpty()) continue;
        auto *group = new ElaMenu(menu);
        group->setTitle(provider.name);
        menu->addMenu(group);
        for (const auto &model : provider.models) {
            auto *action = group->addAction(model.name.isEmpty() ? model.id : model.name);
            action->setCheckable(true);
            action->setChecked(provider.id == current.id && model.id == current.model);
            connect(action, &QAction::triggered, this, [this, selection = provider.connection(model.id)] {
                if (session_.isEmpty()) {
                    pendingModel_ = selection;
                    pendingEffort_.clear();
                    pendingModelOverridden_ = true;
                } else service_->setModel(session_, selection.id, selection.model);
                refreshControls();
            });
        }
    }
    menu->addAction(u"管理模型…"_s, this, [this] { emit settingsRequested({}); });
    menu->popup(model_->mapToGlobal(QPoint(0, model_->height())));
}

void AgentWindow::permissionMenu()
{
    auto *menu = new ElaMenu(permission_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const auto choice : {Permission::ReadOnly, Permission::Ask, Permission::Yolo}) {
        menu->addAction(permissionLabel(choice), this, [this, choice] {
            if (session_.isEmpty()) pendingPermission_ = choice;
            else service_->setPermission(session_, choice);
            refreshControls();
        });
    }
    menu->popup(permission_->mapToGlobal(QPoint(0, permission_->height())));
}

void AgentWindow::sessionMenu()
{
    showSessionContextMenu(session_, more_->mapToGlobal(QPoint(0, more_->height())));
}

void AgentWindow::openSession(const QString &id)
{
    if (!nodes_.contains(id)) return;
    navigation(pageKey(nodes_.value(id).page));
    if (!isVisible()) select(id);
}

void AgentWindow::promptRenameSession(const QString &id)
{
    const auto *item = service_->session(id);
    if (!item || (service_->busy() && service_->activeSession() == id)) return;
    auto *dialog = new AppUi::FormDialog(this, u"重命名会话"_s);
    auto *name = dialog->line(u"会话名称"_s, item->title);
    dialog->submit = [this, id, name] {
        if (name->text().trimmed().isEmpty()) return u"请输入名称"_s;
        service_->renameSession(id, name->text());
        return QString();
    };
    AppUi::run(dialog);
}

void AgentWindow::promptDeleteSessions(const QStringList &ids)
{
    QStringList deletable;
    const auto activeId = service_->busy() ? service_->activeSession() : QString();
    for (const auto &id : ids)
        if (!id.isEmpty() && id != activeId && service_->session(id)) deletable.append(id);
    if (deletable.isEmpty()) {
        notify(u"正在运行的会话无法删除"_s);
        return;
    }
    const auto message = deletable.size() == 1
        ? u"删除所选会话的本地历史？"_s
        : u"删除所选 %1 个会话的本地历史？"_s.arg(deletable.size());
    if (AppUi::confirm(this, u"删除会话"_s, message)) {
        for (const auto &id : deletable) checkedSessions_.remove(id);
        service_->deleteSessions(deletable);
        if (sidebarMultiSelect_ && checkedSessions_.isEmpty()) setSidebarMultiSelect(false);
    }
}

void AgentWindow::showSessionContextMenu(const QString &id, const QPoint &globalPos)
{
    const auto *item = service_->session(id);
    if (!item) return;
    const bool busy = service_->busy();
    const bool isActive = busy && service_->activeSession() == id;
    bool hasUserMessage = false;
    for (const auto &entry : std::as_const(item->events)) {
        const auto obj = entry.toObject();
        if (obj.value(u"kind"_s).toString() == u"user"_s && !obj.value(u"text"_s).toString().trimmed().isEmpty()) {
            hasUserMessage = true;
            break;
        }
    }
    auto *menu = new ElaMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (id != session_)
        addMenuAction(menu, ElaIconType::Message, u"打开会话"_s, this, [this, id] { openSession(id); });
    auto *resume = addMenuAction(menu, ElaIconType::Play, u"继续执行"_s, this, [this, id] {
        QString error;
        if (!service_->continueSession(id, &error)) notify(error);
    });
    resume->setEnabled(!busy && item->resumable);
    auto *rename = addMenuAction(menu, ElaIconType::Pen, u"重命名"_s, this, [this, id] { promptRenameSession(id); });
    rename->setEnabled(!isActive);
    auto *autoTitle = addMenuAction(menu, ElaIconType::Sparkles, u"AI 生成标题"_s, this, [this, id] {
        QString error;
        if (!service_->regenerateTitle(id, &error)) notify(error);
    });
    autoTitle->setEnabled(!busy && hasUserMessage);
    addMenuAction(menu, ElaIconType::ListCheck, sidebarMultiSelect_ ? u"退出多选"_s : u"多选管理"_s, this,
                  [this, id, isActive] {
        if (sidebarMultiSelect_) {
            setSidebarMultiSelect(false);
            return;
        }
        if (!isActive) checkedSessions_.insert(id);
        setSidebarMultiSelect(true);
    });
    menu->addSeparator();
    auto *del = addMenuAction(menu, ElaIconType::Trash, u"删除会话"_s, this, [this, id] { promptDeleteSessions({id}); });
    del->setEnabled(!isActive);
    menu->popup(globalPos);
}

void AgentWindow::showWorkspaceContextMenu(const QString &workspace, const QPoint &globalPos)
{
    auto *menu = new ElaMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addMenuAction(menu, ElaIconType::Plus, u"在此工作区新建对话"_s, this, [this, workspace] {
        pendingWorkspace_ = workspace;
        navigation(pageKey(newPage_));
        select({});
        refreshControls();
    });
    const auto groupKey = groups_.value(workspace);
    if (!groupKey.isEmpty()) {
        const bool expanded = getNavigationNodeIsExpanded(groupKey);
        addMenuAction(menu, expanded ? ElaIconType::AngleUp : ElaIconType::AngleDown,
                      expanded ? u"收起分组"_s : u"展开分组"_s, this, [this, groupKey, expanded] {
            if (expanded) collapseNavigationNode(groupKey);
            else expandNavigationNode(groupKey);
        });
    }
    QStringList idleIds;
    const auto activeId = service_->busy() ? service_->activeSession() : QString();
    for (const auto &item : service_->sessions())
        if (item.workspace == workspace && item.id != activeId) idleIds.append(item.id);
    auto *selectGroup = addMenuAction(menu, ElaIconType::ListCheck, u"多选此工作区会话"_s, this, [this, idleIds] {
        for (const auto &id : idleIds) checkedSessions_.insert(id);
        setSidebarMultiSelect(true);
    });
    selectGroup->setEnabled(!idleIds.isEmpty());
    menu->addSeparator();
    auto *clearGroup = addMenuAction(menu, ElaIconType::Trash, u"清空此工作区闲置会话"_s, this,
                                     [this, idleIds] { promptDeleteSessions(idleIds); });
    clearGroup->setEnabled(!idleIds.isEmpty());
    menu->popup(globalPos);
}

AgentWindow::NavTarget AgentWindow::navTargetForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return {};
    const auto parent = index.parent();
    if (!parent.isValid()) {
        if (index.row() == 0) return {NavTarget::Kind::NewChat, {}};
        const int expIdx = index.row() - 1;
        if (expIdx >= 0 && expIdx < expanderOrder_.size())
            return {NavTarget::Kind::Workspace, expanderOrder_.at(expIdx)};
        return {};
    }
    if (!parent.parent().isValid()) {
        const int expIdx = parent.row() - 1;
        if (expIdx >= 0 && expIdx < expanderOrder_.size()) {
            const auto &list = sessionOrder_.value(expanderOrder_.at(expIdx));
            if (index.row() >= 0 && index.row() < list.size()) {
                const auto &id = list.at(index.row());
                if (nodes_.contains(id)) return {NavTarget::Kind::Session, id};
            }
        }
    }
    return {};
}

void AgentWindow::updateNavHoverAction(const QPoint &viewportPos)
{
    if (!navView_ || !navActionButton_) return;
    if (sidebarMultiSelect_ || navView_->viewport()->width() < 100) {
        hoveredSession_.clear();
        navActionButton_->hide();
        return;
    }
    const auto index = navView_->indexAt(viewportPos);
    const auto target = navTargetForIndex(index);
    if (target.kind != NavTarget::Kind::Session || !service_->session(target.value)) {
        hoveredSession_.clear();
        navActionButton_->hide();
        return;
    }
    const auto rect = navView_->visualRect(index);
    if (!rect.isValid()) {
        hoveredSession_.clear();
        navActionButton_->hide();
        return;
    }
    hoveredSession_ = target.value;
    navActionButton_->setFixedSize(24, 24);
    navActionButton_->move(qMax(4, rect.right() - 28), rect.center().y() - 12);
    navActionButton_->show();
    navActionButton_->raise();
}

void AgentWindow::setupSidebarManagement()
{
    navView_ = findChild<QTreeView *>(u"ElaNavigationView"_s);
    if (!navView_) return;
    navView_->installEventFilter(this);
    navView_->viewport()->installEventFilter(this);
    navActionButton_ = iconButton(ElaIconType::Ellipsis, u"会话操作"_s, navView_->viewport());
    navActionButton_->setObjectName(u"agentNavSessionAction"_s);
    navActionButton_->setFixedSize(24, 24);
    navActionButton_->setBorderRadius(6);
    navActionButton_->setIsTransparent(true);
    navActionButton_->hide();
    navActionButton_->installEventFilter(this);

    if (auto *navBar = navView_->parentWidget(); navBar && navBar->layout()) {
        navMultiBar_ = new QWidget(navBar);
        navMultiBar_->setObjectName(u"agentNavMultiBar"_s);
        auto *barLayout = new QVBoxLayout(navMultiBar_);
        barLayout->setContentsMargins(10, 4, 10, 4);
        barLayout->setSpacing(4);
        navMultiCount_ = AppUi::text(u"已选 0 项"_s, navMultiBar_, 12);
        barLayout->addWidget(navMultiCount_);
        auto *buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(4);
        const auto makeBarButton = [this](const QString &label) {
            auto *btn = composerButton(navMultiBar_);
            btn->setText(label);
            btn->setFixedHeight(28);
            btn->setBorderRadius(8);
            return btn;
        };
        navMultiSelectAll_ = makeBarButton(u"全选"_s);
        navMultiDelete_ = makeBarButton(u"删除"_s);
        navMultiDone_ = makeBarButton(u"完成"_s);
        buttons->addWidget(navMultiSelectAll_);
        buttons->addWidget(navMultiDelete_);
        buttons->addStretch();
        buttons->addWidget(navMultiDone_);
        barLayout->addLayout(buttons);
        navMultiBar_->hide();
        if (auto *box = qobject_cast<QVBoxLayout *>(navBar->layout()))
            box->insertWidget(qMax(0, box->count() - 1), navMultiBar_);
        else
            navBar->layout()->addWidget(navMultiBar_);

        connect(navMultiSelectAll_, &ElaToolButton::clicked, this, [this] {
            const auto activeId = service_->busy() ? service_->activeSession() : QString();
            QStringList idleIds;
            for (const auto &item : service_->sessions())
                if (item.id != activeId) idleIds.append(item.id);
            const bool allChecked = !idleIds.isEmpty() && std::all_of(idleIds.cbegin(), idleIds.cend(),
                [this](const QString &id) { return checkedSessions_.contains(id); });
            if (allChecked) checkedSessions_.clear();
            else for (const auto &id : idleIds) checkedSessions_.insert(id);
            updateNavMultiSelectState();
        });
        connect(navMultiDelete_, &ElaToolButton::clicked, this, [this] {
            promptDeleteSessions(checkedSessions_.values());
        });
        connect(navMultiDone_, &ElaToolButton::clicked, this, [this] {
            setSidebarMultiSelect(false);
        });
    }

    connect(navActionButton_, &ElaToolButton::clicked, this, [this] {
        if (hoveredSession_.isEmpty()) return;
        showSessionContextMenu(hoveredSession_, navActionButton_->mapToGlobal(QPoint(0, navActionButton_->height())));
    });
    connect(navView_, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const auto target = navTargetForIndex(navView_->indexAt(pos));
        if (target.kind == NavTarget::Kind::Session)
            showSessionContextMenu(target.value, navView_->mapToGlobal(pos));
        else if (target.kind == NavTarget::Kind::Workspace)
            showWorkspaceContextMenu(target.value, navView_->mapToGlobal(pos));
    });
    const auto syncOverlays = [this] {
        if (sidebarMultiSelect_) {
            updateNavOverlays();
            QTimer::singleShot(220, this, [this] { if (sidebarMultiSelect_) updateNavOverlays(); });
        } else {
            hoveredSession_.clear();
            if (navActionButton_) navActionButton_->hide();
        }
    };
    connect(navView_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (sidebarMultiSelect_) updateNavOverlays();
        else { hoveredSession_.clear(); if (navActionButton_) navActionButton_->hide(); }
    });
    connect(navView_, &QTreeView::expanded, this, syncOverlays);
    connect(navView_, &QTreeView::collapsed, this, syncOverlays);
}

void AgentWindow::setSidebarMultiSelect(bool enabled)
{
    sidebarMultiSelect_ = enabled;
    if (!enabled) checkedSessions_.clear();
    updateNavMultiSelectState();
}

void AgentWindow::updateNavMultiSelectState()
{
    const auto activeId = service_->busy() ? service_->activeSession() : QString();
    QSet<QString> validIdle;
    for (const auto &item : service_->sessions())
        if (item.id != activeId) validIdle.insert(item.id);
    checkedSessions_.intersect(validIdle);

    const bool wide = navView_ && navView_->viewport()->width() >= 100;
    if (navMultiBar_) {
        navMultiBar_->setVisible(sidebarMultiSelect_ && wide);
        navMultiCount_->setText(u"已选 %1 项"_s.arg(checkedSessions_.size()));
        const bool allChecked = !validIdle.isEmpty() && checkedSessions_.size() == validIdle.size();
        navMultiSelectAll_->setText(allChecked ? u"全不选"_s : u"全选"_s);
        navMultiSelectAll_->setEnabled(!validIdle.isEmpty());
        navMultiDelete_->setEnabled(!checkedSessions_.isEmpty());
    }
    updateNavOverlays();
}

void AgentWindow::updateNavOverlays()
{
    if (!navView_) return;
    if (!sidebarMultiSelect_ || navView_->viewport()->width() < 100) {
        for (auto *box : std::as_const(navCheckBoxes_)) box->hide();
        updateNavHoverAction(navView_->viewport()->mapFromGlobal(QCursor::pos()));
        return;
    }
    hoveredSession_.clear();
    if (navActionButton_) navActionButton_->hide();

    auto *model = navView_->model();
    if (!model) return;
    const auto activeId = service_->busy() ? service_->activeSession() : QString();
    const QRect viewportRect = navView_->viewport()->rect();
    int poolIndex = 0;

    for (int expRow = 0; expRow < expanderOrder_.size(); ++expRow) {
        const auto parentIdx = model->index(expRow + 1, 0);
        if (!parentIdx.isValid() || !navView_->isExpanded(parentIdx)) continue;
        const auto &sessions = sessionOrder_.value(expanderOrder_.at(expRow));
        for (int childRow = 0; childRow < sessions.size(); ++childRow) {
            const auto idx = model->index(childRow, 0, parentIdx);
            const auto rect = navView_->visualRect(idx);
            if (!rect.isValid() || !rect.intersects(viewportRect)) continue;
            const auto &id = sessions.at(childRow);
            if (!nodes_.contains(id)) continue;

            if (poolIndex >= navCheckBoxes_.size()) {
                auto *box = new ElaCheckBox(navView_->viewport());
                box->setFixedSize(22, 22);
                connect(box, &ElaCheckBox::toggled, this, [this, box](bool checked) {
                    const auto sessionId = box->property("sessionId").toString();
                    if (sessionId.isEmpty()) return;
                    if (checked) checkedSessions_.insert(sessionId);
                    else checkedSessions_.remove(sessionId);
                    updateNavMultiSelectState();
                });
                navCheckBoxes_.append(box);
            }
            auto *box = navCheckBoxes_.at(poolIndex++);
            box->setProperty("sessionId", id);
            {
                const QSignalBlocker block(box);
                box->setChecked(checkedSessions_.contains(id));
            }
            box->setEnabled(id != activeId);
            box->setToolTip(id == activeId ? u"运行中的会话不可多选删除"_s : QString());
            box->move(qMax(4, rect.right() - 28), rect.center().y() - 11);
            box->show();
            box->raise();
        }
    }
    for (int i = poolIndex; i < navCheckBoxes_.size(); ++i)
        navCheckBoxes_.at(i)->hide();
}

void AgentWindow::contextMenu()
{
    auto *menu = new ElaMenu(context_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *workspaces = new ElaMenu(menu); workspaces->setTitle(u"工作区"_s); menu->addMenu(workspaces);
    auto paths = service_->workspaces(); paths.prepend(QString());
    const auto *item = service_->session(session_);
    const auto current = item ? item->workspace : pendingWorkspace_;
    for (const auto &path : paths) {
        auto *choice = workspaces->addAction(path.isEmpty() ? u"无工作区"_s : QDir::toNativeSeparators(path), this, [this, path] {
            if (session_.isEmpty()) pendingWorkspace_ = path;
            else service_->setWorkspace(session_, path);
            refreshControls();
        });
        choice->setCheckable(true);
        choice->setChecked(current == path);
    }
    menu->popup(context_->mapToGlobal(QPoint(0, context_->height())));
}

bool AgentWindow::eventFilter(QObject *object, QEvent *event)
{
    if (navCentral_ && object == navCentral_ && event->type() == QEvent::Paint && navStack_) {
        QPainter painter(navCentral_);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto mode = eTheme->getThemeMode();
        const QRect targetRect = navStack_->geometry().adjusted(1, 1, 10, 10);
        painter.setPen(QPen(ElaThemeColor(mode, BasicBaseLine), 1.5));
        painter.setBrush(chatBackground(mode));
        painter.drawRoundedRect(targetRect, 10, 10);
        return true;
    }
    if (event->type() == QEvent::Show && !object->property("ElaPageTransitionRestore").toBool()) {
        if (object == newPage_) {
            select({});
        } else {
            for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it) {
                if (it->page == object) {
                    select(it.key());
                    break;
                }
            }
        }
    }
    if (navView_) {
        if (object == navView_->viewport()) {
            if (sidebarMultiSelect_ &&
                (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick ||
                 event->type() == QEvent::MouseButtonRelease)) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton) {
                    const auto target = navTargetForIndex(navView_->indexAt(mouse->pos()));
                    if (target.kind == NavTarget::Kind::Session) {
                        if (event->type() == QEvent::MouseButtonRelease) {
                            const auto activeId = service_->busy() ? service_->activeSession() : QString();
                            if (target.value != activeId) {
                                if (checkedSessions_.contains(target.value)) checkedSessions_.remove(target.value);
                                else checkedSessions_.insert(target.value);
                                updateNavMultiSelectState();
                            }
                        }
                        return true;
                    }
                }
            }
            if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)
                updateNavHoverAction(navView_->viewport()->mapFromGlobal(QCursor::pos()));
            else if (event->type() == QEvent::Resize)
                updateNavMultiSelectState();
            else if (event->type() == QEvent::Leave && navActionButton_ &&
                     !navActionButton_->rect().contains(navActionButton_->mapFromGlobal(QCursor::pos()))) {
                hoveredSession_.clear();
                navActionButton_->hide();
            }
        } else if (object == navActionButton_ && event->type() == QEvent::Leave) {
            const auto pos = navView_->viewport()->mapFromGlobal(QCursor::pos());
            if (!navView_->viewport()->rect().contains(pos)) {
                hoveredSession_.clear();
                navActionButton_->hide();
            } else {
                updateNavHoverAction(pos);
            }
        } else if (object == navView_ && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (sidebarMultiSelect_ && key->key() == Qt::Key_Escape) {
                setSidebarMultiSelect(false);
                return true;
            }
            const auto target = navTargetForIndex(navView_->currentIndex());
            if (target.kind == NavTarget::Kind::Session) {
                if (!sidebarMultiSelect_ && key->key() == Qt::Key_F2) { promptRenameSession(target.value); return true; }
                if (key->key() == Qt::Key_Delete) {
                    promptDeleteSessions(sidebarMultiSelect_ ? checkedSessions_.values() : QStringList{target.value});
                    return true;
                }
            }
        }
    }
    if (scroll_ && latest_ && object == scroll_->viewport() && event->type() == QEvent::Resize) positionLatest();
    if (object == input_ && event->type() == QEvent::InputMethod)
        imeComposing_ = !static_cast<QInputMethodEvent *>(event)->preeditString().isEmpty();
    if (object == input_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !imeComposing_) {
            if (key->modifiers().testFlag(Qt::ControlModifier)) {
                if (!input_->isReadOnly()) input_->insertPlainText(u"\n"_s);
                return true;
            }
            if (!key->text().isEmpty()) {
                if (!service_->busy()) send();
                return true;
            }
        }
    }
    return ElaWindow::eventFilter(object, event);
}

void AgentWindow::send()
{
    const auto prompt = input_->toPlainText();
    if (service_->busy() || !modelAvailable_ || prompt.trimmed().isEmpty()) return;
    if (session_.isEmpty()) {
        const auto id = service_->createSession(pendingModel_.id, pendingModel_.model, pendingWorkspace_, pendingPermission_);
        service_->setReasoningEffort(id, pendingEffort_);
        setStackSwitchMode(ElaWindowType::None);
        navigation(pageKey(nodes_.value(id).page));
        setStackSwitchMode(ElaWindowType::Popup);
        select(id);
        input_->setPlainText(prompt);
        unsent_.remove(QString());
        pendingPermission_ = Permission::Ask;
        pendingEffort_.clear();
        pendingModel_ = service_->defaultModel();
        pendingWorkspace_ = service_->defaultWorkspace();
        pendingModelOverridden_ = false;
    }
    QString error;
    if (service_->start(session_, prompt, &error)) {
        input_->clear();
        unsent_.remove(session_);
        notify({});
    } else {
        notify(error);
    }
}

void AgentWindow::notify(const QString &message)
{
    notice_->setText(message);
    notice_->setVisible(!message.isEmpty());
}

void AgentWindow::closeEvent(QCloseEvent *event)
{
    if (service_->busy()) {
        auto *dialog = new AppUi::FormDialog(this, u"任务仍在运行"_s, u"停止并关闭"_s);
        dialog->setLeftButtonText(u"返回任务"_s);
        dialog->message(u"关闭窗口会中断模型请求和正在运行的命令。"_s);
        if (!AppUi::run(dialog)) {
            event->ignore();
            return;
        }
        service_->stop();
    }
    ElaWindow::closeEvent(event);
}

