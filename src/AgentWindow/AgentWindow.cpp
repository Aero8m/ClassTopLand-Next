#include "AgentWindow.h"

#include "../Ui/UiWidgets.h"
#include "../Core/Plugins/PluginWorkspace.h"
#include "../Ui/Plugins/PluginHistoryDialog.h"
#include <QJsonDocument>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaPlainTextEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToolButton.h>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QDir>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace
{
using namespace Qt::StringLiterals;
using Permission = AgentService::Permission;

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

QString reasoningPreview(const QString &text)
{
    constexpr qsizetype limit = 24 * 1024;
    if (text.size() <= limit) return text;
    auto head = text.left(limit);
    if (head.at(head.size() - 1).isHighSurrogate()) head.chop(1);
    return head + u"\n\n…（仅显示前 24 Ki 字符）"_s;
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
        setFixedHeight(27);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto color = ElaThemeColor(eTheme->getThemeMode(), BasicText);
        color.setAlpha(underMouse() ? 230 : 155);
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
        copy_->setFixedSize(44, 26);
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
        setFixedSize(32, 32);
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
        painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (property("stopping").toBool()) {
            painter.setBrush(ink);
            painter.drawRoundedRect(QRectF(11, 11, 10, 10), 1, 1);
        } else {
            painter.drawLine(QPointF(16, 22), QPointF(16, 10));
            painter.drawLine(QPointF(11, 15), QPointF(16, 10));
            painter.drawLine(QPointF(16, 10), QPointF(21, 15));
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
        setFixedSize(30, 30);
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
        painter.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        painter.drawLine(QPointF(15, 9), QPointF(15, 21));
        painter.drawLine(QPointF(10, 16), QPointF(15, 21));
        painter.drawLine(QPointF(15, 21), QPointF(20, 16));
    }
};

QColor warningColor(ElaThemeType::ThemeMode mode)
{
    return mode == ElaThemeType::Dark ? QColor(237, 148, 85) : QColor(166, 77, 19);
}

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

class ApprovalCard final : public QWidget
{
public:
    ApprovalCard(const QString &summary, const QString &payload, QWidget *parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(10);
        status_ = new QLabel(u"● 等待审批"_s, this);
        status_->setFixedHeight(status_->fontMetrics().height() + 4);
        status_->setTextFormat(Qt::PlainText);
        layout->addWidget(status_);
        description_ = new QPlainTextEdit(this);
        description_->setObjectName(u"agentApprovalDetails"_s);
        description_->setReadOnly(true);
        description_->setLineWrapMode(QPlainTextEdit::NoWrap);
        description_->setFrameShape(QFrame::NoFrame);
        auto mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(12);
        description_->setFont(mono);
        layout->addWidget(description_);
        auto *actions = new QHBoxLayout;
        actions->setSpacing(8);
        actions->addStretch();
        auto *deny = AppUi::button(u"拒绝"_s, this);
        auto *allow = AppUi::button(u"允许"_s, this, true);
        actions->addWidget(deny);
        actions->addWidget(allow);
        layout->addLayout(actions);
        connect(deny, &ElaPushButton::clicked, this, [this] { if (onDeny) onDeny(); });
        connect(allow, &ElaPushButton::clicked, this, [this] { if (onAllow) onAllow(); });
        setContent(summary, payload);
        applyTheme();
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { applyTheme(); update(); });
    }

    std::function<void()> onAllow, onDeny;

    void setContent(const QString &summary, const QString &payload)
    {
        description_->setPlainText(summary + (payload.isEmpty() ? QString() : u"\n\n"_s + payload));
        description_->setFixedHeight(qBound(64, description_->document()->blockCount() * description_->fontMetrics().lineSpacing() + 28, 180));
        updateGeometry();
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
        description_->setStyleSheet(u"QPlainTextEdit { color:%1;background-color:%2;border:0; }"_s.arg(
            ElaThemeColor(mode, BasicText).name(), mode == ElaThemeType::Dark ? u"#1f1f1f"_s : u"#fffaf3"_s));
    }
    QLabel *status_;
    QPlainTextEdit *description_;
};

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

