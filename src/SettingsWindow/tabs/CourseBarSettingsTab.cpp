#include "CourseBarSettingsTab.h"

#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Core/ThemeManager/ThemeManager.h"
#include "../../Utils/UiAccess/UiAccess.h"
#include "../../ProfileEditWindow/ProfileEditWidgets.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <QDrag>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>
#include <limits>

namespace {
constexpr auto componentMime = "application/x-qabstractitemmodeldatalist";
QString componentName(CourseBarComponentType type)
{
    switch (type) {
    case CourseBarComponentType::Date: return QObject::tr("日期");
    case CourseBarComponentType::CourseView: return QObject::tr("课程查看");
    case CourseBarComponentType::TextTip: return QObject::tr("文本提示");
    }
    return {};
}

ElaPushButton* smallButton(const QString& title, const QString& accessible, QWidget* parent)
{
    auto* button = new ElaPushButton(title, parent);
    button->setFixedSize(title.size() > 1 ? 64 : 36, 34);
    button->setBorderRadius(6);
    button->setAccessibleName(accessible);
    button->setToolTip(accessible);
    return button;
}
}

// Drops propose a move using the original config index, including duplicate types.
// Only a successful disk commit changes either view.
class CourseBarComponentList final : public QListWidget
{
public:
    CourseBarComponentList(bool horizontal, const QString& scope, QWidget* parent)
        : QListWidget(parent), horizontal_(horizontal), scope_(scope)
    {
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setDropIndicatorShown(false);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setFrameShape(QFrame::NoFrame);
        setSpacing(4);
        if (horizontal_) {
            setFlow(QListView::LeftToRight);
            setWrapping(false);
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        } else {
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    std::function<void(int, int)> moved;
    void identify(QListWidgetItem* item, int index) const
    {
        item->setData(Qt::UserRole, index);
        item->setData(Qt::UserRole + 1, scope_);
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        if (!currentItem()) return;
        QDrag drag(this);
        drag.setMimeData(model()->mimeData({model()->index(currentRow(), 0)}));
        drag.setPixmap(viewport()->grab(visualItemRect(currentItem())));
        drag.exec(Qt::MoveAction);
    }
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (!accepts(event->mimeData())) { event->ignore(); return; }
        QListWidget::dragEnterEvent(event);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (!accepts(event->mimeData())) { event->ignore(); return; }
        // Keep Qt's edge auto-scroll when the component list exceeds the viewport.
        QListWidget::dragMoveEvent(event);
        insertion_ = insertionAt(event->position().toPoint());
        viewport()->update();
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        QListWidget::dragLeaveEvent(event);
        insertion_ = -1;
        viewport()->update();
        event->accept();
    }
    void dropEvent(QDropEvent* event) override
    {
        if (!accepts(event->mimeData())) { event->ignore(); return; }
        const int from = sourceIndex(event->mimeData());
        const int slot = insertionAt(event->position().toPoint());
        int to = slot < count() ? item(slot)->data(Qt::UserRole).toInt()
                               : count() ? item(count() - 1)->data(Qt::UserRole).toInt() + 1 : 0;
        if (from < to) --to;
        QDragLeaveEvent leave;
        QListWidget::dragLeaveEvent(&leave); // Stop scrolling without mutating the model.
        insertion_ = -1;
        viewport()->update();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        if (moved) moved(from, to);
    }
    void paintEvent(QPaintEvent* event) override
    {
        QListWidget::paintEvent(event);
        if (insertion_ < 0 || !count()) return;
        const QRect bounds = visualItemRect(item(qMin(insertion_, count() - 1)));
        QPainter painter(viewport());
        painter.setPen(QPen(ThemeManager::instance().themeColor(), 3));
        if (horizontal_) {
            const int x = insertion_ < count() ? bounds.left() - 2 : bounds.right() + 2;
            painter.drawLine(x, bounds.top(), x, bounds.bottom());
        } else {
            const int y = insertion_ < count() ? bounds.top() - 2 : bounds.bottom() + 2;
            painter.drawLine(bounds.left(), y, bounds.right(), y);
        }
    }
private:
    bool accepts(const QMimeData* mime) const
    {
        return sourceIndex(mime) >= 0;
    }
    int sourceIndex(const QMimeData* mime) const
    {
        if (!mime->hasFormat(componentMime)) return -1;
        QDataStream stream(mime->data(componentMime));
        int row = -1, column = -1;
        QMap<int, QVariant> roles;
        stream >> row >> column >> roles;
        if (stream.status() != QDataStream::Ok || !stream.atEnd() ||
            roles.value(Qt::UserRole + 1).toString() != scope_ || !roles.contains(Qt::UserRole)) return -1;
        return roles.value(Qt::UserRole).toInt();
    }
    int insertionAt(const QPoint& point) const
    {
        for (int i = 0; i < count(); ++i) {
            const QRect bounds = visualItemRect(item(i));
            if (horizontal_ ? point.x() < bounds.center().x() : point.y() < bounds.center().y()) return i;
        }
        return count();
    }
    bool horizontal_;
    QString scope_;
    int insertion_ = -1;
};

CourseBarSettingsTab::CourseBarSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace ProfileEditUi;
    setObjectName(QStringLiteral("courseBarSettingsTab"));
    auto* layout = page(this, tr("课程条"));
    layout->addWidget(text(tr("配置课程条的显示、组件和排列顺序；增强置顶切换后重启，其余修改立即生效。"), this));

