// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

/**
 * A local HTTP server that answers every request with a redirect (`status`, 307 by default) to
 * `target` + the request's path, and logs what it was sent. Another port of 127.0.0.1 is
 * another origin, so a redirect to a second server stands for one to another host.
 *
 * Requests whose path starts with a prefix in `answers` get that answer instead (200 and its
 * body), so a test can let a sign-in through or end a redirect to this server itself.
 */
class RedirectServer : public QObject
{
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray body;
        QHash<QByteArray, QByteArray> headers; ///< Names in lower case.
    };
    struct Answer {
        QByteArray pathPrefix;
        int status = 200;
        QByteArray body;
    };

    RedirectServer()
    {
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
        Q_UNUSED(ok);
        connect(&m_server, &QTcpServer::newConnection, this, &RedirectServer::onConnection);
    }

    ~RedirectServer() override
    {
        for (QTcpSocket *s : m_buffers.keys()) {
            QObject::disconnect(s, nullptr, this, nullptr);
        }
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    int status = 307;
    QString target; ///< Base of the Location header; the request's path is appended.
    QList<Answer> answers;
    QList<Request> requests;

    /// Whether any logged request carried `needle` in a header value or in its body.
    bool sawAnywhere(const QByteArray &needle) const
    {
        for (const Request &r : requests) {
            if (r.body.contains(needle)) {
                return true;
            }
            for (const QByteArray &value : r.headers) {
                if (value.contains(needle)) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    void onConnection()
    {
        while (QTcpSocket *s = m_server.nextPendingConnection()) {
            m_buffers[s];
            connect(s, &QTcpSocket::readyRead, this, [this, s]() { process(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s]() {
                m_buffers.remove(s);
                s->deleteLater();
            });
        }
    }

    void process(QTcpSocket *s)
    {
        QByteArray &buf = m_buffers[s];
        buf += s->readAll();
        while (true) {
            const int idx = buf.indexOf("\r\n\r\n");
            if (idx < 0) {
                return;
            }
            const QList<QByteArray> lines = buf.left(idx).split('\n');
            Request req;
            const QList<QByteArray> first = lines.first().trimmed().split(' ');
            req.method = first.value(0);
            req.path = first.value(1);
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = lines.at(i).indexOf(':');
                if (colon > 0) {
                    req.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
                }
            }
            const int len = req.headers.value("content-length").toInt();
            if (buf.size() < idx + 4 + len) {
                return;
            }
            req.body = buf.mid(idx + 4, len);
            buf.remove(0, idx + 4 + len);
            requests.append(req);
            respond(s, req);
        }
    }

    void respond(QTcpSocket *s, const Request &req)
    {
        for (const Answer &a : std::as_const(answers)) {
            if (req.path.startsWith(a.pathPrefix)) {
                write(s, a.status, a.body);
                return;
            }
        }
        write(s, status, QByteArray(), target.toUtf8() + req.path);
    }

    static void write(QTcpSocket *s, int status, const QByteArray &body, const QByteArray &location = QByteArray())
    {
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
        if (!location.isEmpty()) {
            out += "Location: " + location + "\r\n";
        }
        if (!body.isEmpty()) {
            out += "Content-Type: application/json\r\n";
        }
        out += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
        s->write(out);
        s->flush();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