class MessageDocument final : public QTextDocument
{
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

void setMarkdown(ElaText *label, const QString &text)
{
    constexpr qsizetype inlineLimit = 24 * 1024;
    label->setProperty("sourceMarkdown", text);
    if (auto *full = label->parentWidget()->findChild<ElaToolButton *>(u"agentFullMessage"_s))
        full->setVisible(text.size() > inlineLimit);
    auto preview = text.left(inlineLimit);
    if (!preview.isEmpty() && preview.at(preview.size() - 1).isHighSurrogate()) preview.chop(1);
    if (label->property("renderedPreview").toString() == preview && label->property("hasPreview").toBool()) return;
    label->setProperty("renderedPreview", preview);
    label->setProperty("hasPreview", true);
    MessageDocument document;
    document.setDefaultFont(label->font());
    document.setDefaultStyleSheet(u"pre { white-space: pre-wrap; font-family: Consolas; } code { font-family: Consolas; }"_s);
    document.setMarkdown(preview, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
                               QTextDocument::MarkdownNoHTML);
    QList<QPair<int, int>> images;
    for (auto block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                images.append({fragment.position(), fragment.length()});
        }
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        QTextCursor cursor(&document);
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.insertText(u"[图片]"_s, QTextCharFormat());
    }
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
    button->setFixedSize(34, 34);
    return button;
}

