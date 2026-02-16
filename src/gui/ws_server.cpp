#include "ws_server.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

// WebSocket GUID for handshake per RFC 6455
static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WsServer::WsServer(quint16 port, QObject* parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
    , port_(port)
{
}

WsServer::~WsServer()
{
    stop();
}

bool WsServer::start()
{
    if (server_->isListening()) return true;

    connect(server_, &QTcpServer::newConnection,
            this, &WsServer::onNewConnection);

    return server_->listen(QHostAddress::LocalHost, port_);
}

void WsServer::stop()
{
    for (auto* c : clients_) {
        c->close();
        c->deleteLater();
    }
    clients_.clear();
    upgraded_.clear();
    buffers_.clear();
    server_->close();
}

bool WsServer::isListening() const
{
    return server_->isListening();
}

void WsServer::onNewConnection()
{
    while (server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        if (!socket) continue;
        clients_.append(socket);
        connect(socket, &QTcpSocket::readyRead, this, &WsServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &WsServer::onDisconnected);
    }
}

void WsServer::onReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    buffers_[socket].append(socket->readAll());

    if (!upgraded_.contains(socket)) {
        // Still in HTTP upgrade phase
        QByteArray& buf = buffers_[socket];
        if (buf.contains("\r\n\r\n")) {
            handleHttpUpgrade(socket, buf);
            buf.clear();
        }
    } else {
        handleWebSocketFrame(socket);
    }
}

void WsServer::onDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    clients_.removeAll(socket);
    upgraded_.remove(socket);
    buffers_.remove(socket);
    socket->deleteLater();
}

void WsServer::handleHttpUpgrade(QTcpSocket* socket, const QByteArray& request)
{
    // Parse Sec-WebSocket-Key from the HTTP upgrade request
    QString req = QString::fromUtf8(request);
    QString key;
    for (const auto& line : req.split("\r\n")) {
        if (line.startsWith("Sec-WebSocket-Key:", Qt::CaseInsensitive)) {
            key = line.mid(18).trimmed();
            break;
        }
    }

    if (key.isEmpty()) {
        // Not a valid WebSocket upgrade, close
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\n");
        socket->close();
        return;
    }

    // Compute accept hash
    QByteArray accept = QCryptographicHash::hash(
        (key + WS_GUID).toUtf8(), QCryptographicHash::Sha1).toBase64();

    // Send upgrade response
    QByteArray response;
    response.append("HTTP/1.1 101 Switching Protocols\r\n");
    response.append("Upgrade: websocket\r\n");
    response.append("Connection: Upgrade\r\n");
    response.append("Sec-WebSocket-Accept: ");
    response.append(accept);
    response.append("\r\n\r\n");
    socket->write(response);

    upgraded_.insert(socket);
}

void WsServer::handleWebSocketFrame(QTcpSocket* socket)
{
    QByteArray& buf = buffers_[socket];

    while (buf.size() >= 2) {
        quint8 b0 = static_cast<quint8>(buf[0]);
        quint8 b1 = static_cast<quint8>(buf[1]);

        // bool fin = (b0 & 0x80) != 0;
        int opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        quint64 payload_len = b1 & 0x7F;

        int header_size = 2;
        if (payload_len == 126) {
            if (buf.size() < 4) return; // need more data
            payload_len = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar*>(buf.constData() + 2));
            header_size = 4;
        } else if (payload_len == 127) {
            if (buf.size() < 10) return;
            payload_len = qFromBigEndian<quint64>(
                reinterpret_cast<const uchar*>(buf.constData() + 2));
            header_size = 10;
        }

        if (masked) header_size += 4;

        quint64 total = static_cast<quint64>(header_size) + payload_len;
        if (static_cast<quint64>(buf.size()) < total) return; // need more data

        // Extract mask key and payload
        QByteArray payload;
        if (masked) {
            const char* mask_key = buf.constData() + header_size - 4;
            payload.resize(static_cast<int>(payload_len));
            const char* src = buf.constData() + header_size;
            for (quint64 i = 0; i < payload_len; ++i) {
                payload[static_cast<int>(i)] = src[i] ^ mask_key[i % 4];
            }
        } else {
            payload = buf.mid(header_size, static_cast<int>(payload_len));
        }

        buf.remove(0, static_cast<int>(total));

        if (opcode == 0x1) {
            // Text frame
            processMessage(socket, payload);
        } else if (opcode == 0x8) {
            // Close frame
            socket->close();
            return;
        } else if (opcode == 0x9) {
            // Ping -> send Pong
            sendWsText(socket, payload); // simplified: send as pong
        }
        // Ignore other opcodes
    }
}

void WsServer::processMessage(QTcpSocket* socket, const QByteArray& payload)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString url = obj.value("url").toString().trimmed();
    if (url.isEmpty()) return;

    QString filename = obj.value("filename").toString().trimmed();
    QString referrer = obj.value("referrer").toString().trimmed();
    QString cookie = obj.value("cookie").toString().trimmed();

    emit downloadRequested(url, filename, referrer, cookie);

    // Send ack
    QJsonObject reply;
    reply["status"] = "ok";
    reply["message"] = "Download started";
    sendWsText(socket,
               QJsonDocument(reply).toJson(QJsonDocument::Compact));
}

void WsServer::sendWsText(QTcpSocket* socket, const QByteArray& data)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x81)); // FIN + text opcode

    if (data.size() < 126) {
        frame.append(static_cast<char>(data.size()));
    } else if (data.size() < 65536) {
        frame.append(static_cast<char>(126));
        quint16 len = qToBigEndian(static_cast<quint16>(data.size()));
        frame.append(reinterpret_cast<const char*>(&len), 2);
    } else {
        frame.append(static_cast<char>(127));
        quint64 len = qToBigEndian(static_cast<quint64>(data.size()));
        frame.append(reinterpret_cast<const char*>(&len), 8);
    }

    frame.append(data);
    socket->write(frame);
}
