#include "ui/videosourcepanel.h"
#include "video/videosourcemanager.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"

#include "video/display/waylanddisplaycapture.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStackedLayout>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <algorithm>
#include <QSet>

namespace {
QToolButton *createActionButton(QWidget *parent, const char *name, const QString &text,
                                const char *icon)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setIcon(QIcon(QString(":/assets/icons/%1.svg").arg(icon)));
    button->setFixedSize(28, 28);
    return button;
}

QLabel *createPlainLabel(QWidget *parent, const char *name, const QString &text = {})
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}

QDialog *createSourceDialog(QWidget *parent, const char *name, const QString &title)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(560, 380);
    return dialog;
}

// 保留已断开设备的选择，避免刷新列表时悄悄替换用户保存的设备。
template <typename Device>
void fillDevices(QComboBox *combo, const QList<Device> &entries, const QString &selected)
{
    QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto &entry : entries) {
        combo->addItem(entry.name, entry.id);
    }
    int index = combo->findData(selected);
    if (index < 0 && !selected.isEmpty()) {
        combo->insertItem(0, QObject::tr("所选设备已断开，请重新选择"), selected);
        index = 0;
    }
    combo->setCurrentIndex(index < 0 && combo->count() ? 0 : index);
    combo->setPlaceholderText(QObject::tr("暂无可用设备"));
}

class DisplaySettingsWidget : public QWidget
{
public:
    QCheckBox *captureCursorInput;
    QPushButton *selectButton;

    explicit DisplaySettingsWidget(QWidget *parent) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        selectButton = new QPushButton(tr("选择显示器…"), this);
        selectButton->setObjectName("selectDisplayButton");
        layout->addWidget(selectButton);
        captureCursorInput = new QCheckBox(tr("采集鼠标光标"), this);
        captureCursorInput->setObjectName("captureCursorInput");
        captureCursorInput->setChecked(true);
        layout->addWidget(captureCursorInput);
        layout->addWidget(createPlainLabel(this, "displayCaptureHint",
                                           tr("原始分辨率 · 最高 30 FPS · SDR")));
        layout->addWidget(createPlainLabel(this, "portalCaptureHint",
            tr("由系统选择要共享的显示器；隐藏来源会停止共享，再次显示需重新选择。")));
        const uint modes = WaylandDisplayCapture::cursorModes();
        if ((modes & 3) != 3) {
            const bool embedded = modes & 2;
            captureCursorInput->setChecked(embedded);
            captureCursorInput->setEnabled(false);
            const auto hint = embedded ? tr("系统仅支持显示鼠标。")
                                       : tr("系统不支持将鼠标嵌入画面。");
            captureCursorInput->setToolTip(hint);
            layout->addWidget(createPlainLabel(this, "cursorModeHint", hint));
        }
    }
};

class CameraSettingsWidget : public QWidget
{
public:
    QComboBox *deviceInput, *modeInput = nullptr, *formatInput = nullptr;
    QPushButton *refreshButton;
    QLabel *errorLabel;
    bool modesLoading = false;
    std::function<void()> onSettingsChanged;

    CameraSettingsWidget(QWidget *parent, CameraSourceController *cameras)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        deviceInput = new QComboBox(this);
        deviceInput->setMinimumWidth(0);
        deviceInput->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        deviceInput->setObjectName("cameraDeviceInput");
        row->addWidget(deviceInput, 1);
        refreshButton = new QPushButton(tr("刷新"), this);
        refreshButton->setObjectName("refreshCamerasButton");
        row->addWidget(refreshButton);
        errorLabel = createPlainLabel(this, "deviceCatalogError");
        errorLabel->setProperty("uiError", true);
        layout->addWidget(createPlainLabel(this, "cameraFormatLabel", tr("像素格式")));
        formatInput = new QComboBox(this);
        formatInput->setObjectName("cameraFormatInput");
        formatInput->addItem(tr("自动"), QString());
        layout->addWidget(formatInput);
        layout->addWidget(createPlainLabel(this, "cameraModeLabel", tr("分辨率 × 帧率")));
        modeInput = new QComboBox(this);
        modeInput->setObjectName("cameraModeInput");
        layout->addWidget(modeInput);
        fillDevices(deviceInput, cameras->devices(), {});

