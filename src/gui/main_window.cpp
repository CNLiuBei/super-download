#include "main_window.h"
#include "new_download_dialog.h"
#include "settings_dialog.h"
#include "task_model.h"
#include "clipboard_monitor.h"
#include "ws_server.h"
#include "../core/download_manager.h"
#include "../core/logger.h"

#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QHeaderView>
#include <QMessageBox>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#ifdef _WIN32
#include <windows.h>
#endif

MainWindow::MainWindow(DownloadManager* manager, QWidget* parent)
    : QMainWindow(parent), manager_(manager)
{
    setWindowTitle(QStringLiteral("Super Download"));
    resize(1100, 620);
    setAcceptDrops(true);

    // Restore window geometry
    QSettings settings;
    if (settings.contains("window/geometry"))
        restoreGeometry(settings.value("window/geometry").toByteArray());
    if (settings.contains("window/state"))
        restoreState(settings.value("window/state").toByteArray());

    setupSidebar();
    setupTable();

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(sidebar_);
    splitter_->addWidget(table_view_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({180, 920});
    setCentralWidget(splitter_);

    setupToolBar();
    setupStatusBar();
    setupTrayIcon();
    setupClipboardMonitor();
    setupWsServer();
    setupShortcuts();

    session_timer_.start();
}

void MainWindow::addDownloadFromUrl(const QString& url, const QString& referer,
                                     const QString& cookie)
{
    if (url.isEmpty()) return;
    manager_->addDownload(url.toStdString(), std::string(),
                          referer.toStdString(), cookie.toStdString());
    statusBar()->showMessage(
        QString::fromUtf8("已从浏览器添加下载: %1").arg(url.left(80)), 5000);
}

// ---------------------------------------------------------------------------
// Drag & Drop
// ---------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList urls;
    if (event->mimeData()->hasUrls()) {
        for (const auto& u : event->mimeData()->urls())
            urls << u.toString();
    } else if (event->mimeData()->hasText()) {
        for (const auto& line : event->mimeData()->text().split('\n', Qt::SkipEmptyParts)) {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith("http://", Qt::CaseInsensitive) ||
                trimmed.startsWith("https://", Qt::CaseInsensitive))
                urls << trimmed;
        }
    }

    for (const auto& url : urls) {
        if (!url.isEmpty())
            manager_->addDownload(url.toStdString(), std::string());
    }

    if (!urls.isEmpty())
        statusBar()->showMessage(
            QString::fromUtf8("已添加 %1 个下载任务").arg(urls.size()), 3000);
}

// ---------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------

void MainWindow::setupShortcuts()
{
    // Delete key
    auto* delShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(delShortcut, &QShortcut::activated, this, &MainWindow::onDelete);

    // Space = toggle pause/resume
    auto* spaceShortcut = new QShortcut(Qt::Key_Space, this);
    connect(spaceShortcut, &QShortcut::activated, this, [this]() {
        auto ids = selectedTaskIds();
        if (ids.isEmpty()) return;
        auto tasks = manager_->getAllTasks();
        for (int id : ids) {
            for (const auto& t : tasks) {
                if (t.task_id == id) {
                    if (t.state == TaskState::Downloading)
                        manager_->pauseTask(id);
                    else if (t.state == TaskState::Paused || t.state == TaskState::Failed)
                        manager_->resumeTask(id);
                    break;
                }
            }
        }
    });

    // Ctrl+A = select all
    auto* selAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    connect(selAllShortcut, &QShortcut::activated, this, [this]() {
        table_view_->selectAll();
    });

    // Ctrl+F = focus search
    auto* searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        search_edit_->setFocus();
        search_edit_->selectAll();
    });

    // Ctrl+D = new download (alternative)
    auto* newDlShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(newDlShortcut, &QShortcut::activated, this, &MainWindow::onNewDownload);

    // Ctrl+E = export
    auto* exportShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this);
    connect(exportShortcut, &QShortcut::activated, this, &MainWindow::onExportTasks);

    // Ctrl+I = import
    auto* importShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), this);
    connect(importShortcut, &QShortcut::activated, this, &MainWindow::onImportTasks);
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------

void MainWindow::setupSidebar()
{
    sidebar_ = new QTreeWidget(this);
    sidebar_->setHeaderLabel(QString::fromUtf8("分类"));
    sidebar_->setIndentation(16);
    sidebar_->setRootIsDecorated(true);
    sidebar_->setFocusPolicy(Qt::NoFocus);

    auto* all = new QTreeWidgetItem(sidebar_, {QString::fromUtf8("全部任务")});
    all->setExpanded(true);
    sidebar_items_["全部任务"] = all;

    auto* catParent = new QTreeWidgetItem(sidebar_, {QString::fromUtf8("文件分类")});
    catParent->setExpanded(true);
    QStringList cats = {"压缩文件", "文档", "音乐", "程序", "视频", "其他"};
    for (const auto& c : cats) {
        auto* item = new QTreeWidgetItem(catParent, {QString::fromUtf8(c.toUtf8())});
        sidebar_items_[c] = item;
    }

    auto* statusParent = new QTreeWidgetItem(sidebar_, {QString::fromUtf8("状态分类")});
    statusParent->setExpanded(true);
    QStringList statuses = {"正在下载", "未完成", "已完成", "失败", "队列"};
    for (const auto& s : statuses) {
        auto* item = new QTreeWidgetItem(statusParent, {QString::fromUtf8(s.toUtf8())});
        sidebar_items_[s] = item;
    }

    connect(sidebar_, &QTreeWidget::itemClicked,
            this, &MainWindow::onSidebarItemClicked);
    sidebar_->setCurrentItem(all);
}

