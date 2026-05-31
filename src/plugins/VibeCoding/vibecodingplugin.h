/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIBECODINGPLUGIN_H
#define VIBECODINGPLUGIN_H

#include <pluginsystem/IKonsolePlugin.h>

#include <memory>

namespace Konsole
{
class SessionController;
class MainWindow;
class Session;
}

class FileTreePanel;
class GitPanel;
class QDockWidget;

struct VibeCodingPluginPrivate;

/**
 * Konsole plugin that adds a "Vibe Coding" environment with:
 *   - A file-tree dock on the left that follows the terminal's CWD.
 *   - A git-status/action dock on the right that follows the terminal's CWD.
 *
 * Both docks start hidden and can be toggled from the Plugins menu.
 */
class VibeCodingPlugin : public Konsole::IKonsolePlugin
{
    Q_OBJECT

public:
    VibeCodingPlugin(QObject *parent, const QVariantList &args);
    ~VibeCodingPlugin() override;

    // IKonsolePlugin interface
    void createWidgetsForMainWindow(Konsole::MainWindow *mainWindow) override;
    void activeViewChanged(Konsole::SessionController *controller, Konsole::MainWindow *mainWindow) override;
    QList<QAction *> menuBarActions(Konsole::MainWindow *mainWindow) const override;

private:
    std::unique_ptr<VibeCodingPluginPrivate> d;
};

#endif // VIBECODINGPLUGIN_H