ElaToolButton *composerButton(QWidget *parent, bool access = false)
{
    ElaToolButton *button = access ? new AccessButton(parent) : new ElaToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setIsTransparent(true);
    button->setBorderRadius(12);
    button->setFixedHeight(32);
    button->setCursor(Qt::PointingHandCursor);
    return button;
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
}

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
    setStackSwitchMode(ElaWindowType::None);
    for (const auto flag : {ElaAppBarType::StayTopButtonHint, ElaAppBarType::RouteBackButtonHint,
                            ElaAppBarType::RouteForwardButtonHint, ElaAppBarType::ThemeChangeButtonHint})
        setWindowButtonFlag(flag, false);
    resize(AppUi::defaultWindowSize);
    setMinimumSize(AppUi::defaultWindowSize);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });

    newPage_ = new QWidget(this);
    auto *pageLayout = new QVBoxLayout(newPage_);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    addPageNode(u"新对话"_s, newPage_, ElaIconType::Message);
    addFooterNode(u"设置"_s, settingsKey_, 0, ElaIconType::Gear);

    chat_ = new QWidget(newPage_);
    chat_->setObjectName(u"agentChat"_s);
    chat_->installEventFilter(this);
    const auto applyChatTheme = [this] {
        chat_->setStyleSheet(u"#agentChat { background: %1; border-radius: 12px; }"_s.arg(
            chatBackground(eTheme->getThemeMode()).name()));
    };
    connect(eTheme, &ElaTheme::themeModeChanged, this, applyChatTheme);
    applyChatTheme();
    auto *layout = new QVBoxLayout(chat_);
    layout->setContentsMargins(16, 6, 16, 14);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    title_ = AppUi::text({}, chat_, 17);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(title_, 1);
    more_ = iconButton(ElaIconType::Ellipsis, u"会话管理"_s, chat_);
    header->addWidget(more_);
    layout->addLayout(header);

    notice_ = AppUi::text({}, chat_, 13);
    notice_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notice_->hide();
    layout->addWidget(notice_);
    scroll_ = new ElaScrollArea(chat_);
    scroll_->setObjectName(u"agentConversationScroll"_s);
    scroll_->setStyleSheet(u"#agentConversationScroll { background: transparent; border: none; }"_s);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->viewport()->setAutoFillBackground(false);
    auto *conversation = new QWidget(scroll_);
    messages_ = new QVBoxLayout(conversation);
    messages_->setContentsMargins(4, 12, 8, 16);
    messages_->setSpacing(18);
    messages_->addStretch();
    scroll_->setWidget(conversation);
    conversation->setAutoFillBackground(false);
    auto *scrollBar = scroll_->verticalScrollBar();
    connect(scrollBar, &QScrollBar::rangeChanged, this,
            [this, scrollBar, previousMaximum = 0](int, int maximum) mutable {
        if (previousMaximum - scrollBar->value() < 52) followEnd();
        previousMaximum = maximum;
    });
    AppUi::enableTouchScrolling(scroll_);
    layout->addWidget(scroll_, 1);
    latest_ = new LatestButton(scroll_->viewport());
    latest_->setObjectName(u"agentLatest"_s);
    latest_->setToolTip(u"回到最新消息"_s);
    latest_->setAccessibleName(u"回到最新消息"_s);
    scroll_->viewport()->installEventFilter(this);
    latest_->hide();
    connect(latest_, &ElaToolButton::clicked, this, [this] {
        scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
        followEnd();
    });
    const auto updateLatest = [this, scrollBar] {
        positionLatest();
        latest_->setVisible(scrollBar->maximum() - scrollBar->value() > 120);
        if (latest_->isVisible()) latest_->raise();
    };
    connect(scrollBar, &QScrollBar::valueChanged, this, updateLatest);
    connect(scrollBar, &QScrollBar::rangeChanged, this, updateLatest);

    auto *composer = new ComposerArea(chat_);
    composer->setObjectName(u"agentComposer"_s);
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(12, 12, 10, 8);
    composerLayout->setSpacing(8);
    input_ = new ElaPlainTextEdit(composer);
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
    input_->installEventFilter(this);
    composerLayout->addWidget(input_);
    auto *options = new QHBoxLayout;
    options->setSpacing(4);
    context_ = composerButton(composer, true);
    context_->setObjectName(u"agentContext"_s);
    context_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    context_->setElaIcon(ElaIconType::Folder);
    context_->setMaximumWidth(200);
    options->addWidget(context_);
    connect(context_, &ElaToolButton::clicked, this, &AgentWindow::contextMenu);
    model_ = composerButton(composer);
    model_->setObjectName(u"agentModel"_s);
    model_->setMaximumWidth(240);
    permission_ = composerButton(composer, true);
    permission_->setObjectName(u"agentPermission"_s);
    permission_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    permission_->setElaIcon(ElaIconType::Shield);
    send_ = new SendButton(composer);
    send_->setObjectName(u"agentSend"_s);
    options->addWidget(permission_);
    options->addStretch();
    options->addWidget(model_);
    options->addWidget(send_);
    composerLayout->addLayout(options);
    layout->addWidget(composer);
    pageLayout->addWidget(chat_);

    connect(this, &ElaWindow::navigationNodeClicked, this, [this](auto, const QString &key) {
        if (navigating_) return;
        if (key == settingsKey_) {
            emit settingsRequested({});
            return;
        }
        if (key == pageKey(newPage_)) {
            select({});
            return;
        }
        for (auto it = nodes_.cbegin(); it != nodes_.cend(); ++it)
            if (key == pageKey(it->page)) { select(it.key()); return; }
    });
    connect(model_, &ElaToolButton::clicked, this, &AgentWindow::modelMenu);
    connect(permission_, &ElaToolButton::clicked, this, &AgentWindow::permissionMenu);
    connect(more_, &ElaToolButton::clicked, this, &AgentWindow::sessionMenu);
    connect(send_, &ElaToolButton::clicked, this, [this] {
        if (service_->busy() && service_->activeSession() == session_) service_->stop();
        else send();
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
        if (id == session_) updateTool(event);
    });
    connect(service_, &AgentService::busyChanged, this, [this](bool busy) {
        const auto *current = service_->session(session_);
        if (busy && service_->activeSession() == session_ && current && current->events.isEmpty()) render();
        if (!busy && (!toolCards_.isEmpty() || streamLabel_ || !stream_.isEmpty() || reasoningHeader_ || !reasoning_.isEmpty())) {
            auto *bar = scroll_->verticalScrollBar();
            const int position = bar->value();
            const bool atEnd = bar->maximum() - position < 52;
            render();
            if (!atEnd) QTimer::singleShot(0, bar, [bar, position] { bar->setValue(position); });
        }
        refreshControls();
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
        navigation(pageKey(newPage_));
    }
    for (const auto &id : remove) {
        const auto node = nodes_.take(id);
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
        }
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        addPageNode(item.title, page, groups_.value(item.workspace), ElaIconType::Message);

        nodes_.insert(item.id, {page, item.workspace});
        if (!getNavigationNodeIsExpanded(groups_.value(item.workspace)))
            expandNavigationNode(groups_.value(item.workspace));
    }
    for (const auto &workspace : groups_.keys()) {
        bool used = false;
        for (const auto &node : std::as_const(nodes_)) used |= node.workspace == workspace;
        if (!used) removeNavigationNode(groups_.take(workspace));
    }
    if (remove.contains(current)) {
        if (nodes_.contains(current)) {
            nodes_.value(current).page->layout()->addWidget(chat_);
            navigation(pageKey(nodes_.value(current).page));
        } else {
            select({});
            unsent_.remove(current);
        }
    }
    refreshControls();
}

