#include "ReminderSettingsTab.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Core/ProfileManager/ProfileManager.h"
#include "../../Ui/UiWidgets.h"

#include <ElaCheckBox.h>
#include <ElaComboBox.h>
#include <ElaDialog.h>
#include <ElaFlowLayout.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaSpinBox.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QFormLayout>
#include <QHeaderView>
#include <QPainter>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const QStringList dayNames{QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
    QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"), QStringLiteral("周日")};
bool hasSubject(const QString& name)
{
    const auto& subjects = ProfileManager::instance().profile().subjects;
    return std::any_of(subjects.cbegin(), subjects.cend(), [&](const Subject& s) { return s.name == name; });
}
QString summary(const ReminderRule& rule)
{
    QStringList days;
    for (int d : rule.weekdays) days.append(dayNames[d - 1]);
    const QString course = rule.course == ReminderRule::Course::Any ? QStringLiteral("不限课程") :
        rule.course == ReminderRule::Course::Subject ? rule.subject : QStringLiteral("第 %1 节").arg(rule.period);
    const QString time = rule.timing == ReminderRule::Timing::Fixed ? rule.time.toString(QStringLiteral("HH:mm:ss")) :
        (rule.timing == ReminderRule::Timing::Start ? QStringLiteral("上课") : QStringLiteral("下课")) +
        (rule.offsetMinutes == 0 ? QStringLiteral("时") : QStringLiteral("%1 %2 分钟").arg(rule.offsetMinutes < 0 ? QStringLiteral("前") : QStringLiteral("后")).arg(qAbs(rule.offsetMinutes)));
    return days.join(QLatin1Char(' ')) + QStringLiteral(" · ") + course + QStringLiteral(" · ") + time;
}
class ElidingDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem display(option);
        initStyleOption(&display, index);
        display.features.setFlag(QStyleOptionViewItem::WrapText, false);
        display.text = display.fontMetrics.elidedText(display.text, Qt::ElideRight, qMax(0, display.rect.width() - 24));
        painter->save(); painter->setClipRect(display.rect, Qt::IntersectClip);
        display.widget->style()->drawControl(QStyle::CE_ItemViewItem, &display, painter, display.widget);
        painter->restore();
    }
};

