#pragma once

#include <QMainWindow>
#include <QTableView>
#include <QTreeWidget>
#include <QSplitter>
#include <QLabel>
#include <QTimer>
#include <QAction>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QString>
#include <QSet>
#include <QLineEdit>
#include <QVector>
#include <QElapsedTimer>

class DownloadManager;
class TaskModel;
class ProgressBarDelegate;
class ClipboardMonitor;
class WsServer;

/// Main application window with sidebar, task table, toolbar, and status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(DownloadManager* manager, QWidget* parent = nullptr);
    ~MainWindow() override = default;

    /// Add a download from a protocol URL (called from single-instance handler)
    void addDownloadFromUrl(const QString& url, const QString& referer = QString(),
                            const QString& cookie = QString());

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onNewDownload();
    void onBatchDownload();
    void onPause();
    void onResume();
    void onDelete();
    void onSettings();
    void onViewLog();
    void updateStatusBar();
    void toggleVisibility();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

    // Batch operations
    void onPauseAll();
    void onDeleteAll();
    void onStartQueue();
    void onStopQueue();

    // Scheduled download
    void onScheduleDownload();

    // Context menu
    void showContextMenu(const QPoint& pos);

    // Import/Export
    void onExportTasks();
    void onImportTasks();

    // Sidebar filter
    void onSidebarItemClicked(QTreeWidgetItem* item, int column);

    // Table interactions
    void onDoubleClicked(const QModelIndex& index);
    void onSearchChanged(const QString& text);

    // Completion check
    void checkCompletions();

    // Update toolbar button states
    void updateToolBarState();

private:
    void setupSidebar();
    void setupTable();
    void setupToolBar();
    void setupStatusBar();
    void setupTrayIcon();
    void setupClipboardMonitor();
    void setupWsServer();
    void setupShortcuts();
    void updateSidebarCounts();

    QList<int> selectedTaskIds() const;

    DownloadManager* manager_;
    TaskModel* model_;
    QTableView* table_view_;
    QTreeWidget* sidebar_;
    QSplitter* splitter_;
    QLineEdit* search_edit_;

    // Toolbar buttons for state management
    QToolButton* btn_resume_ = nullptr;
    QToolButton* btn_pause_ = nullptr;
    QToolButton* btn_delete_ = nullptr;

    // Empty state overlay
    QLabel* empty_label_;

    // Status bar
    QLabel* speed_label_;
    QLabel* active_label_;
    QLabel* completed_label_;
    QLabel* limit_label_;
    QTimer status_timer_;

    // System tray
    QSystemTrayIcon* tray_icon_ = nullptr;
    QMenu* tray_menu_ = nullptr;

    // Browser sniffing
    ClipboardMonitor* clipboard_monitor_ = nullptr;
    WsServer* ws_server_ = nullptr;

    // Queue control
    bool queue_running_ = true;

    // Track completed tasks for notifications
    QSet<int> notified_tasks_;

    // Sidebar count labels
    QMap<QString, QTreeWidgetItem*> sidebar_items_;

    // Speed history for peak tracking
    double peak_speed_ = 0.0;

    // Session stats
    double session_downloaded_ = 0.0;  // bytes downloaded this session
    QElapsedTimer session_timer_;
};