void AgentWindow::select(const QString &id)
{
    if (session_ == id) return;
    unsent_.insert(session_, input_->toPlainText());
    session_ = id;
    auto *page = nodes_.contains(id) ? nodes_.value(id).page : newPage_;
    page->layout()->addWidget(chat_);
    input_->setPlainText(unsent_.value(id));
    imeComposing_ = false;
    render();
    refreshControls();
    input_->setFocus();
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
    const auto location = item && !item->plugin.isEmpty() ? item->plugin : workspaceName(item ? item->workspace : pendingWorkspace_);
    context_->setText(fontMetrics().elidedText(location, Qt::ElideRight, 130) + u" ⌄"_s);
    context_->setToolTip(u"切换工作区；插件由 AI 根据任务选择\n"_s + (item ? item->workspace : pendingWorkspace_) +
                         (item && !item->plugin.isEmpty() ? u"\n当前插件："_s + item->plugin : QString()));
    context_->setEnabled(!busy);
    more_->setVisible(item != nullptr);
    more_->setEnabled(!active);
    const auto effort = connection.effectiveReasoningEffort();
    model_->setText(connection.id.isEmpty() ? (item && !item->connection.isEmpty() ? u"需重新选择模型"_s : u"选择模型 ⌄"_s)
        : fontMetrics().elidedText(modelName, Qt::ElideRight, 150) +
          (effort.isEmpty() ? QString() : u" · "_s + effortLabel(effort)) + u" ⌄"_s);
    model_->setToolTip(connection.id.isEmpty() ? u"在设置中管理模型供应商"_s
                       : u"%1 · %2\n%3\n思考强度：%4"_s.arg(connection.name, connection.model,
                           connection.endpoint().toDisplayString(), effortLabel(effort)));
    permission_->setText(access == Permission::Yolo ? u"完全访问"_s : permissionLabel(access));
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
    send_->setElaIcon(active ? ElaIconType::Stop : ElaIconType::ArrowUp);
    send_->setProperty("stopping", active);
    send_->update();
    send_->setAccessibleName(active ? u"停止"_s : u"发送"_s);
    send_->setToolTip(send_->accessibleName());
    send_->setEnabled(active || (!busy && !connection.id.isEmpty()));
}

void AgentWindow::render()
{
    streamTimer_.stop();
    streamLabel_ = nullptr;
    if (approvalLayer_) {
        approvalLayer_->hide();
        approvalLayer_->deleteLater();
        approvalLayer_ = nullptr;
        approvalCard_ = nullptr;
    }
    reasoningScroll_ = nullptr;
    reasoningText_ = nullptr;
    reasoningHeader_ = nullptr;
    resetActivities();
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
    if (!stream_.isEmpty()) flushStream();
    if ((!item || item->events.isEmpty()) && !active)
        addMessage(u"welcome"_s, pendingModel_.id.isEmpty()
            ? u"在「设置 → AI Agent」配置供应商和模型后开始对话。"_s
            : u"可以直接提问，也可以在对话中使用系统工具和开发应用插件。"_s);
    if (!service_->draftName(session_).isEmpty() && item && item->events.isEmpty())
        addMessage(u"draft"_s, u"当前会话已有插件草稿"_s);
    followEnd();
}

