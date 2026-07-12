/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "UnifiedInputDialog.hpp"

#include <UserInterface/Widget/ControllerImageWidget.hpp>
#include <Utilities/QtKeyToSdl3Key.hpp>

#include <RMG-Core/Settings.hpp>

#include <SDL3/SDL.h>
#include <hidapi.h>
#include <libusb.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
using InputPluginType = UserInterface::Dialog::UnifiedInputDialog::InputPluginType;
using RecommendationStyle = UserInterface::Dialog::UnifiedInputDialog::RecommendationStyle;
using BindingValue = UserInterface::Dialog::UnifiedInputDialog::BindingValue;
using ControllerPage = UserInterface::Dialog::UnifiedInputDialog::ControllerPage;
using UsbDeviceChoice = UserInterface::Dialog::UnifiedInputDialog::UsbDeviceChoice;

class ControllerPreviewPanel : public QWidget
{
  public:
    explicit ControllerPreviewPanel(QWidget* parent)
        : QWidget(parent)
    {
        this->setMinimumSize(360, 220);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        this->controllerImageWidget = new UserInterface::Widget::ControllerImageWidget(this);
        this->controllerImageWidget->setMinimumSize(360, 220);
        this->controllerImageWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    UserInterface::Widget::ControllerImageWidget* ControllerImage(void) const
    {
        return this->controllerImageWidget;
    }

    void SetOutputWidget(QWidget* widget)
    {
        this->outputWidget = widget;
        if (this->outputWidget != nullptr)
        {
            this->outputWidget->setParent(this);
            this->outputWidget->raise();
            this->positionOutputWidget();
        }
    }

  protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        this->controllerImageWidget->setGeometry(this->rect());
        this->positionOutputWidget();
    }

  private:
    void positionOutputWidget(void)
    {
        if (this->outputWidget == nullptr)
        {
            return;
        }

        constexpr double viewBoxWidth = 475.0;
        constexpr double viewBoxHeight = 450.0;
        constexpr double outputCenterX = 130.5;
        constexpr double outputCenterY = 291.1;

        if (this->width() <= 0 || this->height() <= 0)
        {
            return;
        }

        this->outputWidget->adjustSize();
        this->outputWidget->resize(this->outputWidget->sizeHint());
        const QSize outputSize = this->outputWidget->size();

        const double scale = std::min(
            static_cast<double>(this->width()) / viewBoxWidth,
            static_cast<double>(this->height()) / viewBoxHeight);
        const double imageWidth = viewBoxWidth * scale;
        const double imageHeight = viewBoxHeight * scale;
        const double imageLeft = (static_cast<double>(this->width()) - imageWidth) / 2.0;
        const double imageTop = (static_cast<double>(this->height()) - imageHeight) / 2.0;

        const int x = std::clamp(
            static_cast<int>(std::round(imageLeft + outputCenterX * scale - outputSize.width() / 2.0)),
            0,
            std::max(0, this->width() - outputSize.width()));
        const int y = std::clamp(
            static_cast<int>(std::round(imageTop + outputCenterY * scale - outputSize.height() / 2.0)),
            0,
            std::max(0, this->height() - outputSize.height()));
        this->outputWidget->move(x, y);
        this->outputWidget->raise();
    }

    UserInterface::Widget::ControllerImageWidget* controllerImageWidget = nullptr;
    QWidget* outputWidget = nullptr;
};

constexpr uint16_t kGameCubeAdapterVendorId = 0x057e;
constexpr uint16_t kGameCubeAdapterProductId = 0x0337;
constexpr uint8_t kGameCubeEndpointIn = 0x81;
constexpr uint8_t kGameCubeEndpointOut = 0x02;
constexpr uint8_t kGameCubeCommandPoll = 0x13;
constexpr double kGameCubeN64AxisPeak = 85.0;
constexpr double kGameCubeSensitivityOffset = 0.90;
constexpr double kGameCubeSensitivityPerPercent = 0.0045;
constexpr int kAxisMotionThreshold = SDL_AXIS_PEAK / 2;
constexpr int kListenTimeoutTicks = 300;

constexpr uint16_t kRaphnetVendorId = 0x289b;
constexpr uint8_t kRaphnetRawSiCommand = 0x80;
constexpr uint8_t kRaphnetSuspendPolling = 0x03;
constexpr uint8_t kN64GetStatus = 0x01;

constexpr uint16_t kN64ButtonA = 0x8000;
constexpr uint16_t kN64ButtonB = 0x4000;
constexpr uint16_t kN64ButtonZ = 0x2000;
constexpr uint16_t kN64ButtonStart = 0x1000;
constexpr uint16_t kN64ButtonDUp = 0x0800;
constexpr uint16_t kN64ButtonDDown = 0x0400;
constexpr uint16_t kN64ButtonDLeft = 0x0200;
constexpr uint16_t kN64ButtonDRight = 0x0100;
constexpr uint16_t kN64ButtonL = 0x0020;
constexpr uint16_t kN64ButtonR = 0x0010;
constexpr uint16_t kN64ButtonCUp = 0x0008;
constexpr uint16_t kN64ButtonCDown = 0x0004;
constexpr uint16_t kN64ButtonCLeft = 0x0002;
constexpr uint16_t kN64ButtonCRight = 0x0001;

struct RaphnetAdapterDef
{
    uint16_t productId;
    int interfaceNumber;
    int rawChannels;
    int reportSize;
};

const RaphnetAdapterDef kRaphnetAdapters[] = {
    { 0x0017, 1, 1, 40 },
    { 0x001D, 1, 1, 40 },
    { 0x0020, 1, 1, 40 },
    { 0x0021, 1, 1, 40 },
    { 0x0022, 1, 2, 40 },
    { 0x0030, 1, 2, 40 },
    { 0x0031, 1, 2, 40 },
    { 0x0032, 1, 1, 63 },
    { 0x0033, 1, 1, 63 },
    { 0x0034, 1, 1, 63 },
    { 0x0035, 1, 2, 63 },
    { 0x0036, 1, 2, 63 },
    { 0x0037, 1, 2, 63 },
    { 0x0038, 1, 1, 63 },
    { 0x0039, 1, 1, 63 },
    { 0x003A, 1, 1, 63 },
    { 0x003B, 2, 2, 63 },
    { 0x003C, 2, 2, 63 },
    { 0x003D, 2, 2, 63 },
    { 0x0060, 1, 1, 63 },
    { 0x0061, 1, 1, 63 },
    { 0x0063, 2, 2, 63 },
    { 0x0064, 2, 2, 63 },
    { 0x0067, 1, 1, 63 },
};

enum class GCInput : int
{
    None = -1,
    A = 0,
    B,
    X,
    Y,
    Z,
    Start,
    L,
    R,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftTrigger,
    RightTrigger,
    CStickUp,
    CStickDown,
    CStickLeft,
    CStickRight
};

struct GameCubeState
{
    uint8_t status = 0;
    uint8_t buttons1 = 0;
    uint8_t buttons2 = 0;
    uint8_t leftStickX = 128;
    uint8_t leftStickY = 128;
    uint8_t rightStickX = 128;
    uint8_t rightStickY = 128;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
};

struct BindingTarget
{
    const char* label;
    N64ControllerButton imageButton;
    int axisXDirection;
    int axisYDirection;
    SettingsID usbInputType;
    SettingsID usbName;
    SettingsID usbData;
    SettingsID usbExtraData;
    bool hasGamecubeMapping;
    SettingsID gamecubeMapping;
};

const std::array<BindingTarget, 19> kBindingTargets = {{
    { "A", N64ControllerButton::A, 0, 0, SettingsID::Input_A_InputType, SettingsID::Input_A_Name, SettingsID::Input_A_Data, SettingsID::Input_A_ExtraData, true, SettingsID::GCAInput_Map_A },
    { "B", N64ControllerButton::B, 0, 0, SettingsID::Input_B_InputType, SettingsID::Input_B_Name, SettingsID::Input_B_Data, SettingsID::Input_B_ExtraData, true, SettingsID::GCAInput_Map_B },
    { "Start", N64ControllerButton::Start, 0, 0, SettingsID::Input_Start_InputType, SettingsID::Input_Start_Name, SettingsID::Input_Start_Data, SettingsID::Input_Start_ExtraData, true, SettingsID::GCAInput_Map_Start },
    { "Z", N64ControllerButton::ZTrigger, 0, 0, SettingsID::Input_ZTrigger_InputType, SettingsID::Input_ZTrigger_Name, SettingsID::Input_ZTrigger_Data, SettingsID::Input_ZTrigger_ExtraData, true, SettingsID::GCAInput_Map_Z },
    { "Z 2", N64ControllerButton::ZTrigger2, 0, 0, SettingsID::Input_ZTrigger2_InputType, SettingsID::Input_ZTrigger2_Name, SettingsID::Input_ZTrigger2_Data, SettingsID::Input_ZTrigger2_ExtraData, true, SettingsID::GCAInput_Map_Z2 },
    { "L", N64ControllerButton::LeftShoulder, 0, 0, SettingsID::Input_LeftShoulder_InputType, SettingsID::Input_LeftShoulder_Name, SettingsID::Input_LeftShoulder_Data, SettingsID::Input_LeftShoulder_ExtraData, true, SettingsID::GCAInput_Map_L },
    { "R", N64ControllerButton::RightShoulder, 0, 0, SettingsID::Input_RightShoulder_InputType, SettingsID::Input_RightShoulder_Name, SettingsID::Input_RightShoulder_Data, SettingsID::Input_RightShoulder_ExtraData, true, SettingsID::GCAInput_Map_R },
    { "D-Up", N64ControllerButton::DpadUp, 0, 0, SettingsID::Input_DpadUp_InputType, SettingsID::Input_DpadUp_Name, SettingsID::Input_DpadUp_Data, SettingsID::Input_DpadUp_ExtraData, true, SettingsID::GCAInput_Map_DpadUp },
    { "D-Down", N64ControllerButton::DpadDown, 0, 0, SettingsID::Input_DpadDown_InputType, SettingsID::Input_DpadDown_Name, SettingsID::Input_DpadDown_Data, SettingsID::Input_DpadDown_ExtraData, true, SettingsID::GCAInput_Map_DpadDown },
    { "D-Left", N64ControllerButton::DpadLeft, 0, 0, SettingsID::Input_DpadLeft_InputType, SettingsID::Input_DpadLeft_Name, SettingsID::Input_DpadLeft_Data, SettingsID::Input_DpadLeft_ExtraData, true, SettingsID::GCAInput_Map_DpadLeft },
    { "D-Right", N64ControllerButton::DpadRight, 0, 0, SettingsID::Input_DpadRight_InputType, SettingsID::Input_DpadRight_Name, SettingsID::Input_DpadRight_Data, SettingsID::Input_DpadRight_ExtraData, true, SettingsID::GCAInput_Map_DpadRight },
    { "C-Up", N64ControllerButton::CButtonUp, 0, 0, SettingsID::Input_CButtonUp_InputType, SettingsID::Input_CButtonUp_Name, SettingsID::Input_CButtonUp_Data, SettingsID::Input_CButtonUp_ExtraData, true, SettingsID::GCAInput_Map_CUp },
    { "C-Down", N64ControllerButton::CButtonDown, 0, 0, SettingsID::Input_CButtonDown_InputType, SettingsID::Input_CButtonDown_Name, SettingsID::Input_CButtonDown_Data, SettingsID::Input_CButtonDown_ExtraData, true, SettingsID::GCAInput_Map_CDown },
    { "C-Left", N64ControllerButton::CButtonLeft, 0, 0, SettingsID::Input_CButtonLeft_InputType, SettingsID::Input_CButtonLeft_Name, SettingsID::Input_CButtonLeft_Data, SettingsID::Input_CButtonLeft_ExtraData, true, SettingsID::GCAInput_Map_CLeft },
    { "C-Right", N64ControllerButton::CButtonRight, 0, 0, SettingsID::Input_CButtonRight_InputType, SettingsID::Input_CButtonRight_Name, SettingsID::Input_CButtonRight_Data, SettingsID::Input_CButtonRight_ExtraData, true, SettingsID::GCAInput_Map_CRight },
    { "Stick Up", N64ControllerButton::Invalid, 0, 1, SettingsID::Input_AnalogStickUp_InputType, SettingsID::Input_AnalogStickUp_Name, SettingsID::Input_AnalogStickUp_Data, SettingsID::Input_AnalogStickUp_ExtraData, false, SettingsID::GCAInput_Map_A },
    { "Stick Down", N64ControllerButton::Invalid, 0, -1, SettingsID::Input_AnalogStickDown_InputType, SettingsID::Input_AnalogStickDown_Name, SettingsID::Input_AnalogStickDown_Data, SettingsID::Input_AnalogStickDown_ExtraData, false, SettingsID::GCAInput_Map_A },
    { "Stick Left", N64ControllerButton::Invalid, -1, 0, SettingsID::Input_AnalogStickLeft_InputType, SettingsID::Input_AnalogStickLeft_Name, SettingsID::Input_AnalogStickLeft_Data, SettingsID::Input_AnalogStickLeft_ExtraData, false, SettingsID::GCAInput_Map_A },
    { "Stick Right", N64ControllerButton::Invalid, 1, 0, SettingsID::Input_AnalogStickRight_InputType, SettingsID::Input_AnalogStickRight_Name, SettingsID::Input_AnalogStickRight_Data, SettingsID::Input_AnalogStickRight_ExtraData, false, SettingsID::GCAInput_Map_A },
}};

