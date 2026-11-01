#include "ui/videosourcepanel.h"
#include "video/videosourcemanager.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"

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
#include <QToolButton>
#include <QVBoxLayout>
#include <array>

namespace {
QToolButton *action(QWidget *parent, const char *name, const QString &text, const char *icon)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setIcon(QIcon(QString(":/assets/icons/%1.svg").arg(icon)));
    button->setFixedSize(28, 28);
    return button;
}

QLabel *label(QWidget *parent, const char *name, const QString &text = {})
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}

QDialog *dialogShell(QWidget *parent, const char *name, const QString &title)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(name);
    dialog->setWindowTitle(title);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(560, 380);
    return dialog;
}

// Preserve a disconnected selection so merely refreshing never changes the saved device.
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

class CaptureSettings : public QWidget
{
public:
    QComboBox *device, *mode = nullptr;
    QCheckBox *cursor = nullptr;
    QPushButton *refresh;
    QLabel *error;
    bool loading = false;
    CameraMode originalMode;
    std::function<void()> changed;

    CaptureSettings(QWidget *parent, DisplaySourceController *displays,
                    CameraSourceController *cameras, bool camera,
                    const VideoSource *initial = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *row = new QHBoxLayout;
        layout->addLayout(row);
        device = new QComboBox(this);
        device->setMinimumWidth(0);
        device->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        device->setObjectName(camera ? "cameraDeviceInput" : "displayTargetInput");
        row->addWidget(device, 1);
        refresh = new QPushButton(tr("刷新"), this);
        refresh->setObjectName(camera ? "refreshCamerasButton" : "refreshDisplaysButton");
        row->addWidget(refresh);
        error = label(this, "deviceCatalogError");
        error->setProperty("uiError", true);
        if (!camera) {
            cursor = new QCheckBox(tr("采集鼠标光标"), this);
            cursor->setObjectName("captureCursorInput");
            const DisplaySourceParams params =
                initial ? std::get<DisplaySourceParams>(initial->params) : DisplaySourceParams{};
            cursor->setChecked(params.captureCursor);
            layout->addWidget(cursor);
            layout->addWidget(
                label(this, "displayCaptureHint", tr("原始分辨率 · 最高 30 FPS · SDR")));
            fillDevices(device, displays->displayDevices(), params.displayId);
            connect(displays, &DisplaySourceController::displayDevicesChanged, this, [=] {
                fillDevices(device, displays->displayDevices(), device->currentData().toString());
                if (changed)
                    changed();
            });
            connect(refresh, &QPushButton::clicked, displays,
                    &DisplaySourceController::refreshDisplayDevices);
            displays->refreshDisplayDevices();
        } else {
            const CameraSourceParams params =
                initial ? std::get<CameraSourceParams>(initial->params) : CameraSourceParams{};
            originalMode = {params.resolution.width(), params.resolution.height(),
                            params.frameRateNumerator, params.frameRateDenominator};
            mode = new QComboBox(this);
            mode->setObjectName("cameraModeInput");
            layout->addWidget(mode);
            fillDevices(device, cameras->devices(), params.deviceId);

            const auto loadModes = [=] {
                const auto id = device->currentData().toString();
                loading = !id.isEmpty();
                mode->setEnabled(!loading);
                error->clear();
                if (changed)
                    changed();
                if (loading)
                    cameras->requestModes(id);
            };

            connect(cameras, &CameraSourceController::devicesChanged, this, [=] {
                const auto previous = device->currentData().toString();
                fillDevices(device, cameras->devices(), previous);
                refresh->setEnabled(!cameras->refreshing());
                error->setText(cameras->catalogError());
                if (previous != device->currentData().toString())
                    loadModes();
                if (changed)
                    changed();
            });
            connect(cameras, &CameraSourceController::modesChanged, this,
                    [=](const QString &sourceId, const QString &message) {
                        if (sourceId != device->currentData().toString())
                            return;

                        auto selected = mode->currentData().value<CameraMode>();
                        if (!selected.isValid())
                            selected = originalMode;
                        QSignalBlocker blocker(mode);
                        mode->clear();
                        mode->addItem(tr("设备默认"), QVariant::fromValue(CameraMode{}));
                        int current = 0;
                        for (const auto &entry : cameras->modes(sourceId)) {
                            mode->addItem(entry.name, QVariant::fromValue(entry));
                            if (entry.sameFormat(selected))
                                current = mode->count() - 1;
                        }
                        if (!current && selected.isValid()) {
                            mode->addItem(tr("原采集模式（当前未列出）"),
                                          QVariant::fromValue(selected));
                            current = mode->count() - 1;
                        }
                        mode->setCurrentIndex(current);
                        loading = false;
                        mode->setEnabled(true);
                        error->setText(message);
                        if (changed)
                            changed();
                    });
            connect(device, &QComboBox::activated, this, [=] {
                originalMode = {};
                mode->clear();
                loadModes();
            });
            connect(refresh, &QPushButton::clicked, this, [=] {
                cameras->refreshDevices();
                loadModes();
            });
            cameras->refreshDevices();
            loadModes();
        }
        layout->addWidget(error);
        connect(device, &QComboBox::currentIndexChanged, this, [this] {
            if (changed)
                changed();
        });
    }

