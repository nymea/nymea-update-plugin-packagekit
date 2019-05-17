QT -= gui
QT += network dbus

TARGET = $$qtLibraryTarget(nymea_updatepluginpackagekit)
TEMPLATE = lib

CONFIG += plugin link_pkgconfig c++11
PKGCONFIG += nymea packagekitqt5

SOURCES += \
    updatecontrollerpackagekit.cpp \


HEADERS += \
    updatecontrollerpackagekit.h \


target.path = $$[QT_INSTALL_LIBS]/nymea/platform/
INSTALLS += target
