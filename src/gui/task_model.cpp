#include "task_model.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QApplication>
#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <algorithm>

#include "../core/download_manager.h"

// File extension -> category mapping
static const QMap<QString, QString>& extCategoryMap() {
    static QMap<QString, QString> m;
    if (m.isEmpty()) {
        for (auto& e : {"zip","rar","7z","tar","gz","bz2","xz","iso","dmg"})
            m[e] = QStringLiteral("压缩文件");
        for (auto& e : {"pdf","doc","docx","xls","xlsx","ppt","pptx","txt","rtf","odt"})
            m[e] = QStringLiteral("文档");
        for (auto& e : {"mp3","flac","wav","aac","ogg","wma","m4a"})
            m[e] = QStringLiteral("音乐");
        for (auto& e : {"exe","msi","deb","rpm","apk","appimage","bat","sh"})
            m[e] = QStringLiteral("程序");
        for (auto& e : {"mp4","mkv","avi","mov","wmv","flv","webm","m4v","ts"})
            m[e] = QStringLiteral("视频");
    }
    return m;
}

TaskModel::TaskModel(DownloadManager* manager, QObject* parent)
    : QAbstractTableModel(parent), manager_(manager) {
    connect(&timer_, &QTimer::timeout, this, &TaskModel::refresh);
    timer_.start(500);
}

int TaskModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(tasks_.size());
}

int TaskModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(TaskColumn::Count);
}

QVariant TaskModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(tasks_.size()))
        return {};

    const auto& t = tasks_[index.row()];
    const auto col = static_cast<TaskColumn>(index.column());

    if (role == Qt::DisplayRole) {
        switch (col) {
        case TaskColumn::FileName:      return QString::fromStdString(t.file_name);
        case TaskColumn::FileSize:      return formatFileSize(t.file_size);
        case TaskColumn::Progress:      return QString("%1%").arg(t.progress.progress_percent, 0, 'f', 1);
        case TaskColumn::Speed:
            return t.state == TaskState::Downloading ? formatSpeed(t.progress.speed_bytes_per_sec) : QStringLiteral("--");
        case TaskColumn::RemainingTime:
            return t.state == TaskState::Downloading ? formatRemainingTime(t.progress.remaining_seconds) : QStringLiteral("--");
        case TaskColumn::Status:        return t.state == TaskState::Failed && !t.error_message.empty()
                                                ? QString::fromUtf8("失败: %1").arg(QString::fromStdString(t.error_message))
                                                : stateToString(t.state);
        case TaskColumn::AddedTime:     return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
        default: break;
        }
    }

    // File type icon prefix for FileName column
    if (role == Qt::DecorationRole && col == TaskColumn::FileName) {
        QString cat = classifyFile(t.file_name);
        // Return a colored square as a simple icon indicator
        QPixmap px(14, 14);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c;
        if (cat == "视频")        c = QColor(0xef, 0x44, 0x44);  // red
        else if (cat == "音乐")   c = QColor(0xa8, 0x55, 0xf7);  // purple
        else if (cat == "文档")   c = QColor(0x25, 0x63, 0xeb);  // blue
        else if (cat == "压缩文件") c = QColor(0xf5, 0x9e, 0x0b); // amber
        else if (cat == "程序")   c = QColor(0x16, 0xa3, 0x4a);  // green
        else                      c = QColor(0x6b, 0x72, 0x80);  // gray
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(1, 1, 12, 12, 3, 3);
        p.end();
        return QIcon(px);
    }

    if (role == Qt::ToolTipRole && col == TaskColumn::Status) {
        if (t.state == TaskState::Failed && !t.error_message.empty())
            return QString::fromStdString(t.error_message);
        return {};
    }

    if (role == Qt::ToolTipRole && col == TaskColumn::FileName) {
        return QString::fromStdString(t.url);
    }

    if (role == Qt::UserRole && col == TaskColumn::Progress)
        return t.progress.progress_percent;

    if (role == Qt::UserRole + 1 && col == TaskColumn::Progress)
        return static_cast<int>(t.state);

    if (role == Qt::UserRole + 2 && col == TaskColumn::Progress)
        return static_cast<qint64>(t.file_size);

    if (role == Qt::ForegroundRole && col == TaskColumn::Status) {
        switch (t.state) {
        case TaskState::Downloading: return QColor(0x25, 0x63, 0xeb);
        case TaskState::Completed:   return QColor(0x16, 0xa3, 0x4a);
        case TaskState::Failed:      return QColor(0xdc, 0x26, 0x26);
        case TaskState::Paused:      return QColor(0xd9, 0x77, 0x06);
        case TaskState::Cancelled:   return QColor(0x9c, 0xa3, 0xaf);
        default:                     return QColor(0x6b, 0x72, 0x80);
        }
    }

    return {};
}

