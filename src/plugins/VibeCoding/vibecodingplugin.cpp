/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "vibecodingplugin.h"
#include "filetreepanel.h"
#include "gitpanel.h"

#include "MainWindow.h"
#include "session/Session.h"
#include "session/SessionController.h"

#include <QDockWidget>
#include <QMainWindow>

#include <KActionCollection>
#include <KLocalizedString>

K_PLUGIN_CLASS_WITH_JSON(VibeCodingPlugin, "konsole_vibecoding.json")

// ---------------------------------------------------------------------------
// Private data (PIMPL — keeps the header clean)
// ---------------------------------------------------------------------------

struct VibeCodingPluginPrivate {
    // Per-window widgets
    QMap<Konsole::MainWindow *, FileTreePanel *> fileTreeForWindow;
    QMap<Konsole::MainWindow *, GitPanel *> gitPanelForWindow;
    QMap<Konsole::MainWindow *, QDockWidget *> fileDockForWindow;
    QMap<Konsole::MainWindow *, QDockWidget *> gitDockForWindow;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

VibeCodingPlugin::VibeCodingPlugin(QObject *parent, const QVariantList &args)
    : Konsole::IKonsolePlugin(parent, args)
    , d(std::make_unique<VibeCodingPluginPrivate>())
{
    setName(QStringLiteral("VibeCoding"));
}

VibeCodingPlugin::~VibeCodingPlugin() = default;

// ---------------------------------------------------------------------------
// createWidgetsForMainWindow — called once per MainWindow by PluginManager
// ---------------------------------------------------------------------------

void VibeCodingPlugin::createWidgetsForMainWindow(Konsole::MainWindow *mainWindow)
{
    // Both docks go on the left side, stacked vertically.
    // The user can drag/detach them to any position they like.

    // ======== Left dock — File Tree (top) ====================================
    auto *fileDock = new QDockWidget(i18n("File Tree"), mainWindow);
    fileDock->setObjectName(QStringLiteral("VibeCodingFileTreeDock"));
    fileDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *fileTree = new FileTreePanel(fileDock);
    fileDock->setWidget(fileTree);

    mainWindow->addDockWidget(Qt::LeftDockWidgetArea, fileDock);

    // ======== Left dock — Git Panel (tabbed with File Tree) ===================
    auto *gitDock = new QDockWidget(i18n("Git Panel"), mainWindow);
    gitDock->setObjectName(QStringLiteral("VibeCodingGitDock"));
    gitDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *gitPanel = new GitPanel(gitDock);
    gitDock->setWidget(gitPanel);

    // Tabify: both share the same dock area with a tab bar at the top
    mainWindow->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    mainWindow->addDockWidget(Qt::LeftDockWidgetArea, gitDock);
    mainWindow->tabifyDockWidget(fileDock, gitDock);

    // File Tree is the active tab by default
    fileDock->raise();
    fileDock->setVisible(true);
    gitDock->setVisible(true);

    connect(fileTree, &FileTreePanel::rootPathChanged, mainWindow, [gitPanel](const QString &path) {
        gitPanel->setWorkingDirectory(path);
    });

    d->fileTreeForWindow[mainWindow] = fileTree;
    d->fileDockForWindow[mainWindow] = fileDock;
    d->gitPanelForWindow[mainWindow] = gitPanel;
    d->gitDockForWindow[mainWindow] = gitDock;
}

// ---------------------------------------------------------------------------
// activeViewChanged — called whenever the user switches terminal tabs/splits
// ---------------------------------------------------------------------------

void VibeCodingPlugin::activeViewChanged(Konsole::SessionController *controller, Konsole::MainWindow *mainWindow)
{
    if (!controller || !mainWindow) {
        return;
    }

    // File Tree: set root from the session's initial working directory on
    // first activation only.  After that it stays put — acts as a static
    // project map.  QFileSystemModel's built-in QFileSystemWatcher handles
    // real-time file change detection (create/delete/rename).
    //
    // We use initialWorkingDirectory() instead of currentDir() because
    // activeViewChanged fires before the shell has started — reading
    // /proc/<pid>/cwd at that point returns "/" (race condition).
    // initialWorkingDirectory() is set before the shell spawns, so it's
    // always available immediately.
    auto fileIt = d->fileTreeForWindow.find(mainWindow);
    if (fileIt != d->fileTreeForWindow.end() && !fileIt.value()->hasRoot()) {
        if (Konsole::Session *session = controller->session()) {
            const QString initDir = session->initialWorkingDirectory();
            if (!initDir.isEmpty()) {
                fileIt.value()->setRootPath(initDir);
            }
        }
    }

    // Git Panel: track the file tree root once the user explicitly pins a
    // folder via Open Folder. Otherwise it follows the active session CWD.
    const QString cwd = controller->currentDir();
    const bool fileTreePinned = fileIt != d->fileTreeForWindow.end() && fileIt.value()->isPinned();
    auto gitIt = d->gitPanelForWindow.find(mainWindow);
    if (gitIt != d->gitPanelForWindow.end()) {
        gitIt.value()->setWorkingDirectory(fileTreePinned ? fileIt.value()->rootPath() : cwd);
        gitIt.value()->setSession(controller->session());
    }

    // Keep git panel CWD in sync when the user changes directories in the shell
    static QMap<Konsole::MainWindow *, QMetaObject::Connection> gitConns;

    if (gitConns.contains(mainWindow)) {
        disconnect(gitConns.take(mainWindow));
    }

    gitConns[mainWindow] = connect(controller, &Konsole::SessionController::currentDirectoryChanged, mainWindow,
                                   [this, mainWindow](const QString &dir) {
                                       auto fileIt = d->fileTreeForWindow.find(mainWindow);
                                       if (fileIt != d->fileTreeForWindow.end() && fileIt.value()->isPinned()) {
                                           return;
                                       }
                                       auto it = d->gitPanelForWindow.find(mainWindow);
                                       if (it != d->gitPanelForWindow.end()) {
                                           it.value()->setWorkingDirectory(dir);
                                       }
                                   });
}

// ---------------------------------------------------------------------------
// menuBarActions — items injected into the "Plugins" menu
// ---------------------------------------------------------------------------

QList<QAction *> VibeCodingPlugin::menuBarActions(Konsole::MainWindow *mainWindow) const
{
    // --- toggle File Tree dock ---
    auto *toggleFileTree = new QAction(i18n("Show File Tree"), mainWindow);
    toggleFileTree->setCheckable(true);
    mainWindow->actionCollection()->setDefaultShortcut(toggleFileTree, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F11));

    connect(toggleFileTree, &QAction::triggered, d->fileDockForWindow[mainWindow], &QDockWidget::setVisible);
    connect(d->fileDockForWindow[mainWindow], &QDockWidget::visibilityChanged, toggleFileTree, &QAction::setChecked);

    // --- toggle Git Panel dock ---
    auto *toggleGitPanel = new QAction(i18n("Show Git Panel"), mainWindow);
    toggleGitPanel->setCheckable(true);
    mainWindow->actionCollection()->setDefaultShortcut(toggleGitPanel, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F12));

    connect(toggleGitPanel, &QAction::triggered, d->gitDockForWindow[mainWindow], &QDockWidget::setVisible);
    connect(d->gitDockForWindow[mainWindow], &QDockWidget::visibilityChanged, toggleGitPanel, &QAction::setChecked);

    return {toggleFileTree, toggleGitPanel};
}

#include "moc_vibecodingplugin.cpp"
#include "vibecodingplugin.moc"
