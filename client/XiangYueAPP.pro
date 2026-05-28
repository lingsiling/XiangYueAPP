QT += widgets network

CONFIG += c++17
CONFIG += debug_and_release

# Debug configuration
CONFIG(debug, debug|release) {
    CONFIG += console
    DEFINES += QT_MESSAGELOGCONTEXT
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    fileclient.cpp \
    main.cpp \
    favoritesdialog.cpp \
    logdialog.cpp \
    mainwindow.cpp \
    myuploaddialog.cpp \
    resourcedetaildialog.cpp \
    resourcesearch.cpp \
    transferdialog.cpp \
    usersession.cpp

HEADERS += \
    fileclient.h \
    favoritesdialog.h \
    logdialog.h \
    mainwindow.h \
    myuploaddialog.h \
    resourcedetaildialog.h \
    resourcesearch.h \
    transferdialog.h \
    usersession.h

FORMS += \
    logdialog.ui \
    mainwindow.ui \
    resourcedetaildialog.ui \
    transferdialog.ui \
    favoritesdialog.ui \
    myuploaddialog.ui

# 资源文件 (QSS 样式表)
RESOURCES +=

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    qss.qrc
