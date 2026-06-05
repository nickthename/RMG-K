/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef UNIFIEDINPUTDIALOG_HPP
#define UNIFIEDINPUTDIALOG_HPP

#include "FirstLaunchDialog.hpp"

#include <common.hpp>

#include <QVector>
#include <QDialog>
#include <QStringList>
#include <cstdint>
#include <unordered_map>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QGroupBox;
class QKeyEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QWidget;

struct hid_device_;
typedef struct hid_device_ hid_device;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_Joystick SDL_Joystick;

struct libusb_context;
struct libusb_device_handle;

namespace UserInterface
{
namespace Widget
{
class ControllerImageWidget;
}

namespace Dialog
{
class UnifiedInputDialog : public QDialog
{
    Q_OBJECT

  public:
    using InputPluginType = FirstLaunchDialog::InputPluginType;

    enum class RecommendationStyle
    {
        None,
        Recommended,
        Advisory
    };

    struct InputDetectionReport
    {
        bool foundAnySdlDevice = false;
        bool foundRaphnet = false;
        bool foundNativeGamecube = false;
        bool foundBlockedNativeGamecube = false;
        bool foundUsbModeMayflash = false;
        bool foundOtherUsb = false;
        QStringList lines;
    };

    struct Recommendation
    {
        InputPluginType plugin = InputPluginType::USB;
        QString reason;
        bool hasRecommendation = false;
        RecommendationStyle style = RecommendationStyle::None;
    };

    UnifiedInputDialog(QWidget* parent, InputPluginType currentPlugin);
    ~UnifiedInputDialog(void) override;

    InputPluginType GetSelectedPlugin(void) const;
    int GetSelectedDeviceIndex(void) const;

    static InputDetectionReport ScanInputDevices(void);
    static Recommendation DetectRecommendedPlugin(const InputDetectionReport& report);

  private:
    enum class PreviewBackend
    {
        None,
        Raphnet,
        Gamecube,
        USB
    };

    void setupUi(void);
    QWidget* createControllerPage(int playerIndex);
    void refreshDetection(void);
    void updateRecommendationLabels(void);
    void refreshUsbDevices(void);
    void updateAllPages(void);
    void updatePageMode(int pageIndex);
    void updatePageDeviceChoices(int pageIndex);
    void updatePageBindingButtons(int pageIndex);
    void updateSliderLabels(int pageIndex);
    void setSelectedPlugin(InputPluginType plugin);
    int currentPageIndex(void) const;
    void saveAllSettings(void);
    void saveUsbSettings(int pageIndex);
    void saveGamecubeSettings(void);
    void saveRaphnetSettings(void);
    void loadPageSettings(int pageIndex);
    void restoreCurrentPageDefaults(void);

    void startListeningForBinding(int pageIndex, int bindingIndex);
    void stopListeningForBinding(bool restoreText);
    void clearBinding(int pageIndex, int bindingIndex);
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void openPreviewSource(void);
    void closePreviewSource(void);
    void pollPreview(void);
    void clearPreview(void);

    bool openRaphnetPreview(void);
    bool pollRaphnetPreview(void);
    bool setRaphnetPollingSuspended(bool suspended);
    bool exchangeRaphnetCommand(const unsigned char* command, int commandLength, unsigned char* response, int& responseLength);

    bool openGamecubePreview(void);
    bool pollGamecubePreview(void);

    bool openUsbPreview(void);
    bool pollUsbPreview(void);

  public:
    struct BindingValue
    {
        QVector<int> types;
        QVector<int> data;
        QVector<int> extraData;
        QVector<QString> text;
    };

    struct UsbDeviceChoice
    {
        InputDeviceType type = InputDeviceType::None;
        SDL_JoystickID id = 0;
        bool isGamepad = false;
        QString name;
        QString path;
        QString serial;
    };

    struct ControllerPage
    {
        QWidget* widget = nullptr;
        QComboBox* backendComboBox = nullptr;
        QComboBox* deviceComboBox = nullptr;
        QLabel* statusLabel = nullptr;
        QCheckBox* pluggedInCheckBox = nullptr;
        QGroupBox* mappingsGroupBox = nullptr;
        QGroupBox* usbStickGroupBox = nullptr;
        QGroupBox* gamecubeStickGroupBox = nullptr;
        QGroupBox* portGroupBox = nullptr;
        QVector<QPushButton*> bindingButtons;
        QVector<QPushButton*> clearButtons;
        QVector<BindingValue> usbBindings;
        QVector<int> gamecubeBindings;
        QSlider* usbDeadzoneSlider = nullptr;
        QSlider* usbRangeSlider = nullptr;
        QCheckBox* realN64RangeCheckBox = nullptr;
        QLabel* usbDeadzoneValueLabel = nullptr;
        QLabel* usbRangeValueLabel = nullptr;
        QSlider* gamecubeDeadzoneSlider = nullptr;
        QSlider* gamecubeSensitivitySlider = nullptr;
        QSlider* gamecubeTriggerThresholdSlider = nullptr;
        QLabel* gamecubeDeadzoneValueLabel = nullptr;
        QLabel* gamecubeSensitivityValueLabel = nullptr;
        QLabel* gamecubeTriggerThresholdValueLabel = nullptr;
        QLabel* axisXLabel = nullptr;
        QLabel* axisYLabel = nullptr;
        Widget::ControllerImageWidget* controllerImageWidget = nullptr;
    };

  private:
    InputPluginType selectedPlugin = InputPluginType::USB;
    InputDetectionReport detectionReport;
    PreviewBackend previewBackend = PreviewBackend::None;

    QVector<ControllerPage*> controllerPages;
    QVector<UsbDeviceChoice> usbDevices;
    QTabWidget* tabWidget = nullptr;
    QGroupBox* recommendationGroupBox = nullptr;
    QLabel* raphnetRecommendationLabel = nullptr;
    QLabel* gamecubeRecommendationLabel = nullptr;
    QLabel* usbRecommendationLabel = nullptr;
    QCheckBox* debugDevicesCheckBox = nullptr;
    QGroupBox* detectedDevicesGroupBox = nullptr;
    QPlainTextEdit* detectedDevicesPlainTextEdit = nullptr;
    QPushButton* refreshButton = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QTimer* pollTimer = nullptr;

    hid_device* hidDevice = nullptr;
    int raphnetReportSize = 63;
    int raphnetChannelCount = 1;

    libusb_context* usbContext = nullptr;
    libusb_device_handle* gamecubeHandle = nullptr;
    bool gamecubeInterfaceClaimed = false;

    SDL_Gamepad* sdlGamepad = nullptr;
    SDL_Joystick* sdlJoystick = nullptr;

    int listeningPageIndex = -1;
    int listeningBindingIndex = -1;
    int listeningTicks = 0;
    std::unordered_map<int, bool> keyboardState;
};
} // namespace Dialog
} // namespace UserInterface

#endif // UNIFIEDINPUTDIALOG_HPP
