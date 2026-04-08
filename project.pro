QT += core gui qml quick

CONFIG += c++11 thread

SOURCES += \
    main.cpp \
    ntripclient.cpp

HEADERS += \
    ntripclient.h

RESOURCES += resources.qrc

# Link pthread (for your threads)
LIBS += -lpthread