void MainWindow::onSidebarItemClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    QString text = item->text(0);
    // Strip count badge " (N)" from sidebar text
    int paren = text.lastIndexOf(" (");
    if (paren > 0) text = text.left(paren);

    if (text == QString::fromUtf8("文件分类") ||
        text == QString::fromUtf8("状态分类"))
        return;
    model_->setFilter(text);
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

void MainWindow::setupTable()
{
    model_ = new TaskModel(manager_, this);
    table_view_ = new QTableView(this);
    table_view_->setModel(model_);
    table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);  // multi-select
    table_view_->setAlternatingRowColors(true);
    table_view_->setShowGrid(false);
    table_view_->setFocusPolicy(Qt::NoFocus);
    table_view_->verticalHeader()->hide();
    table_view_->verticalHeader()->setDefaultSectionSize(44);
    table_view_->horizontalHeader()->setHighlightSections(false);
    table_view_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Enable column sorting
    table_view_->setSortingEnabled(true);
    table_view_->horizontalHeader()->setSortIndicatorShown(true);
    connect(table_view_->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, [this](int col, Qt::SortOrder order) {
        model_->sort(col, order);
    });

    auto* hdr = table_view_->horizontalHeader();
    hdr->setSectionResizeMode(static_cast<int>(TaskColumn::FileName), QHeaderView::Stretch);
    hdr->resizeSection(static_cast<int>(TaskColumn::FileSize), 90);
    hdr->resizeSection(static_cast<int>(TaskColumn::Progress), 160);
    hdr->resizeSection(static_cast<int>(TaskColumn::Status), 180);
    hdr->resizeSection(static_cast<int>(TaskColumn::Speed), 100);
    hdr->resizeSection(static_cast<int>(TaskColumn::RemainingTime), 90);
    hdr->resizeSection(static_cast<int>(TaskColumn::AddedTime), 130);

    table_view_->setItemDelegateForColumn(
        static_cast<int>(TaskColumn::Progress), new ProgressBarDelegate(table_view_));

    table_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_view_, &QTableView::customContextMenuRequested,
            this, &MainWindow::showContextMenu);

    // Double-click to open file
    connect(table_view_, &QTableView::doubleClicked,
            this, &MainWindow::onDoubleClicked);

    // Empty state overlay
    empty_label_ = new QLabel(table_view_);
    empty_label_->setText(QString::fromUtf8(
        "📥\n\n还没有下载任务\n\n点击「＋ 新建」或拖拽链接到此处开始下载"));
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setStyleSheet(
        "color: #9ca3af; font-size: 15px; background: transparent; padding: 40px;");
    empty_label_->setWordWrap(true);
    empty_label_->hide();

    // Connect model changes to update empty state
    connect(model_, &QAbstractTableModel::rowsInserted, this, [this]() {
        empty_label_->setVisible(model_->rowCount() == 0);
    });
    connect(model_, &QAbstractTableModel::rowsRemoved, this, [this]() {
        empty_label_->setVisible(model_->rowCount() == 0);
    });
    connect(model_, &QAbstractTableModel::dataChanged, this, [this]() {
        empty_label_->setVisible(model_->rowCount() == 0);
        // Resize overlay to match table
        empty_label_->setGeometry(table_view_->rect());
    });
    // Initial state
    empty_label_->setVisible(model_->rowCount() == 0);

    // Connect selection changes to update toolbar button states
    connect(table_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::updateToolBarState);
}

