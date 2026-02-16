#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>
#include <QUrlQuery>

// Suppress Qt font warnings on Windows (stylesheet pixel sizes can trigger these)
#if defined(Q_OS_WIN) || defined(_WIN32)
static void messageFilter(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (type == QtWarningMsg && msg.contains("QFont::setPointSize"))
        return;
    if (type == QtFatalMsg)
        abort();
}
#endif

#include "core/download_manager.h"
#include "gui/main_window.h"
#include "gui/style.h"

static const char* kLocalServerName = "SuperDownloadSingleInstance";

static QString historyFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/history.json";
}

static void saveHistory(DownloadManager& mgr) {
    auto tasks = mgr.getAllTasks();
    QJsonArray arr;
    for (const auto& t : tasks) {
        if (t.state == TaskState::Completed) {
            QJsonObject obj;
            obj["url"] = QString::fromStdString(t.url);
            obj["file_path"] = QString::fromStdString(t.file_path);
            obj["file_name"] = QString::fromStdString(t.file_name);
            obj["file_size"] = static_cast<qint64>(t.file_size);
            arr.append(obj);
        }
    }
    QString histDir = QFileInfo(historyFilePath()).absolutePath();
    if (!histDir.isEmpty()) QDir().mkpath(histDir);
    QFile f(historyFilePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

/// Parse a superdownload:// URL into download url and referer.
/// Format: superdownload://download?url=ENCODED_URL&referer=ENCODED_REFERER&cookie=ENCODED_COOKIE
struct ProtocolParams {
    QString url;
    QString referer;
    QString cookie;
};

static ProtocolParams parseProtocolUrl(const QString& raw) {
    ProtocolParams p;
    // Handle both "superdownload://download?..." and "superdownload:download?..."
    QString normalized = raw;
    if (normalized.startsWith("superdownload://"))
        normalized = "http://dummy/" + normalized.mid(16);
    else if (normalized.startsWith("superdownload:"))
        normalized = "http://dummy/" + normalized.mid(14);
    else
        return p;

    QUrl parsed(normalized);
    QUrlQuery query(parsed);
    p.url = query.queryItemValue("url", QUrl::FullyDecoded);
    p.referer = query.queryItemValue("referer", QUrl::FullyDecoded);
    p.cookie = query.queryItemValue("cookie", QUrl::FullyDecoded);
    return p;
}

int main(int argc, char* argv[])
{
#if defined(Q_OS_WIN) || defined(_WIN32)
    qInstallMessageHandler(messageFilter);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("Super Download");
    app.setOrganizationName("SuperDownload");

    // Check for protocol URL in command line args
    QString protocolArg;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("superdownload:", Qt::CaseInsensitive)) {
            protocolArg = arg;
            break;
        }
    }

    // Single-instance check: try to connect to existing instance
    {
        QLocalSocket probe;
        probe.connectToServer(kLocalServerName);
        if (probe.waitForConnected(1000)) {
            // Another instance is running
            if (!protocolArg.isEmpty()) {
                // Send the protocol URL to the running instance
                probe.write(protocolArg.toUtf8());
                probe.waitForBytesWritten(2000);
            } else {
                // Just send a "show" command to bring window to front
                probe.write("show");
                probe.waitForBytesWritten(2000);
            }
            probe.disconnectFromServer();
            return 0;
        }
    }

    // We are the first instance — start the local server
    // Remove any stale server (e.g. after crash)
    QLocalServer::removeServer(kLocalServerName);
    QLocalServer localServer;
    localServer.listen(kLocalServerName);

    // --- App icon ---
    QString iconPath;
    QStringList iconSearchPaths = {
        app.applicationDirPath() + "/logo.png",
        app.applicationDirPath() + "/../logo.png",
        app.applicationDirPath() + "/../../../logo.png",
    };
    for (const auto& p : iconSearchPaths) {
        if (QFileInfo::exists(p)) { iconPath = p; break; }
    }
    if (!iconPath.isEmpty())
        app.setWindowIcon(QIcon(iconPath));

    // --- Settings ---
    QSettings settings;
    bool startMinimized = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--minimized")
            startMinimized = true;
    }

    ManagerConfig config;
    config.default_save_dir = settings.value("settings/save_dir",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        .toString().toStdString();
    config.max_concurrent_tasks = settings.value("settings/max_tasks", 3).toInt();
    config.max_blocks_per_task = settings.value("settings/max_blocks", 8).toInt();
    int64_t limitKbps = settings.value("settings/speed_limit_kbps", 0).toLongLong();
    config.speed_limit = limitKbps * 1024;

    DownloadManager manager(config);
    manager.recoverTasks();

    // Apply stylesheet
    app.setStyleSheet(appStyleSheet());

    MainWindow window(&manager);
    if (startMinimized)
        window.hide();
    else
        window.show();

    // Handle protocol URL from command line (first launch with protocol)
    if (!protocolArg.isEmpty()) {
        auto params = parseProtocolUrl(protocolArg);
        if (!params.url.isEmpty()) {
            window.addDownloadFromUrl(params.url, params.referer, params.cookie);
        }
    }

    // Listen for messages from other instances (protocol URLs or "show")
    QObject::connect(&localServer, &QLocalServer::newConnection, [&]() {
        while (localServer.hasPendingConnections()) {
            QLocalSocket* client = localServer.nextPendingConnection();
            if (!client) continue;

            QObject::connect(client, &QLocalSocket::readyRead, [&window, &manager, client]() {
                QByteArray data = client->readAll();
                QString msg = QString::fromUtf8(data).trimmed();

                if (msg == "show") {
                    window.showNormal();
                    window.activateWindow();
                    window.raise();
                } else if (msg.startsWith("superdownload:", Qt::CaseInsensitive)) {
                    auto params = parseProtocolUrl(msg);
                    if (!params.url.isEmpty()) {
                        window.addDownloadFromUrl(params.url, params.referer, params.cookie);
                        window.showNormal();
                        window.activateWindow();
                    }
                }

                client->disconnectFromServer();
                client->deleteLater();
            });
        }
    });

    // Save history on exit
    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        saveHistory(manager);
    });

    return app.exec();
}