const std::array<int, 4> kDpadBindingIndexes = {{ 7, 9, 10, 8 }};
const std::array<int, 4> kAnalogBindingIndexes = {{ 15, 17, 18, 16 }};
const std::array<int, 7> kButtonBindingIndexes = {{ 0, 1, 2, 3, 4, 5, 6 }};
const std::array<int, 4> kCButtonBindingIndexes = {{ 11, 13, 14, 12 }};

double gamecube_sensitivity_percent_to_scale(int sensitivityPercent)
{
    return kGameCubeSensitivityOffset + (static_cast<double>(sensitivityPercent) * kGameCubeSensitivityPerPercent);
}

const RaphnetAdapterDef* find_raphnet_adapter(uint16_t productId, int interfaceNumber)
{
    for (const RaphnetAdapterDef& adapter : kRaphnetAdapters)
    {
        if (adapter.productId == productId && adapter.interfaceNumber == interfaceNumber)
        {
            return &adapter;
        }
    }

    return nullptr;
}

QString string_from_const_char(const char* text)
{
    return QString::fromUtf8(text == nullptr ? "" : text);
}

QString format_usb_id(uint16_t vendorId, uint16_t productId)
{
    return QStringLiteral("%1:%2")
        .arg(vendorId, 4, 16, QChar('0'))
        .arg(productId, 4, 16, QChar('0'));
}

std::string usb_profile_section(int pageIndex)
{
    return "Rosalie's Mupen GUI - Input Plugin Profile " + std::to_string(pageIndex);
}

bool usb_device_is_raphnet(const UsbDeviceChoice& device)
{
    return device.vendorId == kRaphnetVendorId || device.name.toLower().contains(QStringLiteral("raphnet"));
}

bool usb_device_is_mayflash_gamecube(const UsbDeviceChoice& device)
{
    const QString lowered = device.name.toLower();
    return lowered.contains(QStringLiteral("mayflash")) &&
        (lowered.contains(QStringLiteral("gamecube")) || lowered.contains(QStringLiteral("gcn")));
}

bool usb_device_is_gamecube_like(const UsbDeviceChoice& device)
{
    const QString lowered = device.name.toLower();
    return device.vendorId == kGameCubeAdapterVendorId ||
        lowered.contains(QStringLiteral("gamecube")) ||
        lowered.contains(QStringLiteral("gcn")) ||
        lowered.contains(QStringLiteral("mayflash"));
}

void show_status(QLabel* label, const QString& text)
{
    if (label == nullptr)
    {
        return;
    }

    label->setText(text);
    label->setVisible(!text.isEmpty());
}

void clear_status(QLabel* label)
{
    if (label == nullptr)
    {
        return;
    }

    label->clear();
    label->setVisible(false);
}

bool binding_is_empty(const BindingValue& binding)
{
    if (binding.types.isEmpty())
    {
        return true;
    }

    for (int type : binding.types)
    {
        if (static_cast<InputType>(type) != InputType::Invalid)
        {
            return false;
        }
    }

    return true;
}

QString binding_text(const BindingValue& binding)
{
    if (binding_is_empty(binding))
    {
        return QStringLiteral("Not set");
    }

    QStringList parts;
    const int count = std::min(static_cast<int>(binding.types.size()),
        std::min(static_cast<int>(binding.text.size()),
            std::min(static_cast<int>(binding.data.size()), static_cast<int>(binding.extraData.size()))));
    for (int i = 0; i < count; i++)
    {
        if (static_cast<InputType>(binding.types[i]) == InputType::Invalid)
        {
            continue;
        }

        parts.append(binding.text[i].isEmpty() ? QStringLiteral("Input %1").arg(binding.data[i]) : binding.text[i]);
    }

    return parts.isEmpty() ? QStringLiteral("Not set") : parts.join(QStringLiteral(", "));
}

void set_single_binding(BindingValue& binding, InputType type, int data, int extraData, const QString& text)
{
    binding.types = { static_cast<int>(type) };
    binding.data = { data };
    binding.extraData = { extraData };
    binding.text = { text };
}

void clear_binding(BindingValue& binding)
{
    set_single_binding(binding, InputType::Invalid, 0, 0, QString());
}

std::vector<int> to_std_vector(const QVector<int>& values)
{
    std::vector<int> result;
    result.reserve(static_cast<size_t>(values.size()));
    for (int value : values)
    {
        result.push_back(value);
    }
    return result;
}

std::vector<std::string> to_std_string_vector(const QVector<QString>& values)
{
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(values.size()));
    for (const QString& value : values)
    {
        result.push_back(value.toStdString());
    }
    return result;
}

QVector<int> to_qvector(const std::vector<int>& values)
{
    QVector<int> result;
    result.reserve(static_cast<int>(values.size()));
    for (int value : values)
    {
        result.append(value);
    }
    return result;
}

QVector<QString> to_qstring_vector(const std::vector<std::string>& values)
{
    QVector<QString> result;
    result.reserve(static_cast<int>(values.size()));
    for (const std::string& value : values)
    {
        result.append(QString::fromStdString(value));
    }
    return result;
}

BindingValue load_usb_binding(const BindingTarget& target, const std::string& section)
{
    BindingValue binding;
    binding.types = to_qvector(CoreSettingsGetIntListValue(target.usbInputType, section));
    binding.text = to_qstring_vector(CoreSettingsGetStringListValue(target.usbName, section));
    binding.data = to_qvector(CoreSettingsGetIntListValue(target.usbData, section));
    binding.extraData = to_qvector(CoreSettingsGetIntListValue(target.usbExtraData, section));

    const int count = std::min(static_cast<int>(binding.types.size()),
        std::min(static_cast<int>(binding.text.size()),
            std::min(static_cast<int>(binding.data.size()), static_cast<int>(binding.extraData.size()))));
    if (count > 0)
    {
        binding.types.resize(count);
        binding.text.resize(count);
        binding.data.resize(count);
        binding.extraData.resize(count);
        return binding;
    }

    const int type = CoreSettingsGetIntValue(target.usbInputType, section);
    const std::string name = CoreSettingsGetStringValue(target.usbName, section);
    const int data = CoreSettingsGetIntValue(target.usbData, section);
    const int extraData = CoreSettingsGetIntValue(target.usbExtraData, section);
    set_single_binding(binding, static_cast<InputType>(type), data, extraData, QString::fromStdString(name));
    return binding;
}

QString gc_input_to_string(GCInput input)
{
    switch (input)
    {
    case GCInput::A: return QStringLiteral("A");
    case GCInput::B: return QStringLiteral("B");
    case GCInput::X: return QStringLiteral("X");
    case GCInput::Y: return QStringLiteral("Y");
    case GCInput::Z: return QStringLiteral("Z");
    case GCInput::Start: return QStringLiteral("Start");
    case GCInput::L: return QStringLiteral("L (digital)");
    case GCInput::R: return QStringLiteral("R (digital)");
    case GCInput::DpadUp: return QStringLiteral("D-Up");
    case GCInput::DpadDown: return QStringLiteral("D-Down");
    case GCInput::DpadLeft: return QStringLiteral("D-Left");
    case GCInput::DpadRight: return QStringLiteral("D-Right");
    case GCInput::LeftTrigger: return QStringLiteral("L (analog)");
    case GCInput::RightTrigger: return QStringLiteral("R (analog)");
    case GCInput::CStickUp: return QStringLiteral("C-Stick Up");
    case GCInput::CStickDown: return QStringLiteral("C-Stick Down");
    case GCInput::CStickLeft: return QStringLiteral("C-Stick Left");
    case GCInput::CStickRight: return QStringLiteral("C-Stick Right");
    case GCInput::None:
    default:
        return QStringLiteral("Not set");
    }
}

GCInput gc_input_with_trigger_mode(GCInput input, bool analog)
{
    switch (input)
    {
    case GCInput::L:
    case GCInput::LeftTrigger:
        return analog ? GCInput::LeftTrigger : GCInput::L;
    case GCInput::R:
    case GCInput::RightTrigger:
        return analog ? GCInput::RightTrigger : GCInput::R;
    default:
        return input;
    }
}

void apply_gamecube_trigger_mode(QVector<int>& bindings, bool leftTrigger, bool analog)
{
    const GCInput digitalInput = leftTrigger ? GCInput::L : GCInput::R;
    const GCInput analogInput = leftTrigger ? GCInput::LeftTrigger : GCInput::RightTrigger;

    for (int& binding : bindings)
    {
        const GCInput input = static_cast<GCInput>(binding);
        if (input == digitalInput || input == analogInput)
        {
            binding = static_cast<int>(gc_input_with_trigger_mode(input, analog));
        }
    }
}

bool gc_input_active(const GameCubeState& state, GCInput input,
    double triggerThreshold, double cStickThreshold, bool leftTriggerAnalog, bool rightTriggerAnalog)
{
    const int triggerThresh = static_cast<int>(127.0 * triggerThreshold);
    const int cStickThresh = static_cast<int>(127.0 * cStickThreshold);
    const int8_t cX = static_cast<int8_t>(state.rightStickX + 128);
    const int8_t cY = static_cast<int8_t>(state.rightStickY + 128);

    switch (input)
    {
    case GCInput::A: return (state.buttons1 & 0x01) != 0;
    case GCInput::B: return (state.buttons1 & 0x02) != 0;
    case GCInput::X: return (state.buttons1 & 0x04) != 0;
    case GCInput::Y: return (state.buttons1 & 0x08) != 0;
    case GCInput::DpadLeft: return (state.buttons1 & 0x10) != 0;
    case GCInput::DpadRight: return (state.buttons1 & 0x20) != 0;
    case GCInput::DpadDown: return (state.buttons1 & 0x40) != 0;
    case GCInput::DpadUp: return (state.buttons1 & 0x80) != 0;
    case GCInput::Start: return (state.buttons2 & 0x01) != 0;
    case GCInput::Z: return (state.buttons2 & 0x02) != 0;
    case GCInput::R: return !rightTriggerAnalog && (state.buttons2 & 0x04) != 0;
    case GCInput::L: return !leftTriggerAnalog && (state.buttons2 & 0x08) != 0;
    case GCInput::LeftTrigger: return leftTriggerAnalog && state.leftTrigger > triggerThresh;
    case GCInput::RightTrigger: return rightTriggerAnalog && state.rightTrigger > triggerThresh;
    case GCInput::CStickUp: return cY > cStickThresh;
    case GCInput::CStickDown: return cY < -cStickThresh;
    case GCInput::CStickLeft: return cX < -cStickThresh;
    case GCInput::CStickRight: return cX > cStickThresh;
    default: return false;
    }
}

GCInput detect_gamecube_input(const GameCubeState& state,
    double triggerThreshold, double cStickThreshold, bool leftTriggerAnalog, bool rightTriggerAnalog)
{
    const std::array<GCInput, 6> leadingInputs = {{
        GCInput::A, GCInput::B, GCInput::X, GCInput::Y, GCInput::Z, GCInput::Start
    }};

    for (GCInput input : leadingInputs)
    {
        if (gc_input_active(state, input, triggerThreshold, cStickThreshold, leftTriggerAnalog, rightTriggerAnalog))
        {
            return input;
        }
    }

    if ((state.buttons2 & 0x08) != 0)
    {
        return leftTriggerAnalog ? GCInput::LeftTrigger : GCInput::L;
    }
    if ((state.buttons2 & 0x04) != 0)
    {
        return rightTriggerAnalog ? GCInput::RightTrigger : GCInput::R;
    }

    const std::array<GCInput, 10> remainingInputs = {{
        GCInput::DpadUp, GCInput::DpadDown, GCInput::DpadLeft, GCInput::DpadRight,
        GCInput::LeftTrigger, GCInput::RightTrigger,
        GCInput::CStickUp, GCInput::CStickDown, GCInput::CStickLeft, GCInput::CStickRight
    }};

    for (GCInput input : remainingInputs)
    {
        if (gc_input_active(state, input, triggerThreshold, cStickThreshold, leftTriggerAnalog, rightTriggerAnalog))
        {
            return input;
        }
    }

    return GCInput::None;
}

int scale_axis(double input, double deadzone, double n64Max)
{
    const double inputAbs = std::abs(input);
    if (inputAbs <= deadzone)
    {
        return 0;
    }

    const double deadzoneRelation = 1.0 / (1.0 - deadzone);
    const double scaled = (inputAbs - deadzone) * deadzoneRelation * n64Max;
    const int result = static_cast<int>(std::min(scaled, n64Max));
    return input >= 0 ? result : -result;
}

int normalize_axis_to_percent(int value, int maxValue)
{
    if (maxValue <= 0)
    {
        return 0;
    }

    const int clamped = std::clamp(value, -maxValue, maxValue);
    return std::clamp((clamped * 100) / maxValue, -100, 100);
}

void set_button(UserInterface::Widget::ControllerImageWidget* widget, N64ControllerButton button, bool pressed)
{
    if (widget != nullptr && button != N64ControllerButton::Invalid)
    {
        widget->SetButtonState(button, pressed);
    }
}

