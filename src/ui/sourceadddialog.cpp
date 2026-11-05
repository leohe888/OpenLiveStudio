#include "ui/sourcedialogs.h"
#include "ui/sourcewidgets.h"
#include "video/videosourcemanager.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"
#include "video/display/waylanddisplaycapture.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QSet>
#include <algorithm>

using namespace SourceWidgets;

namespace {
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
        layout->addWidget(
            createPlainLabel(this, "displayCaptureHint", tr("原始分辨率 · 最高 30 FPS · SDR")));
        layout->addWidget(createPlainLabel(
            this, "portalCaptureHint",
            tr("由系统选择要共享的显示器；隐藏来源会停止共享，再次显示需重新选择。")));
        const uint modes = WaylandDisplayCapture::cursorModes();
        if ((modes & 3) != 3) {
            const bool embedded = modes & 2;
            captureCursorInput->setChecked(embedded);
            captureCursorInput->setEnabled(false);
            const auto hint =
                embedded ? tr("系统仅支持显示鼠标。") : tr("系统不支持将鼠标嵌入画面。");
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

    CameraSettingsWidget(QWidget *parent, CameraSourceController *cameras) : QWidget(parent)
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
                            const auto label = entry.subtype == "mjpeg" ? QStringLiteral("MJPEG")
                                               : entry.subtype == "yuyv422"
                                                   ? QStringLiteral("YUY2")
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
