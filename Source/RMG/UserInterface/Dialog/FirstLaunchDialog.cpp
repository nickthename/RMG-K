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

#include <SDL3/SDL.h>

#include <QFileDialog>
#include <QDir>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QStyle>

using namespace UserInterface::Dialog;

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

    const QSize iconSize(96, 96);
    const QSize minSize(180, 140);
    const QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    this->gamecubeButton->setIcon(QIcon(":/onboarding/gamecube.svg"));
    this->gamecubeButton->setIconSize(iconSize);
    this->gamecubeButton->setMinimumSize(minSize);
    this->gamecubeButton->setSizePolicy(sizePolicy);

    this->raphnetButton->setIcon(QIcon(":/onboarding/raphnet.svg"));
    this->raphnetButton->setIconSize(iconSize);
    this->raphnetButton->setMinimumSize(minSize);
    this->raphnetButton->setSizePolicy(sizePolicy);

    this->usbButton->setIcon(QIcon(":/onboarding/usb.svg"));
    this->usbButton->setIconSize(iconSize);
    this->usbButton->setMinimumSize(minSize);
    this->usbButton->setSizePolicy(sizePolicy);

    for (QLabel* label : {this->gamecubeRecommendedLabel, this->raphnetRecommendedLabel, this->usbRecommendedLabel})
    {
        label->setProperty("recommendedBadge", false);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        label->setFixedHeight(24);
        label->setText(" ");
        label->setVisible(true);
    }

    this->romDirectoryLineEdit->setReadOnly(true);

    connect(this->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(this->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (QPushButton* okButton = this->buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(tr("Done"));
    }

    this->setStyleSheet(
        "QToolButton {"
        "  border: 1px solid #c9d2dd;"
        "  border-radius: 12px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #f1f4f8);"
        "  padding: 12px;"
        "  font-size: 14px;"
        "}"
        "QToolButton:checked {"
        "  border: 2px solid #2563eb;"
        "}"
        "QLabel[recommendedBadge=\"true\"] {"
        "  background: #d4edda;"
        "  color: #155724;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "}"
        "QLineEdit {"
        "  padding: 6px;"
        "}"
    );

    QString recommendationReason;
    this->recommendedPlugin = this->detectRecommendedPlugin(recommendationReason, this->hasRecommendation);
    this->setRecommendedPlugin(this->recommendedPlugin, recommendationReason, this->hasRecommendation);

    InputPluginType initialPlugin = currentPlugin;
    if (autoSelectRecommended && this->hasRecommendation)
    {
        initialPlugin = this->recommendedPlugin;
    }

    this->setSelectedPluginInternal(initialPlugin, false);

    auto addShadow = [](QToolButton* button)
    {
        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(button);
        shadow->setBlurRadius(18.0);
        shadow->setOffset(0.0, 2.0);
        shadow->setColor(QColor(0, 0, 0, 35));
        button->setGraphicsEffect(shadow);
    };

    addShadow(this->gamecubeButton);
    addShadow(this->raphnetButton);
    addShadow(this->usbButton);
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

void FirstLaunchDialog::setRecommendedPlugin(InputPluginType plugin, const QString& reason, bool hasRecommendation)
{
    QLabel* labels[] = {this->gamecubeRecommendedLabel, this->raphnetRecommendedLabel, this->usbRecommendedLabel};
    for (QLabel* label : labels)
    {
        label->setText(" ");
        label->setProperty("recommendedBadge", false);
        label->style()->unpolish(label);
        label->style()->polish(label);
    }

    if (!hasRecommendation)
    {
        return;
    }

    QLabel* targetLabel = nullptr;
    switch (plugin)
    {
    case InputPluginType::Gamecube:
        targetLabel = this->gamecubeRecommendedLabel;
        break;
    case InputPluginType::Raphnet:
        targetLabel = this->raphnetRecommendedLabel;
        break;
    case InputPluginType::USB:
        targetLabel = this->usbRecommendedLabel;
        break;
    }

    if (targetLabel != nullptr)
    {
        targetLabel->setText(reason);
        targetLabel->setProperty("recommendedBadge", true);
        targetLabel->setVisible(true);
        targetLabel->style()->unpolish(targetLabel);
        targetLabel->style()->polish(targetLabel);
    }
}

void FirstLaunchDialog::setSelectedPluginInternal(InputPluginType plugin, bool emitSignal)
{
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

FirstLaunchDialog::InputPluginType FirstLaunchDialog::detectRecommendedPlugin(QString& reason, bool& hasRecommendation) const
{
    hasRecommendation = false;
    reason.clear();

    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
    {
        SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }

    SDL_UpdateJoysticks();

    int joysticksCount = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&joysticksCount);

    bool foundAny = false;
    bool foundRaphnet = false;
    bool foundGamecube = false;

    for (int i = 0; i < joysticksCount; i++)
    {
        SDL_JoystickID joystickId = joysticks[i];
        const char* deviceName = nullptr;

        if (SDL_IsGamepad(joystickId))
        {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(joystickId);
            if (gamepad != nullptr)
            {
                deviceName = SDL_GetGamepadName(gamepad);
                SDL_CloseGamepad(gamepad);
            }
        }
        else
        {
            SDL_Joystick* joystick = SDL_OpenJoystick(joystickId);
            if (joystick != nullptr)
            {
                deviceName = SDL_GetJoystickName(joystick);
                SDL_CloseJoystick(joystick);
            }
        }

        if (deviceName == nullptr)
        {
            continue;
        }

        QString lowered = QString::fromUtf8(deviceName).toLower();
        if (lowered.isEmpty())
        {
            continue;
        }

        foundAny = true;

        if (lowered.contains("raphnet"))
        {
            foundRaphnet = true;
            break;
        }

        if (lowered.contains("gamecube") || lowered.contains("gcn") || lowered.contains("mayflash"))
        {
            foundGamecube = true;
        }
    }

    if (joysticks != nullptr)
    {
        SDL_free(joysticks);
    }

    if (foundRaphnet)
    {
        hasRecommendation = true;
        reason = tr("Recommended: raphnet adapter detected");
        return InputPluginType::Raphnet;
    }

    if (foundGamecube)
    {
        hasRecommendation = true;
        reason = tr("Recommended: GameCube adapter detected");
        return InputPluginType::Gamecube;
    }

    if (foundAny)
    {
        hasRecommendation = true;
        reason = tr("Recommended: USB controller detected");
        return InputPluginType::USB;
    }

    return InputPluginType::USB;
}