void apply_n64_buttons(UserInterface::Widget::ControllerImageWidget* widget, uint16_t buttons)
{
    set_button(widget, N64ControllerButton::A, (buttons & kN64ButtonA) != 0);
    set_button(widget, N64ControllerButton::B, (buttons & kN64ButtonB) != 0);
    set_button(widget, N64ControllerButton::ZTrigger, (buttons & kN64ButtonZ) != 0);
    set_button(widget, N64ControllerButton::Start, (buttons & kN64ButtonStart) != 0);
    set_button(widget, N64ControllerButton::DpadUp, (buttons & kN64ButtonDUp) != 0);
    set_button(widget, N64ControllerButton::DpadDown, (buttons & kN64ButtonDDown) != 0);
    set_button(widget, N64ControllerButton::DpadLeft, (buttons & kN64ButtonDLeft) != 0);
    set_button(widget, N64ControllerButton::DpadRight, (buttons & kN64ButtonDRight) != 0);
    set_button(widget, N64ControllerButton::LeftShoulder, (buttons & kN64ButtonL) != 0);
    set_button(widget, N64ControllerButton::RightShoulder, (buttons & kN64ButtonR) != 0);
    set_button(widget, N64ControllerButton::CButtonUp, (buttons & kN64ButtonCUp) != 0);
    set_button(widget, N64ControllerButton::CButtonDown, (buttons & kN64ButtonCDown) != 0);
    set_button(widget, N64ControllerButton::CButtonLeft, (buttons & kN64ButtonCLeft) != 0);
    set_button(widget, N64ControllerButton::CButtonRight, (buttons & kN64ButtonCRight) != 0);
}

QPushButton* add_mapping_row(QGridLayout* layout, ControllerPage* page, int row, int bindingIndex, QWidget* parent)
{
    const BindingTarget& target = kBindingTargets[static_cast<size_t>(bindingIndex)];
    auto* label = new QLabel(QString::fromLatin1(target.label), parent);
    auto* button = new QPushButton(parent);
    auto* clearButton = new QPushButton(parent);

    button->setMinimumWidth(128);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    clearButton->setText(QStringLiteral("Clear"));
    clearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    layout->addWidget(label, row, 0);
    layout->addWidget(button, row, 1);
    layout->addWidget(clearButton, row, 2);

    page->bindingButtons[bindingIndex] = button;
    page->clearButtons[bindingIndex] = clearButton;
    return button;
}

QWidget* create_slider_value_row(QWidget* parent, QSlider* slider, QLabel* valueLabel)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    valueLabel->setMinimumWidth(44);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);

    return row;
}

} // namespace

using namespace UserInterface::Dialog;

UnifiedInputDialog::UnifiedInputDialog(QWidget* parent, InputPluginType currentPlugin)
    : QDialog(parent),
      selectedPlugin(currentPlugin)
{
    this->setupUi();
    this->refreshDetection();
    this->setSelectedPlugin(currentPlugin);
}

UnifiedInputDialog::~UnifiedInputDialog(void)
{
    this->closePreviewSource();
    for (ControllerPage* page : this->controllerPages)
    {
        delete page;
    }
}

UnifiedInputDialog::InputPluginType UnifiedInputDialog::GetSelectedPlugin(void) const
{
    return this->selectedPlugin;
}

int UnifiedInputDialog::GetSelectedDeviceIndex(void) const
{
    if (this->controllerPages.isEmpty() || this->controllerPages[0]->deviceComboBox == nullptr)
    {
        return -1;
    }

    return this->controllerPages[0]->deviceComboBox->currentData().toInt();
}

void UnifiedInputDialog::setupUi(void)
{
    this->setWindowTitle(tr("Input Settings"));
    this->setMinimumSize(1120, 760);
    this->resize(1180, 860);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    this->tabWidget = new QTabWidget(this);
    for (int i = 0; i < 4; i++)
    {
        QWidget* pageWidget = this->createControllerPage(i);
        this->tabWidget->addTab(pageWidget, tr("Player %1").arg(i + 1));
    }
    mainLayout->addWidget(this->tabWidget, 1);

    this->debugDevicesCheckBox = new QCheckBox(tr("Show input detection debug"), this);
    mainLayout->addWidget(this->debugDevicesCheckBox);

    this->detectedDevicesGroupBox = new QGroupBox(tr("Detected devices used for recommendations"), this);
    auto* detectedLayout = new QVBoxLayout(this->detectedDevicesGroupBox);
    this->detectedDevicesPlainTextEdit = new QPlainTextEdit(this->detectedDevicesGroupBox);
    this->detectedDevicesPlainTextEdit->setReadOnly(true);
    this->detectedDevicesPlainTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    this->detectedDevicesPlainTextEdit->setMaximumHeight(88);
    detectedLayout->addWidget(this->detectedDevicesPlainTextEdit);
    this->detectedDevicesGroupBox->setVisible(false);
    mainLayout->addWidget(this->detectedDevicesGroupBox);

    auto* actionsLayout = new QHBoxLayout();
    this->refreshButton = new QPushButton(tr("Refresh Devices"), this);
    actionsLayout->addWidget(this->refreshButton);
    actionsLayout->addStretch();
    mainLayout->addLayout(actionsLayout);

    this->buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    if (QPushButton* okButton = this->buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(tr("Apply"));
    }
    mainLayout->addWidget(this->buttonBox);

    this->pollTimer = new QTimer(this);
    this->pollTimer->setInterval(16);

    connect(this->debugDevicesCheckBox, &QCheckBox::toggled, this->detectedDevicesGroupBox, &QGroupBox::setVisible);
    connect(this->refreshButton, &QPushButton::clicked, this, &UnifiedInputDialog::refreshDetection);
    connect(this->pollTimer, &QTimer::timeout, this, &UnifiedInputDialog::pollPreview);
    connect(this->tabWidget, &QTabWidget::currentChanged, this, [this](int)
    {
        this->openPreviewSource();
    });
    connect(this->buttonBox, &QDialogButtonBox::accepted, this, [this]()
    {
        this->saveAllSettings();
        this->accept();
    });
    connect(this->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this->buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
        &UnifiedInputDialog::restoreCurrentPageDefaults);

    this->setStyleSheet(
        "QLabel[warningBox=\"true\"] {"
        "  background: #fdeaea;"
        "  color: #7a1f1f;"
        "  border: 0;"
        "  border-radius: 4px;"
        "  padding: 8px 10px;"
        "}"
        "QGroupBox[plainSurface=\"true\"] {"
        "  border: 0;"
        "  margin-top: 0;"
        "}"
        "QGroupBox[plainSurface=\"true\"]::title {"
        "  height: 0;"
        "  color: transparent;"
        "}"
    );
}