    auto* basics = card(layout, tr("基本设置"));
    auto* enableRow = new QHBoxLayout;
    enableRow->addWidget(text(tr("启用课程条"), this, 16));
    enableRow->addStretch();
    enableSwitch_ = new ElaToggleSwitch(this);
    enableSwitch_->setObjectName(QStringLiteral("courseBarEnableSwitch"));
    enableSwitch_->setAccessibleName(tr("启用课程条"));
    enableRow->addWidget(enableSwitch_);
    basics->addLayout(enableRow);
    auto* uiAccessRow = new QHBoxLayout;
    uiAccessRow->addWidget(text(tr("UIAccess 增强置顶"), this, 16));
    uiAccessRow->addStretch();
    uiAccessSwitch_ = new ElaToggleSwitch(this);
    uiAccessSwitch_->setObjectName(QStringLiteral("courseBarUiAccessSwitch"));
    uiAccessSwitch_->setAccessibleName(tr("UIAccess 增强置顶"));
    uiAccessRow->addWidget(uiAccessSwitch_);
    basics->addLayout(uiAccessRow);
    basics->addWidget(text(tr("切换后自动重启；开启时需要管理员授权。"), this));
    uiAccessStatus_ = text({}, this);
    uiAccessStatus_->setObjectName(QStringLiteral("courseBarUiAccessStatus"));
    basics->addWidget(uiAccessStatus_);
    auto* heightRow = new QHBoxLayout;
    heightRow->addWidget(text(tr("课程条高度"), this, 16));
    heightRow->addStretch();
    heightSpin_ = new ElaSpinBox(this);
    heightSpin_->setObjectName(QStringLiteral("courseBarHeightSpin"));
    heightSpin_->setAccessibleName(tr("课程条高度"));
    heightSpin_->setRange(1, std::numeric_limits<int>::max());
    heightSpin_->setSuffix(QStringLiteral(" px"));
    heightSpin_->setKeyboardTracking(false);
    heightSpin_->setFixedWidth(150);
    heightRow->addWidget(heightSpin_);
    basics->addLayout(heightRow);

    const QString scope = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* previewBody = card(layout, tr("布局预览"));
    previewStatus_ = text({}, this);
    previewStatus_->setObjectName(QStringLiteral("courseBarPreviewStatus"));
    previewBody->addWidget(previewStatus_);
    auto* previewRow = new QHBoxLayout;
    previewRow->setSpacing(8);
    auto* handle = text(tr("收起"), this);
    handle->setAlignment(Qt::AlignCenter);
    handle->setFixedWidth(40);
    handle->setToolTip(tr("收起按钮固定在最左侧"));
    previewRow->addWidget(handle);
    preview_ = new CourseBarComponentList(true, scope, this);
    preview_->setObjectName(QStringLiteral("courseBarPreview"));
    preview_->setAccessibleName(tr("拖拽调整课程条组件位置"));
    preview_->setFixedHeight(92);
    previewRow->addWidget(preview_, 1);
    previewBody->addLayout(previewRow);
    previewBody->addWidget(text(tr("拖动示例组件调整左右位置，点击组件可定位到管理列表。预览只展示启用的组件。"), this));