class RuleEditor final : public ElaDialog
{
public:
    RuleEditor(const ReminderRule& rule, QWidget* parent) : ElaDialog(parent->window()), original_(rule)
    {
        using namespace AppUi;
        setObjectName(QStringLiteral("reminderRuleEditor"));
        setWindowTitle(QStringLiteral("编辑提醒规则"));
        setIsDefaultClosed(false);
        setIsStayTop(false);
        setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
        connect(this, &ElaDialog::closeButtonClicked, this, &QDialog::reject);
        const auto available = screen()->availableGeometry();
        resize(qMin(620, available.width() - 40), qMin(700, available.height() - 60));
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(20, 42, 20, 16);
        auto* scroll = new ElaScrollArea(this); scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
        scroll->viewport()->setAutoFillBackground(false);
        auto* body = new QWidget(scroll);
        auto* layout = new QVBoxLayout(body); layout->setSpacing(12);
        auto* form = new QFormLayout; form->setSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        name_ = new ElaLineEdit(body); name_->setText(rule.name); name_->setObjectName(QStringLiteral("reminderRuleName"));
        form->addRow(text(QStringLiteral("名称"), body), name_);
        enabled_ = new ElaToggleSwitch(body); enabled_->setIsToggled(rule.enabled);
        form->addRow(text(QStringLiteral("启用"), body), enabled_);
        auto* days = new QWidget(body); auto* dayLayout = new ElaFlowLayout(days, 0, 6, 6); dayLayout->setIsAnimation(false);
        for (int i = 0; i < 7; ++i) {
            days_[i] = new ElaCheckBox(dayNames[i], days); days_[i]->setChecked(rule.weekdays.contains(i + 1));
            dayLayout->addWidget(days_[i]);
        }
        form->addRow(text(QStringLiteral("来源星期"), body), days);
        course_ = new ElaComboBox(body); course_->addItems({QStringLiteral("不限课程"), QStringLiteral("指定科目"), QStringLiteral("当天第 N 节课")});
        course_->setCurrentIndex(int(rule.course));
        form->addRow(text(QStringLiteral("课程条件"), body), course_);
        subject_ = new ElaComboBox(body);
        for (const auto& s : ProfileManager::instance().profile().subjects) subject_->addItem(s.name, s.name);
        int selected = subject_->findData(rule.subject);
        if (selected < 0 && !rule.subject.isEmpty()) { subject_->addItem(rule.subject + QStringLiteral("（科目不存在）"), rule.subject); selected = subject_->count() - 1; }
        if (selected >= 0) subject_->setCurrentIndex(selected);
        form->addRow(text(QStringLiteral("科目"), body), subject_);
        period_ = new ElaSpinBox(body); period_->setRange(1, 999); period_->setValue(rule.period); period_->setSuffix(QStringLiteral(" 节"));
        form->addRow(text(QStringLiteral("节次"), body), period_);
        timing_ = new ElaComboBox(body); timing_->addItems({QStringLiteral("固定时刻"), QStringLiteral("相对上课"), QStringLiteral("相对下课")}); timing_->setCurrentIndex(int(rule.timing));
        form->addRow(text(QStringLiteral("时间类型"), body), timing_);
        time_ = new TimePicker(rule.time, body); form->addRow(text(QStringLiteral("固定时刻"), body), time_);
        auto* offset = new QWidget(body); auto* offsetLayout = new QHBoxLayout(offset); offsetLayout->setContentsMargins(0, 0, 0, 0);
        direction_ = new ElaComboBox(offset); direction_->addItems({QStringLiteral("准点"), QStringLiteral("提前"), QStringLiteral("延后")});
        direction_->setCurrentIndex(rule.offsetMinutes == 0 ? 0 : rule.offsetMinutes < 0 ? 1 : 2);
        minutes_ = new ElaSpinBox(offset); minutes_->setRange(1, 1439); minutes_->setValue(qMax(1, qAbs(rule.offsetMinutes))); minutes_->setSuffix(QStringLiteral(" 分钟"));
        offsetLayout->addWidget(direction_); offsetLayout->addWidget(minutes_);
        form->addRow(text(QStringLiteral("相对时间"), body), offset);
        layout->addLayout(form);
        layout->addWidget(text(QStringLiteral("提醒文本"), body));
        text_ = new ElaLineEdit(body); text_->setText(rule.text); text_->setObjectName(QStringLiteral("reminderRuleText")); layout->addWidget(text_);
        hint_ = text({}, body, 12); layout->addWidget(hint_);
        example_ = text({}, body, 12); example_->setObjectName(QStringLiteral("reminderRuleExample")); layout->addWidget(example_);
        layout->addWidget(text(QStringLiteral("星期按课表来源判断；节次按实际开始时间排序。固定时刻每天最多提醒一次；相对时间逐节匹配，跨日结果跳过。"), body, 12));
        layout->addStretch(); scroll->setWidget(body); body->setAutoFillBackground(false);
        root->addWidget(scroll, 1); enableTouchScrolling(scroll);
        error_ = text({}, this, 12); error_->setObjectName(QStringLiteral("reminderRuleError")); error_->hide(); root->addWidget(error_);
        auto* actions = new QHBoxLayout;
        auto* test = button(QStringLiteral("测试展示"), this);
        auto* cancel = button(QStringLiteral("取消"), this);
        auto* save = button(QStringLiteral("保存"), this, true); save->setObjectName(QStringLiteral("saveReminderRule"));
        actions->addWidget(test); actions->addStretch(); actions->addWidget(cancel); actions->addWidget(save); root->addLayout(actions);
        connect(cancel, &ElaPushButton::clicked, this, &QDialog::reject);
        connect(save, &ElaPushButton::clicked, this, [this] {
            QString error;
            const auto rule = value();
            if (Reminders::validate(QList<ReminderRule>{rule}, &error) && submit) error = submit(rule);
            if (!error.isEmpty()) { error_->setText(error); error_->show(); return; }
            accept();
        });
        connect(test, &ElaPushButton::clicked, this, [this] {
            const auto rule = value(); QString error;
            if (!Reminders::validate(QList<ReminderRule>{rule}, &error)) { error_->setText(error); error_->show(); return; }
            if (preview) preview(exampleText());
        });
        auto update = [this] {
            subject_->setEnabled(course_->currentIndex() == 1); period_->setEnabled(course_->currentIndex() == 2);
            const bool fixed = timing_->currentIndex() == 0;
            time_->setEnabled(fixed); direction_->setEnabled(!fixed); minutes_->setEnabled(!fixed && direction_->currentIndex() != 0);
            hint_->setText(fixed ? QStringLiteral("固定时刻请填写固定文本，不支持课程占位符。") : QStringLiteral("可用占位符：{科目}、{节次}；以下为示例。"));
            example_->setText(QStringLiteral("示例：") + exampleText());
        };
        connect(course_, &ElaComboBox::currentIndexChanged, this, update);
        connect(subject_, &ElaComboBox::currentIndexChanged, this, update);
        connect(period_, &ElaSpinBox::valueChanged, this, update);
        connect(timing_, &ElaComboBox::currentIndexChanged, this, update);
        connect(direction_, &ElaComboBox::currentIndexChanged, this, update);
        connect(text_, &ElaLineEdit::textChanged, this, update);
        update();
    }
    std::function<QString(const ReminderRule&)> submit;
    std::function<void(const QString&)> preview;
private:
    ReminderRule value() const
    {
        auto r = original_; r.name = name_->text().trimmed(); r.text = text_->text(); r.enabled = enabled_->getIsToggled();
        r.weekdays.clear(); for (int i = 0; i < 7; ++i) if (days_[i]->isChecked()) r.weekdays.append(i + 1);
        r.course = ReminderRule::Course(course_->currentIndex()); r.subject = subject_->currentData().toString(); r.period = period_->value();
        r.timing = ReminderRule::Timing(timing_->currentIndex()); r.time = time_->time();
        r.offsetMinutes = direction_->currentIndex() == 0 ? 0 : minutes_->value() * (direction_->currentIndex() == 1 ? -1 : 1);
        return r;
    }
    QString exampleText() const
    {
        const auto r = value();
        return r.timing == ReminderRule::Timing::Fixed ? r.text : Reminders::render(r.text,
            r.course == ReminderRule::Course::Subject ? r.subject : QStringLiteral("数学"), r.course == ReminderRule::Course::Period ? r.period : 1);
    }
    ReminderRule original_;
    ElaLineEdit *name_, *text_;
    ElaToggleSwitch* enabled_;
    std::array<ElaCheckBox*, 7> days_{};
    ElaComboBox *course_, *subject_, *timing_, *direction_;
    ElaSpinBox *period_, *minutes_;
    AppUi::TimePicker* time_;
    ElaText *hint_, *example_, *error_;
};
}