QWidget* UnifiedInputDialog::createControllerPage(int playerIndex)
{
    auto* page = new ControllerPage();
    page->bindingButtons.resize(static_cast<int>(kBindingTargets.size()));
    page->clearButtons.resize(static_cast<int>(kBindingTargets.size()));
    page->usbBindings.resize(static_cast<int>(kBindingTargets.size()));
    page->gamecubeBindings.resize(static_cast<int>(kBindingTargets.size()));
    this->controllerPages.append(page);

    auto* root = new QWidget(this);
    page->widget = root;
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto* topLayout = new QHBoxLayout();
    page->portGroupBox = new QGroupBox(playerIndex == 0 ? tr("Controller") : tr("Player Port"), root);
    page->portGroupBox->setProperty("plainSurface", true);
    auto* portLayout = new QFormLayout(page->portGroupBox);
    portLayout->setContentsMargins(0, 0, 0, 0);

    if (playerIndex == 0)
    {
        page->backendComboBox = new QComboBox(page->portGroupBox);
        page->backendComboBox->setMaximumWidth(420);
        portLayout->addRow(tr("Controller:"), page->backendComboBox);
        connect(page->backendComboBox, &QComboBox::currentIndexChanged, this, [this, page](int)
        {
            if (page->backendComboBox->currentIndex() >= 0)
            {
                this->setSelectedPlugin(static_cast<InputPluginType>(page->backendComboBox->currentData().toInt()));
            }
        });
    }

    page->deviceComboBox = new QComboBox(page->portGroupBox);
    page->deviceComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    page->deviceComboBox->setMinimumContentsLength(24);
    page->deviceComboBox->setMaximumWidth(420);
    page->pluggedInCheckBox = new QCheckBox(tr("Enabled for this player"), page->portGroupBox);
    auto* deviceRowWidget = new QWidget(page->portGroupBox);
    auto* deviceRowLayout = new QHBoxLayout(deviceRowWidget);
    deviceRowLayout->setContentsMargins(0, 0, 0, 0);
    deviceRowLayout->setSpacing(12);
    deviceRowLayout->addWidget(page->deviceComboBox, 1);
    deviceRowLayout->addWidget(page->pluggedInCheckBox, 0);
    portLayout->addRow(playerIndex == 0 ? tr("Device / port:") : tr("Device:"), deviceRowWidget);
    connect(page->deviceComboBox, &QComboBox::currentIndexChanged, this, [this, playerIndex](int)
    {
        if (playerIndex == this->currentPageIndex())
        {
            this->openPreviewSource();
        }
        if (playerIndex == 0)
        {
            this->updateWarningLabel();
        }
    });

    page->portGroupBox->setMaximumWidth(560);
    topLayout->addWidget(page->portGroupBox, 0);
    if (playerIndex == 0)
    {
        this->warningLabel = new QLabel(root);
        this->warningLabel->setWordWrap(true);
        this->warningLabel->setProperty("warningBox", true);
        this->warningLabel->setMinimumWidth(420);
        this->warningLabel->setMaximumWidth(520);
        this->warningLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        this->warningLabel->setVisible(false);
        topLayout->addWidget(this->warningLabel, 0, Qt::AlignTop);
        QTimer::singleShot(0, this, [this, page]()
        {
            if (this->warningLabel != nullptr && page->portGroupBox != nullptr)
            {
                this->warningLabel->setFixedHeight(page->portGroupBox->height());
            }
        });
        topLayout->addStretch(1);
    }
    else
    {
        topLayout->addStretch(1);
    }
    rootLayout->addLayout(topLayout);

    page->mappingsGroupBox = new QGroupBox(tr("Controller bindings"), root);
    page->mappingsGroupBox->setProperty("plainSurface", true);
    auto* mappingsLayout = new QHBoxLayout(page->mappingsGroupBox);
    mappingsLayout->setContentsMargins(0, 0, 0, 0);
    auto* leftColumn = new QVBoxLayout();
    auto* centerColumn = new QVBoxLayout();
    auto* rightColumn = new QVBoxLayout();

    auto* dpadGroup = new QGroupBox(tr("Digital Pad"), page->mappingsGroupBox);
    auto* dpadLayout = new QGridLayout(dpadGroup);
    for (int row = 0; row < static_cast<int>(kDpadBindingIndexes.size()); row++)
    {
        add_mapping_row(dpadLayout, page, row, kDpadBindingIndexes[static_cast<size_t>(row)], dpadGroup);
    }
    leftColumn->addWidget(dpadGroup);

    auto* analogGroup = new QGroupBox(tr("Analog Stick"), page->mappingsGroupBox);
    auto* analogLayout = new QGridLayout(analogGroup);
    for (int row = 0; row < static_cast<int>(kAnalogBindingIndexes.size()); row++)
    {
        add_mapping_row(analogLayout, page, row, kAnalogBindingIndexes[static_cast<size_t>(row)], analogGroup);
    }
    leftColumn->addWidget(analogGroup);
    leftColumn->addStretch();

    auto* controllerPanel = new ControllerPreviewPanel(page->mappingsGroupBox);
    page->controllerImageWidget = controllerPanel->ControllerImage();

    auto* axisGroup = new QGroupBox(tr("Stick"), controllerPanel);
    axisGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    axisGroup->setMaximumWidth(120);
    auto* axisLayout = new QGridLayout(axisGroup);
    axisLayout->setHorizontalSpacing(8);
    axisLayout->setVerticalSpacing(4);
    page->axisXLabel = new QLabel(QStringLiteral("0"), axisGroup);
    page->axisYLabel = new QLabel(QStringLiteral("0"), axisGroup);
    page->axisXLabel->setMinimumWidth(24);
    page->axisYLabel->setMinimumWidth(24);
    axisLayout->addWidget(new QLabel(tr("X:"), axisGroup), 0, 0);
    axisLayout->addWidget(page->axisXLabel, 0, 1);
    axisLayout->addWidget(new QLabel(tr("Y:"), axisGroup), 1, 0);
    axisLayout->addWidget(page->axisYLabel, 1, 1);
    controllerPanel->SetOutputWidget(axisGroup);
    centerColumn->addWidget(controllerPanel, 1);

    auto* buttonGroup = new QGroupBox(tr("Buttons"), page->mappingsGroupBox);
    auto* buttonLayout = new QGridLayout(buttonGroup);
    for (int row = 0; row < static_cast<int>(kButtonBindingIndexes.size()); row++)
    {
        add_mapping_row(buttonLayout, page, row, kButtonBindingIndexes[static_cast<size_t>(row)], buttonGroup);
    }
    rightColumn->addWidget(buttonGroup);

    auto* cButtonGroup = new QGroupBox(tr("C Buttons"), page->mappingsGroupBox);
    auto* cButtonLayout = new QGridLayout(cButtonGroup);
    for (int row = 0; row < static_cast<int>(kCButtonBindingIndexes.size()); row++)
    {
        add_mapping_row(cButtonLayout, page, row, kCButtonBindingIndexes[static_cast<size_t>(row)], cButtonGroup);
    }
    rightColumn->addWidget(cButtonGroup);
    rightColumn->addStretch();

    mappingsLayout->addLayout(leftColumn, 1);
    mappingsLayout->addLayout(centerColumn, 2);
    mappingsLayout->addLayout(rightColumn, 1);
    rootLayout->addWidget(page->mappingsGroupBox, 1);

    page->usbStickGroupBox = new QGroupBox(tr("Stick Settings"), page->mappingsGroupBox);
    page->usbStickGroupBox->setMaximumWidth(560);
    auto* usbStickLayout = new QFormLayout(page->usbStickGroupBox);
    page->usbDeadzoneSlider = new QSlider(Qt::Horizontal, page->usbStickGroupBox);
    page->usbDeadzoneSlider->setRange(0, 100);
    page->usbDeadzoneSlider->setTickInterval(10);
    page->usbDeadzoneSlider->setTickPosition(QSlider::TicksBelow);
    page->usbRangeSlider = new QSlider(Qt::Horizontal, page->usbStickGroupBox);
    page->usbRangeSlider->setRange(0, 200);
    page->usbRangeSlider->setTickInterval(10);
    page->usbRangeSlider->setTickPosition(QSlider::TicksBelow);
    page->realN64RangeCheckBox = new QCheckBox(tr("Use real N64 stick range"), page->usbStickGroupBox);
    page->usbDeadzoneValueLabel = new QLabel(page->usbStickGroupBox);
    page->usbRangeValueLabel = new QLabel(page->usbStickGroupBox);
    usbStickLayout->addRow(tr("Deadzone:"), create_slider_value_row(page->usbStickGroupBox,
        page->usbDeadzoneSlider, page->usbDeadzoneValueLabel));
    usbStickLayout->addRow(tr("Range:"), create_slider_value_row(page->usbStickGroupBox,
        page->usbRangeSlider, page->usbRangeValueLabel));
    usbStickLayout->addRow(QString(), page->realN64RangeCheckBox);
    leftColumn->insertWidget(2, page->usbStickGroupBox, 0, Qt::AlignLeft);

    page->gamecubeStickGroupBox = new QGroupBox(tr("Stick Settings"), page->mappingsGroupBox);
    page->gamecubeStickGroupBox->setMaximumWidth(560);
    auto* gamecubeStickLayout = new QFormLayout(page->gamecubeStickGroupBox);
    page->gamecubeDeadzoneSlider = new QSlider(Qt::Horizontal, page->gamecubeStickGroupBox);
    page->gamecubeDeadzoneSlider->setRange(0, 100);
    page->gamecubeSensitivitySlider = new QSlider(Qt::Horizontal, page->gamecubeStickGroupBox);
    page->gamecubeSensitivitySlider->setRange(0, 200);
    page->gamecubeDeadzoneValueLabel = new QLabel(page->gamecubeStickGroupBox);
    page->gamecubeSensitivityValueLabel = new QLabel(page->gamecubeStickGroupBox);
    gamecubeStickLayout->addRow(tr("Deadzone:"), create_slider_value_row(page->gamecubeStickGroupBox,
        page->gamecubeDeadzoneSlider, page->gamecubeDeadzoneValueLabel));
    gamecubeStickLayout->addRow(tr("Stick sensitivity:"), create_slider_value_row(page->gamecubeStickGroupBox,
        page->gamecubeSensitivitySlider, page->gamecubeSensitivityValueLabel));
    leftColumn->insertWidget(3, page->gamecubeStickGroupBox, 0, Qt::AlignLeft);

    page->gamecubeTriggerGroupBox = new QGroupBox(tr("Trigger Settings"), page->mappingsGroupBox);
    page->gamecubeTriggerGroupBox->setMaximumWidth(560);
    auto* gamecubeTriggerLayout = new QFormLayout(page->gamecubeTriggerGroupBox);
    auto* leftTriggerModeWidget = new QWidget(page->gamecubeTriggerGroupBox);
    auto* leftTriggerModeLayout = new QHBoxLayout(leftTriggerModeWidget);
    leftTriggerModeLayout->setContentsMargins(0, 0, 0, 0);
    leftTriggerModeLayout->setSpacing(12);
    page->gamecubeLeftTriggerDigitalRadioButton = new QRadioButton(tr("Digital"), leftTriggerModeWidget);
    page->gamecubeLeftTriggerAnalogRadioButton = new QRadioButton(tr("Analog"), leftTriggerModeWidget);
    leftTriggerModeLayout->addWidget(page->gamecubeLeftTriggerDigitalRadioButton);
    leftTriggerModeLayout->addWidget(page->gamecubeLeftTriggerAnalogRadioButton);
    leftTriggerModeLayout->addStretch(1);

    auto* rightTriggerModeWidget = new QWidget(page->gamecubeTriggerGroupBox);
    auto* rightTriggerModeLayout = new QHBoxLayout(rightTriggerModeWidget);
    rightTriggerModeLayout->setContentsMargins(0, 0, 0, 0);
    rightTriggerModeLayout->setSpacing(12);
    page->gamecubeRightTriggerDigitalRadioButton = new QRadioButton(tr("Digital"), rightTriggerModeWidget);
    page->gamecubeRightTriggerAnalogRadioButton = new QRadioButton(tr("Analog"), rightTriggerModeWidget);
    rightTriggerModeLayout->addWidget(page->gamecubeRightTriggerDigitalRadioButton);
    rightTriggerModeLayout->addWidget(page->gamecubeRightTriggerAnalogRadioButton);
    rightTriggerModeLayout->addStretch(1);

    page->gamecubeTriggerThresholdSlider = new QSlider(Qt::Horizontal, page->gamecubeTriggerGroupBox);
    page->gamecubeTriggerThresholdSlider->setRange(0, 100);
    page->gamecubeTriggerThresholdValueLabel = new QLabel(page->gamecubeTriggerGroupBox);
    gamecubeTriggerLayout->addRow(tr("L trigger:"), leftTriggerModeWidget);
    gamecubeTriggerLayout->addRow(tr("R trigger:"), rightTriggerModeWidget);
    gamecubeTriggerLayout->addRow(tr("Trigger threshold:"), create_slider_value_row(page->gamecubeTriggerGroupBox,
        page->gamecubeTriggerThresholdSlider, page->gamecubeTriggerThresholdValueLabel));
    leftColumn->insertWidget(4, page->gamecubeTriggerGroupBox, 0, Qt::AlignLeft);

    page->statusLabel = new QLabel(root);
    page->statusLabel->setWordWrap(true);
    page->statusLabel->setVisible(false);
    rootLayout->addWidget(page->statusLabel);

    for (int i = 0; i < static_cast<int>(page->bindingButtons.size()); i++)
    {
        connect(page->bindingButtons[i], &QPushButton::clicked, this, [this, playerIndex, i]()
        {
            this->startListeningForBinding(playerIndex, i);
        });
        connect(page->clearButtons[i], &QPushButton::clicked, this, [this, playerIndex, i]()
        {
            this->clearBinding(playerIndex, i);
        });
    }

    auto updateSliders = [this, playerIndex](int)
    {
        this->updateSliderLabels(playerIndex);
    };
    connect(page->usbDeadzoneSlider, &QSlider::valueChanged, this, updateSliders);
    connect(page->usbRangeSlider, &QSlider::valueChanged, this, updateSliders);
    connect(page->realN64RangeCheckBox, &QCheckBox::toggled, this, [this, playerIndex](bool checked)
    {
        ControllerPage* page = this->controllerPages[playerIndex];
        page->usbRangeSlider->setEnabled(!checked);
        this->updateSliderLabels(playerIndex);
    });
    connect(page->gamecubeDeadzoneSlider, &QSlider::valueChanged, this, updateSliders);
    connect(page->gamecubeSensitivitySlider, &QSlider::valueChanged, this, updateSliders);
    connect(page->gamecubeTriggerThresholdSlider, &QSlider::valueChanged, this, updateSliders);
    connect(page->gamecubeLeftTriggerAnalogRadioButton, &QRadioButton::toggled, this, [this, playerIndex](bool checked)
    {
        if (playerIndex == 0)
        {
            this->setGamecubeTriggerAnalog(true, checked);
        }
    });
    connect(page->gamecubeRightTriggerAnalogRadioButton, &QRadioButton::toggled, this, [this, playerIndex](bool checked)
    {
        if (playerIndex == 0)
        {
            this->setGamecubeTriggerAnalog(false, checked);
        }
    });

    this->loadPageSettings(playerIndex);
    return root;
}

void UnifiedInputDialog::refreshDetection(void)
{
    this->closePreviewSource();
    this->keyboardState.clear();
    this->detectionReport = UnifiedInputDialog::ScanInputDevices();
    this->refreshUsbDevices();

    if (this->detectedDevicesPlainTextEdit != nullptr)
    {
        this->detectedDevicesPlainTextEdit->setPlainText(this->detectionReport.lines.join(QStringLiteral("\n")));
    }

    this->updateAllPages();
    this->openPreviewSource();
}

void UnifiedInputDialog::updateWarningLabel(void)
{
    if (this->warningLabel == nullptr)
    {
        return;
    }

    QStringList warnings;
    auto addWarning = [&warnings](const QString& warning)
    {
        if (!warning.isEmpty() && !warnings.contains(warning))
        {
            warnings.append(warning);
        }
    };

    if (this->detectionReport.foundBlockedNativeGamecube)
    {
        addWarning(tr("Native GameCube adapter detected, but driver is missing."));
    }

    if (this->detectionReport.foundUsbModeMayflash)
    {
        addWarning(tr("Mayflash GameCube adapter detected in USB mode. Switch it to Wii U/NS (native) mode for better support."));
    }

    if (this->selectedPlugin == InputPluginType::Gamecube && this->gamecubeSelectedPortMissingController)
    {
        addWarning(tr("GameCube controller selected, but no controller answered on the selected adapter port."));
    }

    if (this->selectedPlugin == InputPluginType::USB &&
        !this->controllerPages.isEmpty() &&
        this->controllerPages[0]->deviceComboBox != nullptr)
    {
        const int deviceIndex = this->controllerPages[0]->deviceComboBox->currentData().toInt();
        if (deviceIndex >= 0 && deviceIndex < static_cast<int>(this->usbDevices.size()))
        {
            const UsbDeviceChoice& device = this->usbDevices[deviceIndex];
            if (device.type == InputDeviceType::Joystick && usb_device_is_raphnet(device))
            {
                addWarning(tr("Raphnet adapter is selected as Other USB. Use N64 Controller (Raphnet) for better support."));
            }
            else if (device.type == InputDeviceType::Joystick && usb_device_is_mayflash_gamecube(device))
            {
                addWarning(tr("Mayflash GameCube adapter detected in USB mode. Switch it to Wii U/NS (native) mode for better support."));
            }
            else if (device.type == InputDeviceType::Joystick &&
                     this->detectionReport.foundNativeGamecube &&
                     usb_device_is_gamecube_like(device))
            {
                addWarning(tr("GameCube adapter is selected as Other USB. Use Gamecube Controller (Native) for better support."));
            }
        }
    }

    this->warningLabel->setText(warnings.join(QStringLiteral("\n")));
    this->warningLabel->setVisible(!warnings.isEmpty());
}

void UnifiedInputDialog::refreshUsbDevices(void)
{
    this->usbDevices.clear();
    this->usbDevices.append({ InputDeviceType::None, 0, false, QStringLiteral("None"), QString(), QString(), 0, 0 });
    this->usbDevices.append({ InputDeviceType::Keyboard, 0, false, QStringLiteral("Keyboard"), QString(), QString(), 0, 0 });

    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        return;
    }

    SDL_UpdateJoysticks();

    int joystickCount = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&joystickCount);
    for (int i = 0; i < joystickCount; i++)
    {
        const SDL_JoystickID joystickId = joysticks[i];
        const bool isGamepad = SDL_IsGamepad(joystickId);
        const char* name = isGamepad ? SDL_GetGamepadNameForID(joystickId) : SDL_GetJoystickNameForID(joystickId);
        const char* path = isGamepad ? SDL_GetGamepadPathForID(joystickId) : SDL_GetJoystickPathForID(joystickId);
        const uint16_t vendorId = isGamepad ? SDL_GetGamepadVendorForID(joystickId) : SDL_GetJoystickVendorForID(joystickId);
        const uint16_t productId = isGamepad ? SDL_GetGamepadProductForID(joystickId) : SDL_GetJoystickProductForID(joystickId);

        this->usbDevices.append({
            InputDeviceType::Joystick,
            joystickId,
            isGamepad,
            string_from_const_char(name),
            string_from_const_char(path),
            QString(),
            vendorId,
            productId
        });
    }

    if (joysticks != nullptr)
    {
        SDL_free(joysticks);
    }
}

void UnifiedInputDialog::updateAllPages(void)
{
    for (int i = 0; i < static_cast<int>(this->controllerPages.size()); i++)
    {
        this->updatePageMode(i);
    }
    this->updateWarningLabel();
}

bool UnifiedInputDialog::isPluginAvailable(InputPluginType plugin) const
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