        const auto loadModes = [=] {
            const auto id = deviceInput->currentData().toString();
            modesLoading = !id.isEmpty();
            modeInput->setEnabled(!modesLoading);
            formatInput->setEnabled(!modesLoading);
            errorLabel->clear();
            if (onSettingsChanged)
                onSettingsChanged();
            if (modesLoading)
                cameras->requestModes(id);
        };

        connect(cameras, &CameraSourceController::devicesChanged, this, [=] {
            const auto previousDeviceId = deviceInput->currentData().toString();
            fillDevices(deviceInput, cameras->devices(), previousDeviceId);
            refreshButton->setEnabled(!cameras->refreshing());
            errorLabel->setText(cameras->catalogError());
            if (previousDeviceId != deviceInput->currentData().toString())
                loadModes();
            if (onSettingsChanged)
                onSettingsChanged();
        });
        const auto fillModes = [=] {
            const auto previous = modeInput->currentData().value<CameraMode>();
            const QString format = formatInput->currentData().toString();
            QSignalBlocker blocker(modeInput);
            modeInput->clear();
            CameraMode deviceDefault;
            deviceDefault.subtype = format;
            modeInput->addItem(tr("自动选择"), QVariant::fromValue(deviceDefault));
            QList<CameraMode> added;
            int selected = 0;
            for (auto entry : cameras->modes(deviceInput->currentData().toString())) {
                if (!entry.supported || (!format.isEmpty() && entry.subtype != format))
                    continue;
                entry.subtype = format;
                if (std::any_of(added.cbegin(), added.cend(), [&](const auto &m) {
                        return m.sameFormat(entry);
                    }))
                    continue;
                added.append(entry);
                modeInput->addItem(entry.name, QVariant::fromValue(entry));
                if (entry.sameFormat(previous))
                    selected = modeInput->count() - 1;
            }
            modeInput->setCurrentIndex(selected);
            if (onSettingsChanged)
                onSettingsChanged();
        };
        connect(formatInput, &QComboBox::currentIndexChanged, this, fillModes);
        connect(modeInput, &QComboBox::currentIndexChanged, this, [=] {
            if (onSettingsChanged)
                onSettingsChanged();
        });
        connect(cameras, &CameraSourceController::modesChanged, this,
                [=](const QString &deviceId, const QString &message) {
                    if (deviceId != deviceInput->currentData().toString())
                        return;
                    const auto previous = formatInput->currentData().toString();
                    {
                        QSignalBlocker blocker(formatInput);
                        formatInput->clear();
                        formatInput->addItem(tr("自动"), QString());
                        QSet<QString> added;
                        for (const auto &entry : cameras->modes(deviceId)) {
                            if (added.contains(entry.subtype))
                                continue;
                            added.insert(entry.subtype);
                            const auto label =
                                entry.subtype == "mjpeg"     ? QStringLiteral("MJPEG")
                                : entry.subtype == "yuyv422" ? QStringLiteral("YUY2")
                                                             : entry.subtype.toUpper();
                            formatInput->addItem(
                                label + (entry.supported ? QString() : tr("（暂不支持）")),
                                entry.subtype);
                            if (!entry.supported) {
                                const int row = formatInput->count() - 1;
                                auto *model =
                                    qobject_cast<QStandardItemModel *>(formatInput->model());
                                model->item(row)->setEnabled(false);
                                model->item(row)->setToolTip(tr("当前版本不支持此采集格式。"));
                            }
                        }
                        formatInput->setCurrentIndex(qMax(0, formatInput->findData(previous)));
                    }
                    modesLoading = false;
                    modeInput->setEnabled(message.isEmpty());
                    formatInput->setEnabled(message.isEmpty());
                    errorLabel->setText(message);
                    fillModes();
                });
        connect(deviceInput, &QComboBox::activated, this, [=] {
            modeInput->clear();
            formatInput->setCurrentIndex(0);
            loadModes();
        });
        connect(refreshButton, &QPushButton::clicked, this, [=] {
            cameras->refreshDevices();
            loadModes();
        });
        cameras->refreshDevices();
        loadModes();
        layout->addWidget(errorLabel);
        connect(deviceInput, &QComboBox::currentIndexChanged, this, [this] {
            if (onSettingsChanged)
                onSettingsChanged();
        });
    }

};
}

