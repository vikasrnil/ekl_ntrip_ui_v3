#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "ntripclient.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    NtripClient client;

    engine.rootContext()->setContextProperty("ntripClient", &client);

    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
