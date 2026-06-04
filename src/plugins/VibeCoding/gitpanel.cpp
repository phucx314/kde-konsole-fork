/*
    SPDX-FileCopyrightText: 2026 Vibe Coding Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gitpanel.h"
#include "session/Session.h"

#include <QDir>
#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QProcess>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int ItemKindRole = Qt::UserRole;
constexpr int FilePathRole = Qt::UserRole + 1;
constexpr int FileFlagsRole = Qt::UserRole + 2;
constexpr int FullPathRole = Qt::UserRole + 3;

QString statusCodeLabel(QChar status)
{
    switch (status.toLatin1()) {
    case 'M':
        return QObject::tr("Modified");
    case 'A':
        return QObject::tr("New");
    case 'D':
        return QObject::tr("Deleted");
    case 'R':
        return QObject::tr("Renamed");
    case 'C':
        return QObject::tr("Copied");
    case 'U':
        return QObject::tr("Unmerged");
    case 'T':
        return QObject::tr("Type Changed");
    default:
        return QObject::tr("Changed");
    }
}

QColor statusCodeColor(QChar status)
{
    switch (status.toLatin1()) {
    case 'A':
    case '?':
        return QColor(QStringLiteral("#2e7d32"));
    case 'M':
    case 'R':
    case 'C':
    case 'T':
        return QColor(QStringLiteral("#b26a00"));
    case 'D':
    case 'U':
        return QColor(QStringLiteral("#c62828"));
    default:
        return QColor(QStringLiteral("#607d8b"));
    }
}

QString shortStatusCode(QChar status, bool untracked)
{
    if (untracked) {
        return QStringLiteral("U");
    }

    if (status == QLatin1Char(' ')) {
        return QStringLiteral("M");
    }

    return QString(status);
}
}

GitPanel::GitPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // --- commit input --------------------------------------------------------
    auto *commitGroup = new QGroupBox(tr("Commit"), this);
    auto *commitLayout = new QVBoxLayout(commitGroup);
    commitLayout->setContentsMargins(4, 4, 4, 4);

    m_commitInput = new QLineEdit(commitGroup);
    m_commitInput->setPlaceholderText(tr("Commit message…"));
    commitLayout->addWidget(m_commitInput);

    auto *commitBtn = new QPushButton(tr("💾 Commit Staged"), commitGroup);
    connect(commitBtn, &QPushButton::clicked, this, &GitPanel::runGitCommit);
    commitLayout->addWidget(commitBtn);

    mainLayout->addWidget(commitGroup);

    // --- branch label --------------------------------------------------------
    m_branchLabel = new QLabel(tr("Branch: —"), this);
    m_branchLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    mainLayout->addWidget(m_branchLabel);

    auto *toolbar = new QWidget(this);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(2);

    auto makeToolButton = [&](const QString &iconText, const QString &toolTip, auto slot) {
        auto *btn = new QToolButton(toolbar);
        btn->setText(iconText);
        btn->setToolTip(toolTip);
        btn->setAutoRaise(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        connect(btn, &QToolButton::clicked, this, slot);
        toolbarLayout->addWidget(btn);
    };

    makeToolButton(QStringLiteral("↻"), tr("Refresh"), &GitPanel::refreshStatus);
    makeToolButton(QStringLiteral("↑"), tr("Stage selected"), &GitPanel::runGitAddSelected);
    makeToolButton(QStringLiteral("↓"), tr("Unstage selected"), &GitPanel::runGitUnstageSelected);
    makeToolButton(QStringLiteral("⟲"), tr("Revert selected"), &GitPanel::runGitRevertSelected);
    makeToolButton(QStringLiteral("⇡"), tr("git push"), &GitPanel::runGitPush);
    makeToolButton(QStringLiteral("⇣"), tr("git pull"), &GitPanel::runGitPull);
    makeToolButton(QStringLiteral("≈"), tr("git diff"), &GitPanel::runGitDiff);
    toolbarLayout->addStretch();

    mainLayout->addWidget(toolbar);

    m_statusList = new QListWidget(this);
    m_statusList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_statusList->setSpacing(1);
    m_statusList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_statusList->viewport()->installEventFilter(this);
    mainLayout->addWidget(m_statusList, 1);

    m_statusMessageLabel = new QLabel(this);
    m_statusMessageLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusMessageLabel);

    mainLayout->addStretch(1);

    // --- file watcher with debounce for auto-refresh -------------------------
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(1000); // 1 second debounce
    connect(m_debounce, &QTimer::timeout, this, &GitPanel::refreshStatus);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        m_debounce->start(); // restart the debounce timer on every change
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
        rebuildWatcher();
        m_debounce->start();
    });
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void GitPanel::setWorkingDirectory(const QString &path)
{
    if (path == m_workingDir) {
        return;
    }

    m_workingDir = path;
    refreshRepositoryPaths();
    rebuildWatcher();
    refreshStatus();
}

void GitPanel::setSession(Konsole::Session *session)
{
    m_session = session;
}

bool GitPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_statusList->viewport() && event->type() == QEvent::Resize) {
        updateElidedPaths();
    }

    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Slots — run git commands via QProcess
// ---------------------------------------------------------------------------

void GitPanel::refreshStatus()
{
    if (m_workingDir.isEmpty()) {
        m_branchLabel->setText(tr("Branch: —"));
        m_statusList->clear();
        m_statusMessageLabel->clear();
        return;
    }

    refreshRepositoryPaths();
    rebuildWatcher();

    // --- get current branch ---
    QProcess branchProc;
    branchProc.setWorkingDirectory(m_workingDir);
    branchProc.start(QStringLiteral("git"), {QStringLiteral("branch"), QStringLiteral("--show-current")});
    branchProc.waitForFinished(3000);
    const QString branch = QString::fromUtf8(branchProc.readAllStandardOutput()).trimmed();
    m_branchLabel->setText(branch.isEmpty() ? tr("Branch: (not a git repo)") : tr("Branch: %1").arg(branch));

    // --- get status ---
    QProcess statusProc;
    statusProc.setWorkingDirectory(m_workingDir);
    statusProc.start(QStringLiteral("git"), {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
                                             QStringLiteral("--untracked-files=all")});
    statusProc.waitForFinished(5000);

    m_statusList->clear();
    m_statusMessageLabel->clear();
    if (statusProc.exitStatus() != QProcess::NormalExit || statusProc.exitCode() != 0) {
        setStatusMessage(tr("(not a git repo)"));
        return;
    }

    const QString output = QString::fromUtf8(statusProc.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    struct Entry {
        QString code;
        QString path;
        int flags = 0;
        QColor color;
        QString toolTip;
    };
    QList<Entry> stagedEntries;
    QList<Entry> unstagedEntries;

    for (const QString &line : lines) {
        if (line.size() < 4) {
            continue;
        }

        const QChar indexStatus = line.at(0);
        const QChar workTreeStatus = line.at(1);
        QString path = line.mid(3);
        const int renameArrow = path.indexOf(QStringLiteral(" -> "));
        if (renameArrow >= 0) {
            path = path.mid(renameArrow + 4);
        }

        int flags = 0;
        QStringList detailLabels;

        if (indexStatus != QLatin1Char(' ') && indexStatus != QLatin1Char('?')) {
            flags |= StagedState;
            detailLabels << tr("staged %1").arg(statusCodeLabel(indexStatus));
        }
        if (workTreeStatus != QLatin1Char(' ') && workTreeStatus != QLatin1Char('?')) {
            flags |= UnstagedState;
            detailLabels << tr("unstaged %1").arg(statusCodeLabel(workTreeStatus));
        }
        if (indexStatus == QLatin1Char('?') && workTreeStatus == QLatin1Char('?')) {
            flags |= UntrackedState | UnstagedState;
            detailLabels << tr("untracked New");
        }

        if (flags == 0) {
            continue;
        }

        const bool isUntracked = indexStatus == QLatin1Char('?') && workTreeStatus == QLatin1Char('?');
        if (flags & StagedState) {
            stagedEntries.append({shortStatusCode(indexStatus, false),
                                  path,
                                  flags,
                                  statusCodeColor(indexStatus),
                                  detailLabels.join(QStringLiteral(", "))});
        }
        if (flags & UnstagedState) {
            const QChar statusCode = isUntracked ? QLatin1Char('?') : workTreeStatus;
            unstagedEntries.append({shortStatusCode(statusCode, isUntracked),
                                    path,
                                    flags,
                                    statusCodeColor(statusCode),
                                    detailLabels.join(QStringLiteral(", "))});
        }
    }

    addSectionHeader(tr("Staged Changes (%1)").arg(stagedEntries.size()),
                     tr("Unstage All"),
                     tr("Unstage all staged files"),
                     SLOT(runGitUnstageAll()));
    if (stagedEntries.isEmpty()) {
        setStatusMessage(tr("No staged changes."));
    } else {
        for (const Entry &entry : std::as_const(stagedEntries)) {
            addStatusItem(entry.code, entry.path, entry.flags, entry.color, entry.toolTip);
        }
    }

    addSectionHeader(tr("Changes (%1)").arg(unstagedEntries.size()),
                     tr("Stage All"),
                     tr("Stage all unstaged files"),
                     SLOT(runGitAddAllUnstaged()));
    for (const Entry &entry : std::as_const(unstagedEntries)) {
        addStatusItem(entry.code, entry.path, entry.flags, entry.color, entry.toolTip);
    }

    if (unstagedEntries.isEmpty() && stagedEntries.isEmpty()) {
        setStatusMessage(tr("(clean working tree)"));
    }
    updateElidedPaths();
}

void GitPanel::runGitAdd()
{
    runGitCommand({QStringLiteral("add"), QStringLiteral("-A")});
}

void GitPanel::runGitAddSelected()
{
    bool hasIneligibleSelection = false;
    const QStringList paths = selectedPaths(UnstagedState | UntrackedState, &hasIneligibleSelection);
    if (paths.isEmpty()) {
        setStatusMessage(hasIneligibleSelection ? tr("(selection cannot be staged)") : tr("(select unstaged files to stage)"));
        return;
    }

    QStringList args{QStringLiteral("add"), QStringLiteral("--")};
    args += paths;
    runGitCommand(args);
}

void GitPanel::runGitAddAllUnstaged()
{
    runGitCommand({QStringLiteral("add"), QStringLiteral("-A")});
}

void GitPanel::runGitCommit()
{
    const QString msg = m_commitInput->text().trimmed();
    if (msg.isEmpty()) {
        m_commitInput->setPlaceholderText(tr("⚠ Enter a commit message first!"));
        return;
    }
    runGitCommand({QStringLiteral("commit"), QStringLiteral("-m"), msg});
    m_commitInput->clear();
}

void GitPanel::runGitUnstageSelected()
{
    bool hasIneligibleSelection = false;
    const QStringList paths = selectedPaths(StagedState, &hasIneligibleSelection);
    if (paths.isEmpty()) {
        setStatusMessage(hasIneligibleSelection ? tr("(selection is not staged)") : tr("(select staged files to unstage)"));
        return;
    }

    QStringList args{QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--")};
    args += paths;
    runGitCommand(args);
}

void GitPanel::runGitUnstageAll()
{
    runGitCommand({QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral(".")});
}

void GitPanel::runGitRevertSelected()
{
    bool hasIneligibleSelection = false;
    const QStringList paths = selectedPaths(UnstagedState, &hasIneligibleSelection);
    if (paths.isEmpty()) {
        setStatusMessage(hasIneligibleSelection ? tr("(selection has no unstaged changes)") : tr("(select unstaged files to revert)"));
        return;
    }

    QStringList args{QStringLiteral("restore"), QStringLiteral("--")};
    args += paths;
    runGitCommand(args);
}

void GitPanel::runGitPush()
{
    sendToTerminal(QStringLiteral("git push"));
}

void GitPanel::runGitPull()
{
    sendToTerminal(QStringLiteral("git pull"));
}

void GitPanel::runGitDiff()
{
    sendToTerminal(QStringLiteral("git diff"));
}

// ---------------------------------------------------------------------------
// Helper — send text to the attached Konsole session
// ---------------------------------------------------------------------------

void GitPanel::sendToTerminal(const QString &command)
{
    if (!m_session) {
        return;
    }
    m_session->sendTextToTerminal(command, QLatin1Char('\r'));
}

// ---------------------------------------------------------------------------
// Helper — run a git command via QProcess (unused directly but available)
// ---------------------------------------------------------------------------

void GitPanel::runGitCommand(const QStringList &args)
{
    if (m_workingDir.isEmpty()) {
        return;
    }
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(m_workingDir);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (proc->exitCode() != 0 && !err.isEmpty()) {
            setStatusMessage(err);
        } else if (!out.trimmed().isEmpty()) {
            setStatusMessage(out.trimmed());
        }
        refreshStatus();
        proc->deleteLater();
    });
    proc->start(QStringLiteral("git"), args);
}

void GitPanel::refreshRepositoryPaths()
{
    m_repoRoot.clear();
    m_gitDir.clear();

    if (m_workingDir.isEmpty()) {
        return;
    }

    QProcess repoProc;
    repoProc.setWorkingDirectory(m_workingDir);
    repoProc.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    repoProc.waitForFinished(3000);
    if (repoProc.exitStatus() == QProcess::NormalExit && repoProc.exitCode() == 0) {
        m_repoRoot = QString::fromUtf8(repoProc.readAllStandardOutput()).trimmed();
    }

    QProcess gitDirProc;
    gitDirProc.setWorkingDirectory(m_workingDir);
    gitDirProc.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--git-dir")});
    gitDirProc.waitForFinished(3000);
    if (gitDirProc.exitStatus() == QProcess::NormalExit && gitDirProc.exitCode() == 0) {
        QString gitDir = QString::fromUtf8(gitDirProc.readAllStandardOutput()).trimmed();
        if (!gitDir.isEmpty()) {
            const QDir baseDir(m_repoRoot.isEmpty() ? m_workingDir : m_repoRoot);
            m_gitDir = QFileInfo(baseDir, gitDir).absoluteFilePath();
        }
    }
}

void GitPanel::rebuildWatcher()
{
    const QStringList existingPaths = m_watcher->directories() + m_watcher->files();
    if (!existingPaths.isEmpty()) {
        m_watcher->removePaths(existingPaths);
    }

    QStringList pathsToWatch;
    if (!m_repoRoot.isEmpty()) {
        pathsToWatch << m_repoRoot;
    } else if (!m_workingDir.isEmpty()) {
        pathsToWatch << m_workingDir;
    }

    if (!m_gitDir.isEmpty()) {
        pathsToWatch << (m_gitDir + QStringLiteral("/index"));
        pathsToWatch << (m_gitDir + QStringLiteral("/HEAD"));
        pathsToWatch << (m_gitDir + QStringLiteral("/refs"));
    }

    QStringList validPaths;
    for (const QString &path : std::as_const(pathsToWatch)) {
        if (path.isEmpty()) {
            continue;
        }
        const QFileInfo info(path);
        if (info.exists()) {
            validPaths << info.absoluteFilePath();
        }
    }

    validPaths.removeDuplicates();
    if (!validPaths.isEmpty()) {
        m_watcher->addPaths(validPaths);
    }
}

void GitPanel::setStatusMessage(const QString &text)
{
    m_statusMessageLabel->setText(text);
}

void GitPanel::updateElidedPaths()
{
    const int viewportWidth = m_statusList->viewport()->width();
    for (int i = 0; i < m_statusList->count(); ++i) {
        QListWidgetItem *item = m_statusList->item(i);
        if (!item || item->data(ItemKindRole).toInt() != static_cast<int>(ItemKind::FileStatus)) {
            continue;
        }

        QWidget *row = m_statusList->itemWidget(item);
        if (!row) {
            continue;
        }

        QLabel *pathLabel = row->findChild<QLabel *>(QStringLiteral("GitPathLabel"));
        QLabel *codeLabel = row->findChild<QLabel *>(QStringLiteral("GitCodeLabel"));
        if (!pathLabel || !codeLabel) {
            continue;
        }

        const QString fullPath = pathLabel->property("fullPath").toString();
        const int availableWidth = qMax(40, viewportWidth - codeLabel->sizeHint().width() - 32);
        const QString elided = pathLabel->fontMetrics().elidedText(fullPath, Qt::ElideMiddle, availableWidth);
        pathLabel->setText(elided);
        pathLabel->setToolTip(fullPath);
    }
}

QStringList GitPanel::selectedPaths(int requiredFlags, bool *hasIneligibleSelection) const
{
    QStringList paths;
    bool sawIneligible = false;

    for (QListWidgetItem *item : m_statusList->selectedItems()) {
        if (item->data(ItemKindRole).toInt() != static_cast<int>(ItemKind::FileStatus)) {
            continue;
        }

        const int flags = item->data(FileFlagsRole).toInt();
        if ((flags & requiredFlags) == 0) {
            sawIneligible = true;
            continue;
        }

        const QString path = item->data(FilePathRole).toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }

    paths.removeDuplicates();
    if (hasIneligibleSelection) {
        *hasIneligibleSelection = sawIneligible;
    }
    return paths;
}

void GitPanel::addSectionHeader(const QString &label, const QString &actionText, const QString &actionToolTip, const char *slot)
{
    auto *item = new QListWidgetItem(m_statusList);
    item->setData(ItemKindRole, static_cast<int>(ItemKind::Meta));
    item->setFlags(Qt::NoItemFlags);

    auto *row = new QWidget(m_statusList);
    row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->setSpacing(8);

    auto *titleLabel = new QLabel(label, row);
    titleLabel->setStyleSheet(QStringLiteral("color: #cfd8dc; font-weight: 700;"));
    titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *actionButton = new QToolButton(row);
    actionButton->setText(actionText);
    actionButton->setToolTip(actionToolTip);
    actionButton->setAutoRaise(true);
    actionButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    actionButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    actionButton->setStyleSheet(QStringLiteral("color: #90caf9; font-weight: 700; padding: 0 2px;"));
    connect(actionButton, SIGNAL(clicked()), this, slot);

    layout->addWidget(titleLabel);
    layout->addWidget(actionButton, 0, Qt::AlignRight);

    item->setSizeHint(QSize(0, row->sizeHint().height()));
    m_statusList->setItemWidget(item, row);
}

void GitPanel::addStatusItem(const QString &code, const QString &path, int flags, const QColor &color, const QString &toolTip)
{
    auto *item = new QListWidgetItem(m_statusList);
    item->setData(ItemKindRole, static_cast<int>(ItemKind::FileStatus));
    item->setData(FilePathRole, path);
    item->setData(FileFlagsRole, flags);
    item->setToolTip(toolTip);

    auto *row = new QWidget(m_statusList);
    row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 1, 6, 1);
    layout->setSpacing(8);

    auto *codeLabel = new QLabel(code, row);
    codeLabel->setObjectName(QStringLiteral("GitCodeLabel"));
    codeLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 700;").arg(color.name()));
    codeLabel->setMinimumWidth(18);

    auto *pathLabel = new QLabel(path, row);
    pathLabel->setObjectName(QStringLiteral("GitPathLabel"));
    pathLabel->setProperty("fullPath", path);
    pathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    pathLabel->setStyleSheet(QStringLiteral("color: palette(text);"));

    layout->addWidget(codeLabel);
    layout->addWidget(pathLabel, 1);

    item->setSizeHint(QSize(0, row->sizeHint().height()));
    m_statusList->setItemWidget(item, row);
}
