/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef FIRSTLAUNCHDIALOG_HPP
#define FIRSTLAUNCHDIALOG_HPP

#include <QDialog>
#include <QButtonGroup>

#include "ui_FirstLaunchDialog.h"

namespace UserInterface
{
namespace Dialog
{
class FirstLaunchDialog : public QDialog, private Ui::FirstLaunchDialog
{
    Q_OBJECT

  public:
    enum class InputPluginType
    {
        USB = 0,
        Raphnet = 1,
        Gamecube = 2
    };
    Q_ENUM(InputPluginType)

    FirstLaunchDialog(QWidget* parent, InputPluginType currentPlugin, bool autoSelectRecommended = true);

    void SetSelectedPlugin(InputPluginType plugin);
    InputPluginType GetSelectedPlugin(void) const;

    void SetRomDirectory(const QString& directory);

  signals:
    void InputPluginSelected(InputPluginType plugin);
    void RomDirectorySelected(const QString& directory);

  private slots:
    void on_romDirectoryBrowseButton_clicked(void);

  private:
    void setRecommendedPlugin(InputPluginType plugin, const QString& reason, bool hasRecommendation);
    void setSelectedPluginInternal(InputPluginType plugin, bool emitSignal);
    void updateButtonStyles(void);

    InputPluginType detectRecommendedPlugin(QString& reason, bool& hasRecommendation) const;

    QButtonGroup* pluginGroup = nullptr;
    InputPluginType selectedPlugin = InputPluginType::USB;
    InputPluginType recommendedPlugin = InputPluginType::USB;
    bool hasRecommendation = false;
};
} // namespace Dialog
} // namespace UserInterface

#endif // FIRSTLAUNCHDIALOG_HPP
