QT += widgets network sql

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    authservice.cpp \
    clientworker.cpp \
    commentrepository.cpp \
    commentservice.cpp \
    dbmanager.cpp \
    main.cpp \
    serverwidget.cpp \
    threadedtcpserver.cpp \
    userrepository.cpp

HEADERS += \
    authservice.h \
    clientworker.h \
    commentrecord.h \
    commentrepository.h \
    commentservice.h \
    dbmanager.h \
    serverwidget.h \
    threadedtcpserver.h \
    userrepository.h

FORMS += \
    serverwidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
