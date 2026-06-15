QT += widgets network

CONFIG += c++17
CONFIG += debug_and_release

# 第三方库头文件搜索路径（hnswlib 为纯头文件库，无需链接）
INCLUDEPATH += $$PWD/third_party

# ---- 模块化目录：把各功能模块文件夹加入头文件搜索路径 ----
# 这样所有源码里平铺式的 #include "xxx.h" 无需改写即可跨文件夹找到头文件。
INCLUDEPATH += $$PWD/app \
               $$PWD/network \
               $$PWD/session \
               $$PWD/login \
               $$PWD/resource \
               $$PWD/favorite \
               $$PWD/search \
               $$PWD/preview

# ---- 文件预览模块（preview/）所需 ----
# 图片预览只依赖 QtGui/QtWidgets（已包含，无需额外配置）。
# PDF 预览依赖 QtPdfWidgets 模块：仅当该模块已安装时才启用，
# 并定义 HAVE_QT_PDF 供源码做编译期门控；同时把 PDF 相关的
# 源文件 / 头文件 / .ui 一并纳入构建，避免在缺少模块的 Kit 上
# 因引用 QPdfView 而编译失败（自动降级为“仅图片预览”）。
qtHaveModule(pdfwidgets) {
    QT += pdfwidgets
    DEFINES += HAVE_QT_PDF
    SOURCES += preview/pdfpreviewprovider.cpp
    HEADERS += preview/pdfpreviewprovider.h
    FORMS   += preview/pdfpreviewprovider.ui
}

# Debug configuration
CONFIG(debug, debug|release) {
    CONFIG += console
    DEFINES += QT_MESSAGELOGCONTEXT
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    app/main.cpp \
    app/mainwindow.cpp \
    network/fileclient.cpp \
    session/usersession.cpp \
    login/logdialog.cpp \
    resource/uploadresourcedialog.cpp \
    resource/resourcedetaildialog.cpp \
    resource/myuploaddialog.cpp \
    resource/commentbubble.cpp \
    favorite/favoritesdialog.cpp \
    search/tagsearchengine.cpp \
    preview/filepreviewdialog.cpp \
    preview/imagepreviewprovider.cpp

HEADERS += \
    app/mainwindow.h \
    network/fileclient.h \
    session/usersession.h \
    login/logdialog.h \
    resource/uploadresourcedialog.h \
    resource/resourcedetaildialog.h \
    resource/myuploaddialog.h \
    resource/commentbubble.h \
    favorite/favoritesdialog.h \
    search/tagsearchengine.h \
    preview/filepreviewdialog.h \
    preview/previewprovider.h \
    preview/imagepreviewprovider.h

FORMS += \
    app/mainwindow.ui \
    login/logdialog.ui \
    resource/uploadresourcedialog.ui \
    resource/resourcedetaildialog.ui \
    resource/myuploaddialog.ui \
    favorite/favoritesdialog.ui \
    preview/imagepreviewprovider.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 资源文件 (QSS 样式表)。qss.qrc 用 alias 保持 :/qss/xxx.qss 路径不变。
RESOURCES += \
    qss.qrc