void MainWindow::onDoubleClicked(const QModelIndex& index)
{
    if (!index.isValid()) return;
    const TaskInfo* info = model_->taskInfoAtRow(index.row());
    if (!info) return;

    if (info->state == TaskState::Completed) {
        QString path = QString::fromStdString(info->file_path);
        if (QFileInfo(path).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (info->state == TaskState::Paused || info->state == TaskState::Failed) {
        manager_->resumeTask(info->task_id);
    } else if (info->state == TaskState::Downloading) {
        manager_->pauseTask(info->task_id);
    }
}

// ---------------------------------------------------------------------------
// Toolbar (with search bar)
// ---------------------------------------------------------------------------

static QToolButton* makeBtn(const QString& text, const QString& name, QToolBar* tb) {
    auto* b = new QToolButton(tb);
    b->setText(text);
    b->setObjectName(name);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setCursor(Qt::PointingHandCursor);
    tb->addWidget(b);
    return b;
}

void MainWindow::setupToolBar()
{
    auto* tb = addToolBar(QStringLiteral("工具栏"));
    tb->setMovable(false);
    tb->setFloatable(false);

    auto* btnNew = makeBtn(QString::fromUtf8("＋ 新建"), "btn_new", tb);
    btnNew->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    connect(btnNew, &QToolButton::clicked, this, &MainWindow::onNewDownload);

    auto* btnBatch = makeBtn(QString::fromUtf8("📋 批量"), "btn_batch", tb);
    connect(btnBatch, &QToolButton::clicked, this, &MainWindow::onBatchDownload);

    auto* btnSchedule = makeBtn(QString::fromUtf8("🕐 计划"), "btn_schedule", tb);
    connect(btnSchedule, &QToolButton::clicked, this, &MainWindow::onScheduleDownload);

    tb->addSeparator();

    auto* btnResume = makeBtn(QString::fromUtf8("▶ 继续"), "btn_resume", tb);
    connect(btnResume, &QToolButton::clicked, this, &MainWindow::onResume);
    btn_resume_ = btnResume;

    auto* btnPause = makeBtn(QString::fromUtf8("⏸ 暂停"), "btn_pause", tb);
    connect(btnPause, &QToolButton::clicked, this, &MainWindow::onPause);
    btn_pause_ = btnPause;

    auto* btnDel = makeBtn(QString::fromUtf8("✕ 删除"), "btn_delete", tb);
    connect(btnDel, &QToolButton::clicked, this, &MainWindow::onDelete);
    btn_delete_ = btnDel;

    tb->addSeparator();

    auto* btnStartQ = makeBtn(QString::fromUtf8("▶ 全部开始"), "btn_start_queue", tb);
    connect(btnStartQ, &QToolButton::clicked, this, &MainWindow::onStartQueue);

    auto* btnStopQ = makeBtn(QString::fromUtf8("⏹ 全部停止"), "btn_stop_queue", tb);
    connect(btnStopQ, &QToolButton::clicked, this, &MainWindow::onStopQueue);

    auto* btnDelAll = makeBtn(QString::fromUtf8("🗑 清除已完成"), "btn_delete_all", tb);
    connect(btnDelAll, &QToolButton::clicked, this, &MainWindow::onDeleteAll);

    tb->addSeparator();

    auto* btnSettings = makeBtn(QString::fromUtf8("⚙ 设置"), "btn_settings", tb);
    connect(btnSettings, &QToolButton::clicked, this, &MainWindow::onSettings);

    auto* btnLog = makeBtn(QString::fromUtf8("📋 日志"), "btn_log", tb);
    connect(btnLog, &QToolButton::clicked, this, &MainWindow::onViewLog);

    auto* btnExport = makeBtn(QString::fromUtf8("📤 导出"), "btn_export", tb);
    btnExport->setToolTip(QString::fromUtf8("导出下载列表为 JSON 文件"));
    connect(btnExport, &QToolButton::clicked, this, &MainWindow::onExportTasks);

    auto* btnImport = makeBtn(QString::fromUtf8("📥 导入"), "btn_import", tb);
    btnImport->setToolTip(QString::fromUtf8("从 JSON 或文本文件导入下载链接"));
    connect(btnImport, &QToolButton::clicked, this, &MainWindow::onImportTasks);

    // Add tooltips to all toolbar buttons
    btnNew->setToolTip(QString::fromUtf8("新建下载任务 (Ctrl+N)"));
    btnBatch->setToolTip(QString::fromUtf8("批量添加多个下载链接"));
    btnSchedule->setToolTip(QString::fromUtf8("设置定时下载任务"));
    btnResume->setToolTip(QString::fromUtf8("继续选中的下载"));
    btnPause->setToolTip(QString::fromUtf8("暂停选中的下载"));
    btnDel->setToolTip(QString::fromUtf8("删除选中的任务"));
    btnStartQ->setToolTip(QString::fromUtf8("开始所有暂停的任务"));
    btnStopQ->setToolTip(QString::fromUtf8("暂停所有正在下载的任务"));
    btnDelAll->setToolTip(QString::fromUtf8("清除所有已完成的任务"));
    btnSettings->setToolTip(QString::fromUtf8("打开设置"));
    btnLog->setToolTip(QString::fromUtf8("查看下载日志"));

    // Search bar at the right end
    tb->addSeparator();
    search_edit_ = new QLineEdit(tb);
    search_edit_->setPlaceholderText(QString::fromUtf8("🔍 搜索文件名..."));
    search_edit_->setMaximumWidth(200);
    search_edit_->setClearButtonEnabled(true);
    connect(search_edit_, &QLineEdit::textChanged,
            this, &MainWindow::onSearchChanged);
    tb->addWidget(search_edit_);
}

void MainWindow::onSearchChanged(const QString& text)
{
    model_->setSearch(text);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void MainWindow::setupStatusBar()
{
    speed_label_ = new QLabel(this);
    active_label_ = new QLabel(this);
    completed_label_ = new QLabel(this);
    limit_label_ = new QLabel(this);
    limit_label_->setStyleSheet("color: #d97706; font-weight: 600;");
    limit_label_->hide();
    statusBar()->addPermanentWidget(limit_label_);
    statusBar()->addPermanentWidget(speed_label_);
    statusBar()->addPermanentWidget(active_label_);
    statusBar()->addPermanentWidget(completed_label_);
    connect(&status_timer_, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    connect(&status_timer_, &QTimer::timeout, this, &MainWindow::checkCompletions);
    connect(&status_timer_, &QTimer::timeout, this, &MainWindow::updateToolBarState);
    status_timer_.start(1000);
    updateStatusBar();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QList<int> MainWindow::selectedTaskIds() const
{
    QList<int> ids;
    auto rows = table_view_->selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        int id = model_->taskIdAtRow(idx.row());
        if (id >= 0) ids << id;
    }
    return ids;
}

static QString fmtSpeed(double bps)
{
    if (bps < 1024.0) return QString::number(bps, 'f', 0) + " B/s";
    if (bps < 1024.0 * 1024.0) return QString::number(bps / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bps / (1024.0 * 1024.0), 'f', 2) + " MB/s";
}

// ---------------------------------------------------------------------------
// Download completion notifications
// ---------------------------------------------------------------------------

void MainWindow::checkCompletions()
{
    auto tasks = manager_->getAllTasks();
    QSettings settings;
    bool autoOpen = settings.value("settings/auto_open_folder", false).toBool();

    for (const auto& t : tasks) {
        if (t.state == TaskState::Completed && !notified_tasks_.contains(t.task_id)) {
            notified_tasks_.insert(t.task_id);
            // Show tray notification
            if (tray_icon_ && tray_icon_->isVisible()) {
                tray_icon_->showMessage(
                    QStringLiteral("Super Download"),
                    QString::fromUtf8("下载完成: %1")
                        .arg(QString::fromStdString(t.file_name)),
                    QSystemTrayIcon::Information, 3000);
            }
            // Play system notification sound
#ifdef _WIN32
            MessageBeep(MB_OK);
#endif
            // Auto-open folder if enabled
            if (autoOpen) {
                QString path = QString::fromStdString(t.file_path);
#ifdef _WIN32
                if (QFileInfo(path).exists()) {
                    QString nativePath = QDir::toNativeSeparators(path);
                    QProcess::startDetached("explorer.exe", {"/select,", nativePath});
                }
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(path).absolutePath()));
#endif
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::onNewDownload()
{
    NewDownloadDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString url = dlg.getUrl();
    QString dir = dlg.getSavePath();
    if (!url.isEmpty())
        manager_->addDownload(url.toStdString(), dir.toStdString());
}

void MainWindow::onBatchDownload()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QString::fromUtf8("批量下载"));
    dlg->setMinimumSize(520, 380);

    auto* label = new QLabel(QString::fromUtf8("每行一个下载链接:"), dlg);
    auto* edit = new QPlainTextEdit(dlg);
    edit->setPlaceholderText(QString::fromUtf8(
        "https://example.com/file1.zip\nhttps://example.com/file2.zip\n..."));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(
        QString::fromUtf8("开始下载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(
        QString::fromUtf8("取消"));
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(10);
    lay->addWidget(label);
    lay->addWidget(edit, 1);
    lay->addWidget(buttons);

    if (dlg->exec() == QDialog::Accepted) {
        int count = 0;
        for (const auto& line : edit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
            QString url = line.trimmed();
            if (url.startsWith("http://", Qt::CaseInsensitive) ||
                url.startsWith("https://", Qt::CaseInsensitive)) {
                manager_->addDownload(url.toStdString(), std::string());
                ++count;
            }
        }
        if (count > 0)
            statusBar()->showMessage(
                QString::fromUtf8("已添加 %1 个下载任务").arg(count), 3000);
    }
    delete dlg;
}

void MainWindow::onPause()
{
    for (int id : selectedTaskIds())
        manager_->pauseTask(id);
}

void MainWindow::onResume()
{
    for (int id : selectedTaskIds())
        manager_->resumeTask(id);
}

void MainWindow::onDelete()
{
    auto ids = selectedTaskIds();
    if (ids.isEmpty()) return;

    // Build confirmation dialog with "delete file" checkbox
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QString::fromUtf8("确认删除"));
    dlg->setMinimumWidth(360);

    QString msg = ids.size() == 1
        ? QString::fromUtf8("删除选中的任务？")
        : QString::fromUtf8("删除选中的 %1 个任务？").arg(ids.size());
    auto* label = new QLabel(msg, dlg);
    auto* deleteFileCheck = new QCheckBox(QString::fromUtf8("同时删除已下载的文件"), dlg);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Yes | QDialogButtonBox::No, dlg);
    buttons->button(QDialogButtonBox::Yes)->setText(QString::fromUtf8("删除"));
    buttons->button(QDialogButtonBox::No)->setText(QString::fromUtf8("取消"));
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(20, 20, 20, 20);
    lay->setSpacing(12);
    lay->addWidget(label);
    lay->addWidget(deleteFileCheck);
    lay->addWidget(buttons);

    if (dlg->exec() == QDialog::Accepted) {
        bool deleteFiles = deleteFileCheck->isChecked();
        if (deleteFiles) {
            // Get file paths before removing tasks
            auto tasks = manager_->getAllTasks();
            for (int id : ids) {
                for (const auto& t : tasks) {
                    if (t.task_id == id) {
                        QFile::remove(QString::fromStdString(t.file_path));
                        break;
                    }
                }
            }
        }
        for (int id : ids)
            manager_->removeTask(id);
    }
    delete dlg;
}

void MainWindow::onPauseAll()
{
    auto tasks = manager_->getAllTasks();
    for (const auto& t : tasks) {
        if (t.state == TaskState::Downloading)
            manager_->pauseTask(t.task_id);
    }
}

void MainWindow::onDeleteAll()
{
    if (QMessageBox::question(this, QString::fromUtf8("确认"),
            QString::fromUtf8("删除全部已完成的任务？")) != QMessageBox::Yes)
        return;
    auto tasks = manager_->getAllTasks();
    for (const auto& t : tasks) {
        if (t.state == TaskState::Completed)
            manager_->removeTask(t.task_id);
    }
}

void MainWindow::onStartQueue()
{
    queue_running_ = true;
    auto tasks = manager_->getAllTasks();
    for (const auto& t : tasks) {
        if (t.state == TaskState::Paused || t.state == TaskState::Queued)
            manager_->resumeTask(t.task_id);
    }
}

void MainWindow::onStopQueue()
{
    queue_running_ = false;
    onPauseAll();
}

void MainWindow::onSettings()
{
    ManagerConfig current;
    current.default_save_dir = "";
    current.max_blocks_per_task = 8;
    current.max_concurrent_tasks = 3;
    current.speed_limit = 0;
    SettingsDialog dlg(current, this);
    if (dlg.exec() == QDialog::Accepted)
        manager_->updateConfig(dlg.result());
}

void MainWindow::onScheduleDownload()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QString::fromUtf8("计划下载"));
    dlg->setMinimumWidth(480);

    auto* urlLabel = new QLabel(QString::fromUtf8("下载地址"), dlg);
    auto* urlEdit = new QLineEdit(dlg);
    urlEdit->setPlaceholderText(QString::fromUtf8("粘贴下载链接..."));

    auto* timeLabel = new QLabel(QString::fromUtf8("计划时间"), dlg);
    auto* timeEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), dlg);
    timeEdit->setDisplayFormat("yyyy-MM-dd hh:mm");
    timeEdit->setCalendarPopup(true);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(8);
    layout->addWidget(urlLabel);
    layout->addWidget(urlEdit);
    layout->addSpacing(8);
    layout->addWidget(timeLabel);
    layout->addWidget(timeEdit);
    layout->addSpacing(16);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() != QDialog::Accepted) { delete dlg; return; }

    QString url = urlEdit->text().trimmed();
    QDateTime scheduled = timeEdit->dateTime();
    delete dlg;

    if (url.isEmpty()) return;

    int delay_ms = static_cast<int>(QDateTime::currentDateTime().msecsTo(scheduled));
    if (delay_ms <= 0) {
        manager_->addDownload(url.toStdString(), std::string());
    } else {
        QTimer::singleShot(delay_ms, this, [this, url]() {
            manager_->addDownload(url.toStdString(), std::string());
        });
        QMessageBox::information(this, QStringLiteral("Super Download"),
            QString::fromUtf8("已计划在 %1 开始下载").arg(
                scheduled.toString("yyyy-MM-dd hh:mm")));
    }
}