    auto* body = card(layout, tr("组件管理"));
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(text(tr("添加组件"), this));
    typeCombo_ = new ElaComboBox(this);
    typeCombo_->setObjectName(QStringLiteral("courseBarComponentType"));
    typeCombo_->setAccessibleName(tr("添加的组件类型"));
    typeCombo_->addItem(tr("日期"), static_cast<int>(CourseBarComponentType::Date));
    typeCombo_->addItem(tr("课程查看"), static_cast<int>(CourseBarComponentType::CourseView));
    typeCombo_->addItem(tr("文本提示"), static_cast<int>(CourseBarComponentType::TextTip));
    addRow->addWidget(typeCombo_, 1);
    auto* add = button(tr("添加"), this, true);
    add->setObjectName(QStringLiteral("addCourseBarComponent"));
    addRow->addWidget(add);
    body->addLayout(addRow);
    components_ = new CourseBarComponentList(false, scope, this);
    components_->setObjectName(QStringLiteral("courseBarComponents"));
    components_->setAccessibleName(tr("课程条组件列表"));
    body->addWidget(components_);
    emptyLabel_ = text(tr("尚未添加组件，请从上方选择组件并添加。"), this);
    body->addWidget(emptyLabel_);
    body->addWidget(text(tr("从左到右按列表顺序排列；拖动组件行或使用箭头调整位置。关闭组件会保留其位置。"), this));

    auto* footer = new QHBoxLayout;
    footer->addStretch();
    auto* reset = button(tr("恢复默认"), this);
    reset->setObjectName(QStringLiteral("resetCourseBarSettings"));
    footer->addWidget(reset);
    layout->addLayout(footer);
    layout->addStretch();

    connect(enableSwitch_, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        if (enabled == config_.enable) return;
        auto candidate = config_; candidate.enable = enabled; save(candidate, selected_);
    });
    connect(uiAccessSwitch_, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        if (uiAccessSwitching_ || !UiAccess::supported()) return;
        if (enabled == config_.uiAccessEnabled && enabled == UiAccess::enabled()) return;
        setUiAccessSwitching(true);
        emit uiAccessChangeRequested(enabled);
    });
    connect(heightSpin_, &ElaSpinBox::valueChanged, this, [this](int height) {
        if (height == config_.height) return;
        auto candidate = config_; candidate.height = height; save(candidate, selected_);
    });
    connect(add, &ElaPushButton::clicked, this, [this] {
        auto candidate = config_;
        candidate.components.append({static_cast<CourseBarComponentType>(typeCombo_->currentData().toInt()), true});
        save(candidate, candidate.components.size() - 1);
    });
    connect(reset, &ElaPushButton::clicked, this, [this] {
        if (uiAccessSwitching_) return;
        if (UiAccess::supported() && (config_.uiAccessEnabled || UiAccess::enabled())) {
            resetPending_ = true;
            setUiAccessSwitching(true);
            emit uiAccessChangeRequested(false);
            // Apply the remaining defaults only after the permission transition
            // has been accepted; a canceled exit must preserve all settings.
            return;
        }
        save(CourseBarConfig{}, 0);
    });
    connect(preview_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item) selectComponent(item->data(Qt::UserRole).toInt());
    });
    connect(components_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item) selectComponent(item->data(Qt::UserRole).toInt());
    });
    preview_->moved = components_->moved = [this](int from, int to) { moveComponent(from, to); };
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { refreshTheme(); });
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, this, &CourseBarSettingsTab::refreshTheme);
    refresh();
}

void CourseBarSettingsTab::showEvent(QShowEvent* event)
{
    ElaScrollPage::showEvent(event);
    refresh();
}

