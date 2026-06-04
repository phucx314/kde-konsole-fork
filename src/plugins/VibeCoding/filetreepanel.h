/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FILETREEPANEL_H
#define FILETREEPANEL_H

#include <QWidget>

class QFileSystemModel;
class QTreeView;
class QModelIndex;
class QToolBar;

/**
 * A panel that displays a file-system tree rooted at the terminal's
 * current working directory.  Automatically follows the active session
 * as the user changes directories in the shell.
 */
class FileTreePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FileTreePanel(QWidget *parent = nullptr);

    /** Set the root directory shown in the tree. */
    void setRootPath(const QString &path);

    /** Returns true if a root path has been set. */
    bool hasRoot() const;

    /** Returns the current root path shown in the tree. */
    QString rootPath() const;

    /** Returns true when the tree was explicitly pinned via Open Folder. */
    bool isPinned() const;

Q_SIGNALS:
    void rootPathChanged(const QString &path);
    void pinnedChanged(bool pinned);

private Q_SLOTS:
    void onItemDoubleClicked(const QModelIndex &index);
    void goUp();
    void openFolder();
    void newFile();
    void newFolder();
    void deleteSelected();

private:
    void setPinned(bool pinned);

    QFileSystemModel *m_fsModel = nullptr;
    QTreeView *m_treeView = nullptr;
    QString m_currentRoot;
    bool m_isPinned = false;
};

#endif // FILETREEPANEL_H