VideoSourcePanel::VideoSourcePanel(VideoSourceManager *manager, QWidget *parent) : QWidget(parent)
{
    setObjectName("sourcesPanel");
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *content = new QWidget(this);
    auto *stack = new QStackedLayout(content);
    stack->setStackingMode(QStackedLayout::StackAll);
    layout->addWidget(content, 1);

    auto *list = new QListWidget(content);
    list->setObjectName("sourceList");
    stack->addWidget(list);

    auto *empty = createPlainLabel(content, "emptySourcesLabel", tr("暂无来源"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    stack->addWidget(empty);
    stack->setCurrentWidget(empty);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    layout->addLayout(buttons);

    auto *add = createActionButton(this, "addSourceButton", tr("添加来源"), "plus");
    buttons->addWidget(add);

    auto *remove = createActionButton(this, "removeSourceButton", tr("移除来源"), "minus");
    buttons->addWidget(remove);

    auto *up = createActionButton(this, "moveSourceUpButton", tr("来源上移"), "arrow-up");
    buttons->addWidget(up);

    auto *down = createActionButton(this, "moveSourceDownButton", tr("来源下移"), "arrow-down");
    buttons->addWidget(down);

    auto *filters =
        createActionButton(this, "sourceFiltersButton", tr("来源滤镜"), "sliders-horizontal");
    buttons->addWidget(filters);
    buttons->addStretch();

    const auto selection = [=] {
        QSignalBlocker block(list);
        int selected = -1;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == manager->currentSourceId())
                selected = row;
        list->setCurrentRow(selected);
        remove->setEnabled(selected >= 0);
        up->setEnabled(manager->canMoveCurrentSourceUp());
        down->setEnabled(manager->canMoveCurrentSourceDown());
        filters->setEnabled(manager->canFilterCurrentSource());
    };

    const auto updateRow = [=](const QString &sourceId) {
        const auto name = manager->sourceName(sourceId);
        if (name.isNull())
            return;
        const auto *config = manager->sourceConfig(sourceId);
        const bool image = config && std::holds_alternative<ImageSourceConfig>(*config);
        QListWidgetItem *item = nullptr;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == sourceId) {
                item = list->item(row);
                break;
            }
        }
        if (!item)
            return;
        auto *widget = list->itemWidget(item);
        const auto runtime = manager->sourceRuntime(sourceId);
        const auto state = image ? VideoSourceState::Idle : runtime.state();
        const QString error = image ? QString() : runtime.error();
        auto *text = widget->findChild<QLabel *>();
        text->setText(name + (state == VideoSourceState::Error ? tr(" · 采集失败") : QString()));
        text->setToolTip(error.isEmpty() ? name + (runtime.cameraStatus().isEmpty()
                                                       ? QString()
                                                       : "\n" + runtime.cameraStatus())
                                         : error);
        item->setData(Qt::AccessibleTextRole, name);
        item->setData(Qt::AccessibleDescriptionRole, error);

        auto *visibility = widget->findChild<QToolButton *>("sourceVisibilityButton");
        visibility->setChecked(manager->sourceVisible(sourceId));
        visibility->setIcon(QIcon(manager->sourceVisible(sourceId) ? ":/assets/icons/eye.svg"
                                                                   : ":/assets/icons/eye-off.svg"));
        auto *retry = widget->findChild<QPushButton *>("retryDisplayButton");
        if (retry)
            retry->setVisible(config && std::holds_alternative<DisplaySourceConfig>(*config) &&
                              state == VideoSourceState::Error);
        auto *lock = widget->findChild<QToolButton *>("sourceLockButton");
        lock->setChecked(manager->sourceLocked(sourceId));
        lock->setIcon(QIcon(manager->sourceLocked(sourceId) ? ":/assets/icons/lock.svg"
                                                            : ":/assets/icons/lock-open.svg"));
    };

    const auto rebuild = [=] {
        QSignalBlocker block(list);
        list->clear();
        for (const auto &source : manager->sources()) {
            const auto id = source.id();
            auto *item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(180, 34));

            auto *widget = new QWidget(list);
            widget->setObjectName("sourceRow_" + id);

            auto *rowLayout = new QHBoxLayout(widget);
            rowLayout->setContentsMargins(8, 0, 0, 0);
            rowLayout->setSpacing(0);

            auto *text = createPlainLabel(widget, "sourceName");
            text->setWordWrap(false);
            text->setMinimumWidth(0);
            text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            text->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(text, 1);

            auto *retry = new QPushButton(tr("重试"), widget);
            retry->setObjectName("retryDisplayButton");
            retry->hide();
            rowLayout->addWidget(retry);
            connect(retry, &QPushButton::clicked, this, [this, id] { emit retryRequested(id); });

            auto *visible =
                createActionButton(widget, "sourceVisibilityButton", tr("显示/隐藏来源"), "eye");
            rowLayout->addWidget(visible);

            auto *locked =
                createActionButton(widget, "sourceLockButton", tr("锁定/解锁来源"), "lock-open");
            rowLayout->addWidget(locked);
            visible->setCheckable(true);
            locked->setCheckable(true);
            connect(visible, &QToolButton::clicked, this, [=](bool value) {
                manager->setSourceVisible(id, value);
            });
            connect(locked, &QToolButton::clicked, this, [=](bool value) {
                manager->setSourceLocked(id, value);
            });
            list->setItemWidget(item, widget);
        }
        empty->setVisible(manager->sources().isEmpty());
        for (const auto &source : manager->sources())
            updateRow(source.id());
        selection();
    };

    connect(manager, &VideoSourceManager::sourcesChanged, this, rebuild);
    connect(manager, &VideoSourceManager::sourceChanged, this, updateRow);
    connect(manager, &VideoSourceManager::sourceRuntimeChanged, this, updateRow);
    connect(manager, &VideoSourceManager::currentSourceIdChanged, this, selection);
    connect(manager, &VideoSourceManager::currentSourceMoveAvailabilityChanged, this, selection);
    connect(list, &QListWidget::currentRowChanged, this, [=](int row) {
        manager->selectSource(row < 0 ? QString() : list->item(row)->data(Qt::UserRole).toString());
    });
    connect(add, &QToolButton::clicked, this, &VideoSourcePanel::addRequested);
    connect(remove, &QToolButton::clicked, this, [=] {
        emit removalRequested(manager->currentSourceId());
    });
    connect(up, &QToolButton::clicked, this, [=] {
        manager->moveSource(manager->currentSourceId(), -1);
    });
    connect(down, &QToolButton::clicked, this, [=] {
        manager->moveSource(manager->currentSourceId(), 1);
    });
    connect(filters, &QToolButton::clicked, this, [=] {
        emit filtersRequested(manager->currentSourceId());
    });
    rebuild();
}

QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove)
{
    auto *dialog = createSourceDialog(parent, "sourceRemovalDialog", QObject::tr("移除来源"));
    dialog->resize(460, 220);

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(createPlainLabel(dialog, "sourceRemovalMessage",
                                       QObject::tr("确定移除来源“%1”吗？").arg(name)));
    layout->addWidget(createPlainLabel(dialog, "sourceRemovalHint", hint));

    auto *error = createPlainLabel(dialog, "sourceRemovalError");
    error->setProperty("uiError", true);
    layout->addWidget(error);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("移除"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("confirmSourceRemovalButton");

    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setObjectName("cancelSourceRemovalButton");
    cancel->setText(QObject::tr("取消"));
    cancel->setDefault(true);
    cancel->setFocus();
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        if (remove())
            dialog->accept();
        else
            error->setText(QObject::tr("无法移除来源：目标来源可能已不存在。"));
    });
    return dialog;
}

QDialog *createSourceAddDialog(QWidget *parent, VideoSourceManager *manager,
                               ImageSourceController *images, DisplaySourceController *displays,
                               CameraSourceController *cameras)
{
    auto *dialog = createSourceDialog(parent, "sourceAddDialog", QObject::tr("添加来源"));
    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *type = new QComboBox(dialog);
    type->setObjectName("sourceTypeInput");
    type->addItem(QObject::tr("显示器采集"), "displayCapture");
    type->addItem(QObject::tr("摄像头"), "camera");
    type->addItem(QObject::tr("图片"), "image");

    auto *name = new QLineEdit(manager->suggestedSourceName(type->currentText()), dialog);
    name->setObjectName("sourceNameInput");
    form->addRow(QObject::tr("来源类型"), type);
    form->addRow(QObject::tr("名称"), name);

    auto *pages = new QStackedWidget(dialog);
    layout->addWidget(pages);

    auto *display = new DisplaySettingsWidget(pages);
    pages->addWidget(display);
    // 切到摄像头页面后才查询设备，避免添加图片时也访问摄像头。
    auto *cameraPage = new QWidget(pages);
    auto *cameraLayout = new QVBoxLayout(cameraPage);
    cameraLayout->setContentsMargins(0, 0, 0, 0);
    pages->addWidget(cameraPage);

    auto *imagePage = new QWidget(pages);
    auto *imageLayout = new QHBoxLayout(imagePage);
    imageLayout->setContentsMargins(0, 0, 0, 0);

    auto *path = new QLineEdit(imagePage);
    path->setObjectName("sourceFileInput");
    path->setPlaceholderText(QObject::tr("本地图片路径"));
    imageLayout->addWidget(path);

    auto *browse = new QPushButton(QObject::tr("浏览…"), imagePage);
    browse->setObjectName("browseSourceImageButton");
    imageLayout->addWidget(browse);
    pages->addWidget(imagePage);

    auto *error = createPlainLabel(dialog, "sourceAddError");
    error->setProperty("uiError", true);
    layout->addWidget(error);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);

    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setObjectName("confirmSourceAddButton");
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("取消"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelSourceAddButton");

    const auto refresh = [=] {
        const bool busy = dialog->property("loading").toBool();

        auto *camera = cameraPage->findChild<QComboBox *>("cameraDeviceInput");
        const bool cameraReady = camera && !camera->currentData().toString().isEmpty() &&
            cameraPage->findChild<QComboBox *>("cameraModeInput")->isEnabled();
        ok->setEnabled(!busy && (type->currentIndex() != 1 || cameraReady));
        ok->setText(busy ? QObject::tr("加载中…") : QObject::tr("确定"));
        type->setEnabled(!busy);
        name->setEnabled(!busy);
        path->setEnabled(!busy);
        browse->setEnabled(!busy);
        display->setEnabled(!busy);
    };

    QObject::connect(display->selectButton, &QPushButton::clicked, ok, &QPushButton::click);
    QObject::connect(name, &QLineEdit::textEdited, dialog, [=] {
        dialog->setProperty("nameEdited", true);
        error->clear();
    });
    QObject::connect(path, &QLineEdit::textEdited, error, &QLabel::clear);
    QObject::connect(type, &QComboBox::currentIndexChanged, dialog, [=](int index) {
        pages->setCurrentIndex(index);
        error->clear();
        if (!dialog->property("nameEdited").toBool())
            name->setText(manager->suggestedSourceName(type->currentText()));
        if (index == 1 && !cameraPage->findChild<QComboBox *>("cameraDeviceInput")) {
            auto *camera = new CameraSettingsWidget(cameraPage, cameras);
            cameraLayout->addWidget(camera);
            camera->onSettingsChanged = refresh;
        }
        refresh();
    });
    QObject::connect(browse, &QPushButton::clicked, dialog, [=] {
        const auto file = images->browseImage(path->text());
        if (!file.isEmpty()) {
            path->setText(file);
            error->clear();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(dialog, &QDialog::finished, images, [=] {
        images->cancelAdd(dialog);
    });
    QObject::connect(images, &ImageSourceController::imageAdded, dialog, [=](QObject *requester) {
        if (requester == dialog)
            dialog->accept();
    });
    QObject::connect(images, &ImageSourceController::addFailed, dialog,
                     [=](QObject *requester, const QString &message) {
                         if (requester != dialog)
                             return;
                         dialog->setProperty("loading", false);
                         error->setText(message);
                         refresh();
                     });
    QObject::connect(dialog, &QDialog::finished, displays, [=] {
        displays->cancelAdd(dialog);
    });
    QObject::connect(displays, &DisplaySourceController::displayAdded, dialog,
                     [=](QObject *requester) {
        if (requester == dialog)
            dialog->accept();
    });
    QObject::connect(displays, &DisplaySourceController::addFailed, dialog,
                     [=](QObject *requester, const QString &message) {
        if (requester != dialog)
            return;
        dialog->setProperty("loading", false);
        error->setText(message);
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        if (!ok->isEnabled())
            return;
        if (type->currentIndex() == 2) {
            dialog->setProperty("loading", true);
            error->clear();
            refresh();
            images->addImage(name->text(), path->text(), dialog);
            return;
        }
        if (type->currentIndex() == 0) {
            dialog->setProperty("loading", true);
            error->clear();
            refresh();
            displays->requestAdd(name->text(), display->captureCursorInput->isChecked(), dialog);
            return;
        }
        const auto id = cameras->addCamera(
                name->text(), cameraPage->findChild<QComboBox *>("cameraModeInput")
                                  ->currentData()
                                  .value<CameraMode>()
                                  .config(cameraPage->findChild<QComboBox *>("cameraDeviceInput")
                                              ->currentData()
                                              .toString()));
        if (!id.isEmpty())
            dialog->accept();
        else
            error->setText(QObject::tr("请输入有效且不重复的名称并选择设备；摄像头不能重复添加。"));
    });
    refresh();
    name->setFocus();
    name->selectAll();
    return dialog;
}

QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId)
{
    auto *dialog = createSourceDialog(parent, "sourceFiltersDialog", QObject::tr("来源滤镜"));
    dialog->setProperty("filterSourceId", sourceId);
    dialog->resize(680, 380);

    auto *layout = new QVBoxLayout(dialog);
    auto *row = new QHBoxLayout;
    layout->addLayout(row, 1);

    auto *left = new QVBoxLayout;
    row->addLayout(left, 1);
    left->addWidget(createPlainLabel(dialog, "filterHint", QObject::tr("滤镜 · 从上到下处理")));

    auto *list = new QListWidget(dialog);
    list->setObjectName("filterList");
    left->addWidget(list);

    auto *actions = new QHBoxLayout;
    left->addLayout(actions);

    auto *add = createActionButton(dialog, "addFilterButton", QObject::tr("添加色彩校正"), "plus");
    actions->addWidget(add);

    auto *remove =
        createActionButton(dialog, "removeFilterButton", QObject::tr("删除滤镜"), "minus");
    actions->addWidget(remove);

    auto *up = createActionButton(dialog, "upFilterButton", QObject::tr("滤镜上移"), "arrow-up");
    actions->addWidget(up);

    auto *down =
        createActionButton(dialog, "downFilterButton", QObject::tr("滤镜下移"), "arrow-down");
    actions->addWidget(down);

    auto *form = new QFormLayout;
    row->addLayout(form, 1);
    form->addRow(
        createPlainLabel(dialog, "filterApplyHint", QObject::tr("修改立即生效，关闭后保留。")));
    QList<QDoubleSpinBox *> inputs;
    QList<QSlider *> sliders;
    for (const auto &key : {"brightness", "contrast", "saturation"}) {
        auto *input = new QDoubleSpinBox(dialog);
        input->setObjectName(QString(key) + "Input");
        input->setDecimals(2);
        input->setSingleStep(.01);
        input->setRange(QString(key) == "brightness" ? -1 : 0,
                        QString(key) == "brightness" ? 1 : 2);

        auto *slider = new QSlider(Qt::Horizontal, dialog);
        slider->setObjectName(QString(key) + "Slider");
        slider->setRange(int(input->minimum() * 100), int(input->maximum() * 100));

        auto *pair = new QHBoxLayout;
        pair->addWidget(slider, 1);
        pair->addWidget(input);
        form->addRow(QString(key) == "brightness" ? QObject::tr("亮度")
                     : QString(key) == "contrast" ? QObject::tr("对比度")
                                                  : QObject::tr("饱和度"),
                     pair);
        inputs.append(input);
        sliders.append(slider);
    }

    auto *reset = new QPushButton(QObject::tr("恢复默认"), dialog);
    reset->setObjectName("resetFilterButton");
    form->addRow(reset);

    const auto updateSelection = [=] {
        const auto filters = manager->sourceFilters(sourceId);
        const int index = list->currentRow();

        const VideoSourceFilter *filter =
            index >= 0 && index < filters.size() ? &filters[index] : nullptr;
        remove->setEnabled(filter != nullptr);
        const auto *correction =
            filter ? std::get_if<ColorCorrectionConfig>(&filter->config()) : nullptr;
        reset->setEnabled(correction != nullptr);
        up->setEnabled(manager->canMoveFilterUp(sourceId, manager->currentFilterId(sourceId)));
        down->setEnabled(manager->canMoveFilterDown(sourceId, manager->currentFilterId(sourceId)));

        std::array<double, 3> values{0, 1, 1};
        if (correction)
            values = {correction->brightness(), correction->contrast(), correction->saturation()};
        for (int n = 0; n < inputs.size(); ++n) {
            QSignalBlocker inputBlocker(inputs[n]);
            QSignalBlocker sliderBlocker(sliders[n]);
            inputs[n]->setValue(values[n]);
            sliders[n]->setValue(qRound(values[n] * 100));
            inputs[n]->setEnabled(correction != nullptr);
            sliders[n]->setEnabled(correction != nullptr);
        }
    };

    const auto syncSelection = [=] {
        QSignalBlocker blocker(list);
        int selectedRow = -1;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() ==
                manager->currentFilterId(sourceId)) {
                selectedRow = row;
                break;
            }
        }
        list->setCurrentRow(selectedRow);
        updateSelection();
    };

    const auto refresh = [=] {
        if (!manager->canFilterSource(sourceId)) {
            dialog->close();
            return;
        }

        QSignalBlocker blocker(list);
        list->clear();
        int index = 0;
        for (const auto &entry : manager->sourceFilters(sourceId)) {
            QString name;
            switch (entry.type()) {
            case VideoSourceFilterType::ColorCorrection:
                name = QObject::tr("色彩校正");
                break;
            }
            auto *item = new QListWidgetItem(QString("%1 %2").arg(name).arg(++index), list);
            item->setData(Qt::UserRole, entry.id());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(entry.enabled() ? Qt::Checked : Qt::Unchecked);
        }
        syncSelection();
    };

    const auto updateFilter = [=](const QString &filterId,
                                  const std::function<void(VideoSourceFilter &)> &update) {
        auto filters = manager->sourceFilters(sourceId);
        for (auto &filter : filters) {
            if (filter.id() == filterId) {
                update(filter);
                manager->setSourceFilters(sourceId, filters);
                return;
            }
        }
    };
    const auto applyConfig = [=] {
        if (!manager->currentFilterId(sourceId).isEmpty())
            updateFilter(manager->currentFilterId(sourceId), [=](VideoSourceFilter &filter) {
                filter.setConfig(ColorCorrectionConfig{float(inputs[0]->value()),
                                                       float(inputs[1]->value()),
                                                       float(inputs[2]->value())});
            });
    };
    for (int n = 0; n < inputs.size(); ++n) {
        QObject::connect(inputs[n], &QDoubleSpinBox::valueChanged, dialog, applyConfig);
        QObject::connect(sliders[n], &QSlider::valueChanged, dialog, [=](int value) {
            inputs[n]->setValue(value / 100.);
        });
    }
    QObject::connect(manager, &VideoSourceManager::sourceFiltersChanged, dialog,
                     [=](const QString &source) {
                         if (source == sourceId)
                             refresh();
                     });
    QObject::connect(manager, &VideoSourceManager::sourcesChanged, dialog, refresh);
    QObject::connect(manager, &VideoSourceManager::currentFilterIdChanged, dialog,
                     [=](const QString &source) {
                         if (source == sourceId)
                             syncSelection();
                     });
    QObject::connect(list, &QListWidget::currentRowChanged, dialog, [=] {
        const auto *item = list->currentItem();
        manager->selectFilter(sourceId, item ? item->data(Qt::UserRole).toString() : QString());
        updateSelection();
    });
    QObject::connect(list, &QListWidget::itemChanged, dialog, [=](QListWidgetItem *item) {
        updateFilter(item->data(Qt::UserRole).toString(), [=](VideoSourceFilter &filter) {
            filter.setEnabled(item->checkState() == Qt::Checked);
        });
    });
    QObject::connect(add, &QToolButton::clicked, dialog, [=] {
        const auto added = manager->addFilter(sourceId, VideoSourceFilterType::ColorCorrection);
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->data(Qt::UserRole).toString() == added)
                list->setCurrentRow(n);
    });
    QObject::connect(remove, &QToolButton::clicked, dialog, [=] {
        manager->removeFilter(sourceId, manager->currentFilterId(sourceId));
    });
    QObject::connect(up, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, manager->currentFilterId(sourceId), -1);
    });
    QObject::connect(down, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, manager->currentFilterId(sourceId), 1);
    });
    QObject::connect(reset, &QPushButton::clicked, dialog, [=] {
        updateFilter(manager->currentFilterId(sourceId), [](VideoSourceFilter &filter) {
            filter.setConfig(ColorCorrectionConfig{});
        });
    });

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    close->button(QDialogButtonBox::Close)->setObjectName("closeFiltersButton");
    close->button(QDialogButtonBox::Close)->setText(QObject::tr("关闭"));
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    refresh();
    return dialog;
}
