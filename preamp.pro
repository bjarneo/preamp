QT += core gui qml quick network dbus

CONFIG += c++20 release link_pkgconfig
PKGCONFIG += libpipewire-0.3 lilv-0
TARGET = preamp
TEMPLATE = app

# The audio thread works on floats it never shares with anything slower.
QMAKE_CXXFLAGS_RELEASE += -O3

HEADERS += \
    src/analyzer.h \
    src/autoeq.h \
    src/brand.h \
    src/cli.h \
    src/controller.h \
    src/daemon.h \
    src/dsp.h \
    src/eq.h \
    src/graph.h \
    src/hyprland.h \
    src/ipc.h \
    src/plugins.h \
    src/pwengine.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/analyzer.cpp \
    src/autoeq.cpp \
    src/brand.cpp \
    src/cli.cpp \
    src/controller.cpp \
    src/daemon.cpp \
    src/dsp.cpp \
    src/eq.cpp \
    src/graph.cpp \
    src/hyprland.cpp \
    src/ipc.cpp \
    src/main.cpp \
    src/plugins.cpp \
    src/pwengine.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