QVariant TaskModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (static_cast<TaskColumn>(section)) {
    case TaskColumn::FileName:      return QStringLiteral("文件名");
    case TaskColumn::FileSize:      return QStringLiteral("大小");
    case TaskColumn::Progress:      return QStringLiteral("进度");
    case TaskColumn::Status:        return QStringLiteral("状态");
    case TaskColumn::Speed:         return QStringLiteral("速度");
    case TaskColumn::RemainingTime: return QStringLiteral("剩余时间");
    case TaskColumn::AddedTime:     return QStringLiteral("添加时间");
    default: break;
    }
    return {};
}

int TaskModel::taskIdAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(tasks_.size())) return -1;
    return tasks_[row].task_id;
}

const TaskInfo* TaskModel::taskInfoAtRow(int row) const {
    if (row < 0 || row >= static_cast<int>(tasks_.size())) return nullptr;
    return &tasks_[row];
}

void TaskModel::setFilter(const QString& filter) {
    filter_ = filter;
    refresh();
}

void TaskModel::setSearch(const QString& text) {
    search_ = text.trimmed();
    refresh();
}

void TaskModel::sort(int column, Qt::SortOrder order) {
    sort_column_ = column;
    sort_order_ = order;
    refresh();
}

QString TaskModel::classifyFile(const std::string& filename) {
    QString name = QString::fromStdString(filename).toLower();
    int dot = name.lastIndexOf('.');
    if (dot < 0) return QStringLiteral("其他");
    QString ext = name.mid(dot + 1);
    return extCategoryMap().value(ext, QStringLiteral("其他"));
}

bool TaskModel::matchesFilter(const TaskInfo& t) const {
    // Search filter
    if (!search_.isEmpty()) {
        QString name = QString::fromStdString(t.file_name);
        if (!name.contains(search_, Qt::CaseInsensitive))
            return false;
    }

    if (filter_.isEmpty() || filter_ == QStringLiteral("全部任务"))
        return true;

    // Status filters
    if (filter_ == QStringLiteral("正在下载"))
        return t.state == TaskState::Downloading;
    if (filter_ == QStringLiteral("未完成"))
        return t.state != TaskState::Completed && t.state != TaskState::Cancelled;
    if (filter_ == QStringLiteral("已完成"))
        return t.state == TaskState::Completed;
    if (filter_ == QStringLiteral("失败"))
        return t.state == TaskState::Failed;
    if (filter_ == QStringLiteral("队列"))
        return t.state == TaskState::Queued;

    // Category filters
    return classifyFile(t.file_name) == filter_;
}