void CourseBarSettingsTab::refresh()
{
    using namespace ProfileEditUi;
    config_ = ConfigManager::instance().config().courseBarConfig;
    const QSignalBlocker enableBlock(enableSwitch_), heightBlock(heightSpin_);
    const QSignalBlocker previewBlock(preview_), listBlock(components_);
    enableSwitch_->setIsToggled(config_.enable);
    const QSignalBlocker uiAccessBlock(uiAccessSwitch_);
    uiAccessSwitch_->setIsToggled(config_.uiAccessEnabled);
    uiAccessSwitch_->setEnabled(UiAccess::supported() && !uiAccessSwitching_);
    uiAccessStatus_->setText(!UiAccess::supported() ? tr("仅支持 Windows")
        : uiAccessSwitching_ ? tr("切换中…") : UiAccess::enabled() ? tr("增强置顶") : tr("普通置顶"));
    heightSpin_->setValue(config_.height);
    preview_->clear();
    components_->clear();
    for (int i = 0; i < config_.components.size(); ++i) {
        const auto component = config_.components.at(i);
        const QString name = componentName(component.type);
        auto* item = new QListWidgetItem(components_);
        components_->identify(item, i);
        item->setSizeHint(QSize(0, 54));
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 4, 10, 4);
        rowLayout->setSpacing(6);
        auto* grip = new ElaText(row);
        grip->setElaIcon(ElaIconType::GripVertical);
        grip->setTextPixelSize(16);
        grip->setFixedSize(18, 30);
        grip->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(grip);
        auto* label = text(tr("%1. %2%3").arg(i + 1).arg(name, component.enabled ? QString() : tr("（已关闭）")), row);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(label, 1);
        if (component.type == CourseBarComponentType::TextTip) {
            auto* edit = new ElaLineEdit(row);
            edit->setObjectName(QStringLiteral("courseBarComponentText_%1").arg(i));
            edit->setAccessibleName(tr("第 %1 个文本提示的内容").arg(i + 1));
            edit->setPlaceholderText(tr("显示文本"));
            edit->setText(component.text);
            edit->setMinimumWidth(120);
            rowLayout->addWidget(edit, 2);
            connect(edit, &ElaLineEdit::editingFinished, this, [this, i, edit] {
                if (edit->text() == config_.components.at(i).text) return;
                auto candidate = config_;
                candidate.components[i].text = edit->text();
                save(candidate, i);
            });
        }
        auto* toggle = new ElaToggleSwitch(row);
        toggle->setObjectName(QStringLiteral("courseBarComponentEnable_%1").arg(i));
        toggle->setAccessibleName(tr("启用第 %1 个组件：%2").arg(i + 1).arg(name));
        toggle->setIsToggled(component.enabled);
        rowLayout->addWidget(toggle);
        auto* left = smallButton(QStringLiteral("←"), tr("将第 %1 个组件左移").arg(i + 1), row);
        auto* right = smallButton(QStringLiteral("→"), tr("将第 %1 个组件右移").arg(i + 1), row);
        auto* remove = smallButton(tr("移除"), tr("移除第 %1 个组件").arg(i + 1), row);
        left->setObjectName(QStringLiteral("courseBarComponentLeft_%1").arg(i));
        right->setObjectName(QStringLiteral("courseBarComponentRight_%1").arg(i));
        remove->setObjectName(QStringLiteral("courseBarComponentRemove_%1").arg(i));
        left->setEnabled(i > 0);
        right->setEnabled(i + 1 < config_.components.size());
        rowLayout->addWidget(left); rowLayout->addWidget(right); rowLayout->addWidget(remove);
        components_->setItemWidget(item, row);
        connect(toggle, &ElaToggleSwitch::toggled, this, [this, i](bool enabled) {
            auto candidate = config_; candidate.components[i].enabled = enabled; save(candidate, i);
        });
        connect(left, &ElaPushButton::clicked, this, [this, i] { moveComponent(i, i - 1); });
        connect(right, &ElaPushButton::clicked, this, [this, i] { moveComponent(i, i + 1); });
        connect(remove, &ElaPushButton::clicked, this, [this, i] {
            auto candidate = config_; candidate.components.removeAt(i); save(candidate, i);
        });
        if (component.enabled) {
            auto* sample = new QListWidgetItem(preview_);
            preview_->identify(sample, i);
            switch (component.type) {
            case CourseBarComponentType::Date:
                sample->setText(tr("日期\n周一 10/05"));
                break;
            case CourseBarComponentType::CourseView:
                sample->setText(tr("课程查看\n语文　数学　英语"));
                break;
            case CourseBarComponentType::TextTip:
                sample->setText(tr("文本提示\n%1").arg(component.text));
                break;
            }
            sample->setTextAlignment(Qt::AlignCenter);
            sample->setToolTip(tr("第 %1 个组件：%2；拖动调整位置").arg(i + 1).arg(name));
            sample->setSizeHint(QSize(component.type == CourseBarComponentType::Date ? 140 : 240,
                                      qBound(48, config_.height, 64)));
        }
    }
    components_->setVisible(!config_.components.isEmpty());
    components_->setFixedHeight(qBound(62, static_cast<int>(config_.components.size()) * 62, 310));
    emptyLabel_->setVisible(config_.components.isEmpty());
    previewStatus_->setText(!config_.enable ? tr("课程条已关闭，仍可编辑下次启用时的布局。")
        : preview_->count() == 0 ? tr("没有启用的组件，课程条仅显示收起按钮。")
                               : tr("示例内容 · 实际高度 %1 px").arg(config_.height));
    selected_ = config_.components.isEmpty() ? -1 : qBound(0, selected_, static_cast<int>(config_.components.size()) - 1);
    selectComponent(selected_);
    refreshTheme();
}

