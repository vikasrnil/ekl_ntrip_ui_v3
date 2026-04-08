#include "ntripclient.h"

#include <QByteArray>
#include <QMetaObject>

#include <stdio.h>
#include <thread>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

// ================= GLOBAL =================
static pthread_mutex_t gga_file_mutex;

// ================= SERIAL =================
static int init_serial(const char *dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    struct termios tio;
    tcgetattr(fd, &tio);

    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;

    tio.c_lflag = 0;
    tio.c_oflag = 0;
    tio.c_iflag = 0;

    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tio);
    return fd;
}

// ================= CHECKSUM =================
static int verifyChecksum(const char *s)
{
    if (!s || s[0] != '$') return 0;

    uint8_t calc = 0;
    const char *p = s + 1;

    while (*p && *p != '*')
        calc ^= *p++;

    if (*p != '*') return 0;
    p++;

    char cs[3] = {p[0], p[1], '\0'};
    uint8_t given = strtol(cs, NULL, 16);

    return calc == given;
}

// ================= LAT/LON =================
static double lat_filt(float d)
{
    int deg = (int)(d * 0.01);
    return deg + (d - deg * 100) / 60.0;
}

static double lng_filt(float d)
{
    int deg = (int)(d * 0.01);
    return deg + (d - deg * 100) / 60.0;
}

// ================= GGA PARSER =================
static void parseGGA(char *buf,
                     int *time,
                     double *lat, char *latDir,
                     double *lon, char *lonDir,
                     int *fix, int *sat,
                     double *hdop,
                     double *alt,
                     double *geoid,
                     int *diff_age)
{
    char *tok = strtok(buf, ",");
    int i = 0;

    while (tok)
    {
        switch (i)
        {
        case 1:  *time = atoi(tok); break;
        case 2:  *lat = lat_filt(atof(tok)); break;
        case 3:  *latDir = tok[0]; break;
        case 4:  *lon = lng_filt(atof(tok)); break;
        case 5:  *lonDir = tok[0]; break;
        case 6:  *fix = atoi(tok); break;
        case 7:  *sat = atoi(tok); break;
        case 8:  *hdop = atof(tok); break;
        case 9:  *alt = atof(tok); break;
        case 11: *geoid = atof(tok); break;
        case 13: *diff_age = atoi(tok); break;
        }

        tok = strtok(NULL, ",");
        i++;
    }
}

// ================= RMC PARSER =================
static void parseRMC(char *buf,
                     int *time,
                     char *status,
                     double *lat, char *latDir,
                     double *lon, char *lonDir,
                     double *speed,
                     double *heading,
                     int *date)
{
    char *tok = strtok(buf, ",");
    int i = 0;

    while (tok)
    {
        switch (i)
        {
        case 1: *time = atoi(tok); break;
        case 2: *status = tok[0]; break;
        case 3: *lat = lat_filt(atof(tok)); break;
        case 4: *latDir = tok[0]; break;
        case 5: *lon = lng_filt(atof(tok)); break;
        case 6: *lonDir = tok[0]; break;
        case 7: *speed = atof(tok); break;
        case 8: *heading = atof(tok); break;
        case 9: *date = atoi(tok); break;
        }

        tok = strtok(NULL, ",");
        i++;
    }
}

// ================= SOCKET =================
static int connect_socket(QString host, int port, QString request)
{
    struct hostent *server;
    struct sockaddr_in serv_addr;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    server = gethostbyname(host.toStdString().c_str());
    if (!server) return -1;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        return -1;

    write(sock, request.toStdString().c_str(), request.length());

    return sock;
}

// ================= CONSTRUCTOR =================
NtripClient::NtripClient(QObject *parent) : QObject(parent)
{
    tdata.running     = false;
    tdata.serial_fd   = -1;
    tdata.useFileGGA  = false;
    tdata.ggaFilePath = "gga.txt";

    ntrip_tid  = 0;
    serial_tid = 0;
}

