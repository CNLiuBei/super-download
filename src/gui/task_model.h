#pragma once

#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QString>
#include <vector>

#include "../core/task.h"

class DownloadManager;

enum class TaskColumn : int {
    FileName = 0,
    FileSize,
    Progress,
    Status,
    Speed,
    RemainingTime,
    AddedTime,
    Count
};

/// Table model with category/status filtering.
class TaskModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit TaskModel(DownloadManager* manager, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    int taskIdAtRow(int row) const;
    const TaskInfo* taskInfoAtRow(int row) const;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    /// Set filter. Empty string = show all.
    /// Category filters: "压缩文件", "文档", "音乐", "程序", "视频", "其他"
    /// Status filters: "未完成", "已完成", "队列"
    void setFilter(const QString& filter);
    QString currentFilter() const { return filter_; }

    void setSearch(const QString& text);
    QString currentSearch() const { return search_; }

    static QString classifyFile(const std::string& filename);

    static QString formatFileSize(int64_t bytes);
    static QString formatSpeed(double bytes_per_sec);
    static QString formatRemainingTime(int seconds);
    static QString stateToString(TaskState state);

private slots:
    void refresh();

private:
    bool matchesFilter(const TaskInfo& t) const;

    DownloadManager* manager_;
    QTimer timer_;
    std::vector<TaskInfo> all_tasks_;     // unfiltered
    std::vector<TaskInfo> tasks_;         // filtered (displayed)
    QString filter_;
    QString search_;
    int sort_column_ = -1;
    Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};

/// Custom delegate for progress bar column.
class ProgressBarDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};