void CourseBarSettingsTab::setUiAccessSwitching(bool switching)
{
    uiAccessSwitching_ = switching;
    if (!switching) resetPending_ = false;
    refresh();
}

void CourseBarSettingsTab::refreshTheme()
{
    const bool dark = eTheme->getThemeMode() == ElaThemeType::Dark;
    const QString accent = ThemeManager::instance().themeColor().name();
    const QString style = QStringLiteral(
        "QListWidget { background: transparent; color: %1; outline: none; }"
        "QListWidget::item { background: %2; border: 1px solid %3; border-radius: 6px; }"
        "QListWidget::item:selected { color: %1; border: 2px solid %4; }"
        "QListWidget::item:hover { background: %5; }")
        .arg(dark ? "#f0f0f0" : "#202020", dark ? "#343434" : "#f7f7f7",
             dark ? "#555555" : "#dedede", accent, dark ? "#414141" : "#eeeeee");
    preview_->setStyleSheet(style);
    components_->setStyleSheet(style);
}

void CourseBarSettingsTab::selectComponent(int index)
{
    selected_ = index;
    const QSignalBlocker previewBlock(preview_), listBlock(components_);
    components_->setCurrentRow(index);
    preview_->setCurrentRow(-1);
    for (int i = 0; i < preview_->count(); ++i)
        if (preview_->item(i)->data(Qt::UserRole).toInt() == index) {
            preview_->setCurrentRow(i); break;
        }
}

void CourseBarSettingsTab::moveComponent(int from, int to)
{
    if (from < 0 || from >= config_.components.size() || to < 0 || to >= config_.components.size() || from == to) return;
    auto candidate = config_;
    candidate.components.move(from, to);
    save(candidate, to);
}

void CourseBarSettingsTab::save(const CourseBarConfig& candidate, int selection)
{
    Config latest = ConfigManager::instance().config();
    latest.courseBarConfig = candidate;
    QString error;
    if (ConfigManager::instance().commit(latest, &error)) {
        config_ = candidate;
        selected_ = selection;
        emit configChanged();
    } else {
        ProfileEditUi::error(this, tr("课程条配置保存失败：%1").arg(error));
    }
    // Reconstruct after the sender's button/drag handler returns.
    scheduleRefresh();
}

void CourseBarSettingsTab::scheduleRefresh()
{
    if (refreshPending_) return;
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] { refreshPending_ = false; refresh(); });
}
