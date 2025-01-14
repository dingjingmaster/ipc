//
// Created by dingjing on 11/6/24.
//

// ReSharper disable All
#include "ipc-base.h"

#include <QFile>
#include <QDebug>
#include <QTimer>
#include <utility>
#include <QThread>
#include <qeventloop.h>

#include <glib.h>
#include <sys/un.h>
#include <gio/gio.h>
#include <sys/socket.h>

#include "3thrd/macros/macros.h"

static gsize    read_all_data               (GSocket* fr, QByteArray& C_OUT);
static void     process_client_request      (gpointer data, gpointer userData);
static gboolean new_request                 (GSocketService* ls, GSocketConnection* conn, GObject* srcObj, gpointer uData);

class IpcBasePrivate
{
    friend class IpcBase;
    friend gboolean new_request (GSocketService* ls, GSocketConnection* conn, GObject* srcObj, gpointer uData);
public:
    explicit IpcBasePrivate(QString listenerPath, QString  sendToPath, IpcBase* parent);

    void initListener();
    IpcClientProcess getClientProcess(cuint32 type);
    void addClientProcess(cuint32 type, IpcClientProcess clientProcess);

    void responseToClient(GSocket* sock, const QByteArray& data);

    QByteArray sendToClient(const QByteArray& data, bool waitResp);

    void sendToClient(cuint32 type, const QByteArray& data);
    QByteArray sendToClientWaitResp(cuint32 type, const QByteArray& data);

private:
    IpcBase*                                q_ptr;
    QString                                 mSendToPath;
    QString                                 mListenerPath;
    GSocketService*                         mServer{};
    GSocketAddress*                         mServerAddr{};
    GThreadPool*                            mServerWorker{};
    GSocket*                                mServerSocket{};
    QMap<cuint32, IpcClientProcess>         mClientProcessor;
    Q_DECLARE_PUBLIC(IpcBase);
};

IpcBasePrivate::IpcBasePrivate(QString listenerPath, QString sendToPath, IpcBase* parent)
    : q_ptr(parent), mSendToPath(std::move(sendToPath)), mListenerPath(std::move(listenerPath))
{
}

void IpcBasePrivate::initListener()
{
    GError* error = nullptr;

    do {
        struct sockaddr_un addrT = {};
        addrT.sun_family = AF_LOCAL;
        strncpy (addrT.sun_path, mListenerPath.toUtf8().constData(), sizeof(addrT.sun_path) - 1);
        mServerSocket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
        if (error) {
            qWarning() << "g_socket_new error: " << error->message;
            break;
        }

        g_socket_set_blocking (mServerSocket, true);

        if (QFile::exists(mListenerPath)) {
            QFile::remove(mListenerPath);
        }

        mServerAddr = g_socket_address_new_from_native(&addrT, sizeof (addrT));

        if (!g_socket_bind (mServerSocket, mServerAddr, false, &error)) {
            qWarning() << "bind error: " << error->message;
            break;
        }

        if (!g_socket_listen (mServerSocket, &error)) {
            qWarning() << "listen error: " << error->message;
            break;
        }

        mServer = g_socket_service_new();
        g_socket_listener_add_socket (G_SOCKET_LISTENER(mServer), mServerSocket, nullptr, &error);
        if (error) {
            qWarning() << "g_socket_listener_add_socket error: " << error->message;
            break;
        }

        mServerWorker = g_thread_pool_new (process_client_request, q_ptr, 30, false, &error);
        if (error) {
            qWarning() << "g_thread_pool_new error: " << error->message;
            break;
        }

        QFile::setPermissions(mListenerPath, QFile::ReadOwner | QFile::WriteUser | QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther);
        g_signal_connect (G_SOCKET_LISTENER(mServer), "incoming", reinterpret_cast<GCallback>(new_request), this);
    } while (false);

    if (error) { g_error_free(error); }
}

IpcClientProcess IpcBasePrivate::getClientProcess(const cuint32 type)
{
    if (mClientProcessor.contains(type)) {
        return mClientProcessor[type];
    }

    return nullptr;
}