ReminderSettingsTab::ReminderSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("reminderSettingsTab"));
    auto* layout = page(this, tr("提醒"), 24);
    auto* basics = section(layout, tr("通用提醒"));
    auto* top = new QHBoxLayout;
    top->addWidget(text(tr("启用提醒"), this), 1); enable_ = new ElaToggleSwitch(this); enable_->setObjectName(QStringLiteral("reminderEnable")); top->addWidget(enable_); basics->addLayout(top);
    basics->addWidget(text(tr("通过课程条展示文字提醒，收起时会自动展开；关闭课程条后不展示。关闭本页后仍会提醒。"), this, 12));
    const QStringList titles{tr("上课提醒"), tr("下课提醒"), tr("课前提醒")};
    const ReminderConfig defaults;
    const std::array<QString ReminderConfig::*, 3> fields{&ReminderConfig::startedText, &ReminderConfig::endedText, &ReminderConfig::soonText};
    const std::array<bool ReminderConfig::*, 3> toggles{&ReminderConfig::classStarted, &ReminderConfig::classEnded, &ReminderConfig::startingSoon};
    for (int i = 0; i < 3; ++i) {
        auto* body = section(layout, titles[i]);
        auto* row = new QHBoxLayout; row->addWidget(text(tr("启用"), this), 1);
        switches_[i] = new ElaToggleSwitch(this); switches_[i]->setObjectName(QStringLiteral("reminderType%1").arg(i)); row->addWidget(switches_[i]); body->addLayout(row);
        if (i == 2) {
            auto* advanceRow = new QHBoxLayout; advanceRow->addWidget(text(tr("提前时间"), this), 1);
            advance_ = new ElaSpinBox(this); advance_->setRange(1, 1439); advance_->setSuffix(tr(" 分钟")); advance_->setKeyboardTracking(false); advance_->setFixedWidth(150);
            advance_->setObjectName(QStringLiteral("reminderAdvanceMinutes")); advanceRow->addWidget(advance_); body->addLayout(advanceRow);
        }
        texts_[i] = new ElaLineEdit(this); texts_[i]->setObjectName(QStringLiteral("reminderText%1").arg(i)); texts_[i]->setAccessibleName(titles[i] + tr("文本")); body->addWidget(texts_[i]);
        body->addWidget(text(tr("可用占位符：{科目}、{节次}；也可填写固定文字。结束编辑后保存。"), this, 12));
        examples_[i] = text({}, this, 12); body->addWidget(examples_[i]);
        auto* actions = new QHBoxLayout;
        auto* reset = button(tr("恢复默认文本"), this); auto* test = button(tr("测试展示"), this);
        actions->addWidget(reset); actions->addWidget(test); actions->addStretch(); body->addLayout(actions);
        connect(switches_[i], &ElaToggleSwitch::toggled, this, [this, i, toggles](bool checked) {
            if (!refreshing_ && !saveConfig([&](ReminderConfig& c) { c.*toggles[i] = checked; })) refreshConfig();
        });
        connect(texts_[i], &ElaLineEdit::textEdited, this, [this, i] { pending_[i] = true; });
        connect(texts_[i], &ElaLineEdit::textChanged, this, [this, i](const QString& value) { examples_[i]->setText(tr("示例：%1").arg(Reminders::render(value, tr("数学"), 1))); });
        const auto saveText = [this, i, fields] {
            if (refreshing_ || !pending_[i]) return;
            const QString value = texts_[i]->text();
            if (saveConfig([&](ReminderConfig& c) { c.*fields[i] = value; })) pending_[i] = false;
        };
        connect(texts_[i], &ElaLineEdit::editingFinished, this, saveText);
        connect(reset, &ElaPushButton::clicked, this, [this, i, fields, defaults, saveText] { pending_[i] = true; texts_[i]->setText(defaults.*fields[i]); saveText(); });
        connect(test, &ElaPushButton::clicked, this, [this, i] { preview(Reminders::render(texts_[i]->text(), tr("数学"), 1)); });
    }
    auto* custom = section(layout, tr("自定义提醒"));
    profile_ = text({}, this, 12); custom->addWidget(profile_);
    custom->addWidget(text(tr("星期按课表来源判断，例如周六使用周一课表时匹配周一规则。自定义提醒与通用提醒分别生效，依次展示。"), this, 12));
    auto* actions = new ElaFlowLayout(0, 6, 6); actions->setIsAnimation(false);
    auto* add = button(tr("新增"), this, true); add->setObjectName(QStringLiteral("addReminderRule"));
    edit_ = button(tr("编辑"), this); remove_ = button(tr("删除"), this); toggle_ = button(tr("启用／停用"), this); test_ = button(tr("测试展示"), this);
    for (auto* b : {add, edit_, remove_, toggle_, test_}) actions->addWidget(b);
    custom->addLayout(actions);
    table_ = table(custom, {tr("名称"), tr("触发条件"), tr("状态")}, model_); table_->setObjectName(QStringLiteral("reminderRuleList"));
    table_->setItemDelegate(new ElidingDelegate(table_));
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed); table_->horizontalHeader()->resizeSection(2, 128);
    empty_ = text(tr("暂无自定义提醒，点击“新增”创建。"), this); custom->addWidget(empty_); layout->addStretch();
    connect(enable_, &ElaToggleSwitch::toggled, this, [this](bool checked) {
        if (!refreshing_ && !saveConfig([&](ReminderConfig& c) { c.enable = checked; })) refreshConfig();
    });
    connect(advance_, &ElaSpinBox::valueChanged, this, [this](int value) {
        if (!refreshing_ && !saveConfig([&](ReminderConfig& c) { c.advanceMinutes = value; })) refreshConfig();
    });
    connect(&ConfigManager::instance(), &ConfigManager::committed, this, &ReminderSettingsTab::refreshConfig);
    connect(&ProfileManager::instance(), &ProfileManager::committed, this, &ReminderSettingsTab::refreshRules);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ReminderSettingsTab::updateActions);
    connect(add, &ElaPushButton::clicked, this, [this] { editRule(true); });
    connect(edit_, &ElaPushButton::clicked, this, [this] { editRule(false); });
    connect(table_, &ElaTableView::doubleClicked, this, [this] { editRule(false); });
    connect(remove_, &ElaPushButton::clicked, this, [this] {
        const int index = AppUi::selected(table_); const auto rules = ProfileManager::instance().profile().reminderRules;
        if (index < 0 || index >= rules.size()) return;
        const auto id = rules[index].id;
        if (!AppUi::confirm(this, tr("删除提醒"), tr("确定删除“%1”？").arg(rules[index].name))) return;
        auto latest = ProfileManager::instance().profile().reminderRules;
        latest.erase(std::remove_if(latest.begin(), latest.end(), [&](const auto& r) { return r.id == id; }), latest.end());
        saveRules(latest);
    });
    connect(toggle_, &ElaPushButton::clicked, this, [this] {
        auto rules = ProfileManager::instance().profile().reminderRules; const int index = AppUi::selected(table_);
        if (index < 0 || index >= rules.size()) return;
        rules[index].enabled = !rules[index].enabled; saveRules(rules);
    });
    connect(test_, &ElaPushButton::clicked, this, [this] {
        const auto& rules = ProfileManager::instance().profile().reminderRules; const int index = AppUi::selected(table_);
        if (index < 0 || index >= rules.size()) return;
        const auto& r = rules[index]; preview(r.timing == ReminderRule::Timing::Fixed ? r.text : Reminders::render(r.text,
            r.course == ReminderRule::Course::Subject ? r.subject : tr("数学"), r.course == ReminderRule::Course::Period ? r.period : 1));
    });
    refreshConfig(); refreshRules();
}