UnifiedInputDialog::InputPluginType UnifiedInputDialog::availablePluginOrFallback(InputPluginType plugin) const
{
    if (this->isPluginAvailable(plugin))
    {
        return plugin;
    }

    const Recommendation recommendation = UnifiedInputDialog::DetectRecommendedPlugin(this->detectionReport);
    if (recommendation.hasRecommendation && this->isPluginAvailable(recommendation.plugin))
    {
        return recommendation.plugin;
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

void UnifiedInputDialog::updateBackendChoices(void)
{
    if (this->controllerPages.isEmpty() || this->controllerPages[0]->backendComboBox == nullptr)
    {
        return;
    }

    QComboBox* comboBox = this->controllerPages[0]->backendComboBox;
    QSignalBlocker blocker(comboBox);
    comboBox->clear();

    if (this->detectionReport.foundRaphnet)
    {
        comboBox->addItem(tr("N64 Controller (Raphnet)"), static_cast<int>(InputPluginType::Raphnet));
    }
    if (this->detectionReport.foundNativeGamecube)
    {
        comboBox->addItem(tr("Gamecube Controller (Native)"), static_cast<int>(InputPluginType::Gamecube));
    }
    comboBox->addItem(tr("Other USB controller"), static_cast<int>(InputPluginType::USB));

    this->selectedPlugin = this->availablePluginOrFallback(this->selectedPlugin);
    const int targetIndex = comboBox->findData(static_cast<int>(this->selectedPlugin));
    comboBox->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
}

void UnifiedInputDialog::updatePageMode(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];

    if (pageIndex == 0 && page->backendComboBox != nullptr)
    {
        this->updateBackendChoices();
    }

    this->updatePageDeviceChoices(pageIndex);

    const bool usbMode = this->selectedPlugin == InputPluginType::USB;
    const bool gamecubeMode = this->selectedPlugin == InputPluginType::Gamecube;

    page->mappingsGroupBox->setVisible(true);
    page->usbStickGroupBox->setVisible(usbMode);
    page->gamecubeStickGroupBox->setVisible(gamecubeMode && pageIndex == 0);
    page->gamecubeTriggerGroupBox->setVisible(gamecubeMode && pageIndex == 0);
    page->pluggedInCheckBox->setVisible(pageIndex > 0 && (usbMode || gamecubeMode));
    if (pageIndex == 0)
    {
        page->pluggedInCheckBox->setChecked(true);
    }

    if (usbMode)
    {
        page->portGroupBox->setTitle(pageIndex == 0 ? tr("Controller") : tr("Input Device"));
    }
    else if (gamecubeMode)
    {
        page->portGroupBox->setTitle(pageIndex == 0 ? tr("Controller") : tr("Adapter Port"));
    }
    else
    {
        page->portGroupBox->setTitle(pageIndex == 0 ? tr("Controller") : tr("Adapter Port"));
    }

    for (int i = 0; i < static_cast<int>(page->bindingButtons.size()); i++)
    {
        const bool analogBinding = kBindingTargets[static_cast<size_t>(i)].axisXDirection != 0 ||
            kBindingTargets[static_cast<size_t>(i)].axisYDirection != 0;
        const bool enabled = usbMode || (gamecubeMode && pageIndex == 0 && !analogBinding);
        page->bindingButtons[i]->setEnabled(enabled);
        page->clearButtons[i]->setEnabled(enabled);
    }

    this->updatePageBindingButtons(pageIndex);
    this->updateSliderLabels(pageIndex);
}

void UnifiedInputDialog::updatePageDeviceChoices(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    const QVariant oldData = page->deviceComboBox->currentData();
    QSignalBlocker blocker(page->deviceComboBox);
    page->deviceComboBox->clear();

    if (this->selectedPlugin == InputPluginType::USB)
    {
        for (int i = 0; i < static_cast<int>(this->usbDevices.size()); i++)
        {
            const UsbDeviceChoice& device = this->usbDevices[i];
            if (pageIndex == 0 && device.type == InputDeviceType::None)
            {
                continue;
            }

            page->deviceComboBox->addItem(device.name.isEmpty() ? tr("Unnamed SDL device") : device.name, i);
        }

        if (oldData.isValid())
        {
            const int targetIndex = page->deviceComboBox->findData(oldData);
            if (targetIndex >= 0)
            {
                page->deviceComboBox->setCurrentIndex(targetIndex);
                return;
            }
        }

        const std::string section = usb_profile_section(pageIndex);
        if (CoreSettingsSectionExists(section))
        {
            const QString savedName = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Input_DeviceName, section));
            const QString savedPath = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Input_DevicePath, section));
            const InputDeviceType savedType = static_cast<InputDeviceType>(CoreSettingsGetIntValue(SettingsID::Input_DeviceType, section));
            for (int i = 0; i < static_cast<int>(this->usbDevices.size()); i++)
            {
                const UsbDeviceChoice& device = this->usbDevices[i];
                if (device.type == savedType && device.name == savedName &&
                    (savedPath.isEmpty() || device.path == savedPath))
                {
                    const int targetIndex = page->deviceComboBox->findData(i);
                    if (targetIndex >= 0)
                    {
                        page->deviceComboBox->setCurrentIndex(targetIndex);
                        return;
                    }
                }
            }
        }

        if (pageIndex == 0)
        {
            for (int i = 0; i < page->deviceComboBox->count(); i++)
            {
                const int deviceIndex = page->deviceComboBox->itemData(i).toInt();
                if (deviceIndex >= 0 &&
                    deviceIndex < static_cast<int>(this->usbDevices.size()) &&
                    this->usbDevices[deviceIndex].type == InputDeviceType::Joystick)
                {
                    page->deviceComboBox->setCurrentIndex(i);
                    return;
                }
            }
        }

        page->deviceComboBox->setCurrentIndex(0);
        return;
    }

    const int portCount = this->selectedPlugin == InputPluginType::Raphnet ? std::max(1, this->raphnetChannelCount) : 4;
    for (int i = 0; i < portCount; i++)
    {
        page->deviceComboBox->addItem(tr("Adapter port %1").arg(i + 1), i);
    }

    if (oldData.isValid())
    {
        const int targetIndex = page->deviceComboBox->findData(oldData);
        if (targetIndex >= 0)
        {
            page->deviceComboBox->setCurrentIndex(targetIndex);
            return;
        }
    }

    if (this->selectedPlugin == InputPluginType::Raphnet && pageIndex == 0)
    {
        const int configuredPort = std::clamp(CoreSettingsGetIntValue(SettingsID::RaphnetRaw_Player1AdapterPort), 1, portCount);
        page->deviceComboBox->setCurrentIndex(configuredPort - 1);
        return;
    }

    page->deviceComboBox->setCurrentIndex(std::min(pageIndex, portCount - 1));
}

void UnifiedInputDialog::updatePageBindingButtons(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    const bool usbMode = this->selectedPlugin == InputPluginType::USB;
    const bool gamecubeMode = this->selectedPlugin == InputPluginType::Gamecube;

    for (int i = 0; i < static_cast<int>(page->bindingButtons.size()); i++)
    {
        if (this->listeningPageIndex == pageIndex && this->listeningBindingIndex == i)
        {
            page->bindingButtons[i]->setText(tr("Press input..."));
            continue;
        }

        if (usbMode)
        {
            page->bindingButtons[i]->setText(binding_text(page->usbBindings[i]));
        }
        else if (gamecubeMode && kBindingTargets[static_cast<size_t>(i)].hasGamecubeMapping)
        {
            page->bindingButtons[i]->setText(gc_input_to_string(static_cast<GCInput>(page->gamecubeBindings[i])));
        }
        else
        {
            page->bindingButtons[i]->setText(tr("Fixed"));
        }
    }
}

void UnifiedInputDialog::updateSliderLabels(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    if (page->usbDeadzoneValueLabel != nullptr && page->usbRangeValueLabel != nullptr)
    {
        page->usbDeadzoneValueLabel->setText(tr("%1%").arg(page->usbDeadzoneSlider->value()));
        page->usbRangeValueLabel->setText(tr("%1%")
            .arg(page->realN64RangeCheckBox->isChecked() ? 100 : page->usbRangeSlider->value()));
    }

    if (page->gamecubeDeadzoneValueLabel != nullptr &&
        page->gamecubeSensitivityValueLabel != nullptr &&
        page->gamecubeTriggerThresholdValueLabel != nullptr)
    {
        page->gamecubeDeadzoneValueLabel->setText(tr("%1%").arg(page->gamecubeDeadzoneSlider->value()));
        page->gamecubeSensitivityValueLabel->setText(tr("%1%").arg(page->gamecubeSensitivitySlider->value()));
        page->gamecubeTriggerThresholdValueLabel->setText(tr("%1%").arg(page->gamecubeTriggerThresholdSlider->value()));
    }
}

void UnifiedInputDialog::setSelectedPlugin(InputPluginType plugin)
{
    this->selectedPlugin = this->availablePluginOrFallback(plugin);
    this->stopListeningForBinding(true);
    for (int i = 0; i < static_cast<int>(this->controllerPages.size()); i++)
    {
        this->loadPageSettings(i);
    }
    this->updateAllPages();
    this->openPreviewSource();
}

int UnifiedInputDialog::currentPageIndex(void) const
{
    if (this->tabWidget == nullptr || this->controllerPages.isEmpty())
    {
        return 0;
    }

    return std::clamp(this->tabWidget->currentIndex(), 0, static_cast<int>(this->controllerPages.size()) - 1);
}

void UnifiedInputDialog::loadPageSettings(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    const std::string section = usb_profile_section(pageIndex);
    const bool usbSectionExists = CoreSettingsSectionExists(section);

    if (usbSectionExists)
    {
        page->pluggedInCheckBox->setChecked(pageIndex == 0 ||
            CoreSettingsGetBoolValue(SettingsID::Input_PluggedIn, section));
        page->usbDeadzoneSlider->setValue(CoreSettingsGetIntValue(SettingsID::Input_Deadzone, section));
        page->usbRangeSlider->setValue(CoreSettingsGetIntValue(SettingsID::Input_Range, section));
        page->realN64RangeCheckBox->setChecked(CoreSettingsGetBoolValue(SettingsID::Input_RealN64Range, section));
    }
    else
    {
        page->pluggedInCheckBox->setChecked(pageIndex == 0);
        page->usbDeadzoneSlider->setValue(9);
        page->usbRangeSlider->setValue(66);
        page->realN64RangeCheckBox->setChecked(true);
    }

    page->usbRangeSlider->setEnabled(!page->realN64RangeCheckBox->isChecked());

    for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
    {
        if (usbSectionExists)
        {
            page->usbBindings[i] = load_usb_binding(kBindingTargets[static_cast<size_t>(i)], section);
        }
        else
        {
            clear_binding(page->usbBindings[i]);
        }
        if (!kBindingTargets[static_cast<size_t>(i)].hasGamecubeMapping)
        {
            page->gamecubeBindings[i] = static_cast<int>(GCInput::None);
            continue;
        }

        page->gamecubeBindings[i] = CoreSettingsGetIntValue(kBindingTargets[static_cast<size_t>(i)].gamecubeMapping);
    }

    page->gamecubeDeadzoneSlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_Deadzone));
    page->gamecubeSensitivitySlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_Sensitivity));
    page->gamecubeTriggerThresholdSlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_TriggerTreshold));
    const bool leftTriggerAnalog = CoreSettingsGetBoolValue(SettingsID::GCAInput_LeftTriggerAnalog);
    const bool rightTriggerAnalog = CoreSettingsGetBoolValue(SettingsID::GCAInput_RightTriggerAnalog);
    {
        QSignalBlocker leftDigitalBlocker(page->gamecubeLeftTriggerDigitalRadioButton);
        QSignalBlocker leftAnalogBlocker(page->gamecubeLeftTriggerAnalogRadioButton);
        QSignalBlocker rightDigitalBlocker(page->gamecubeRightTriggerDigitalRadioButton);
        QSignalBlocker rightAnalogBlocker(page->gamecubeRightTriggerAnalogRadioButton);
        page->gamecubeLeftTriggerDigitalRadioButton->setChecked(!leftTriggerAnalog);
        page->gamecubeLeftTriggerAnalogRadioButton->setChecked(leftTriggerAnalog);
        page->gamecubeRightTriggerDigitalRadioButton->setChecked(!rightTriggerAnalog);
        page->gamecubeRightTriggerAnalogRadioButton->setChecked(rightTriggerAnalog);
    }
    apply_gamecube_trigger_mode(page->gamecubeBindings, true, leftTriggerAnalog);
    apply_gamecube_trigger_mode(page->gamecubeBindings, false, rightTriggerAnalog);

    if (this->selectedPlugin == InputPluginType::Gamecube)
    {
        const std::array<SettingsID, 4> portSettings = {{
            SettingsID::GCAInput_Port1Enabled,
            SettingsID::GCAInput_Port2Enabled,
            SettingsID::GCAInput_Port3Enabled,
            SettingsID::GCAInput_Port4Enabled
        }};
        page->pluggedInCheckBox->setChecked(pageIndex == 0 ||
            CoreSettingsGetBoolValue(portSettings[static_cast<size_t>(pageIndex)]));
    }

    this->updatePageBindingButtons(pageIndex);
    this->updateSliderLabels(pageIndex);
}

void UnifiedInputDialog::saveAllSettings(void)
{
    if (this->selectedPlugin == InputPluginType::USB)
    {
        for (int i = 0; i < static_cast<int>(this->controllerPages.size()); i++)
        {
            this->saveUsbSettings(i);
        }
    }
    else if (this->selectedPlugin == InputPluginType::Gamecube)
    {
        this->saveGamecubeSettings();
    }
    else if (this->selectedPlugin == InputPluginType::Raphnet)
    {
        this->saveRaphnetSettings();
    }

    CoreSettingsSave();
}

