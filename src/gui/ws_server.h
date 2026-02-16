#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QString>

/// Lightweight local WebSocket server using QTcpServer.
/// Receives download URLs from browser extensions on ws://127.0.0.1:18615.
class WsServer : public QObject {
    Q_OBJECT

public:
    explicit WsServer(quint16 port = 18615, QObject* parent = nullptr);
    ~WsServer() override;

    bool start();
    void stop();
    bool isListening() const;

signals:
    void downloadRequested(const QString& url, const QString& filename,
                           const QString& referrer, const QString& cookie);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleHttpUpgrade(QTcpSocket* socket, const QByteArray& request);
    void handleWebSocketFrame(QTcpSocket* socket);
    void processMessage(QTcpSocket* socket, const QByteArray& payload);
    void sendWsText(QTcpSocket* socket, const QByteArray& data);

    QTcpServer* server_;
    QList<QTcpSocket*> clients_;
    // Track which sockets have completed the WebSocket handshake
    QSet<QTcpSocket*> upgraded_;
    // Buffer for partial reads
    QMap<QTcpSocket*, QByteArray> buffers_;
    quint16 port_;
};