void TaskModel::refresh() {
    if (!manager_) return;

    auto snapshot = manager_->getAllTasks();

    // Apply filter
    std::vector<TaskInfo> filtered;
    for (auto& t : snapshot) {
        if (matchesFilter(t))
            filtered.push_back(std::move(t));
    }

    // Apply sorting
    if (sort_column_ >= 0) {
        auto cmp = [this](const TaskInfo& a, const TaskInfo& b) -> bool {
            bool less = false;
            switch (static_cast<TaskColumn>(sort_column_)) {
            case TaskColumn::FileName:
                less = a.file_name < b.file_name; break;
            case TaskColumn::FileSize:
                less = a.file_size < b.file_size; break;
            case TaskColumn::Progress:
                less = a.progress.progress_percent < b.progress.progress_percent; break;
            case TaskColumn::Status:
                less = static_cast<int>(a.state) < static_cast<int>(b.state); break;
            case TaskColumn::Speed:
                less = a.progress.speed_bytes_per_sec < b.progress.speed_bytes_per_sec; break;
            case TaskColumn::RemainingTime:
                less = a.progress.remaining_seconds < b.progress.remaining_seconds; break;
            default: less = a.task_id < b.task_id; break;
            }
            return sort_order_ == Qt::AscendingOrder ? less : !less;
        };
        std::sort(filtered.begin(), filtered.end(), cmp);
    }

    int old_count = static_cast<int>(tasks_.size());
    int new_count = static_cast<int>(filtered.size());

    if (new_count > old_count) {
        beginInsertRows(QModelIndex(), old_count, new_count - 1);
        tasks_ = std::move(filtered);
        endInsertRows();
        if (old_count > 0)
            emit dataChanged(index(0, 0), index(old_count - 1, columnCount() - 1));
    } else if (new_count < old_count) {
        beginRemoveRows(QModelIndex(), new_count, old_count - 1);
        tasks_ = std::move(filtered);
        endRemoveRows();
        if (new_count > 0)
            emit dataChanged(index(0, 0), index(new_count - 1, columnCount() - 1));
    } else {
        tasks_ = std::move(filtered);
        if (new_count > 0)
            emit dataChanged(index(0, 0), index(new_count - 1, columnCount() - 1));
    }
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

QString TaskModel::formatFileSize(int64_t bytes) {
    if (bytes <= 0) return QStringLiteral("--");
    constexpr double KB = 1024.0, MB = KB * 1024.0, GB = MB * 1024.0;
    if (bytes >= GB) return QString("%1 GB").arg(bytes / GB, 0, 'f', 2);
    if (bytes >= MB) return QString("%1 MB").arg(bytes / MB, 0, 'f', 2);
    if (bytes >= KB) return QString("%1 KB").arg(bytes / KB, 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

QString TaskModel::formatSpeed(double bps) {
    if (bps <= 0.0) return QStringLiteral("--");
    constexpr double KB = 1024.0, MB = KB * 1024.0;
    if (bps >= MB) return QString("%1 MB/s").arg(bps / MB, 0, 'f', 2);
    if (bps >= KB) return QString("%1 KB/s").arg(bps / KB, 0, 'f', 1);
    return QString("%1 B/s").arg(bps, 0, 'f', 0);
}

QString TaskModel::formatRemainingTime(int seconds) {
    if (seconds <= 0) return QStringLiteral("--");
    int h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0) return QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    return QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
}

QString TaskModel::stateToString(TaskState state) {
    switch (state) {
    case TaskState::Queued:      return QStringLiteral("等待中");
    case TaskState::Downloading: return QStringLiteral("下载中");
    case TaskState::Paused:      return QStringLiteral("已暂停");
    case TaskState::Completed:   return QStringLiteral("已完成");
    case TaskState::Failed:      return QStringLiteral("失败");
    case TaskState::Cancelled:   return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

// ---------------------------------------------------------------------------
// ProgressBarDelegate
// ---------------------------------------------------------------------------

void ProgressBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    bool ok = false;
    double percent = index.data(Qt::UserRole).toDouble(&ok);
    if (!ok) { QStyledItemDelegate::paint(painter, option, index); return; }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF bar = QRectF(option.rect).adjusted(12, 12, -12, -12);
    double r = bar.height() / 2.0;

    int si = index.data(Qt::UserRole + 1).toInt();
    auto state = static_cast<TaskState>(si);
    QColor fill;
    switch (state) {
    case TaskState::Downloading: fill = QColor(0x25,0x63,0xeb); break;
    case TaskState::Completed:   fill = QColor(0x16,0xa3,0x4a); break;
    case TaskState::Failed:      fill = QColor(0xdc,0x26,0x26); break;
    case TaskState::Paused:      fill = QColor(0xd9,0x77,0x06); break;
    default:                     fill = QColor(0x9c,0xa3,0xaf); break;
    }

    QPainterPath bg; bg.addRoundedRect(bar, r, r);
    painter->fillPath(bg, QColor(0xe5,0xe7,0xeb));

    if (percent > 0.0) {
        double w = bar.width() * std::min(percent, 100.0) / 100.0;
        if (w < r * 2) w = r * 2;
        QPainterPath fp; fp.addRoundedRect(QRectF(bar.left(), bar.top(), w, bar.height()), r, r);
        // Gradient fill for active downloads
        if (state == TaskState::Downloading) {
            QLinearGradient grad(bar.left(), 0, bar.left() + w, 0);
            grad.setColorAt(0.0, QColor(0x25, 0x63, 0xeb));
            grad.setColorAt(1.0, QColor(0x60, 0xa5, 0xfa));
            painter->fillPath(fp, grad);
        } else {
            painter->fillPath(fp, fill);
        }
    }

    painter->setPen(QColor(0x33,0x33,0x33));
    QFont f = painter->font(); f.setPixelSize(11); f.setWeight(QFont::Medium);
    painter->setFont(f);

    // Show "downloaded / total" or just percent
    qint64 fileSize = index.data(Qt::UserRole + 2).toLongLong();
    QString label;
    if (fileSize > 0 && percent < 100.0) {
        double downloaded = fileSize * percent / 100.0;
        auto fmtSz = [](double b) -> QString {
            if (b < 1024.0) return QString::number(b, 'f', 0) + "B";
            if (b < 1024.0*1024.0) return QString::number(b/1024.0, 'f', 0) + "K";
            if (b < 1024.0*1024.0*1024.0) return QString::number(b/(1024.0*1024.0), 'f', 1) + "M";
            return QString::number(b/(1024.0*1024.0*1024.0), 'f', 1) + "G";
        };
        label = QString("%1 / %2").arg(fmtSz(downloaded)).arg(fmtSz(fileSize));
    } else {
        label = QString("%1%").arg(percent, 0, 'f', 1);
    }
    painter->drawText(bar, Qt::AlignCenter, label);
    painter->restore();
}
