QT += widgets network

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    fileclient.cpp \
    main.cpp \
    logdialog.cpp \
    mainwindow.cpp \
    resourcedetaildialog.cpp \
    resourcesearch.cpp

HEADERS += \
    fileclient.h \
    logdialog.h \
    mainwindow.h \
    resourcedetaildialog.h \
    resourcesearch.h

FORMS += \
    logdialog.ui \
    mainwindow.ui \
    resourcedetaildialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
