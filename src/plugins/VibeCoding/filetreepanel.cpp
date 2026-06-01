/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "filetreepanel.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
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

    // --- action buttons (new file, new folder, delete) -----------------------
    auto *actionsBar = new QWidget(this);
    auto *actionsLayout = new QHBoxLayout(actionsBar);
    actionsLayout->setContentsMargins(4, 0, 4, 4);
    actionsLayout->setSpacing(4);

    auto *newFileBtn = new QPushButton(tr("📄 New File"), actionsBar);
    connect(newFileBtn, &QPushButton::clicked, this, &FileTreePanel::newFile);

    auto *newFolderBtn = new QPushButton(tr("📁 New Folder"), actionsBar);
    connect(newFolderBtn, &QPushButton::clicked, this, &FileTreePanel::newFolder);

    auto *deleteBtn = new QPushButton(tr("🗑 Delete"), actionsBar);
    connect(deleteBtn, &QPushButton::clicked, this, &FileTreePanel::deleteSelected);

    actionsLayout->addWidget(newFileBtn);
    actionsLayout->addWidget(newFolderBtn);
    actionsLayout->addWidget(deleteBtn);
    actionsLayout->addStretch();

    mainLayout->addWidget(actionsBar);

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

bool FileTreePanel::hasRoot() const
{
    return !m_currentRoot.isEmpty();
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

void FileTreePanel::newFile()
{
    if (m_currentRoot.isEmpty()) {
        return;
    }

    bool ok;
    const QString name = QInputDialog::getText(this, tr("New File"), tr("File name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    const QString filePath = m_currentRoot + QLatin1Char('/') + name.trimmed();
    if (QFile::exists(filePath)) {
        QMessageBox::warning(this, tr("File Exists"), tr("A file named \"%1\" already exists.").arg(name));
        return;
    }

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.close();
    }
}

void FileTreePanel::newFolder()
{
    if (m_currentRoot.isEmpty()) {
        return;
    }

    bool ok;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    QDir dir(m_currentRoot);
    if (!dir.mkdir(name.trimmed())) {
        QMessageBox::warning(this, tr("Error"), tr("Could not create folder \"%1\".").arg(name));
    }
}

void FileTreePanel::deleteSelected()
{
    const QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }

    const QString filePath = m_fsModel->filePath(index);
    const QFileInfo info(filePath);
    const QString question = info.isDir()
        ? tr("Delete folder \"%1\" and all its contents?").arg(info.fileName())
        : tr("Delete file \"%1\"?").arg(info.fileName());

    if (QMessageBox::question(this, tr("Confirm Delete"), question) != QMessageBox::Yes) {
        return;
    }

    if (info.isDir()) {
        QDir dir(filePath);
        dir.removeRecursively();
    } else {
        QFile::remove(filePath);
    }
}