void ReminderSettingsTab::refreshConfig()
{
    QScopedValueRollback<bool> guard(refreshing_, true);
    const auto& c = ConfigManager::instance().config().reminderConfig;
    enable_->setIsToggled(c.enable); advance_->setValue(c.advanceMinutes);
    const std::array<bool, 3> enabled{c.classStarted, c.classEnded, c.startingSoon};
    const std::array<QString, 3> values{c.startedText, c.endedText, c.soonText};
    for (int i = 0; i < 3; ++i) { switches_[i]->setIsToggled(enabled[i]); if (!pending_[i]) texts_[i]->setText(values[i]); }
}

bool ReminderSettingsTab::saveConfig(const std::function<void(ReminderConfig&)>& update)
{
    auto candidate = ConfigManager::instance().config(); update(candidate.reminderConfig);
    QString error;
    if (ConfigManager::instance().commit(candidate, &error)) return true;
    AppUi::error(this, tr("提醒设置保存失败，原设置继续生效：%1").arg(error)); return false;
}

void ReminderSettingsTab::refreshRules()
{
    const int previous = AppUi::selected(table_);
    model_->removeRows(0, model_->rowCount());
    const auto& profile = ProfileManager::instance().profile(); profile_->setText(tr("当前档案：%1").arg(profile.name));
    for (qsizetype i = 0; i < profile.reminderRules.size(); ++i) {
        const auto& rule = profile.reminderRules[i];
        const bool missing = rule.course == ReminderRule::Course::Subject && !hasSubject(rule.subject);
        AppUi::appendRow(model_, {rule.name, summary(rule), missing ? tr("科目不存在") : rule.enabled ? tr("已启用") : tr("已停用")}, int(i));
    }
    AppUi::select(table_, previous); AppUi::emptyState(table_, empty_); updateActions();
}