void MainWindow::onViewLog()
{
    auto logs = Logger::instance().getRecentLogs(200);
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QString::fromUtf8("日志"));
    dlg->resize(720, 420);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    auto* edit = new QTextEdit(dlg);
    edit->setReadOnly(true);
    for (const auto& line : logs)
        edit->append(QString::fromStdString(line));
    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->addWidget(edit);
    dlg->show();
}

void MainWindow::onExportTasks()
{
    QString path = QFileDialog::getSaveFileName(this,
        QString::fromUtf8("导出下载列表"), QString(), "JSON (*.json)");
    if (path.isEmpty()) return;

    auto tasks = manager_->getAllTasks();
    QJsonArray arr;
    for (const auto& t : tasks) {
        QJsonObject obj;
        obj["url"] = QString::fromStdString(t.url);
        obj["file_path"] = QString::fromStdString(t.file_path);
        obj["file_name"] = QString::fromStdString(t.file_name);
        obj["file_size"] = static_cast<qint64>(t.file_size);
        obj["state"] = static_cast<int>(t.state);
        obj["progress"] = t.progress.progress_percent;
        arr.append(obj);
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        statusBar()->showMessage(
            QString::fromUtf8("已导出 %1 个任务").arg(tasks.size()), 3000);
    }
}

void MainWindow::onImportTasks()
{
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("导入下载列表"), QString(), "JSON (*.json);;Text (*.txt)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    int count = 0;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            for (const auto& val : doc.array()) {
                QJsonObject obj = val.toObject();
                QString url = obj["url"].toString();
                if (!url.isEmpty()) {
                    manager_->addDownload(url.toStdString(), std::string());
                    ++count;
                }
            }
        }
    } else {
        // Plain text: one URL per line
        while (!f.atEnd()) {
            QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith("http://", Qt::CaseInsensitive) ||
                line.startsWith("https://", Qt::CaseInsensitive)) {
                manager_->addDownload(line.toStdString(), std::string());
                ++count;
            }
        }
    }
    if (count > 0)
        statusBar()->showMessage(
            QString::fromUtf8("已导入 %1 个任务").arg(count), 3000);
}

