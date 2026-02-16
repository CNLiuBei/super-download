#pragma once

#include <QObject>
#include <QClipboard>
#include <QTimer>
#include <QString>
#include <QSet>

/// Monitors the system clipboard for URLs and emits a signal when a new
/// download-worthy link is detected.
class ClipboardMonitor : public QObject {
    Q_OBJECT

public:
    explicit ClipboardMonitor(QObject* parent = nullptr);

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

signals:
    /// Emitted when a new download URL is detected on the clipboard.
    void urlDetected(const QString& url);

private slots:
    void onClipboardChanged();

private:
    bool looksLikeDownloadUrl(const QString& url) const;

    bool enabled_ = true;
    QString last_url_;
    QSet<QString> seen_urls_;
};
