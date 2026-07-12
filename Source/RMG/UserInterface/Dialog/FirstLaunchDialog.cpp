/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "FirstLaunchDialog.hpp"

#include <libusb.h>
#include <SDL3/SDL.h>

#include <cstdint>

#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>

using namespace UserInterface::Dialog;

static constexpr uint16_t kGameCubeAdapterVendorId = 0x057e;
static constexpr uint16_t kGameCubeAdapterProductId = 0x0337;

static void set_button_checked(QButtonGroup* group, int id)
{
    if (group == nullptr)
    {
        return;
    }

    QAbstractButton* button = group->button(id);
    if (button != nullptr)
    {
        button->setChecked(true);
    }
}

static QString format_usb_id(uint16_t vendorId, uint16_t productId)
{
    return QStringLiteral("%1:%2")
        .arg(vendorId, 4, 16, QChar('0'))
        .arg(productId, 4, 16, QChar('0'));
}

FirstLaunchDialog::FirstLaunchDialog(QWidget* parent, InputPluginType currentPlugin, bool autoSelectRecommended)
    : QDialog(parent),
      selectedPlugin(currentPlugin)
{
    this->setupUi(this);

    this->pluginGroup = new QButtonGroup(this);
    this->pluginGroup->setExclusive(true);

    auto addButton = [this](QToolButton* button, InputPluginType plugin)
    {
        button->setCheckable(true);
        this->pluginGroup->addButton(button, static_cast<int>(plugin));
    };

    addButton(this->gamecubeButton, InputPluginType::Gamecube);
    addButton(this->raphnetButton, InputPluginType::Raphnet);
    addButton(this->usbButton, InputPluginType::USB);

    connect(this->pluginGroup, &QButtonGroup::idClicked, this, [this](int id)
    {
        this->setSelectedPluginInternal(static_cast<InputPluginType>(id), true);
    });

    const QSize iconSize(112, 112);
    const QSize buttonMinSize(190, 164);
    const QSizePolicy buttonSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    const QSizePolicy labelSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    this->gamecubeButton->setIcon(QIcon(":/onboarding/gamecube.png"));
    this->gamecubeButton->setIconSize(iconSize);
    this->gamecubeButton->setMinimumSize(buttonMinSize);
    this->gamecubeButton->setSizePolicy(buttonSizePolicy);

    this->raphnetButton->setIcon(QIcon(":/onboarding/raphnet.png"));
    this->raphnetButton->setIconSize(iconSize);
    this->raphnetButton->setMinimumSize(buttonMinSize);
    this->raphnetButton->setSizePolicy(buttonSizePolicy);

    this->usbButton->setIcon(QIcon(":/onboarding/usb.png"));
    this->usbButton->setIconSize(iconSize);
    this->usbButton->setMinimumSize(buttonMinSize);
    this->usbButton->setSizePolicy(buttonSizePolicy);

    for (QLabel* label : {this->gamecubeRecommendedLabel, this->raphnetRecommendedLabel, this->usbRecommendedLabel})
    {
        label->setProperty("recommendedBadge", false);
        label->setProperty("advisoryBadge", false);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        label->setWordWrap(true);
        label->setFixedHeight(58);
        label->setSizePolicy(labelSizePolicy);
        label->setText(" ");
        label->setVisible(true);
    }

    this->romDirectoryLineEdit->setReadOnly(true);
    this->romDirectoryLineEdit->setMinimumHeight(30);
    this->romDirectoryBrowseButton->setMinimumHeight(30);

    this->detectedDevicesPlainTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

    connect(this->debugRecommendationComboBox, &QComboBox::currentIndexChanged, this,
        [this](int index)
    {
        this->applyDebugRecommendationOverride(index);
    });

    connect(this->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(this->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (QPushButton* okButton = this->buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(tr("Finish Setup"));
    }

    this->setStyleSheet(
        "QToolButton {"
        "  border: 1px solid #b8c0cc;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "  font-size: 13px;"
        "}"
        "QToolButton:checked {"
        "  border: 2px solid #2f74c0;"
        "  background: #eaf3ff;"
        "}"
        "QLabel[recommendedBadge=\"true\"] {"
        "  background: #e7f5eb;"
        "  color: #1f6f3a;"
        "  border-radius: 4px;"
        "  padding: 3px 8px;"
        "}"
        "QLabel[advisoryBadge=\"true\"] {"
        "  background: #fff3cd;"
        "  color: #7a4b00;"
        "  border-radius: 4px;"
        "  padding: 3px 8px;"
        "}"
        "QLineEdit {"
        "  padding: 6px;"
        "}"
    );

    this->detectionReport = this->scanInputDevices();
    this->updateAvailableControllerOptions();
    this->updateDetectedDevices(this->detectionReport);

    this->recommendedPlugin = this->detectRecommendedPlugin(this->detectionReport, this->recommendedReason, this->hasRecommendation);
    this->updateDetectedRecommendationLabels(this->detectionReport);

    InputPluginType initialPlugin = currentPlugin;
    if (autoSelectRecommended && this->hasRecommendation)
    {
        initialPlugin = this->recommendedPlugin;
    }
    initialPlugin = this->availablePluginOrFallback(initialPlugin);

    this->setSelectedPluginInternal(initialPlugin, false);
}

void FirstLaunchDialog::SetSelectedPlugin(InputPluginType plugin)
{
    this->setSelectedPluginInternal(plugin, false);
}

FirstLaunchDialog::InputPluginType FirstLaunchDialog::GetSelectedPlugin(void) const
{
    return this->selectedPlugin;
}

void FirstLaunchDialog::SetRomDirectory(const QString& directory)
{
    this->romDirectoryLineEdit->setText(directory);
}

void FirstLaunchDialog::on_romDirectoryBrowseButton_clicked(void)
{
    QString currentDir = this->romDirectoryLineEdit->text();
    if (!QDir(currentDir).exists())
    {
        currentDir = "";
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("Select ROM Directory"), currentDir);
    if (dir.isEmpty())
    {
        return;
    }

    QString nativeDir = QDir::toNativeSeparators(dir);
    this->romDirectoryLineEdit->setText(nativeDir);
    emit this->RomDirectorySelected(nativeDir);
}

void FirstLaunchDialog::setRecommendedPlugin(InputPluginType plugin, const QString& reason, bool hasRecommendation,
    RecommendationStyle style)
{
    this->clearRecommendationLabels();

    if (!hasRecommendation)
    {
        return;
    }

    this->setRecommendationLabel(plugin, reason, style);
}

void FirstLaunchDialog::clearRecommendationLabels(void)
{
    QLabel* labels[] = {this->gamecubeRecommendedLabel, this->raphnetRecommendedLabel, this->usbRecommendedLabel};
    for (QLabel* label : labels)
    {
        label->setText(" ");
        label->setProperty("recommendedBadge", false);
        label->setProperty("advisoryBadge", false);
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
}

void FirstLaunchDialog::setRecommendationLabel(InputPluginType plugin, const QString& reason, RecommendationStyle style)
{
    QToolButton* targetButton = nullptr;
    QLabel* targetLabel = nullptr;
    switch (plugin)
    {
    case InputPluginType::Gamecube:
        targetButton = this->gamecubeButton;
        targetLabel = this->gamecubeRecommendedLabel;
        break;
    case InputPluginType::Raphnet:
        targetButton = this->raphnetButton;
        targetLabel = this->raphnetRecommendedLabel;
        break;
    case InputPluginType::USB:
        targetButton = this->usbButton;
        targetLabel = this->usbRecommendedLabel;
        break;
    }

    if (targetLabel != nullptr && targetButton != nullptr && targetButton->isVisible())
    {
        targetLabel->setText(reason);
        targetLabel->setProperty("recommendedBadge", style == RecommendationStyle::Recommended);
        targetLabel->setProperty("advisoryBadge", style == RecommendationStyle::Advisory);
        targetLabel->setVisible(true);
        targetLabel->style()->unpolish(targetLabel);
        targetLabel->style()->polish(targetLabel);
    }
}

bool FirstLaunchDialog::isPluginAvailable(InputPluginType plugin) const
{
    switch (plugin)
    {
    case InputPluginType::Raphnet:
        return this->detectionReport.foundRaphnet;
    case InputPluginType::Gamecube:
        return this->detectionReport.foundNativeGamecube;
    case InputPluginType::USB:
    default:
        return true;
    }
}

FirstLaunchDialog::InputPluginType FirstLaunchDialog::availablePluginOrFallback(InputPluginType plugin) const
{
    if (this->isPluginAvailable(plugin))
    {
        return plugin;
    }

    if (this->detectionReport.foundRaphnet)
    {
        return InputPluginType::Raphnet;
    }
    if (this->detectionReport.foundNativeGamecube)
    {
        return InputPluginType::Gamecube;
    }

    return InputPluginType::USB;
}

void FirstLaunchDialog::updateAvailableControllerOptions(void)
{
    this->raphnetButton->setVisible(this->detectionReport.foundRaphnet);
    this->raphnetRecommendedLabel->setVisible(this->detectionReport.foundRaphnet);
    this->gamecubeButton->setVisible(this->detectionReport.foundNativeGamecube);
    this->gamecubeRecommendedLabel->setVisible(this->detectionReport.foundNativeGamecube);
    this->usbButton->setVisible(true);
    this->usbRecommendedLabel->setVisible(true);
}

void FirstLaunchDialog::updateDetectedRecommendationLabels(const InputDetectionReport& report)
{
    this->clearRecommendationLabels();

    if (report.foundRaphnet)
    {
        this->setRecommendationLabel(InputPluginType::Raphnet,
            tr("Recommended: raphnet adapter detected"), RecommendationStyle::Recommended);
    }

    if (report.foundNativeGamecube)
    {
        this->setRecommendationLabel(InputPluginType::Gamecube,
            tr("Recommended: GameCube adapter detected in native mode"), RecommendationStyle::Recommended);
    }
    else if (report.foundBlockedNativeGamecube)
    {
        this->setRecommendationLabel(InputPluginType::Gamecube,
            tr("Wii U/NS (native) adapter detected, but driver is missing"), RecommendationStyle::Advisory);
    }

    if (report.foundUsbModeMayflash)
    {
        this->setRecommendationLabel(InputPluginType::USB,
            tr("Mayflash USB mode detected; switch to Wii U/NS (native) mode for better support"),
            RecommendationStyle::Advisory);
    }
    else if (report.foundOtherUsb)
    {
        this->setRecommendationLabel(InputPluginType::USB,
            tr("Recommended: USB controller detected"), RecommendationStyle::Recommended);
    }
}

void FirstLaunchDialog::setSelectedPluginInternal(InputPluginType plugin, bool emitSignal)
{
    plugin = this->availablePluginOrFallback(plugin);

    if (this->selectedPlugin != plugin)
    {
        this->selectedPlugin = plugin;
    }

    set_button_checked(this->pluginGroup, static_cast<int>(plugin));
    this->updateButtonStyles();

    if (emitSignal)
    {
        emit this->InputPluginSelected(plugin);
    }
}

void FirstLaunchDialog::updateButtonStyles(void)
{
    for (QToolButton* button : {this->gamecubeButton, this->raphnetButton, this->usbButton})
    {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void FirstLaunchDialog::updateDetectedDevices(const InputDetectionReport& report)
{
    this->detectedDevicesPlainTextEdit->setPlainText(report.lines.join(QStringLiteral("\n")));
}

void FirstLaunchDialog::applyDebugRecommendationOverride(int index)
{
    switch (index)
    {
    case 0:
        this->updateDetectedRecommendationLabels(this->detectionReport);
        break;
    case 1:
        this->setRecommendedPlugin(InputPluginType::USB, QString(), false);
        break;
    case 2:
        this->setRecommendedPlugin(InputPluginType::Raphnet, tr("Recommended: raphnet adapter detected"), true);
        break;
    case 3:
        this->setRecommendedPlugin(InputPluginType::Gamecube, tr("Recommended: GameCube adapter detected in native mode"), true);
        break;
    case 4:
        this->setRecommendedPlugin(InputPluginType::USB, tr("Recommended: USB controller detected"), true);
        break;
    case 5:
        this->setRecommendedPlugin(InputPluginType::USB,
            tr("Mayflash USB mode detected; switch to Wii U/NS (native) mode for better support"),
            true, RecommendationStyle::Advisory);
        break;
    case 6:
        this->setRecommendedPlugin(InputPluginType::Gamecube,
            tr("Wii U/NS (native) adapter detected, but driver is missing"),
            true, RecommendationStyle::Advisory);
        break;
    default:
        this->updateDetectedRecommendationLabels(this->detectionReport);
        break;
    }
}

FirstLaunchDialog::InputDetectionReport FirstLaunchDialog::scanInputDevices(void) const
{
    InputDetectionReport report;

    libusb_context* usbContext = nullptr;
    int usbResult = libusb_init(&usbContext);
    if (usbResult == LIBUSB_SUCCESS)
    {
        libusb_device** devices = nullptr;
        const ssize_t deviceCount = libusb_get_device_list(usbContext, &devices);
        bool sawNativeGamecube = false;
        bool openedNativeGamecube = false;
        QString nativeGamecubeOpenError;

        if (deviceCount >= 0)
        {
            for (ssize_t i = 0; i < deviceCount; i++)
            {
                libusb_device_descriptor descriptor = {};
                if (libusb_get_device_descriptor(devices[i], &descriptor) != LIBUSB_SUCCESS)
                {
                    continue;
                }

                if (descriptor.idVendor != kGameCubeAdapterVendorId ||
                    descriptor.idProduct != kGameCubeAdapterProductId)
                {
                    continue;
                }

                sawNativeGamecube = true;

                libusb_device_handle* handle = nullptr;
                const int openResult = libusb_open(devices[i], &handle);
                if (openResult == LIBUSB_SUCCESS)
                {
                    openedNativeGamecube = true;
                    libusb_close(handle);
                }
                else if (nativeGamecubeOpenError.isEmpty())
                {
                    nativeGamecubeOpenError = QString::fromLatin1(libusb_error_name(openResult));
                }
            }

            libusb_free_device_list(devices, 1);
        }

        if (openedNativeGamecube)
        {
            report.foundNativeGamecube = true;
            report.lines.append(tr("Native GameCube adapter: USB %1 detected and openable -> GameCube native")
                .arg(format_usb_id(kGameCubeAdapterVendorId, kGameCubeAdapterProductId)));
        }
        else if (sawNativeGamecube)
        {
            report.foundBlockedNativeGamecube = true;
            if (nativeGamecubeOpenError.isEmpty())
            {
                nativeGamecubeOpenError = tr("unknown error");
            }

            report.lines.append(tr("Native GameCube adapter: USB %1 detected, but libusb open failed (%2)")
                .arg(format_usb_id(kGameCubeAdapterVendorId, kGameCubeAdapterProductId), nativeGamecubeOpenError));
        }
        else if (deviceCount >= 0)
        {
            report.lines.append(tr("Native GameCube adapter: USB %1 not detected")
                .arg(format_usb_id(kGameCubeAdapterVendorId, kGameCubeAdapterProductId)));
        }
        else
        {
            report.lines.append(tr("Native GameCube adapter scan failed: %1")
                .arg(QString::fromLatin1(libusb_error_name(static_cast<int>(deviceCount)))));
        }

        libusb_exit(usbContext);
    }
    else
    {
        report.lines.append(tr("Native GameCube adapter scan unavailable: %1")
            .arg(QString::fromLatin1(libusb_error_name(usbResult))));
    }

    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
    {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        {
            report.lines.append(tr("SDL scan unavailable: %1").arg(QString::fromUtf8(SDL_GetError())));
            return report;
        }
    }

    SDL_UpdateJoysticks();

    int joysticksCount = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&joysticksCount);

    for (int i = 0; i < joysticksCount; i++)
    {
        SDL_JoystickID joystickId = joysticks[i];
        const bool isGamepad = SDL_IsGamepad(joystickId);
        const char* deviceNamePtr = isGamepad ?
            SDL_GetGamepadNameForID(joystickId) :
            SDL_GetJoystickNameForID(joystickId);
        const uint16_t vendorId = isGamepad ?
            SDL_GetGamepadVendorForID(joystickId) :
            SDL_GetJoystickVendorForID(joystickId);
        const uint16_t productId = isGamepad ?
            SDL_GetGamepadProductForID(joystickId) :
            SDL_GetJoystickProductForID(joystickId);

        const QString deviceKind = isGamepad ? tr("SDL gamepad") : tr("SDL joystick");
        const QString deviceName = QString::fromUtf8(deviceNamePtr == nullptr ? "" : deviceNamePtr);
        const QString usbId = (vendorId == 0 && productId == 0) ?
            tr("VID:PID unknown") :
            tr("VID:PID %1").arg(format_usb_id(vendorId, productId));

        if (deviceName.isEmpty())
        {
            report.lines.append(tr("%1 id %2: name unavailable [%3] -> ignored")
                .arg(deviceKind)
                .arg(static_cast<qlonglong>(joystickId))
                .arg(usbId));
            continue;
        }

        report.foundAnySdlDevice = true;

        const QString lowered = deviceName.toLower();
        QString classification;

        if (lowered.contains("raphnet"))
        {
            report.foundRaphnet = true;
            classification = tr("Raphnet");
        }
        else if (lowered.contains("mayflash") &&
                 (lowered.contains("gamecube") || lowered.contains("gcn")))
        {
            report.foundUsbModeMayflash = true;
            classification = tr("Mayflash GameCube adapter in USB mode; switch to Wii U/NS (native) mode for better support");
        }
        else if (lowered.contains("gamecube") || lowered.contains("gcn") || lowered.contains("mayflash"))
        {
            report.foundOtherUsb = true;
            classification = tr("GameCube-like SDL name; native mode not confirmed");
        }
        else
        {
            report.foundOtherUsb = true;
            classification = tr("Other USB");
        }

        report.lines.append(tr("%1 id %2: \"%3\" [%4] -> %5")
            .arg(deviceKind)
            .arg(static_cast<qlonglong>(joystickId))
            .arg(deviceName, usbId, classification));
    }

    if (!report.foundAnySdlDevice)
    {
        report.lines.append(tr("SDL scan: no named gamepad or joystick devices detected"));
    }

    if (joysticks != nullptr)
    {
        SDL_free(joysticks);
    }

    return report;
}

FirstLaunchDialog::InputPluginType FirstLaunchDialog::detectRecommendedPlugin(
    const InputDetectionReport& report, QString& reason, bool& hasRecommendation) const
{
    hasRecommendation = false;
    reason.clear();

    if (report.foundRaphnet)
    {
        hasRecommendation = true;
        reason = tr("Recommended: raphnet adapter detected");
        return InputPluginType::Raphnet;
    }

    if (report.foundNativeGamecube)
    {
        hasRecommendation = true;
        reason = tr("Recommended: GameCube adapter detected in native mode");
        return InputPluginType::Gamecube;
    }

    if (report.foundAnySdlDevice)
    {
        hasRecommendation = true;
        reason = tr("Recommended: USB controller detected");
        return InputPluginType::USB;
    }

    return InputPluginType::USB;
}
