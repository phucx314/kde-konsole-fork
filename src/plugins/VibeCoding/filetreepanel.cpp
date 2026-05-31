/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "filetreepanel.h"

#include <QDesktopServices>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

FileTreePanel::FileTreePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- path bar + up button ------------------------------------------------
    auto *navBar = new QWidget(this);
    auto *navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(4, 4, 4, 4);
    navLayout->setSpacing(4);

    auto *upButton = new QPushButton(QStringLiteral("⬆"), navBar);
    upButton->setToolTip(tr("Go to parent directory"));
    upButton->setMaximumWidth(28);
    connect(upButton, &QPushButton::clicked, this, &FileTreePanel::goUp);

    auto *pathDisplay = new QLineEdit(navBar);
    pathDisplay->setReadOnly(true);
    pathDisplay->setPlaceholderText(tr("No directory"));

    navLayout->addWidget(upButton);
    navLayout->addWidget(pathDisplay);

    mainLayout->addWidget(navBar);

    // --- tree view -----------------------------------------------------------
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_fsModel);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setHeaderHidden(true);

    // Only show the Name column (0); hide Size, Type, Date.
    for (int col = 1; col < m_fsModel->columnCount(); ++col) {
        m_treeView->hideColumn(col);
    }
    m_treeView->header()->setStretchLastSection(true);

    connect(m_treeView, &QTreeView::doubleClicked, this, &FileTreePanel::onItemDoubleClicked);

    mainLayout->addWidget(m_treeView);

    // Keep pathDisplay in sync when setRootPath is called.
    // We store a pointer via a lambda connection.
    connect(this, &FileTreePanel::windowTitleChanged, pathDisplay, &QLineEdit::setText);
}

void FileTreePanel::setRootPath(const QString &path)
{
    if (path.isEmpty() || path == m_currentRoot) {
        return;
    }

    m_currentRoot = path;
    QModelIndex rootIdx = m_fsModel->setRootPath(path);
    m_treeView->setRootIndex(rootIdx);
    setWindowTitle(path); // triggers the connection to pathDisplay
}

void FileTreePanel::onItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString filePath = m_fsModel->filePath(index);
    const QFileInfo info(filePath);

    if (info.isDir()) {
        // Navigate into the directory
        setRootPath(filePath);
    } else {
        // Open the file with the system default handler
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    }
}

void FileTreePanel::goUp()
{
    if (m_currentRoot.isEmpty()) {
        return;
    }

    QDir dir(m_currentRoot);
    if (dir.cdUp()) {
        setRootPath(dir.absolutePath());
    }
}