void IpcBasePrivate::addClientProcess(cuint32 type, IpcClientProcess clientProcess)
{
    if (clientProcess != nullptr) {
        mClientProcessor[type] = clientProcess;
    }
}

void IpcBasePrivate::responseToClient(GSocket* sock, const QByteArray & data)
{
    g_return_if_fail (sock != nullptr);

    GError* error = nullptr;

    IpcMessage msg{};
    msg.type = 0;
    msg.dataLen = data.length();

    QByteArray buf;
    buf.append(reinterpret_cast<char*>(&msg), sizeof(msg));
    buf.append(data);

    if (g_socket_condition_wait(sock, G_IO_OUT, nullptr, &error)) {
        if (g_socket_send_with_blocking(sock, buf.data(), buf.size(), true, nullptr, &error)) {
            qDebug() << "send data to client OK!";
        }
        else {
            qWarning() << "send data to client error: " << error->message;
        }
    }
    else {
        qWarning() << "g_socket_condition_wait error: " << error->message;
    }

    if (error) { g_error_free(error); }
}

QByteArray IpcBasePrivate::sendToClient(const QByteArray& data, bool waitResp)
{
    GError* error = nullptr;

    QByteArray resp;

    do {
        struct sockaddr_un addrT{};
        addrT.sun_family = AF_LOCAL;
        strncpy (addrT.sun_path, mSendToPath.toUtf8().constData(), sizeof(addrT.sun_path) - 1);

        GSocketAddress* addr = g_socket_address_new_from_native(&addrT, sizeof (addrT));
        if (addr) {
            GSocket* sock = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
            if (sock) {
                if (g_socket_connect(sock, addr, nullptr, &error)) {
                    if (g_socket_condition_wait(sock, G_IO_OUT, nullptr, &error)) {
                        if (g_socket_send_with_blocking(sock, data.data(), data.size(), true, nullptr, &error)) {
                            qDebug() << "send data to client OK!";
                            if (waitResp) {
                                if (g_socket_condition_wait(sock, G_IO_IN, nullptr, &error)) {
                                    read_all_data(sock, resp);
                                }
                                else {
                                    qWarning() << "g_socket_condition_wait error: " << (error ? error->message : "");
                                }
                            }
                        }
                        else {
                            qWarning() << "sendDataToClient error: " << error->message;
                        }
                    }
                    else {
                        qWarning() << "g_socket_condition_wait error: " << error->message;
                    }
                }
                else {
                    qWarning() << "g_socket_connect error: " << error->message;
                }
                g_object_unref(sock);
            }
            else {
                qWarning() << "g_socket_new error: " << error->message;
            }
            g_object_unref(addr);
        }
    } while (false);

    if (error) { g_error_free(error); }

    return resp;
}

void IpcBasePrivate::sendToClient(cuint32 type, const QByteArray& data)
{
    IpcMessage msg{};

    msg.type = type;
    msg.dataLen = data.length();

    QByteArray buf;
    buf.append(reinterpret_cast<char*>(&msg), sizeof(msg));
    buf.append(data);

    sendToClient(buf, false);
}

QByteArray IpcBasePrivate::sendToClientWaitResp(cuint32 type, const QByteArray & data)
{
    IpcMessage msg{};

    msg.type = type;
    msg.dataLen = data.length();

    QByteArray buf;
    buf.append(reinterpret_cast<char*>(&msg), sizeof(msg));
    buf.append(data);

    QByteArray resp = sendToClient(buf, true);
    if (resp.length() < sizeof(IpcMessage)) {
        qWarning() << "recive error: " << resp;
        return {};
    }

    IpcMessage* respMsg = reinterpret_cast<IpcMessage*>(resp.data());

    return QByteArray(respMsg->data, respMsg->dataLen);
}

IpcBase::IpcBase(const QString & listenPath, const QString & sendToPath, QObject * parent)
    : QObject(parent), d_ptr(new IpcBasePrivate(listenPath, sendToPath, this))
{
}

IpcClientProcess IpcBase::getClientProcess(cuint32 type)
{
    Q_D(IpcBase);

    return d->getClientProcess(type);
}