void UnifiedInputDialog::saveUsbSettings(int pageIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    const std::string section = usb_profile_section(pageIndex);
    const int deviceIndex = page->deviceComboBox->currentData().toInt();
    UsbDeviceChoice device;
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(this->usbDevices.size()))
    {
        device = this->usbDevices[deviceIndex];
    }

    CoreSettingsSetValue(SettingsID::Input_UseProfile, section, std::string(""));
    if (pageIndex == 0 && device.type == InputDeviceType::None)
    {
        for (const UsbDeviceChoice& fallbackDevice : this->usbDevices)
        {
            if (fallbackDevice.type == InputDeviceType::Joystick)
            {
                device = fallbackDevice;
                break;
            }
        }
        if (device.type == InputDeviceType::None)
        {
            for (const UsbDeviceChoice& fallbackDevice : this->usbDevices)
            {
                if (fallbackDevice.type == InputDeviceType::Keyboard)
                {
                    device = fallbackDevice;
                    break;
                }
            }
        }
    }

    const bool pluggedIn = pageIndex == 0 ||
        (page->pluggedInCheckBox->isChecked() && device.type != InputDeviceType::None);
    CoreSettingsSetValue(SettingsID::Input_PluggedIn, section, pluggedIn);
    CoreSettingsSetValue(SettingsID::Input_DeviceName, section, device.name.toStdString());
    CoreSettingsSetValue(SettingsID::Input_DeviceType, section, static_cast<int>(device.type));
    CoreSettingsSetValue(SettingsID::Input_DevicePath, section, device.path.toStdString());
    CoreSettingsSetValue(SettingsID::Input_DeviceSerial, section, device.serial.toStdString());
    CoreSettingsSetValue(SettingsID::Input_Deadzone, section, page->usbDeadzoneSlider->value());
    CoreSettingsSetValue(SettingsID::Input_Range, section, page->usbRangeSlider->value());
    CoreSettingsSetValue(SettingsID::Input_RealN64Range, section, page->realN64RangeCheckBox->isChecked());
    CoreSettingsSetValue(SettingsID::Input_Pak, section, static_cast<int>(N64ControllerPak::MemoryPak));
    CoreSettingsSetValue(SettingsID::Input_RemoveDuplicateMappings, section, true);
    CoreSettingsSetValue(SettingsID::Input_FilterEventsForButtons, section, true);
    CoreSettingsSetValue(SettingsID::Input_FilterEventsForAxis, section, true);

    for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
    {
        const BindingTarget& target = kBindingTargets[static_cast<size_t>(i)];
        const BindingValue& binding = page->usbBindings[i];
        CoreSettingsSetValue(target.usbInputType, section, to_std_vector(binding.types));
        CoreSettingsSetValue(target.usbName, section, to_std_string_vector(binding.text));
        CoreSettingsSetValue(target.usbData, section, to_std_vector(binding.data));
        CoreSettingsSetValue(target.usbExtraData, section, to_std_vector(binding.extraData));
    }
}

void UnifiedInputDialog::saveGamecubeSettings(void)
{
    ControllerPage* page = this->controllerPages[0];
    CoreSettingsSetValue(SettingsID::GCAInput_Deadzone, page->gamecubeDeadzoneSlider->value());
    CoreSettingsSetValue(SettingsID::GCAInput_Sensitivity, page->gamecubeSensitivitySlider->value());
    CoreSettingsSetValue(SettingsID::GCAInput_TriggerTreshold, page->gamecubeTriggerThresholdSlider->value());
    CoreSettingsSetValue(SettingsID::GCAInput_LeftTriggerAnalog, page->gamecubeLeftTriggerAnalogRadioButton->isChecked());
    CoreSettingsSetValue(SettingsID::GCAInput_RightTriggerAnalog, page->gamecubeRightTriggerAnalogRadioButton->isChecked());

    const std::array<SettingsID, 4> portSettings = {{
        SettingsID::GCAInput_Port1Enabled,
        SettingsID::GCAInput_Port2Enabled,
        SettingsID::GCAInput_Port3Enabled,
        SettingsID::GCAInput_Port4Enabled
    }};
    std::array<bool, 4> enabledPorts = {{ false, false, false, false }};
    for (int i = 0; i < static_cast<int>(this->controllerPages.size()); i++)
    {
        ControllerPage* playerPage = this->controllerPages[i];
        if (i > 0 && !playerPage->pluggedInCheckBox->isChecked())
        {
            continue;
        }

        const int port = std::clamp(playerPage->deviceComboBox->currentData().toInt(), 0, 3);
        enabledPorts[static_cast<size_t>(port)] = true;
    }

    for (int i = 0; i < 4; i++)
    {
        CoreSettingsSetValue(portSettings[static_cast<size_t>(i)], enabledPorts[static_cast<size_t>(i)]);
    }

    for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
    {
        const BindingTarget& target = kBindingTargets[static_cast<size_t>(i)];
        if (!target.hasGamecubeMapping)
        {
            continue;
        }

        CoreSettingsSetValue(target.gamecubeMapping, page->gamecubeBindings[i]);
    }
}

void UnifiedInputDialog::saveRaphnetSettings(void)
{
    if (this->controllerPages.isEmpty())
    {
        return;
    }

    const int port = std::max(0, this->controllerPages[0]->deviceComboBox->currentData().toInt());
    CoreSettingsSetValue(SettingsID::RaphnetRaw_Player1AdapterPort, port + 1);
}

void UnifiedInputDialog::restoreCurrentPageDefaults(void)
{
    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];

    this->stopListeningForBinding(true);

    if (this->selectedPlugin == InputPluginType::USB)
    {
        page->pluggedInCheckBox->setChecked(pageIndex == 0);
        page->usbDeadzoneSlider->setValue(9);
        page->usbRangeSlider->setValue(66);
        page->realN64RangeCheckBox->setChecked(true);
        for (BindingValue& binding : page->usbBindings)
        {
            clear_binding(binding);
        }
    }
    else if (this->selectedPlugin == InputPluginType::Gamecube && pageIndex == 0)
    {
        page->gamecubeDeadzoneSlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_Deadzone));
        page->gamecubeSensitivitySlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_Sensitivity));
        page->gamecubeTriggerThresholdSlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_TriggerTreshold));
        const bool leftTriggerAnalog = CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_LeftTriggerAnalog);
        const bool rightTriggerAnalog = CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_RightTriggerAnalog);
        {
            QSignalBlocker leftDigitalBlocker(page->gamecubeLeftTriggerDigitalRadioButton);
            QSignalBlocker leftAnalogBlocker(page->gamecubeLeftTriggerAnalogRadioButton);
            QSignalBlocker rightDigitalBlocker(page->gamecubeRightTriggerDigitalRadioButton);
            QSignalBlocker rightAnalogBlocker(page->gamecubeRightTriggerAnalogRadioButton);
            page->gamecubeLeftTriggerDigitalRadioButton->setChecked(!leftTriggerAnalog);
            page->gamecubeLeftTriggerAnalogRadioButton->setChecked(leftTriggerAnalog);
            page->gamecubeRightTriggerDigitalRadioButton->setChecked(!rightTriggerAnalog);
            page->gamecubeRightTriggerAnalogRadioButton->setChecked(rightTriggerAnalog);
        }
        for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
        {
            const BindingTarget& target = kBindingTargets[static_cast<size_t>(i)];
            page->gamecubeBindings[i] = target.hasGamecubeMapping ?
                CoreSettingsGetDefaultIntValue(target.gamecubeMapping) :
                static_cast<int>(GCInput::None);
        }
        apply_gamecube_trigger_mode(page->gamecubeBindings, true, leftTriggerAnalog);
        apply_gamecube_trigger_mode(page->gamecubeBindings, false, rightTriggerAnalog);
    }

    this->updatePageBindingButtons(pageIndex);
    this->updateSliderLabels(pageIndex);
}

void UnifiedInputDialog::startListeningForBinding(int pageIndex, int bindingIndex)
{
    if (this->selectedPlugin == InputPluginType::Raphnet)
    {
        return;
    }

    if (this->selectedPlugin == InputPluginType::Gamecube &&
        (pageIndex != 0 || !kBindingTargets[static_cast<size_t>(bindingIndex)].hasGamecubeMapping))
    {
        return;
    }

    this->stopListeningForBinding(true);
    this->listeningPageIndex = pageIndex;
    this->listeningBindingIndex = bindingIndex;
    this->listeningTicks = 0;
    this->updatePageBindingButtons(pageIndex);
    this->openPreviewSource();
}

void UnifiedInputDialog::stopListeningForBinding(bool restoreText)
{
    const int oldPageIndex = this->listeningPageIndex;
    this->listeningPageIndex = -1;
    this->listeningBindingIndex = -1;
    this->listeningTicks = 0;

    if (restoreText && oldPageIndex >= 0 && oldPageIndex < static_cast<int>(this->controllerPages.size()))
    {
        this->updatePageBindingButtons(oldPageIndex);
    }
}

void UnifiedInputDialog::clearBinding(int pageIndex, int bindingIndex)
{
    ControllerPage* page = this->controllerPages[pageIndex];
    if (this->selectedPlugin == InputPluginType::USB)
    {
        clear_binding(page->usbBindings[bindingIndex]);
    }
    else if (this->selectedPlugin == InputPluginType::Gamecube &&
             kBindingTargets[static_cast<size_t>(bindingIndex)].hasGamecubeMapping)
    {
        page->gamecubeBindings[bindingIndex] = static_cast<int>(GCInput::None);
    }

    this->updatePageBindingButtons(pageIndex);
}

void UnifiedInputDialog::setGamecubeTriggerAnalog(bool leftTrigger, bool analog)
{
    if (this->selectedPlugin != InputPluginType::Gamecube ||
        this->controllerPages.isEmpty())
    {
        return;
    }

    apply_gamecube_trigger_mode(this->controllerPages[0]->gamecubeBindings, leftTrigger, analog);
    this->updatePageBindingButtons(0);
}

void UnifiedInputDialog::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr || event->isAutoRepeat())
    {
        QDialog::keyPressEvent(event);
        return;
    }

    const int key = Utilities::QtKeyToSdl3Key(event->key());
    this->keyboardState[key] = true;

    if (this->selectedPlugin == InputPluginType::USB &&
        this->listeningPageIndex >= 0 &&
        this->listeningBindingIndex >= 0)
    {
        ControllerPage* page = this->controllerPages[this->listeningPageIndex];
        set_single_binding(page->usbBindings[this->listeningBindingIndex], InputType::Keyboard, key, 0,
            QString::fromUtf8(SDL_GetScancodeName(static_cast<SDL_Scancode>(key))));
        const int pageIndex = this->listeningPageIndex;
        this->stopListeningForBinding(false);
        this->updatePageBindingButtons(pageIndex);
        return;
    }

    QDialog::keyPressEvent(event);
}

void UnifiedInputDialog::keyReleaseEvent(QKeyEvent* event)
{
    if (event == nullptr || event->isAutoRepeat())
    {
        QDialog::keyReleaseEvent(event);
        return;
    }

    const int key = Utilities::QtKeyToSdl3Key(event->key());
    this->keyboardState[key] = false;
    QDialog::keyReleaseEvent(event);
}

void UnifiedInputDialog::clearPreview(void)
{
    const int pageIndex = this->currentPageIndex();
    if (pageIndex < 0 || pageIndex >= static_cast<int>(this->controllerPages.size()))
    {
        return;
    }

    ControllerPage* page = this->controllerPages[pageIndex];
    if (page->controllerImageWidget != nullptr)
    {
        page->controllerImageWidget->ClearControllerState();
    }

    if (page->axisXLabel != nullptr && page->axisYLabel != nullptr)
    {
        page->axisXLabel->setText(QStringLiteral("0"));
        page->axisYLabel->setText(QStringLiteral("0"));
    }
}

void UnifiedInputDialog::openPreviewSource(void)
{
    this->closePreviewSource();
    this->clearPreview();
    this->gamecubeSelectedPortMissingController = false;
    this->updateWarningLabel();

    bool opened = false;
    switch (this->selectedPlugin)
    {
    case InputPluginType::Raphnet:
        opened = this->openRaphnetPreview();
        break;
    case InputPluginType::Gamecube:
        opened = this->openGamecubePreview();
        break;
    case InputPluginType::USB:
    default:
        opened = this->openUsbPreview();
        break;
    }

    if (opened)
    {
        this->pollTimer->start();
    }
}

void UnifiedInputDialog::closePreviewSource(void)
{
    if (this->pollTimer != nullptr)
    {
        this->pollTimer->stop();
    }

    if (this->hidDevice != nullptr)
    {
        this->setRaphnetPollingSuspended(false);
        hid_close(this->hidDevice);
        this->hidDevice = nullptr;
        hid_exit();
    }

    if (this->gamecubeHandle != nullptr)
    {
        if (this->gamecubeInterfaceClaimed)
        {
            libusb_release_interface(this->gamecubeHandle, 0);
        }
        libusb_close(this->gamecubeHandle);
        this->gamecubeHandle = nullptr;
        this->gamecubeInterfaceClaimed = false;
    }
    if (this->usbContext != nullptr)
    {
        libusb_exit(this->usbContext);
        this->usbContext = nullptr;
    }

    if (this->sdlGamepad != nullptr)
    {
        SDL_CloseGamepad(this->sdlGamepad);
        this->sdlGamepad = nullptr;
        this->sdlJoystick = nullptr;
    }
    else if (this->sdlJoystick != nullptr)
    {
        SDL_CloseJoystick(this->sdlJoystick);
        this->sdlJoystick = nullptr;
    }

    this->previewBackend = PreviewBackend::None;
}