void MainWindow::updateStatusBar()
{
    auto tasks = manager_->getAllTasks();
    double total = 0; int active = 0;
    for (const auto& t : tasks) {
        if (t.state == TaskState::Downloading) {
            total += t.progress.speed_bytes_per_sec;
            ++active;
        }
    }
    // Track peak speed
    if (total > peak_speed_) peak_speed_ = total;
    if (active == 0) peak_speed_ = 0.0;

    // Accumulate session download (speed * 1 second interval)
    session_downloaded_ += total;

    speed_label_->setText(QString::fromUtf8("↓ %1").arg(fmtSpeed(total)));

    // Format session downloaded
    auto fmtSize = [](double bytes) -> QString {
        if (bytes < 1024.0) return QString::number(bytes, 'f', 0) + " B";
        if (bytes < 1024.0 * 1024.0) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
        if (bytes < 1024.0 * 1024.0 * 1024.0) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
        return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    };

    // Format uptime
    qint64 upSecs = session_timer_.elapsed() / 1000;
    int uh = static_cast<int>(upSecs / 3600);
    int um = static_cast<int>((upSecs % 3600) / 60);
    QString uptimeStr = uh > 0
        ? QString::fromUtf8("%1时%2分").arg(uh).arg(um, 2, 10, QChar('0'))
        : QString::fromUtf8("%1分").arg(um);

    active_label_->setText(QString::fromUtf8("  活跃: %1  总计: %2  已传输: %3  运行: %4")
                               .arg(active).arg(tasks.size())
                               .arg(fmtSize(session_downloaded_)).arg(uptimeStr));
    int completed = 0;
    for (const auto& t : tasks) {
        if (t.state == TaskState::Completed) ++completed;
    }
    completed_label_->setText(QString::fromUtf8("  已完成: %1").arg(completed));

    // Show speed limit indicator
    QSettings limitSettings;
    int64_t limitKbps = limitSettings.value("settings/speed_limit_kbps", 0).toLongLong();
    if (limitKbps > 0) {
        limit_label_->setText(QString::fromUtf8("🔒 限速 %1 KB/s").arg(limitKbps));
        limit_label_->show();
    } else {
        limit_label_->hide();
    }

    // Update window title with speed info
    if (active > 0) {
        setWindowTitle(QString::fromUtf8("Super Download - ↓ %1  峰值 %2 (%3 个活跃)")
                           .arg(fmtSpeed(total)).arg(fmtSpeed(peak_speed_)).arg(active));
        if (tray_icon_)
            tray_icon_->setToolTip(QString::fromUtf8("Super Download\n↓ %1  峰值 %2\n活跃: %3")
                                       .arg(fmtSpeed(total)).arg(fmtSpeed(peak_speed_)).arg(active));
    } else {
        setWindowTitle(QStringLiteral("Super Download"));
        if (tray_icon_)
            tray_icon_->setToolTip(QStringLiteral("Super Download"));
    }
    updateSidebarCounts();
}