void IpcBase::addClientProcess(cuint32 type, IpcClientProcess clientProcess)
{
    Q_D(IpcBase);

    d->addClientProcess(type, clientProcess);
}

void IpcBase::respToClient(GSocket* sock, int data)
{
    Q_D(IpcBase);

    d->responseToClient(sock, QByteArray(reinterpret_cast<char*>(&data), sizeof(data)));
}

void IpcBase::respToClient(GSocket * sock, const QString& data)
{
    Q_D(IpcBase);

    d->responseToClient(sock, data.toUtf8());
}

void IpcBase::respToClient(GSocket * sock, const char* data, int dataLen)
{
    Q_D(IpcBase);

    d->responseToClient(sock, QByteArray(data, dataLen));
}

void IpcBase::sendToClient(cuint32 type, int data)
{
    Q_D(IpcBase);

    d->sendToClient(type, QByteArray(reinterpret_cast<char*>(&data), sizeof(data)));
}

void IpcBase::sendToClient(cuint32 type, const QString & data)
{
    Q_D(IpcBase);

    d->sendToClient(type, data.toUtf8());
}

void IpcBase::sendToClient(cuint32 type, const char * data, int dataLen)
{
    Q_D(IpcBase);

    d->sendToClient(type, QByteArray(data, dataLen));
}

QByteArray IpcBase::sendToClientWaitResp(cuint32 type, int data)
{
    Q_D(IpcBase);

    return d->sendToClientWaitResp(type, QByteArray(reinterpret_cast<char*>(&data), sizeof(data)));
}

QByteArray IpcBase::sendToClientWaitResp(cuint32 type, const QString & data)
{
    Q_D(IpcBase);

    return d->sendToClientWaitResp(type, data.toUtf8());
}

QByteArray IpcBase::sendToClientWaitResp(cuint32 type, const char * data, int dataLen)
{
    Q_D(IpcBase);

    return d->sendToClientWaitResp(type, QByteArray(data, dataLen));
}

int IpcBase::sendToClientWaitRespInt(cuint32 type, int data)
{
    QByteArray resp = sendToClientWaitResp(type, data);

    int respInt;
    memcpy(&respInt, resp.data(), sizeof(respInt));

    return respInt;
}

int IpcBase::sendToClientWaitRespInt(cuint32 type, const QString & data)
{
    QByteArray resp = sendToClientWaitResp(type, data);

    int respInt;
    memcpy(&respInt, resp.data(), sizeof(respInt));

    return respInt;
}

int IpcBase::sendToClientWaitRespInt(cuint32 type, const char * data, int dataLen)
{
    QByteArray resp = sendToClientWaitResp(type, data, dataLen);

    int respInt;
    memcpy(&respInt, resp.data(), sizeof(respInt));

    return respInt;
}

QByteArray IpcBase::sendAndWaitResp(const QString & ipcPath, cuint32 type, const QByteArray & data, bool isWaitResp)
{
    GError* error = nullptr;

    IpcMessage msg;
    msg.type = type;
    msg.dataLen = data.size();

    QByteArray sendBuf;
    sendBuf.append((char*)&msg, sizeof(msg));
    sendBuf.append(data);

    QByteArray resp = sendRawAndWaitResp(ipcPath, sendBuf, isWaitResp);

    if (!resp.isEmpty()) {
        IpcMessage* respMsg = (IpcMessage*) resp.data();
        QByteArray bufT(respMsg->data, (int) respMsg->dataLen);
        resp = bufT;
    }

    if (error) { g_error_free(error); }

    return resp;
}

