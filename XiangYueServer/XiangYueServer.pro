QT += widgets network sql

CONFIG += c++17

# ---- 原生 Windows IOCP 所需 ----
# Winsock2：WSAStartup/WSARecv/WSASend/accept 等都在 ws2_32。
# 用阻塞 accept（不用 AcceptEx），故无需 -lmswsock；
# CreateIoCompletionPort/GetQueuedCompletionStatus 属 kernel32（默认链接）。
LIBS += -lws2_32
# 指定目标 Windows 版本（Win7+），启用 inet_ntop 等 API。
DEFINES += _WIN32_WINNT=0x0601

# ---- 模块化目录：把各功能模块文件夹加入头文件搜索路径 ----
# 这样所有源码里平铺式的 #include "xxx.h" 无需改写即可跨文件夹找到头文件。
INCLUDEPATH += $$PWD/app \
               $$PWD/network \
               $$PWD/db \
               $$PWD/repository \
               $$PWD/service

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    app/main.cpp \
    app/serverwidget.cpp \
    network/iocpserver.cpp \
    network/connection.cpp \
    network/clientworker.cpp \
    network/taskqueue.cpp \
    network/threadpool.cpp \
    db/dbmanager.cpp \
    db/dbconnectionpool.cpp \
    repository/userrepository.cpp \
    repository/resourcerepository.cpp \
    repository/uploadrepository.cpp \
    repository/commentrepository.cpp \
    repository/favoritesrepository.cpp \
    service/authservice.cpp \
    service/resourceservice.cpp \
    service/uploadservice.cpp \
    service/commentservice.cpp \
    service/favoritesservice.cpp

HEADERS += \
    app/serverwidget.h \
    network/iocpserver.h \
    network/connection.h \
    network/iocontext.h \
    network/serverconfig.h \
    network/clientworker.h \
    network/taskqueue.h \
    network/threadpool.h \
    db/dbmanager.h \
    db/dbconnectionpool.h \
    repository/userrepository.h \
    repository/resourcerepository.h \
    repository/uploadrepository.h \
    repository/commentrepository.h \
    repository/favoritesrepository.h \
    repository/commentrecord.h \
    repository/favoriterecord.h \
    service/authservice.h \
    service/resourceservice.h \
    service/uploadservice.h \
    service/commentservice.h \
    service/favoritesservice.h

FORMS += \
    app/serverwidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
