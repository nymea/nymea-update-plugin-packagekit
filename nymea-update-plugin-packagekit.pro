QT += network dbus
QT -= gui

TARGET = $$qtLibraryTarget(nymea_updatepluginpackagekit)
TEMPLATE = lib

greaterThan(QT_MAJOR_VERSION, 5) {
    message("Building using Qt6 support")
    CONFIG *= c++17
    QMAKE_LFLAGS *= -std=c++17
    QMAKE_CXXFLAGS *= -std=c++17
    # Python init is crashing in Qt6,
    # disable by default until fixed
    CONFIG += withoutpython

    # libpackagekitqt6-dev
    PKGCONFIG += packagekitqt6

} else {
    message("Building using Qt5 support")
    CONFIG *= c++11
    QMAKE_LFLAGS *= -std=c++11
    QMAKE_CXXFLAGS *= -std=c++11
    DEFINES += QT_DISABLE_DEPRECATED_UP_TO=0x050F00

    # There are several bugs in packagekit's pkgconfig
    # On xenial, the include directive states /usr/include/PackageKit/packagekitqt5/ erraneously
    # On stretch, the linker flags have a standalong "-l" in there and breaks linkage of Qt5Xml
    # Adding the package manually. Update this to pkgconfig when issues are resolved.
    #PKGCONFIG += packagekitqt5
    INCLUDEPATH += /usr/include/packagekitqt5/PackageKit
    LIBS += -lpackagekitqt5
}

QMAKE_CXXFLAGS += -Werror
QMAKE_CXXFLAGS *= -g

CONFIG += plugin link_pkgconfig
PKGCONFIG += nymea

SOURCES += \
    updatecontrollerpackagekit.cpp

HEADERS += \
    updatecontrollerpackagekit.h


target.path = $$[QT_INSTALL_LIBS]/nymea/platform/
INSTALLS += target
