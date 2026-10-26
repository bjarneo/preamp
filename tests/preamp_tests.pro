QT += core gui network quick testlib
CONFIG += c++20 testcase link_pkgconfig
PKGCONFIG += lilv-0
TARGET = preamp_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += PREAMP_SOURCE_DIR=\\\"$$PWD/..\\\"

HEADERS += \
    ../src/analyzer.h \
    ../src/autoeq.h \
    ../src/brand.h \
    ../src/dsp.h \
    ../src/eq.h \
    ../src/graph.h \
    ../src/plugins.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    preamp_tests.cpp \
    ../src/analyzer.cpp \
    ../src/autoeq.cpp \
    ../src/brand.cpp \
    ../src/dsp.cpp \
    ../src/eq.cpp \
    ../src/graph.cpp \
    ../src/plugins.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
