TEMPLATE = app
TARGET = similarity.wdx
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += ../../../sdk

SOURCES += \
    Unit1.cpp \
    simil.c

QMAKE_LFLAGS += "-z relro -z now -shared"

HEADERS += \
    simil.h \
    deelx.h
