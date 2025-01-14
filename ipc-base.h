//
// Created by dingjing on 11/6/24.
//

#ifndef assistant_IPC_BASE_H
#define assistant_IPC_BASE_H
#include <QObject>
#include <gio/gio.h>

#include "ipc.h"

class IpcBase;
class IpcBasePrivate;

typedef void(*IpcClientProcess)(IpcBase*, const QByteArray& data, GSocket* clientSock);

class IpcBase : public QObject
{
    Q_OBJECT
public:
    explicit IpcBase(const QString& listenPath, const QString& sendToPath, QObject *parent = nullptr);

    IpcClientProcess getClientProcess(cuint32 type);
    void addClientProcess(cuint32 type, IpcClientProcess clientProcess);

    void respToClient(GSocket* sock, int data);
    void respToClient(GSocket* sock, const QString& data);
    void respToClient(GSocket* sock, const char* data, int dataLen);

    void sendToClient(cuint32 type, int data);
    void sendToClient(cuint32 type, const QString& data);
    void sendToClient(cuint32 type, const char* data, int dataLen);

    QByteArray sendToClientWaitResp(cuint32 type, int data);
    QByteArray sendToClientWaitResp(cuint32 type, const QString& data);
    QByteArray sendToClientWaitResp(cuint32 type, const char* data, int dataLen);

    int sendToClientWaitRespInt(cuint32 type, int data);
    int sendToClientWaitRespInt(cuint32 type, const QString& data);
    int sendToClientWaitRespInt(cuint32 type, const char* data, int dataLen);

    static QByteArray sendAndWaitResp(const QString& ipcPath, cuint32 type, const QByteArray& data, bool isWaitResp);
    static QByteArray sendRawAndWaitResp(const QString& ipcPath, const QByteArray& data, bool isWaitResp);

private:
    IpcBasePrivate*         d_ptr;
    Q_DECLARE_PRIVATE(IpcBase);
};


#endif // assistant_IPC_BASE_H
