/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "filetreepanel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QToolButton>
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

    auto makeToolButton = [&](const QString &text, const QString &toolTip, auto slot) {
        auto *button = new QToolButton(actionsBar);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(button, &QToolButton::clicked, this, slot);
        return button;
    };

    auto *openFolderBtn = makeToolButton(QStringLiteral("📂"), tr("Open Folder"), &FileTreePanel::openFolder);
    auto *newFileBtn = makeToolButton(QStringLiteral("📄"), tr("New File"), &FileTreePanel::newFile);
    auto *newFolderBtn = makeToolButton(QStringLiteral("📁"), tr("New Folder"), &FileTreePanel::newFolder);
    auto *deleteBtn = makeToolButton(QStringLiteral("🗑"), tr("Delete Selected"), &FileTreePanel::deleteSelected);

    actionsLayout->addWidget(openFolderBtn);
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

QString FileTreePanel::rootPath() const
{
    return m_currentRoot;
}

bool FileTreePanel::isPinned() const
{
    return m_isPinned;
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
    Q_EMIT rootPathChanged(path);
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

void FileTreePanel::openFolder()
{
    const QString startDir = m_currentRoot.isEmpty() ? QDir::homePath() : m_currentRoot;
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder"), startDir);
    if (folder.isEmpty()) {
        return;
    }

    setPinned(true);
    setRootPath(folder);
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
        ? tr("Move folder \"%1\" to trash?").arg(info.fileName())
        : tr("Move file \"%1\" to trash?").arg(info.fileName());

    if (QMessageBox::question(this, tr("Confirm Trash"), question) != QMessageBox::Yes) {
        return;
    }

    // FreeDesktop trash spec: ~/.local/share/Trash/
    const QString trashBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    const QString trashFiles = trashBase + QStringLiteral("/files");
    const QString trashInfo = trashBase + QStringLiteral("/info");

    QDir().mkpath(trashFiles);
    QDir().mkpath(trashInfo);

    // Handle name collisions by appending a counter
    QString destName = info.fileName();
    QString destPath = trashFiles + QLatin1Char('/') + destName;
    int counter = 1;
    while (QFile::exists(destPath) || QDir(destPath).exists()) {
        destName = info.fileName() + QStringLiteral(".%1").arg(counter++);
        destPath = trashFiles + QLatin1Char('/') + destName;
    }

    // Write the .trashinfo metadata file
    const QString infoPath = trashInfo + QLatin1Char('/') + destName + QStringLiteral(".trashinfo");
    QFile infoFile(infoPath);
    if (infoFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&infoFile);
        out << "[Trash Info]\n";
        out << "Path=" << filePath << '\n';
        out << "DeletionDate=" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddThh:mm:ss")) << '\n';
        infoFile.close();
    }

    // Move the file/folder to trash
    if (!QFile::rename(filePath, destPath)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not move \"%1\" to trash.").arg(info.fileName()));
        // Clean up the info file if the move failed
        QFile::remove(infoPath);
    }
}

void FileTreePanel::setPinned(bool pinned)
{
    if (m_isPinned == pinned) {
        return;
    }

    m_isPinned = pinned;
    Q_EMIT pinnedChanged(pinned);
}
