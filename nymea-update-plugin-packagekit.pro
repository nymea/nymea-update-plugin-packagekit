QT -= gui
QT += network dbus

TARGET = $$qtLibraryTarget(nymea_updatepluginpackagekit)
TEMPLATE = lib

CONFIG += plugin link_pkgconfig c++11
PKGCONFIG += nymea packagekitqt5

# There is a bug in packagekit's pkgconfig < 1.0 
# The include directive states /usr/include/PackageKit/packagekitqt5/ erraneously
# Adding the correct path manually. Drop this when not supported any more.
INCLUDEPATH+=/usr/include/packagekitqt5/PackageKit/

SOURCES += \
    updatecontrollerpackagekit.cpp \


HEADERS += \
    updatecontrollerpackagekit.h \


target.path = $$[QT_INSTALL_LIBS]/nymea/platform/
INSTALLS += target
