TEMPLATE = lib
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += ../../sdk

SOURCES += \
        exif.c \
        winapi.c

HEADERS += \
    winapi.h