// ---------------------------------------------------------------------------
// Sidebar count badges
// ---------------------------------------------------------------------------

void MainWindow::updateSidebarCounts()
{
    auto tasks = manager_->getAllTasks();
    QMap<QString, int> counts;

    for (const auto& t : tasks) {
        // Category count
        QString cat = TaskModel::classifyFile(t.file_name);
        counts[cat]++;

        // Status count
        if (t.state == TaskState::Completed)
            counts["已完成"]++;
        else if (t.state == TaskState::Queued)
            counts["队列"]++;
        else if (t.state == TaskState::Failed)
            counts["失败"]++;
        else if (t.state == TaskState::Downloading)
            counts["正在下载"]++;

        if (t.state != TaskState::Completed && t.state != TaskState::Cancelled)
            counts["未完成"]++;
    }

    counts["全部任务"] = static_cast<int>(tasks.size());

    for (auto it = sidebar_items_.begin(); it != sidebar_items_.end(); ++it) {
        int cnt = counts.value(it.key(), 0);
        QString base = it.key();
        if (cnt > 0)
            it.value()->setText(0, QString::fromUtf8("%1 (%2)").arg(base).arg(cnt));
        else
            it.value()->setText(0, base);
    }
}

// ---------------------------------------------------------------------------
// Toolbar button state management
// ---------------------------------------------------------------------------

void MainWindow::updateToolBarState()
{
    auto ids = selectedTaskIds();
    bool hasSelection = !ids.isEmpty();

    if (btn_delete_) btn_delete_->setEnabled(hasSelection);

    if (!hasSelection) {
        if (btn_resume_) btn_resume_->setEnabled(false);
        if (btn_pause_) btn_pause_->setEnabled(false);
        return;
    }

    bool canResume = false, canPause = false;
    auto tasks = manager_->getAllTasks();
    for (int id : ids) {
        for (const auto& t : tasks) {
            if (t.task_id == id) {
                if (t.state == TaskState::Downloading) canPause = true;
                if (t.state == TaskState::Paused || t.state == TaskState::Failed ||
                    t.state == TaskState::Queued) canResume = true;
                break;
            }
        }
    }
    if (btn_resume_) btn_resume_->setEnabled(canResume);
    if (btn_pause_) btn_pause_->setEnabled(canPause);
}

// ---------------------------------------------------------------------------
// Context menu (supports multi-select)
// ---------------------------------------------------------------------------

