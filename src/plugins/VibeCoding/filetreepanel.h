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

private Q_SLOTS:
    void onItemDoubleClicked(const QModelIndex &index);
    void goUp();

private:
    QFileSystemModel *m_fsModel = nullptr;
    QTreeView *m_treeView = nullptr;
    QString m_currentRoot;
};

#endif // FILETREEPANEL_H
