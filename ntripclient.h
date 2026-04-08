#ifndef NTRIPCLIENT_H
#define NTRIPCLIENT_H

#include <QObject>
#include <QStringList>
#include <pthread.h>

class NtripClient : public QObject
{
    Q_OBJECT

public:
    explicit NtripClient(QObject *parent = nullptr);

    Q_INVOKABLE void fetchMountPoints(QString host, int port);

    Q_INVOKABLE void connectToMountPoint(QString host, int port,
                                         QString mountpoint,
                                         QString auth);

    Q_INVOKABLE void disconnectClient();
    Q_INVOKABLE void setUseFileGGA(bool enable);

signals:
    void mountPointsReceived(QStringList list);
    void connectionStatus(QString status);
    void dataUpdated(QString line);

private:
    static void* ntrip_thread(void *arg);
    static void* serial_thread(void *arg);

struct ThreadData {
    int serial_fd;
    QString host;
    int port;
    QString mountpoint;
    QString auth;
    volatile bool running;

    bool useFileGGA;       
    QString ggaFilePath;    

    NtripClient *self;
};

    pthread_t ntrip_tid;
    pthread_t serial_tid;

    ThreadData tdata;
};

#endif