QByteArray IpcBase::sendRawAndWaitResp(const QString & ipcPath, const QByteArray & data, bool isWaitResp)
{
    GError* error = nullptr;

    QByteArray sendBuf;
    sendBuf.append(data);

    QByteArray resp;

    do {
        struct sockaddr_un addrT{};
        addrT.sun_family = AF_LOCAL;
        strncpy (addrT.sun_path, ipcPath.toUtf8().constData(), sizeof(addrT.sun_path) - 1);

        GSocketAddress* addr = g_socket_address_new_from_native(&addrT, sizeof (addrT));
        if (addr) {
            GSocket* sock = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
            if (sock) {
                if (g_socket_connect(sock, addr, nullptr, &error)) {
                    if (g_socket_condition_wait(sock, G_IO_OUT, nullptr, &error)) {
                        if (g_socket_send_with_blocking(sock, sendBuf.data(), sendBuf.size(), true, nullptr, &error)) {
                            qDebug() << "send data to client OK!";
                            if (isWaitResp) {
                                QEventLoop eventLoop;
                                QTimer timer;
                                timer.setInterval(100);
                                timer.connect(&timer, &QTimer::timeout, [&]() ->void {
                                    if (g_socket_condition_timed_wait(sock, G_IO_IN, 100, nullptr, &error)) {
                                        eventLoop.quit();
                                    }
                                });
                                timer.start();
                                eventLoop.exec();
                                if (g_socket_condition_wait(sock, G_IO_IN, nullptr, &error)) {
                                    read_all_data(sock, resp);
                                }
                                else {
                                    qWarning() << "g_socket_condition_wait error: " << (error ? error->message : "");
                                }
                            }
                        }
                        else {
                            qWarning() << "sendDataToClient error: " << error->message;
                        }
                    }
                    else {
                        qWarning() << "g_socket_condition_wait error: " << error->message;
                    }
                }
                else {
                    qWarning() << "g_socket_connect error: " << error->message;
                }
                g_object_unref(sock);
            }
            else {
                qWarning() << "g_socket_new error: " << error->message;
            }
            g_object_unref(addr);
        }
    } while (false);

    if (error) { g_error_free(error); }

    return resp;
}

static gboolean new_request (GSocketService* ls, GSocketConnection* conn, GObject* srcObj, gpointer uData)
{
    qDebug() << "new request!";

    g_return_val_if_fail(uData, false);

    g_object_ref(conn);

    GError* error = nullptr;
    g_thread_pool_push(static_cast<IpcBasePrivate*>(uData)->mServerWorker, conn, &error);
    if (error) {
        qDebug() << error->message;
        g_error_free(error);
        error = nullptr;
        g_object_unref(conn);
        return false;
    }

    return true;

    (void) ls;
    (void) srcObj;
}

static void process_client_request (gpointer data, gpointer userData)
{
    g_return_if_fail(data);

    if (!userData) {
        qWarning() << "client user data null";
        g_object_unref(static_cast<GSocketConnection*>(data));
        return;
    }

    QByteArray binStr;
    const auto sc = static_cast<IpcBase*>(userData);
    const auto conn = static_cast<GSocketConnection*>(data);

    GSocket* socket = g_socket_connection_get_socket (conn);

    const guint64 strLen = read_all_data (socket, binStr);
    if (strLen <= 0) {
        qWarning() << "read client data null";
        goto out;
    }

    if (binStr.length() >= static_cast<int>(sizeof(IpcMessage))) {
        const auto msg = reinterpret_cast<IpcMessage*>(binStr.data());
        const IpcClientProcess processor = sc->getClientProcess(msg->type);
        if (processor) {
            g_object_ref(socket);
            (*processor)(sc, QByteArray(msg->data, static_cast<int>(msg->dataLen)), socket);
            g_object_unref(socket);
        }
        else {
            qWarning() << "unknown request type: " << msg->type;
        }
    }
    else {
        qWarning() << "unknown request size: " << binStr.length();
    }

out:
    g_object_unref (conn);

    g_thread_pool_stop_unused_threads();

    (void) data;
    (void) userData;
}

static gsize read_all_data (GSocket* fr, QByteArray& C_OUT out)
{
    GError* error = nullptr;

    gssize readLen = 0;
    char buf[1024] = {0};

    while ((readLen = g_socket_receive(fr, buf, sizeof(buf) - 1, nullptr, &error)) > 0) {
        if (error) {
            qWarning() << error->message;
            break;
        }

        out.append(buf, static_cast<int>(readLen));
        if (readLen < sizeof (buf) - 1) {
            break;
        }
    }

    if(error) g_error_free(error);

    return out.length();
}