void AgentWindow::addEvent(const QJsonObject &event)
{
    const auto kind = event.value(u"kind"_s).toString();
    auto text = event.value(u"text"_s).toString();
    if (kind == "user"_L1) resetActivities();
    if (kind == "turn"_L1) {
        resetActivities();
        auto *elapsed = AppUi::text(text, scroll_->widget(), 12);
        elapsed->setObjectName(u"agentTurnDuration"_s);
        addRow(elapsed);
        if (const auto *item = service_->session(session_); item && item->resumable) {
            auto *resume = AppUi::button(u"继续执行"_s, chat_);
            connect(resume, &ElaPushButton::clicked, this, [this] {
                QString error;
                if (!service_->continueSession(session_, &error)) notify(error);
            });
            addRow(resume);
        }
        return;
    }
    if (kind == "preview"_L1) {
        auto *card=new QWidget(chat_);auto *layout=new QVBoxLayout(card);
        const QPixmap image(text);
        auto *label=new QLabel(card);label->setObjectName(u"pluginPreviewScreenshot"_s);
        if(image.isNull()) label->setText(u"预览截图已不可用"_s);
        else {label->setPixmap(image.scaledToWidth(qMin(640,image.width()),Qt::SmoothTransformation));label->setToolTip(u"插件预览截图；供用户检查界面"_s);}
        layout->addWidget(label);
        addRow(card);return;
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
            reasoningHeader_->setText(text);
            reasoningHeader_->setProperty("processText", process);
            if (reasoningText_ && reasoningText_->isVisible())
                setMarkdown(reasoningText_, reasoningPreview(process));
        } else {
            addReasoning(process, text);
        }
        reasoning_.clear();
        reasoningHeader_ = nullptr;
        reasoningText_ = nullptr;
        reasoningScroll_ = nullptr;
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
            resetActivities();
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
    auto *row = new QWidget(scroll_->widget());
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(right ? 62 : 0, 0, 0, 0);
    if (!right) card->setMaximumWidth(780);
    if (right) layout->addStretch();
    layout->addWidget(card, right ? 0 : 1);
    messages_->insertWidget(messages_->count() - 1, row);
}

void AgentWindow::resetActivities()
{
    if (activityHeader_) activityHeader_->setChecked(false);
    activities_ = activityItems_ = nullptr;
    activityHeader_ = nullptr;
    activityLayout_ = nullptr;
}