void ReminderSettingsTab::updateActions()
{
    const int i = AppUi::selected(table_); const auto count = ProfileManager::instance().profile().reminderRules.size();
    for (auto* b : {edit_, remove_, toggle_, test_}) b->setEnabled(i >= 0 && i < count);
}

bool ReminderSettingsTab::saveRules(const QList<ReminderRule>& rules)
{
    auto candidate = ProfileManager::instance().profile(); candidate.reminderRules = rules;
    QString error;
    if (ProfileManager::instance().commit(candidate, &error)) return true;
    AppUi::error(this, tr("提醒规则保存失败：%1").arg(error)); return false;
}

void ReminderSettingsTab::editRule(bool add)
{
    const auto& rules = ProfileManager::instance().profile().reminderRules;
    const int index = AppUi::selected(table_);
    if (!add && (index < 0 || index >= rules.size())) return;
    const ReminderRule original = add ? ReminderRule{} : rules[index];
    RuleEditor dialog(original, this);
    const auto path = ProfileManager::instance().filePath();
    dialog.preview = [this](const QString& text) { preview(text); };
    dialog.submit = [this, original, add, path](const ReminderRule& value) -> QString {
        auto& manager = ProfileManager::instance();
        if (manager.filePath() != path) return tr("当前档案已切换，请重新打开编辑器。");
        auto candidate = manager.profile();
        auto it = std::find_if(candidate.reminderRules.begin(), candidate.reminderRules.end(), [&](const auto& r) { return r.id == original.id; });
        if (!add && (it == candidate.reminderRules.end() || Reminders::toJson(QList<ReminderRule>{*it}) != Reminders::toJson(QList<ReminderRule>{original})))
            return tr("该规则已变化，请重新打开编辑器。");
        if (add) candidate.reminderRules.append(value); else *it = value;
        QString error; if (!manager.commit(candidate, &error)) return tr("保存失败：%1").arg(error);
        return {};
    };
    dialog.exec();
}

void ReminderSettingsTab::preview(const QString& value)
{
    if (value.trimmed().isEmpty()) { AppUi::error(this, tr("提醒文本不能为空")); return; }
    if (!ConfigManager::instance().config().courseBarConfig.enable) { AppUi::error(this, tr("请先启用课程条，再测试展示。")); return; }
    emit previewRequested(value);
}
