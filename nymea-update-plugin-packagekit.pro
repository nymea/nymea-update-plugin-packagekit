QT -= gui
QT += network dbus

TARGET = $$qtLibraryTarget(nymea_updatepluginpackagekit)
TEMPLATE = lib
QMAKE_CXXFLAGS *= -Werror -std=c++11 -g

CONFIG += plugin link_pkgconfig c++11
PKGCONFIG += nymea

# There are several bugs in packagekit's pkgconfig
# On xenial, the include directive states /usr/include/PackageKit/packagekitqt5/ erraneously
# On stretch, the linker flags have a standalong "-l" in there and breaks linkage of Qt5Xml
# Adding the package manually. Update this to pkgconfig when issues are resolved.
#PKGCONFIG += packagekitqt5
INCLUDEPATH += /usr/include/packagekitqt5/PackageKit
LIBS += -lpackagekitqt5

SOURCES += \
    updatecontrollerpackagekit.cpp


HEADERS += \
    updatecontrollerpackagekit.h


target.path = $$[QT_INSTALL_LIBS]/nymea/platform/
INSTALLS += target