    bool ready() const
    {
        return !device->currentData().toString().isEmpty() && !loading;
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

    auto *empty = label(content, "emptySourcesLabel", tr("暂无来源"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    stack->addWidget(empty);
    stack->setCurrentWidget(empty);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    layout->addLayout(buttons);

    auto *add = action(this, "addSourceButton", tr("添加来源"), "plus");
    buttons->addWidget(add);

    auto *remove = action(this, "removeSourceButton", tr("移除来源"), "minus");
    buttons->addWidget(remove);

    auto *up = action(this, "moveSourceUpButton", tr("来源上移"), "arrow-up");
    buttons->addWidget(up);

    auto *down = action(this, "moveSourceDownButton", tr("来源下移"), "arrow-down");
    buttons->addWidget(down);

    auto *filters = action(this, "sourceFiltersButton", tr("来源滤镜"), "sliders-horizontal");
    buttons->addWidget(filters);
    buttons->addStretch();

    auto *properties = action(this, "sourcePropertiesButton", tr("来源属性"), "settings");
    buttons->addWidget(properties);

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
        properties->setEnabled(manager->canEditCurrentSource());
    };

    const auto updateRow = [=](const QString &sourceId) {
        const auto *source = manager->source(sourceId);
        if (!source)
            return;
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
        const auto runtime = manager->sourceStatus(sourceId);
        const QString state = source->type() == VideoSourceType::Image ? QString() : runtime.state;
        const QString error = source->type() == VideoSourceType::Image ? QString() : runtime.error;
        auto *text = widget->findChild<QLabel *>();
        text->setText(source->name + (state == "error" ? tr(" · 采集失败") : QString()));
        text->setToolTip(error.isEmpty() ? source->name : error);
        item->setData(Qt::AccessibleTextRole, source->name);
        item->setData(Qt::AccessibleDescriptionRole, error);

        auto *visibility = widget->findChild<QToolButton *>("sourceVisibilityButton");
        visibility->setChecked(source->visible);
        visibility->setIcon(
            QIcon(source->visible ? ":/assets/icons/eye.svg" : ":/assets/icons/eye-off.svg"));
        auto *lock = widget->findChild<QToolButton *>("sourceLockButton");
        lock->setChecked(source->locked);
        lock->setIcon(
            QIcon(source->locked ? ":/assets/icons/lock.svg" : ":/assets/icons/lock-open.svg"));
    };

    const auto rebuild = [=] {
        QSignalBlocker block(list);
        list->clear();
        for (const auto &source : manager->sources()) {
            const auto id = source.id;
            auto *item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, id);
            item->setSizeHint(QSize(180, 34));

            auto *widget = new QWidget(list);
            widget->setObjectName("sourceRow_" + id);

            auto *rowLayout = new QHBoxLayout(widget);
            rowLayout->setContentsMargins(8, 0, 0, 0);
            rowLayout->setSpacing(0);

            auto *text = label(widget, "sourceName");
            text->setWordWrap(false);
            text->setMinimumWidth(0);
            text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            text->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(text, 1);

            auto *visible = action(widget, "sourceVisibilityButton", tr("显示/隐藏来源"), "eye");
            rowLayout->addWidget(visible);

            auto *locked = action(widget, "sourceLockButton", tr("锁定/解锁来源"), "lock-open");
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
            updateRow(source.id);
        selection();
    };

    connect(manager, &VideoSourceManager::sourcesChanged, this, rebuild);
    connect(manager, &VideoSourceManager::sourceChanged, this, updateRow);
    connect(manager, &VideoSourceManager::sourceStatusChanged, this, updateRow);
    connect(manager, &VideoSourceManager::currentSourceIdChanged, this, selection);
    connect(manager, &VideoSourceManager::sourceMoveAvailabilityChanged, this, selection);
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
    connect(properties, &QToolButton::clicked, this, [=] {
        emit propertiesRequested(manager->currentSourceId());
    });
    rebuild();
}

QDialog *createRemovalDialog(QWidget *parent, const QString &name, const QString &hint,
                             const std::function<bool()> &remove)
{
    auto *dialog = dialogShell(parent, "sourceRemovalDialog", QObject::tr("移除来源"));
    dialog->resize(460, 220);

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(
        label(dialog, "sourceRemovalMessage", QObject::tr("确定移除来源“%1”吗？").arg(name)));
    layout->addWidget(label(dialog, "sourceRemovalHint", hint));

    auto *error = label(dialog, "sourceRemovalError");
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
    auto *dialog = dialogShell(parent, "sourceAddDialog", QObject::tr("添加来源"));
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

    auto *display = new CaptureSettings(pages, displays, cameras, false);
    pages->addWidget(display);
    // Camera discovery starts when that page is selected, not when opening an unrelated image
    // dialog.
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

    auto *error = label(dialog, "sourceAddError");
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
        ok->setEnabled(
            !busy &&
            (type->currentIndex() == 2 ||
             (type->currentIndex() == 0
                  ? display->ready()
                  : camera && !camera->currentData().toString().isEmpty() &&
                        cameraPage->findChild<QComboBox *>("cameraModeInput")->isEnabled())));
        ok->setText(busy ? QObject::tr("加载中…") : QObject::tr("确定"));
        type->setEnabled(!busy);
        name->setEnabled(!busy);
        path->setEnabled(!busy);
        browse->setEnabled(!busy);
    };
    display->changed = refresh;

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
            auto *camera = new CaptureSettings(cameraPage, displays, cameras, true);
            cameraLayout->addWidget(camera);
            camera->changed = refresh;
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
        QString id;
        if (type->currentIndex() == 0)
            id = displays->addDisplay(name->text(), display->device->currentData().toString(),
                                      display->cursor->isChecked());
        else
            id = cameras->addCamera(
                name->text(), cameraPage->findChild<QComboBox *>("cameraModeInput")
                                  ->currentData()
                                  .value<CameraMode>()
                                  .params(cameraPage->findChild<QComboBox *>("cameraDeviceInput")
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

QDialog *createSourcePropertiesDialog(QWidget *parent, VideoSourceManager *manager,
                                      const QString &sourceId, DisplaySourceController *displays,
                                      CameraSourceController *cameras)
{
    const bool camera = manager->cameraSource(sourceId) != nullptr;

    auto *dialog =
        dialogShell(parent, camera ? "cameraPropertiesDialog" : "displayPropertiesDialog",
                    QObject::tr("来源属性"));
    dialog->setProperty("displaySourceId", sourceId);

    auto *layout = new QVBoxLayout(dialog);
    auto *settings =
        new CaptureSettings(dialog, displays, cameras, camera, manager->source(sourceId));
    layout->addWidget(settings);

    auto *state = label(dialog, camera ? "cameraStateLabel" : "captureStateLabel");
    layout->addWidget(state);

    auto *error = label(dialog, camera ? "cameraErrorLabel" : "captureErrorLabel");
    error->setProperty("uiError", true);
    layout->addWidget(error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);

    auto *apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(camera ? "confirmCameraPropertiesButton" : "applyDisplayPropertiesButton");
    apply->setText(camera ? QObject::tr("确定") : QObject::tr("应用"));
    buttons->button(QDialogButtonBox::Close)->setText(QObject::tr("关闭"));

    auto *retry = buttons->addButton(QObject::tr("重试"), QDialogButtonBox::ActionRole);
    retry->setObjectName(camera ? "retryCameraButton" : "retryDisplayButton");
    buttons->button(QDialogButtonBox::Close)
        ->setObjectName(camera ? "cancelCameraPropertiesButton" : "closeDisplayPropertiesButton");

    const auto refresh = [=] {
        const auto *cameraSource = manager->cameraSource(sourceId);
        const auto *displaySource = manager->displaySource(sourceId);
        if ((camera && !cameraSource) || (!camera && !displaySource)) {
            dialog->close();
            return;
        }
        const auto runtime = manager->sourceStatus(sourceId);
        const QString status = runtime.state;
        QString text = status == "capturing" ? QObject::tr("采集中")
                       : status == "paused"  ? QObject::tr("已暂停：来源当前隐藏")
                       : status == "error"   ? QObject::tr("采集失败")
                                             : QObject::tr("等待采集画面…");
        if (camera && status == "capturing") {
            text += QObject::tr(" · %1 × %2 · 协商帧率 %3 FPS")
                        .arg(runtime.pixelSize.width())
                        .arg(runtime.pixelSize.height())
                        .arg(double(runtime.actualFrameRateNumerator) /
                                 qMax(1, runtime.actualFrameRateDenominator),
                             0, 'f', 2);
        }
        state->setText(text);
        error->setText(runtime.error);
        retry->setEnabled(!camera || status == "error");
        apply->setEnabled(settings->ready());
    };
    settings->changed = refresh;

    QObject::connect(manager, &VideoSourceManager::sourceStatusChanged, dialog,
                     [=](const QString &changedSourceId) {
                         if (changedSourceId == sourceId)
                             refresh();
                     });
    QObject::connect(manager, &VideoSourceManager::sourcesChanged, dialog, refresh);
    QObject::connect(apply, &QPushButton::clicked, dialog, [=] {
        bool success =
            camera ? cameras->applyProperties(
                         sourceId, settings->mode->currentData().value<CameraMode>().params(
                                       settings->device->currentData().toString()))
                   : displays->applyProperties(sourceId, settings->device->currentData().toString(),
                                               settings->cursor->isChecked());
        if (success && camera)
            dialog->accept();
        else if (!success)
            error->setText(QObject::tr("无法应用配置：设备已被使用或配置无效。"));
    });
    QObject::connect(retry, &QPushButton::clicked, dialog, [=] {
        if (camera)
            cameras->retry(sourceId);
        else
            displays->retry(sourceId);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    refresh();
    return dialog;
}

QDialog *createSourceFiltersDialog(QWidget *parent, VideoSourceManager *manager,
                                   const QString &sourceId)
{
    auto *dialog = dialogShell(parent, "sourceFiltersDialog", QObject::tr("来源滤镜"));
    dialog->setProperty("filterSourceId", sourceId);
    dialog->resize(680, 380);

    auto *layout = new QVBoxLayout(dialog);
    auto *row = new QHBoxLayout;
    layout->addLayout(row, 1);

    auto *left = new QVBoxLayout;
    row->addLayout(left, 1);
    left->addWidget(label(dialog, "filterHint", QObject::tr("滤镜 · 从上到下处理")));

    auto *list = new QListWidget(dialog);
    list->setObjectName("filterList");
    left->addWidget(list);

    auto *actions = new QHBoxLayout;
    left->addLayout(actions);

    auto *add = action(dialog, "addFilterButton", QObject::tr("添加色彩校正"), "plus");
    actions->addWidget(add);

    auto *remove = action(dialog, "removeFilterButton", QObject::tr("删除滤镜"), "minus");
    actions->addWidget(remove);

    auto *up = action(dialog, "upFilterButton", QObject::tr("滤镜上移"), "arrow-up");
    actions->addWidget(up);

    auto *down = action(dialog, "downFilterButton", QObject::tr("滤镜下移"), "arrow-down");
    actions->addWidget(down);

    auto *form = new QFormLayout;
    row->addLayout(form, 1);
    form->addRow(label(dialog, "filterApplyHint", QObject::tr("修改立即生效，关闭后保留。")));
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

    const auto selected = [=] {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
    };

    const auto updateSelection = [=] {
        const auto filters = manager->sourceFilters(sourceId);
        const int index = list->currentRow();

        const VideoSourceFilter *filter =
            index >= 0 && index < filters.size() ? &filters[index] : nullptr;
        remove->setEnabled(filter != nullptr);
        reset->setEnabled(filter != nullptr);
        up->setEnabled(index > 0);
        down->setEnabled(index >= 0 && index + 1 < filters.size());

        std::array<double, 3> values{0, 1, 1};
        if (filter)
            values = {filter->brightness, filter->contrast, filter->saturation};
        for (int n = 0; n < inputs.size(); ++n) {
            QSignalBlocker inputBlocker(inputs[n]);
            QSignalBlocker sliderBlocker(sliders[n]);
            inputs[n]->setValue(values[n]);
            sliders[n]->setValue(qRound(values[n] * 100));
            inputs[n]->setEnabled(filter != nullptr);
            sliders[n]->setEnabled(filter != nullptr);
        }
    };

    const auto refresh = [=] {
        if (!manager->canFilterSource(sourceId)) {
            dialog->close();
            return;
        }

        const auto current = selected();
        QSignalBlocker blocker(list);
        list->clear();
        int index = 0;
        for (const auto &entry : manager->sourceFilters(sourceId)) {
            auto *item = new QListWidgetItem(QObject::tr("色彩校正 %1").arg(++index), list);
            item->setData(Qt::UserRole, entry.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
            if (entry.id == current)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        updateSelection();
    };

    const auto parameters = [=] {
        if (!selected().isEmpty())
            manager->setFilterParameters(sourceId, selected(), inputs[0]->value(),
                                         inputs[1]->value(), inputs[2]->value());
    };
    for (int n = 0; n < inputs.size(); ++n) {
        QObject::connect(inputs[n], &QDoubleSpinBox::valueChanged, dialog, parameters);
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
    QObject::connect(list, &QListWidget::currentRowChanged, dialog, updateSelection);
    QObject::connect(list, &QListWidget::itemChanged, dialog, [=](QListWidgetItem *item) {
        manager->setFilterEnabled(sourceId, item->data(Qt::UserRole).toString(),
                                  item->checkState() == Qt::Checked);
    });
    QObject::connect(add, &QToolButton::clicked, dialog, [=] {
        const auto added = manager->addFilter(sourceId);
        for (int n = 0; n < list->count(); ++n)
            if (list->item(n)->data(Qt::UserRole).toString() == added)
                list->setCurrentRow(n);
    });
    QObject::connect(remove, &QToolButton::clicked, dialog, [=] {
        manager->removeFilter(sourceId, selected());
    });
    QObject::connect(up, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, selected(), -1);
    });
    QObject::connect(down, &QToolButton::clicked, dialog, [=] {
        manager->moveFilter(sourceId, selected(), 1);
    });
    QObject::connect(reset, &QPushButton::clicked, dialog, [=] {
        manager->setFilterParameters(sourceId, selected(), 0, 1, 1);
    });

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    close->button(QDialogButtonBox::Close)->setObjectName("closeFiltersButton");
    close->button(QDialogButtonBox::Close)->setText(QObject::tr("关闭"));
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    refresh();
    return dialog;
}