// ================= FETCH MOUNT POINTS =================
void NtripClient::fetchMountPoints(QString host, int port)
{
    std::thread([=]() {

        QStringList list;

        QString req =
            "GET / HTTP/1.1\r\n"
            "User-Agent: NTRIP QtClient/1.0\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n\r\n";

        int sock = connect_socket(host, port, req);
        if (sock < 0)
        {
            QMetaObject::invokeMethod(this, "mountPointsReceived",
                                      Qt::QueuedConnection,
                                      Q_ARG(QStringList, list));
            return;
        }

        char buffer[4096];
        QString partial;

        while (true)
        {
            int n = read(sock, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;

            buffer[n] = '\0';
            partial += QString::fromUtf8(buffer, n);

            while (partial.contains("\n"))
            {
                int idx = partial.indexOf("\n");
                QString line = partial.left(idx).trimmed();
                partial = partial.mid(idx + 1);

                if (line.startsWith("STR;"))
                {
                    QStringList parts = line.split(";");
                    if (parts.size() > 1)
                        list.append(parts[1].trimmed());
                }
            }
        }

        close(sock);

        if (!partial.isEmpty() && partial.startsWith("STR;"))
        {
            QStringList parts = partial.split(";");
            if (parts.size() > 1)
                list.append(parts[1].trimmed());
        }

        QMetaObject::invokeMethod(this, "mountPointsReceived",
                                  Qt::QueuedConnection,
                                  Q_ARG(QStringList, list));

    }).detach();
}

// ================= CONNECT =================
void NtripClient::connectToMountPoint(QString host, int port,
                                      QString mountpoint, QString auth)
{
    if (tdata.running)
    {
        emit connectionStatus("Already Connected");
        return;
    }

    int fd = init_serial("/dev/ttyUSB0");
    if (fd < 0)
    {
        emit connectionStatus("Serial Error");
        return;
    }

    tdata.serial_fd = fd;
    tdata.host      = host;
    tdata.port      = port;
    tdata.mountpoint = mountpoint;
    tdata.auth      = auth;
    tdata.running   = true;
    tdata.self      = this;

    pthread_create(&ntrip_tid, NULL, ntrip_thread, &tdata);
    pthread_create(&serial_tid, NULL, serial_thread, &tdata);

    emit connectionStatus("Connected");
}

// ================= DISCONNECT =================
void NtripClient::disconnectClient()
{
    if (!tdata.running) return;

    tdata.running = false;
    usleep(200000);

    if (tdata.serial_fd > 0)
    {
        close(tdata.serial_fd);
        tdata.serial_fd = -1;
    }

    emit connectionStatus("Disconnected");
}

void NtripClient::setUseFileGGA(bool enable)
{
    tdata.useFileGGA = enable;
}

// ================= NTRIP THREAD =================
void* NtripClient::ntrip_thread(void *arg)
{
    ThreadData *d = (ThreadData*)arg;

    char buf[1024];
    char gga_line[256];
    int reconnect_count = 1;

    while (d->running)
    {
        // ===== CONNECT =====
        QString auth = "Basic " + d->auth.toUtf8().toBase64();

        QString req =
            "GET /" + d->mountpoint + " HTTP/1.1\r\n"
            "Host: " + d->host + "\r\n"
            "Authorization: " + auth + "\r\n\r\n";

        int sock = connect_socket(d->host, d->port, req);

        if (sock < 0)
        {
            printf("Connect failed... retrying\n");
            sleep(2);
            continue;
        }

        printf("Connected to caster\n");

        // ===== MAIN LOOP =====
        while (d->running)
        {
            // ---------- SEND GGA ----------
            if (d->useFileGGA)
            {
                pthread_mutex_lock(&gga_file_mutex);

                FILE *fp = fopen(d->ggaFilePath.toStdString().c_str(), "r");
                if (fp)
                {
                    if (fgets(gga_line, sizeof(gga_line), fp))
                    {
                        write(sock, gga_line, strlen(gga_line));
                    }
                    fclose(fp);
                }

                pthread_mutex_unlock(&gga_file_mutex);
            }

            // ---------- RECEIVE RTCM ----------
            memset(buf, 0, sizeof(buf));

            int n = read(sock, buf, sizeof(buf));

            if (n > 0)
            {
                write(d->serial_fd, buf, n);
                // printf("RTCM: %d bytes\n", n);
            }
            else
            {
                printf("Reconnecting... Attempt: %d\n", reconnect_count++);
                close(sock);
                break;  // break inner loop → reconnect
            }

            // ---------- LOOP DELAY ----------
            usleep(500000);   
        }
    }

    return NULL;
}

// ================= SERIAL THREAD =================
void* NtripClient::serial_thread(void *arg)
{
    ThreadData *d = (ThreadData*)arg;

    char line[256];
    int idx = 0, c;

    int gga_time = 0, fix = 0, sat = 0, diff_age = 0;
    double lat = 0, lon = 0, hdop = 0, alt = 0, geoid = 0;

    int rmc_time = 0, date = 0;
    double spd = 0, hd = 0;
    char status = 0;

    char latDir = '-', lonDir = '-';

    while (d->running)
    {
        if (read(d->serial_fd, &c, 1) <= 0)
            continue;

        if (c == '\n')
        {
            line[idx] = '\0';
            idx = 0;

            if (!verifyChecksum(line))
                continue;

            char temp[256];
            strcpy(temp, line);

            // ---- GGA ----
            if (strstr(line, "GGA"))
            {
                parseGGA(temp, &gga_time,
                         &lat, &latDir,
                         &lon, &lonDir,
                         &fix, &sat,
                         &hdop, &alt,
                         &geoid, &diff_age);

                pthread_mutex_lock(&gga_file_mutex);

                FILE *fp = fopen(d->ggaFilePath.toStdString().c_str(), "w");
                if (fp)
                {
                    fprintf(fp, "%s\n", line);
                    fclose(fp);
                }

                pthread_mutex_unlock(&gga_file_mutex);
            }
            // ---- RMC ----
            else if (strstr(line, "RMC"))
            {
                parseRMC(temp, &rmc_time, &status,
                         &lat, &latDir,
                         &lon, &lonDir,
                         &spd, &hd, &date);
            }

            QString out = QString(
                "GGA: %1 Lat:%2 %3 Lon:%4 %5 Fix:%6 Sat:%7 HDOP:%8 Alt:%9 DiffAge:%10\n"
                "RMC: Time:%11 Status:%12 Speed:%13 Heading:%14 Date:%15"
            )
            .arg(gga_time)
            .arg(lat, 0, 'f', 6).arg(latDir)
            .arg(lon, 0, 'f', 6).arg(lonDir)
            .arg(fix)
            .arg(sat)
            .arg(hdop, 0, 'f', 1)
            .arg(alt, 0, 'f', 1)
            .arg(diff_age)
            .arg(rmc_time)
            .arg(status)
            .arg(spd, 0, 'f', 2)
            .arg(hd, 0, 'f', 1)
            .arg(date);

            QMetaObject::invokeMethod(d->self, "dataUpdated",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, out));
        }
        else if (c != '\r' && idx < 255)
        {
            line[idx++] = c;
        }
    }

    return NULL;
}