void AgentWindow::ensureActivities()
{
    if (activities_) return;
    activities_ = new QWidget(scroll_->widget());
    activities_->setObjectName(u"agentActivities"_s);
    auto *layout = new QVBoxLayout(activities_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    activityHeader_ = new ActivityButton(activities_);
    activityHeader_->setObjectName(u"agentActivitySummary"_s);
    activityHeader_->setText(u"活动记录"_s);
    activityHeader_->setElaIcon(ElaIconType::Code);
    layout->addWidget(activityHeader_);
    activityItems_ = new QWidget(activities_);
    activityLayout_ = new QVBoxLayout(activityItems_);
    activityLayout_->setContentsMargins(0, 0, 0, 0);
    activityLayout_->setSpacing(2);
    layout->addWidget(activityItems_);
    activityItems_->hide();
    connect(activityHeader_, &ElaToolButton::toggled, activityItems_, &QWidget::setVisible);
    addRow(activities_);
    activities_->hide();
}

void AgentWindow::addActivity(QWidget *widget)
{
    ensureActivities();
    activityLayout_->addWidget(widget);
    activityHeader_->setCheckable(true);
    QStringList summaries;
    for (int i = 0; i < activityLayout_->count(); ++i) {
        const auto summary = activityLayout_->itemAt(i)->widget()->property("activitySummary").toString();
        if (!summary.isEmpty() && !summaries.contains(summary)) summaries.append(summary);
    }
    activityHeader_->setText(summaries.isEmpty() ? u"思考与操作"_s : summaries.join(u" · "_s));
    activityHeader_->setToolTip(activityHeader_->text());
    activities_->show();
}

void AgentWindow::addMessage(const QString &kind, const QString &text, bool final)
{
    if (kind == u"assistant"_s) resetActivities();
    const auto *bar = scroll_->verticalScrollBar();
    const bool follow = bar->maximum() - bar->value() < 52;
    if (kind == "plan"_L1) {
        auto *card = new QWidget(scroll_->widget());
        card->setProperty("activitySummary", u"更新任务步骤"_s);
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
        if (follow) followEnd();
        return;
    }
    if (kind == "tool"_L1) {
        auto *row = new ActivityButton(scroll_->widget());
        row->setProperty("activitySummary", text.contains(u"失败"_s) || text.contains(u"已拒绝"_s)
            ? text.simplified() : text.section(u" · "_s, 0, 0).simplified());
        row->setText(text.simplified());
        row->setToolTip(text);
        row->setElaIcon(ElaIconType::Code);
        addActivity(row);
        if (follow) followEnd();
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
    if (follow) followEnd();
}

void AgentWindow::addFinalCopy(ElaText *content)
{
    auto *card = content->parentWidget();
    if (card->findChild<ElaToolButton *>(u"agentFinalCopy"_s)) return;
    auto *actions = card->findChild<QHBoxLayout *>(u"agentMessageActions"_s);
    if (!actions) return;
    auto *copy = iconButton(ElaIconType::Copy, u"复制最终答复"_s, card);
    copy->setObjectName(u"agentFinalCopy"_s);
    copy->setFixedSize(26, 26);
    actions->insertWidget(0, copy);
    connect(copy, &ElaToolButton::clicked, card, [content] {
        QApplication::clipboard()->setText(content->property("sourceMarkdown").toString());
    });
}

void AgentWindow::positionLatest()
{
    const auto area = scroll_->viewport()->rect();
    latest_->move(qMax(0, (area.width() - latest_->width()) / 2), qMax(0, area.height() - latest_->height() - 12));
}

void AgentWindow::updateTool(const QJsonObject &event)
{
    const auto id = event.value(u"id"_s).toString();
    const auto *bar = scroll_->verticalScrollBar();
    const bool follow = bar->maximum() - bar->value() < 52;
    if (!toolCards_.contains(id)) {
        auto *card = new QWidget(scroll_->widget());
        card->setProperty("activitySummary", toolLabel(event.value(u"name"_s).toString()));
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto *header = new ActivityButton(card);
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
        addActivity(card);
        toolCards_.insert(id, {header, details, contents, review, activityHeader_});
    }
    const auto card = toolCards_.value(id);
    const auto state = event.value(u"state"_s).toString();
    const auto name = event.value(u"name"_s).toString();
    const auto detail = event.value(u"detail"_s).toString();
    card.header->setText(u"%1 · %2"_s.arg(toolLabel(name), state));
    card.header->setToolTip(card.header->text());
    static_cast<ToolDetails *>(card.contents)->setEvent(event);
    card.header->setEnabled(true);
    card.review->setVisible(name == "write_plugin_draft"_L1 && state != u"正在执行"_s &&
                            !service_->draftName(session_).isEmpty());
    if (state == u"待审批"_s) {
        card.header->setChecked(true);
        card.group->setChecked(true);
        const auto split = detail.indexOf(u"\n\n"_s);
        const auto summary = split < 0 ? detail : detail.left(split);
        const auto payload = split < 0 ? QString() : detail.mid(split + 2);
        if (auto *existing = static_cast<ApprovalCard *>(approvalCard_)) {
            existing->setContent(summary, payload);
        } else {
            approvalLayer_ = new QWidget(chat_);
            approvalLayer_->setObjectName(u"agentApprovalLayer"_s);
            approvalLayer_->setAttribute(Qt::WA_NoSystemBackground, true);
            approvalLayer_->setAutoFillBackground(false);
            auto *layerLayout = new QVBoxLayout(approvalLayer_);
            layerLayout->setContentsMargins(16, 0, 16, 14);
            layerLayout->setSpacing(0);
            layerLayout->addStretch();
            auto *approval = new ApprovalCard(summary, payload, approvalLayer_);
            approval->onAllow = [this, id] { service_->decide(id, true); };
            approval->onDeny = [this, id] { service_->decide(id, false); };
            layerLayout->addWidget(approval);
            approvalCard_ = approval;
            approvalLayer_->setGeometry(chat_->rect());
            approvalLayer_->show();
        }
        approvalLayer_->raise();
    } else if (approvalLayer_) {
        approvalLayer_->hide();
        approvalLayer_->deleteLater();
        approvalLayer_ = nullptr;
        approvalCard_ = nullptr;
    }
    if (follow) followEnd();
}

void AgentWindow::flushStream()
{
    if (stream_.isEmpty()) return;
    const auto *bar = scroll_->verticalScrollBar();
    const bool follow = bar->maximum() - bar->value() < 52;
    if (!streamLabel_) addMessage(u"assistant"_s, {});
    setMarkdown(streamLabel_, stream_);
    if (follow) followEnd();
}

void AgentWindow::addReasoning(const QString &text, const QString &status)
{
    auto *card = new QWidget(scroll_->widget());
    card->setProperty("activitySummary", u"思考"_s);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    reasoningHeader_ = new ActivityButton(card);
    reasoningHeader_->setText(status);
    reasoningHeader_->setObjectName(u"agentReasoning"_s);
    reasoningHeader_->setCheckable(true);
    reasoningHeader_->setElaIcon(ElaIconType::Clock);
    reasoningHeader_->setProperty("processText", text);
    reasoningHeader_->setEnabled(!text.isEmpty());
    reasoningHeader_->setToolTip(text.isEmpty() ? u"思考过程仅在本次运行期间可用"_s : u"展开或收起模型返回的思考过程"_s);
    layout->addWidget(reasoningHeader_);
    auto *details = new QWidget(card);
    auto *body = new QVBoxLayout(details);
    body->setContentsMargins(0, 0, 0, 0);
    reasoningScroll_ = new ElaScrollArea(details);
    reasoningScroll_->setObjectName(u"agentReasoningView"_s);
    reasoningScroll_->setStyleSheet(u"#agentReasoningView { background: transparent; border: none; }"_s);
    reasoningScroll_->setWidgetResizable(true);
    reasoningScroll_->setFrameShape(QFrame::NoFrame);
    reasoningScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    reasoningScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    reasoningScroll_->setFixedHeight(200);
    reasoningScroll_->viewport()->setAutoFillBackground(false);
    auto *content = new QWidget(reasoningScroll_);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    reasoningText_ = messageLabel(content, 13);
    reasoningText_->setIsWrapAnywhere(false);
    reasoningText_->setWordWrap(true);
    reasoningText_->setTextFormat(Qt::RichText);
    reasoningText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    contentLayout->addWidget(reasoningText_);
    contentLayout->addStretch();
    reasoningScroll_->setWidget(content);
    // QScrollArea::setWidget turns on auto-fill; without this the widget paints the
    // system palette base (dark under Windows dark mode) behind the transparent label.
    content->setAutoFillBackground(false);
    reasoningText_->setAutoFillBackground(false);
    body->addWidget(reasoningScroll_);
    auto *header = reasoningHeader_;
    auto *view = reasoningText_;
    auto *scroll = reasoningScroll_;
    connect(eTheme, &ElaTheme::themeModeChanged, view, [header, view] {
        if (!view->isVisible()) return;
        view->setProperty("hasPreview", false);
        setMarkdown(view, reasoningPreview(header->property("processText").toString()));
    });
    details->hide();
    layout->addWidget(details);
    connect(header, &ElaToolButton::toggled, details, [header, view, scroll, details](bool expanded) {
        if (expanded) {
            view->setProperty("hasPreview", false);
            setMarkdown(view, reasoningPreview(header->property("processText").toString()));
            QTimer::singleShot(0, scroll, [scroll] {
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            });
        }
        details->setVisible(expanded);
    });
    addActivity(card);
}

void AgentWindow::flushReasoning()
{
    if (reasoning_.isEmpty()) return;
    auto *bar = scroll_->verticalScrollBar();
    const bool follow = bar->maximum() - bar->value() < 52;
    if (!reasoningHeader_) addReasoning(reasoning_, u"正在思考…"_s);
    reasoningHeader_->setProperty("processText", reasoning_);
    if (reasoningText_ && reasoningText_->isVisible() && reasoningScroll_) {
        auto *inner = reasoningScroll_->verticalScrollBar();
        const bool followInner = inner->maximum() - inner->value() < 24;
        setMarkdown(reasoningText_, reasoningPreview(reasoning_));
        if (followInner)
            QTimer::singleShot(0, inner, [inner] { inner->setValue(inner->maximum()); });
    }
    if (follow) followEnd();
}

void AgentWindow::followEnd()
{
    auto *bar = scroll_->verticalScrollBar();
    const auto previous = bar->value();
    QTimer::singleShot(0, bar, [bar, previous] {
        if (bar->value() >= previous - 3) bar->setValue(bar->maximum());
    });
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
    const auto id = session_;
    auto *menu = new ElaMenu(more_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *resume = menu->addAction(u"继续执行"_s, this, [this, id] {
        QString error;
        if (!service_->continueSession(id, &error)) notify(error);
    });
    const auto *item = service_->session(id);
    resume->setEnabled(!service_->busy() && item && item->resumable);
    menu->addAction(u"重命名"_s, this, [this, id] {
        const auto *item = service_->session(id);
        if (!item) return;
        auto *dialog = new AppUi::FormDialog(this, u"重命名会话"_s);
        auto *name = dialog->line(u"会话名称"_s, item->title);
        dialog->submit = [this, id, name] {
            if (name->text().trimmed().isEmpty()) return u"请输入名称"_s;
            service_->renameSession(id, name->text());
            return QString();
        };
        AppUi::run(dialog);
    });
    menu->addAction(u"删除会话"_s, this, [this, id] {
        if (AppUi::confirm(this, u"删除会话"_s, u"删除所选会话的本地历史？"_s))
            service_->deleteSession(id);
    });
    menu->popup(more_->mapToGlobal(QPoint(0, more_->height())));
}

void AgentWindow::contextMenu()
{
    auto *menu = new ElaMenu(context_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *workspaces = new ElaMenu(menu); workspaces->setTitle(u"工作区"_s); menu->addMenu(workspaces);
    auto paths = service_->workspaces(); paths.prepend(QString());
    for (const auto &path : paths) workspaces->addAction(path.isEmpty() ? u"无工作区"_s : QDir::toNativeSeparators(path), this, [this, path] {
        if (session_.isEmpty()) pendingWorkspace_ = path;
        else service_->setWorkspace(session_, path);
        refreshControls();
    });
    menu->popup(context_->mapToGlobal(QPoint(0, context_->height())));
}

bool AgentWindow::eventFilter(QObject *object, QEvent *event)
{
    if (scroll_ && latest_ && object == scroll_->viewport() && event->type() == QEvent::Resize) positionLatest();
    if (object == chat_ && event->type() == QEvent::Resize && approvalLayer_)
        approvalLayer_->setGeometry(chat_->rect());
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
    if (service_->busy() || prompt.trimmed().isEmpty()) return;
    if (session_.isEmpty()) {
        if (pendingModel_.id.isEmpty()) { emit settingsRequested({}); return; }
        const auto id = service_->createSession(pendingModel_.id, pendingModel_.model, pendingWorkspace_, pendingPermission_);
        service_->setReasoningEffort(id, pendingEffort_);
        navigation(pageKey(nodes_.value(id).page));
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