void MainWindow::showContextMenu(const QPoint& pos)
{
    QModelIndex index = table_view_->indexAt(pos);
    auto ids = selectedTaskIds();

    QMenu menu(this);

    if (!ids.isEmpty() && index.isValid()) {
        const TaskInfo* info = model_->taskInfoAtRow(index.row());
        if (!info) return;

        // Single-item actions use the clicked row
        const int task_id = info->task_id;
        const QString file_path = QString::fromStdString(info->file_path);
        const QString url = QString::fromStdString(info->url);
        const TaskState state = info->state;

        bool done = (state == TaskState::Completed);
        bool dl = (state == TaskState::Downloading);
        bool paused = (state == TaskState::Paused);
        bool failed = (state == TaskState::Failed);

        auto* aOpen = menu.addAction(QString::fromUtf8("📄 打开文件"));
        aOpen->setEnabled(done);
        connect(aOpen, &QAction::triggered, this, [file_path]() {
            if (QFileInfo(file_path).exists())
                QDesktopServices::openUrl(QUrl::fromLocalFile(file_path));
        });

        auto* aFolder = menu.addAction(QString::fromUtf8("📂 打开文件夹"));
        aFolder->setEnabled(done || dl || paused);
        connect(aFolder, &QAction::triggered, this, [file_path]() {
#ifdef _WIN32
            // Use Explorer /select to highlight the file
            if (QFileInfo(file_path).exists()) {
                QString nativePath = QDir::toNativeSeparators(file_path);
                QProcess::startDetached("explorer.exe",
                    {"/select,", nativePath});
            } else {
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(file_path).absolutePath()));
            }
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo(file_path).absolutePath()));
#endif
        });

        menu.addSeparator();

        // Multi-select pause/resume
        if (ids.size() > 1) {
            auto* aPauseAll = menu.addAction(
                QString::fromUtf8("⏸  暂停选中 (%1)").arg(ids.size()));
            connect(aPauseAll, &QAction::triggered, this, [this, ids]() {
                for (int id : ids) manager_->pauseTask(id);
            });
            auto* aResumeAll = menu.addAction(
                QString::fromUtf8("▶  恢复选中 (%1)").arg(ids.size()));
            connect(aResumeAll, &QAction::triggered, this, [this, ids]() {
                for (int id : ids) manager_->resumeTask(id);
            });
        } else {
            if (dl) {
                auto* a = menu.addAction(QString::fromUtf8("⏸  暂停"));
                connect(a, &QAction::triggered, this, [this, task_id]() {
                    manager_->pauseTask(task_id);
                });
            }
            if (paused || failed) {
                auto* a = menu.addAction(QString::fromUtf8("▶  恢复"));
                connect(a, &QAction::triggered, this, [this, task_id]() {
                    manager_->resumeTask(task_id);
                });
            }
        }

        menu.addSeparator();

        auto* aUp = menu.addAction(QString::fromUtf8("⬆  上移"));
        connect(aUp, &QAction::triggered, this, [this, task_id]() {
            manager_->moveTaskUp(task_id);
        });
        auto* aDown = menu.addAction(QString::fromUtf8("⬇  下移"));
        connect(aDown, &QAction::triggered, this, [this, task_id]() {
            manager_->moveTaskDown(task_id);
        });

        menu.addSeparator();

        auto* aRedl = menu.addAction(QString::fromUtf8("🔄 重新下载"));
        aRedl->setEnabled(done || failed);
        connect(aRedl, &QAction::triggered, this, [this, task_id, url, file_path]() {
            std::string dir = QFileInfo(file_path).absolutePath().toStdString();
            manager_->removeTask(task_id);
            manager_->addDownload(url.toStdString(), dir);
        });

        auto* aCopy = menu.addAction(QString::fromUtf8("🔗 复制链接"));
        connect(aCopy, &QAction::triggered, this, [url]() {
            QApplication::clipboard()->setText(url);
        });

        auto* aOpenUrl = menu.addAction(QString::fromUtf8("🌐 浏览器打开链接"));
        connect(aOpenUrl, &QAction::triggered, this, [url]() {
            QDesktopServices::openUrl(QUrl(url));
        });

        auto* aCopyName = menu.addAction(QString::fromUtf8("📋 复制文件名"));
        const QString file_name_copy = QString::fromStdString(info->file_name);
        connect(aCopyName, &QAction::triggered, this, [file_name_copy]() {
            QApplication::clipboard()->setText(file_name_copy);
        });

        // Capture all info values by copy for the detail dialog (info pointer may dangle)
        const TaskInfo info_copy = *info;
        auto* aDetail = menu.addAction(QString::fromUtf8("ℹ️  查看详情"));
        connect(aDetail, &QAction::triggered, this, [this, info_copy]() {
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(QString::fromUtf8("任务详情"));
            dlg->setMinimumWidth(480);
            dlg->setAttribute(Qt::WA_DeleteOnClose);

            auto* lay = new QVBoxLayout(dlg);
            lay->setContentsMargins(20, 20, 20, 20);
            lay->setSpacing(6);

            auto addRow = [&](const QString& label, const QString& value) {
                auto* row = new QHBoxLayout;
                auto* lbl = new QLabel(label, dlg);
                lbl->setFixedWidth(80);
                lbl->setStyleSheet("color: #6b7280; font-weight: 600;");
                auto* val = new QLabel(value, dlg);
                val->setTextInteractionFlags(Qt::TextSelectableByMouse);
                val->setWordWrap(true);
                row->addWidget(lbl);
                row->addWidget(val, 1);
                lay->addLayout(row);
            };

            addRow(QString::fromUtf8("文件名"), QString::fromStdString(info_copy.file_name));
            addRow(QString::fromUtf8("大小"), TaskModel::formatFileSize(info_copy.file_size));
            addRow(QString::fromUtf8("状态"), TaskModel::stateToString(info_copy.state));
            addRow(QString::fromUtf8("进度"), QString("%1%").arg(info_copy.progress.progress_percent, 0, 'f', 1));
            addRow(QString::fromUtf8("路径"), QString::fromStdString(info_copy.file_path));
            addRow(QString::fromUtf8("链接"), QString::fromStdString(info_copy.url));

            auto* closeBtn = new QPushButton(QString::fromUtf8("关闭"), dlg);
            connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
            lay->addSpacing(12);
            lay->addWidget(closeBtn, 0, Qt::AlignRight);

            dlg->show();
        });

        menu.addSeparator();

        // Select all files of same type
        const QString file_category = TaskModel::classifyFile(info->file_name);
        auto* aSelectType = menu.addAction(
            QString::fromUtf8("☑ 全选「%1」类型").arg(file_category));
        connect(aSelectType, &QAction::triggered, this, [this, file_category]() {
            table_view_->clearSelection();
            for (int r = 0; r < model_->rowCount(); ++r) {
                const TaskInfo* ti = model_->taskInfoAtRow(r);
                if (ti && TaskModel::classifyFile(ti->file_name) == file_category) {
                    table_view_->selectRow(r);
                }
            }
        });

        // Delete (multi-select aware)
        QString delText = ids.size() > 1
            ? QString::fromUtf8("🗑  删除选中 (%1)").arg(ids.size())
            : QString::fromUtf8("🗑  删除");
        auto* aDel = menu.addAction(delText);
        connect(aDel, &QAction::triggered, this, [this]() {
            onDelete();  // Reuse the enhanced delete with file option
        });
    } else {
        auto* a = menu.addAction(QString::fromUtf8("＋ 新建下载"));
        connect(a, &QAction::triggered, this, &MainWindow::onNewDownload);
        auto* b = menu.addAction(QString::fromUtf8("📋 批量下载"));
        connect(b, &QAction::triggered, this, &MainWindow::onBatchDownload);
    }

    menu.exec(table_view_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Clipboard monitor
// ---------------------------------------------------------------------------

void MainWindow::setupClipboardMonitor()
{
    clipboard_monitor_ = new ClipboardMonitor(this);
    connect(clipboard_monitor_, &ClipboardMonitor::urlDetected,
            this, [this](const QString& url) {
        if (!isVisible()) { showNormal(); activateWindow(); }
        NewDownloadDialog dlg(QString(), this);
        dlg.setUrl(url);
        if (dlg.exec() == QDialog::Accepted) {
            QString u = dlg.getUrl(), d = dlg.getSavePath();
            if (!u.isEmpty())
                manager_->addDownload(u.toStdString(), d.toStdString());
        }
    });
}

// ---------------------------------------------------------------------------
// WebSocket server
// ---------------------------------------------------------------------------

void MainWindow::setupWsServer()
{
    ws_server_ = new WsServer(18615, this);
    connect(ws_server_, &WsServer::downloadRequested,
            this, [this](const QString& url, const QString&, const QString& referrer, const QString& cookie) {
        if (!url.isEmpty())
            manager_->addDownload(url.toStdString(), std::string(),
                                  referrer.toStdString(), cookie.toStdString());
    });
    ws_server_->start();
}

// ---------------------------------------------------------------------------
// System tray
// ---------------------------------------------------------------------------

void MainWindow::setupTrayIcon()
{
    tray_menu_ = new QMenu(this);
    tray_menu_->addAction(QString::fromUtf8("显示/隐藏"), this, &MainWindow::toggleVisibility);
    tray_menu_->addSeparator();
    tray_menu_->addAction(QString::fromUtf8("新建下载"), this, &MainWindow::onNewDownload);
    tray_menu_->addAction(QString::fromUtf8("全部暂停"), this, &MainWindow::onPauseAll);
    tray_menu_->addAction(QString::fromUtf8("全部继续"), this, &MainWindow::onStartQueue);
    tray_menu_->addSeparator();
    tray_menu_->addAction(QString::fromUtf8("导入列表"), this, &MainWindow::onImportTasks);
    tray_menu_->addAction(QString::fromUtf8("导出列表"), this, &MainWindow::onExportTasks);
    tray_menu_->addSeparator();
    tray_menu_->addAction(QString::fromUtf8("退出"), qApp, &QApplication::quit);

    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setIcon(QApplication::windowIcon().isNull()
        ? QIcon::fromTheme("application-x-executable")
        : QApplication::windowIcon());
    tray_icon_->setToolTip(QStringLiteral("Super Download"));
    tray_icon_->setContextMenu(tray_menu_);
    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
    tray_icon_->show();
}

void MainWindow::toggleVisibility()
{
    if (isVisible()) hide();
    else { showNormal(); activateWindow(); }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        showNormal(); activateWindow();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save window geometry
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());

    // Check for active downloads
    auto tasks = manager_->getAllTasks();
    int active = 0;
    for (const auto& t : tasks) {
        if (t.state == TaskState::Downloading) ++active;
    }

    if (tray_icon_ && tray_icon_->isVisible()) {
        // If active downloads, show a reminder in tray
        if (active > 0) {
            tray_icon_->showMessage(
                QStringLiteral("Super Download"),
                QString::fromUtf8("仍有 %1 个任务正在下载，程序已最小化到托盘").arg(active),
                QSystemTrayIcon::Information, 3000);
        }
        hide();
        event->ignore();
    } else {
        if (active > 0) {
            auto ret = QMessageBox::question(this,
                QStringLiteral("Super Download"),
                QString::fromUtf8("还有 %1 个任务正在下载，确定退出吗？").arg(active),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) {
                event->ignore();
                return;
            }
        }
        event->accept();
    }
}