void UnifiedInputDialog::pollPreview(void)
{
    bool ok = false;
    switch (this->previewBackend)
    {
    case PreviewBackend::Raphnet:
        ok = this->pollRaphnetPreview();
        break;
    case PreviewBackend::Gamecube:
        ok = this->pollGamecubePreview();
        break;
    case PreviewBackend::USB:
        ok = this->pollUsbPreview();
        break;
    case PreviewBackend::None:
    default:
        return;
    }

    if (!ok)
    {
        this->clearPreview();
    }

    if (this->listeningPageIndex >= 0)
    {
        this->listeningTicks++;
        if (this->listeningTicks >= kListenTimeoutTicks)
        {
            this->stopListeningForBinding(true);
        }
    }
}

bool UnifiedInputDialog::openRaphnetPreview(void)
{
    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];

    hid_init();

    struct hid_device_info* devices = hid_enumerate(kRaphnetVendorId, 0);
    struct hid_device_info* current = devices;

    while (current != nullptr)
    {
        const RaphnetAdapterDef* adapter = find_raphnet_adapter(
            static_cast<uint16_t>(current->product_id), current->interface_number);
        if (adapter != nullptr)
        {
            this->hidDevice = hid_open_path(current->path);
            if (this->hidDevice != nullptr)
            {
                this->raphnetReportSize = adapter->reportSize;
                this->raphnetChannelCount = adapter->rawChannels;
                hid_set_nonblocking(this->hidDevice, 1);
                break;
            }
        }

        current = current->next;
    }

    hid_free_enumeration(devices);

    if (this->hidDevice == nullptr)
    {
        show_status(page->statusLabel, tr("No raphnet raw-access adapter was detected."));
        hid_exit();
        return false;
    }

    this->updatePageDeviceChoices(pageIndex);
    if (pageIndex == 0)
    {
        const int configuredPort = std::clamp(
            CoreSettingsGetIntValue(SettingsID::RaphnetRaw_Player1AdapterPort),
            1, std::max(1, this->raphnetChannelCount));
        QSignalBlocker blocker(page->deviceComboBox);
        page->deviceComboBox->setCurrentIndex(configuredPort - 1);
    }
    this->setRaphnetPollingSuspended(true);
    this->previewBackend = PreviewBackend::Raphnet;
    clear_status(page->statusLabel);
    return true;
}

bool UnifiedInputDialog::exchangeRaphnetCommand(const unsigned char* command, int commandLength, unsigned char* response, int& responseLength)
{
    if (this->hidDevice == nullptr || command == nullptr || response == nullptr ||
        commandLength <= 0 || commandLength > this->raphnetReportSize)
    {
        return false;
    }

    unsigned char buffer[64] = {};
    buffer[0] = 0x00;
    std::memcpy(buffer + 1, command, static_cast<size_t>(commandLength));

    int result = hid_send_feature_report(this->hidDevice, buffer, this->raphnetReportSize + 1);
    if (result < 0)
    {
        return false;
    }

    for (int attempt = 0; attempt < 8; attempt++)
    {
        std::memset(buffer, 0, sizeof(buffer));
        buffer[0] = 0x00;

        result = hid_get_feature_report(this->hidDevice, buffer, this->raphnetReportSize + 1);
        if (result < 0)
        {
            return false;
        }
        if (result <= 1)
        {
            continue;
        }

        responseLength = result - 1;
        std::memcpy(response, buffer + 1, static_cast<size_t>(responseLength));
        if (response[0] == command[0])
        {
            return true;
        }
    }

    return false;
}

bool UnifiedInputDialog::setRaphnetPollingSuspended(bool suspended)
{
    unsigned char command[2] = { kRaphnetSuspendPolling, static_cast<unsigned char>(suspended ? 1 : 0) };
    unsigned char response[64] = {};
    int responseLength = 0;
    return this->exchangeRaphnetCommand(command, sizeof(command), response, responseLength);
}

bool UnifiedInputDialog::pollRaphnetPreview(void)
{
    if (this->hidDevice == nullptr)
    {
        return false;
    }

    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];
    const int channel = std::max(0, page->deviceComboBox->currentData().toInt());
    unsigned char command[4] = { kRaphnetRawSiCommand, static_cast<unsigned char>(channel), 0x01, kN64GetStatus };
    unsigned char response[64] = {};
    int responseLength = 0;

    if (!this->exchangeRaphnetCommand(command, sizeof(command), response, responseLength) ||
        responseLength < 7 || response[2] < 4)
    {
        return false;
    }

    const uint16_t buttons = (static_cast<uint16_t>(response[3]) << 8) | response[4];
    const int8_t xAxis = static_cast<int8_t>(response[5]);
    const int8_t yAxis = static_cast<int8_t>(response[6]);

    apply_n64_buttons(page->controllerImageWidget, buttons);
    page->controllerImageWidget->SetXAxisState(-normalize_axis_to_percent(xAxis, 127));
    page->controllerImageWidget->SetYAxisState(normalize_axis_to_percent(yAxis, 127));
    page->controllerImageWidget->UpdateImage();
    page->axisXLabel->setText(QString::number(xAxis));
    page->axisYLabel->setText(QString::number(yAxis));
    clear_status(page->statusLabel);
    return true;
}

bool UnifiedInputDialog::openGamecubePreview(void)
{
    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];

    int result = libusb_init(&this->usbContext);
    if (result != LIBUSB_SUCCESS)
    {
        show_status(page->statusLabel,
            tr("GameCube adapter scan unavailable: %1").arg(QString::fromLatin1(libusb_error_name(result))));
        this->usbContext = nullptr;
        return false;
    }

    this->gamecubeHandle = libusb_open_device_with_vid_pid(
        this->usbContext, kGameCubeAdapterVendorId, kGameCubeAdapterProductId);
    if (this->gamecubeHandle == nullptr)
    {
        show_status(page->statusLabel, tr("No GameCube adapter detected in Wii U/NS (native) mode."));
        return false;
    }

    libusb_control_transfer(this->gamecubeHandle, 0x21, 11, 0x0001, 0, nullptr, 0, 1000);

    if (libusb_kernel_driver_active(this->gamecubeHandle, 0) == 1)
    {
        result = libusb_detach_kernel_driver(this->gamecubeHandle, 0);
        if (result != LIBUSB_SUCCESS)
        {
            show_status(page->statusLabel, tr("GameCube adapter detected, but driver is missing."));
            return false;
        }
    }

    result = libusb_claim_interface(this->gamecubeHandle, 0);
    if (result != LIBUSB_SUCCESS)
    {
        show_status(page->statusLabel, tr("GameCube adapter detected, but driver is missing."));
        return false;
    }
    this->gamecubeInterfaceClaimed = true;

    uint8_t command = kGameCubeCommandPoll;
    result = libusb_interrupt_transfer(this->gamecubeHandle, kGameCubeEndpointOut, &command, sizeof(command), nullptr, 16);
    if (result != LIBUSB_SUCCESS)
    {
        show_status(page->statusLabel,
            tr("GameCube adapter detected, but polling failed: %1")
                .arg(QString::fromLatin1(libusb_error_name(result))));
        return false;
    }

    this->previewBackend = PreviewBackend::Gamecube;
    clear_status(page->statusLabel);
    return true;
}

bool UnifiedInputDialog::pollGamecubePreview(void)
{
    if (this->gamecubeHandle == nullptr)
    {
        return false;
    }

    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];
    uint8_t readBuffer[37] = {};
    int transferred = 0;
    const int result = libusb_interrupt_transfer(
        this->gamecubeHandle, kGameCubeEndpointIn, readBuffer, sizeof(readBuffer), &transferred, 16);
    if (result != LIBUSB_SUCCESS || transferred != static_cast<int>(sizeof(readBuffer)))
    {
        return false;
    }

    const int port = std::clamp(page->deviceComboBox->currentData().toInt(), 0, 3);
    const int offset = port * 9;
    GameCubeState state;
    state.status = readBuffer[offset + 1];
    state.buttons1 = readBuffer[offset + 2];
    state.buttons2 = readBuffer[offset + 3];
    state.leftStickX = readBuffer[offset + 4];
    state.leftStickY = readBuffer[offset + 5];
    state.rightStickX = readBuffer[offset + 6];
    state.rightStickY = readBuffer[offset + 7];
    state.leftTrigger = readBuffer[offset + 8];
    state.rightTrigger = readBuffer[offset + 9];

    if (!state.status)
    {
        clear_status(page->statusLabel);
        if (!this->gamecubeSelectedPortMissingController)
        {
            this->gamecubeSelectedPortMissingController = true;
            this->updateWarningLabel();
        }
        return false;
    }

    if (this->gamecubeSelectedPortMissingController)
    {
        this->gamecubeSelectedPortMissingController = false;
        this->updateWarningLabel();
    }

    const double triggerThreshold = static_cast<double>(page->gamecubeTriggerThresholdSlider->value()) / 100.0;
    const double cStickThreshold = static_cast<double>(CoreSettingsGetIntValue(SettingsID::GCAInput_CButtonTreshold)) / 100.0;
    const bool leftTriggerAnalog = page->gamecubeLeftTriggerAnalogRadioButton->isChecked();
    const bool rightTriggerAnalog = page->gamecubeRightTriggerAnalogRadioButton->isChecked();

    if (this->listeningPageIndex == pageIndex && this->listeningBindingIndex >= 0)
    {
        const GCInput detected = detect_gamecube_input(state, triggerThreshold, cStickThreshold,
            leftTriggerAnalog, rightTriggerAnalog);
        if (detected != GCInput::None)
        {
            page->gamecubeBindings[this->listeningBindingIndex] = static_cast<int>(detected);
            const int oldPageIndex = this->listeningPageIndex;
            this->stopListeningForBinding(false);
            this->updatePageBindingButtons(oldPageIndex);
        }
    }

    for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
    {
        const BindingTarget& target = kBindingTargets[static_cast<size_t>(i)];
        if (!target.hasGamecubeMapping)
        {
            continue;
        }

        set_button(page->controllerImageWidget, target.imageButton,
            gc_input_active(state, static_cast<GCInput>(page->gamecubeBindings[i]), triggerThreshold, cStickThreshold,
                leftTriggerAnalog, rightTriggerAnalog));
    }

    const int8_t x = static_cast<int8_t>(state.leftStickX + 128);
    const int8_t y = static_cast<int8_t>(state.leftStickY + 128);
    const double inputX = static_cast<double>(x) / static_cast<double>(INT8_MAX);
    const double inputY = static_cast<double>(y) / static_cast<double>(INT8_MAX);
    const double deadzone = static_cast<double>(page->gamecubeDeadzoneSlider->value()) / 100.0;
    const double n64Max = kGameCubeN64AxisPeak *
        gamecube_sensitivity_percent_to_scale(page->gamecubeSensitivitySlider->value());
    const int xValue = std::clamp(scale_axis(inputX, deadzone, n64Max), -INT8_MAX, INT8_MAX);
    const int yValue = std::clamp(scale_axis(inputY, deadzone, n64Max), -INT8_MAX, INT8_MAX);

    page->controllerImageWidget->SetXAxisState(-normalize_axis_to_percent(xValue, 127));
    page->controllerImageWidget->SetYAxisState(normalize_axis_to_percent(yValue, 127));
    page->controllerImageWidget->UpdateImage();
    page->axisXLabel->setText(QString::number(xValue));
    page->axisYLabel->setText(QString::number(yValue));
    clear_status(page->statusLabel);
    return true;
}

bool UnifiedInputDialog::openUsbPreview(void)
{
    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];
    const int deviceIndex = page->deviceComboBox->currentData().toInt();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(this->usbDevices.size()))
    {
        show_status(page->statusLabel, tr("No SDL controller selected."));
        return false;
    }

    const UsbDeviceChoice& device = this->usbDevices[deviceIndex];
    if (device.type == InputDeviceType::None)
    {
        show_status(page->statusLabel, tr("This player is disabled."));
        return false;
    }
    if (device.type == InputDeviceType::Keyboard)
    {
        this->previewBackend = PreviewBackend::USB;
        clear_status(page->statusLabel);
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        show_status(page->statusLabel, tr("SDL input unavailable: %1").arg(QString::fromUtf8(SDL_GetError())));
        return false;
    }

    if (device.isGamepad && SDL_IsGamepad(device.id))
    {
        this->sdlGamepad = SDL_OpenGamepad(device.id);
        if (this->sdlGamepad == nullptr)
        {
            show_status(page->statusLabel, tr("Could not open SDL gamepad: %1").arg(QString::fromUtf8(SDL_GetError())));
            return false;
        }

        this->sdlJoystick = SDL_GetGamepadJoystick(this->sdlGamepad);
    }
    else
    {
        this->sdlJoystick = SDL_OpenJoystick(device.id);
        if (this->sdlJoystick == nullptr)
        {
            show_status(page->statusLabel, tr("Could not open SDL joystick: %1").arg(QString::fromUtf8(SDL_GetError())));
            return false;
        }
    }

    this->previewBackend = PreviewBackend::USB;
    clear_status(page->statusLabel);
    return true;
}

