#include "clipboard_monitor.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QUrl>
#include <QRegularExpression>

ClipboardMonitor::ClipboardMonitor(QObject* parent)
    : QObject(parent)
{
    auto* clipboard = QApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged,
            this, &ClipboardMonitor::onClipboardChanged);
}

void ClipboardMonitor::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

void ClipboardMonitor::onClipboardChanged()
{
    if (!enabled_) return;

    auto* clipboard = QApplication::clipboard();
    QString text = clipboard->text().trimmed();

    if (text.isEmpty() || text == last_url_) return;
    last_url_ = text;

    if (!looksLikeDownloadUrl(text)) return;

    // Don't emit for the same URL twice
    if (seen_urls_.contains(text)) return;
    seen_urls_.insert(text);

    emit urlDetected(text);
}

bool ClipboardMonitor::looksLikeDownloadUrl(const QString& url) const
{
    // Must start with http:// or https://
    if (!url.startsWith("http://", Qt::CaseInsensitive) &&
        !url.startsWith("https://", Qt::CaseInsensitive)) {
        return false;
    }

    // Must be a valid URL
    QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty()) return false;

    // Check if the URL path ends with a known download extension
    QString path = parsed.path().toLower();
    int qmark = path.indexOf('?');
    if (qmark >= 0) path = path.left(qmark);

    QStringList exts = SettingsDialog::fileTypes();
    for (const auto& ext : exts) {
        if (path.endsWith("." + ext)) return true;
    }

    // Also match common download patterns in URL
    static QRegularExpression downloadPattern(
        R"((download|dl|get|fetch|release|attachment))",
        QRegularExpression::CaseInsensitiveOption);
    if (downloadPattern.match(url).hasMatch()) {
        // Has download-like keyword and is a valid URL
        return true;
    }

    return false;
}