bool UnifiedInputDialog::pollUsbPreview(void)
{
    const int pageIndex = this->currentPageIndex();
    ControllerPage* page = this->controllerPages[pageIndex];
    const int deviceIndex = page->deviceComboBox->currentData().toInt();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(this->usbDevices.size()))
    {
        return false;
    }

    const UsbDeviceChoice& device = this->usbDevices[deviceIndex];
    if (device.type == InputDeviceType::Joystick)
    {
        if (this->sdlGamepad == nullptr && this->sdlJoystick == nullptr)
        {
            return false;
        }
        SDL_UpdateJoysticks();
    }

    auto bindingPressed = [this](const BindingValue& binding) -> bool
    {
        const int count = std::min(static_cast<int>(binding.types.size()),
            std::min(static_cast<int>(binding.data.size()), static_cast<int>(binding.extraData.size())));
        for (int i = 0; i < count; i++)
        {
            switch (static_cast<InputType>(binding.types[i]))
            {
            case InputType::Keyboard:
            {
                const auto it = this->keyboardState.find(binding.data[i]);
                if (it != this->keyboardState.end() && it->second)
                {
                    return true;
                }
                break;
            }
            case InputType::GamepadButton:
                if (this->sdlGamepad != nullptr &&
                    SDL_GetGamepadButton(this->sdlGamepad, static_cast<SDL_GamepadButton>(binding.data[i])))
                {
                    return true;
                }
                break;
            case InputType::GamepadAxis:
                if (this->sdlGamepad != nullptr)
                {
                    const int value = SDL_GetGamepadAxis(this->sdlGamepad, static_cast<SDL_GamepadAxis>(binding.data[i]));
                    if (std::abs(value) >= kAxisMotionThreshold && ((binding.extraData[i] != 0) == (value > 0)))
                    {
                        return true;
                    }
                }
                break;
            case InputType::JoystickButton:
                if (this->sdlJoystick != nullptr && SDL_GetJoystickButton(this->sdlJoystick, binding.data[i]))
                {
                    return true;
                }
                break;
            case InputType::JoystickAxis:
                if (this->sdlJoystick != nullptr)
                {
                    const int value = SDL_GetJoystickAxis(this->sdlJoystick, binding.data[i]);
                    if (std::abs(value) >= kAxisMotionThreshold && ((binding.extraData[i] != 0) == (value > 0)))
                    {
                        return true;
                    }
                }
                break;
            case InputType::JoystickHat:
                if (this->sdlJoystick != nullptr &&
                    (SDL_GetJoystickHat(this->sdlJoystick, binding.data[i]) & binding.extraData[i]) != 0)
                {
                    return true;
                }
                break;
            default:
                break;
            }
        }

        return false;
    };

    auto bindingAxis = [this](const BindingValue& binding, int direction) -> double
    {
        double axisState = 0.0;
        const int count = std::min(static_cast<int>(binding.types.size()),
            std::min(static_cast<int>(binding.data.size()), static_cast<int>(binding.extraData.size())));
        for (int i = 0; i < count; i++)
        {
            switch (static_cast<InputType>(binding.types[i]))
            {
            case InputType::Keyboard:
            {
                const auto it = this->keyboardState.find(binding.data[i]);
                if (it != this->keyboardState.end() && it->second)
                {
                    return static_cast<double>(direction);
                }
                break;
            }
            case InputType::GamepadButton:
                if (this->sdlGamepad != nullptr &&
                    SDL_GetGamepadButton(this->sdlGamepad, static_cast<SDL_GamepadButton>(binding.data[i])))
                {
                    return static_cast<double>(direction);
                }
                break;
            case InputType::JoystickButton:
                if (this->sdlJoystick != nullptr && SDL_GetJoystickButton(this->sdlJoystick, binding.data[i]))
                {
                    return static_cast<double>(direction);
                }
                break;
            case InputType::JoystickHat:
                if (this->sdlJoystick != nullptr &&
                    (SDL_GetJoystickHat(this->sdlJoystick, binding.data[i]) & binding.extraData[i]) != 0)
                {
                    return static_cast<double>(direction);
                }
                break;
            case InputType::GamepadAxis:
                if (this->sdlGamepad != nullptr)
                {
                    int value = SDL_GetGamepadAxis(this->sdlGamepad, static_cast<SDL_GamepadAxis>(binding.data[i]));
                    if (value < -SDL_AXIS_PEAK)
                    {
                        value = -SDL_AXIS_PEAK;
                    }
                    if ((binding.extraData[i] != 0) == (value > 0))
                    {
                        axisState = std::abs(static_cast<double>(value) / SDL_AXIS_PEAK) * direction;
                    }
                }
                break;
            case InputType::JoystickAxis:
                if (this->sdlJoystick != nullptr)
                {
                    int value = SDL_GetJoystickAxis(this->sdlJoystick, binding.data[i]);
                    if (value < -SDL_AXIS_PEAK)
                    {
                        value = -SDL_AXIS_PEAK;
                    }
                    if ((binding.extraData[i] != 0) == (value > 0))
                    {
                        axisState = std::abs(static_cast<double>(value) / SDL_AXIS_PEAK) * direction;
                    }
                }
                break;
            default:
                break;
            }
        }

        return axisState;
    };

    if (this->listeningPageIndex == pageIndex && this->listeningBindingIndex >= 0)
    {
        bool captured = false;
        BindingValue capturedBinding;
        if (this->sdlGamepad != nullptr)
        {
            for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && !captured; i++)
            {
                if (SDL_GetGamepadButton(this->sdlGamepad, static_cast<SDL_GamepadButton>(i)))
                {
                    set_single_binding(capturedBinding, InputType::GamepadButton, i, 0,
                        string_from_const_char(SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(i))));
                    captured = true;
                }
            }
            for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT && !captured; i++)
            {
                const int value = SDL_GetGamepadAxis(this->sdlGamepad, static_cast<SDL_GamepadAxis>(i));
                if (std::abs(value) >= kAxisMotionThreshold)
                {
                    QString text = string_from_const_char(SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(i)));
                    text += value > 0 ? QStringLiteral("+") : QStringLiteral("-");
                    set_single_binding(capturedBinding, InputType::GamepadAxis, i, value > 0 ? 1 : 0, text);
                    captured = true;
                }
            }
        }
        else if (this->sdlJoystick != nullptr)
        {
            const int buttonCount = SDL_GetNumJoystickButtons(this->sdlJoystick);
            for (int i = 0; i < buttonCount && !captured; i++)
            {
                if (SDL_GetJoystickButton(this->sdlJoystick, i))
                {
                    set_single_binding(capturedBinding, InputType::JoystickButton, i, 0, tr("button %1").arg(i));
                    captured = true;
                }
            }

            const int axisCount = SDL_GetNumJoystickAxes(this->sdlJoystick);
            for (int i = 0; i < axisCount && !captured; i++)
            {
                const int value = SDL_GetJoystickAxis(this->sdlJoystick, i);
                if (std::abs(value) >= kAxisMotionThreshold)
                {
                    set_single_binding(capturedBinding, InputType::JoystickAxis, i, value > 0 ? 1 : 0,
                        tr("axis %1%2").arg(i).arg(value > 0 ? QStringLiteral("+") : QStringLiteral("-")));
                    captured = true;
                }
            }

            const int hatCount = SDL_GetNumJoystickHats(this->sdlJoystick);
            for (int i = 0; i < hatCount && !captured; i++)
            {
                const int value = SDL_GetJoystickHat(this->sdlJoystick, i);
                if (value != SDL_HAT_CENTERED)
                {
                    set_single_binding(capturedBinding, InputType::JoystickHat, i, value, tr("hat %1:%2").arg(i).arg(value));
                    captured = true;
                }
            }
        }

        if (captured)
        {
            page->usbBindings[this->listeningBindingIndex] = capturedBinding;
            const int oldPageIndex = this->listeningPageIndex;
            this->stopListeningForBinding(false);
            this->updatePageBindingButtons(oldPageIndex);
        }
    }

    for (int i = 0; i < static_cast<int>(kBindingTargets.size()); i++)
    {
        const BindingTarget& target = kBindingTargets[static_cast<size_t>(i)];
        if (target.imageButton != N64ControllerButton::Invalid)
        {
            set_button(page->controllerImageWidget, target.imageButton, bindingPressed(page->usbBindings[i]));
        }
    }

    double inputY = bindingAxis(page->usbBindings[15], 1);
    const double inputYDown = bindingAxis(page->usbBindings[16], -1);
    if (inputY != 0.0 && inputYDown != 0.0)
    {
        inputY = 0.0;
    }
    else if (inputYDown != 0.0)
    {
        inputY = inputYDown;
    }

    double inputX = bindingAxis(page->usbBindings[18], 1);
    const double inputXLeft = bindingAxis(page->usbBindings[17], -1);
    if (inputX != 0.0 && inputXLeft != 0.0)
    {
        inputX = 0.0;
    }
    else if (inputXLeft != 0.0)
    {
        inputX = inputXLeft;
    }

    const double deadzone = static_cast<double>(page->usbDeadzoneSlider->value()) / 100.0;
    const double range = page->realN64RangeCheckBox->isChecked() ? 100.0 : static_cast<double>(page->usbRangeSlider->value());
    const double n64Max = 127.0 * (range / 100.0);
    const int xValue = scale_axis(inputX, deadzone, n64Max);
    const int yValue = scale_axis(inputY, deadzone, n64Max);

    page->controllerImageWidget->SetXAxisState(n64Max > 0 ? static_cast<int>(-xValue * 100.0 / 127.0) : 0);
    page->controllerImageWidget->SetYAxisState(n64Max > 0 ? static_cast<int>(yValue * 100.0 / 127.0) : 0);
    page->controllerImageWidget->UpdateImage();
    page->axisXLabel->setText(QString::number(xValue));
    page->axisYLabel->setText(QString::number(yValue));
    return true;
}

UnifiedInputDialog::InputDetectionReport UnifiedInputDialog::ScanInputDevices(void)
{
    InputDetectionReport report;

    if (hid_init() == 0)
    {
        struct hid_device_info* devices = hid_enumerate(kRaphnetVendorId, 0);
        for (struct hid_device_info* current = devices; current != nullptr; current = current->next)
        {
            const RaphnetAdapterDef* adapter = find_raphnet_adapter(
                static_cast<uint16_t>(current->product_id), current->interface_number);
            if (adapter == nullptr)
            {
                continue;
            }

            report.foundRaphnet = true;
            report.lines.append(tr("raphnet adapter: HID %1 raw interface detected (%2 channel%3)")
                .arg(format_usb_id(kRaphnetVendorId, static_cast<uint16_t>(current->product_id)))
                .arg(adapter->rawChannels)
                .arg(adapter->rawChannels == 1 ? QString() : QStringLiteral("s")));
        }
        hid_free_enumeration(devices);
        hid_exit();
    }

    libusb_context* context = nullptr;
    int usbResult = libusb_init(&context);
    if (usbResult == LIBUSB_SUCCESS)
    {
        libusb_device** devices = nullptr;
        const ssize_t deviceCount = libusb_get_device_list(context, &devices);
        bool sawNativeGamecube = false;
        bool openedNativeGamecube = false;

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
            report.lines.append(tr("Native GameCube adapter: USB %1 detected, but driver is missing")
                .arg(format_usb_id(kGameCubeAdapterVendorId, kGameCubeAdapterProductId)));
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

        libusb_exit(context);
    }
    else
    {
        report.lines.append(tr("Native GameCube adapter scan unavailable: %1")
            .arg(QString::fromLatin1(libusb_error_name(usbResult))));
    }

    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
    {
        report.lines.append(tr("SDL scan unavailable: %1").arg(QString::fromUtf8(SDL_GetError())));
        return report;
    }

    SDL_UpdateJoysticks();

    int joysticksCount = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&joysticksCount);
    for (int i = 0; i < joysticksCount; i++)
    {
        const SDL_JoystickID joystickId = joysticks[i];
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

    if (joysticks != nullptr)
    {
        SDL_free(joysticks);
    }

    if (!report.foundAnySdlDevice)
    {
        report.lines.append(tr("SDL scan: no named gamepad or joystick devices detected"));
    }

    return report;
}

UnifiedInputDialog::Recommendation UnifiedInputDialog::DetectRecommendedPlugin(const InputDetectionReport& report)
{
    Recommendation recommendation;

    if (report.foundRaphnet)
    {
        recommendation.hasRecommendation = true;
        recommendation.plugin = InputPluginType::Raphnet;
        recommendation.reason = tr("Recommended: raphnet adapter detected");
        recommendation.style = RecommendationStyle::Recommended;
        return recommendation;
    }

    if (report.foundNativeGamecube)
    {
        recommendation.hasRecommendation = true;
        recommendation.plugin = InputPluginType::Gamecube;
        recommendation.reason = tr("Recommended: GameCube adapter detected in Wii U/NS (native) mode");
        recommendation.style = RecommendationStyle::Recommended;
        return recommendation;
    }

    if (report.foundUsbModeMayflash)
    {
        recommendation.hasRecommendation = true;
        recommendation.plugin = InputPluginType::USB;
        recommendation.reason = tr("Mayflash USB mode detected; switch to Wii U/NS (native) mode for better support");
        recommendation.style = RecommendationStyle::Advisory;
        return recommendation;
    }

    if (report.foundAnySdlDevice)
    {
        recommendation.hasRecommendation = true;
        recommendation.plugin = InputPluginType::USB;
        recommendation.reason = tr("Recommended: USB controller detected");
        recommendation.style = RecommendationStyle::Recommended;
    }

    return recommendation;
}
