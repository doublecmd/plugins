TEMPLATE = app
TARGET = ooxml.wdx
CONFIG += console c++11
CONFIG -= app_bundle

SOURCES += \
    OfficeFullText.cpp \
    OfficeCore.cpp \
    OfficeApp.cpp \
    Office2007.cpp \
    tinyxmlerror.cpp \
    tinyxml.cpp \
    tinystr.cpp \
    tinyxmlparser.cpp \
    unzip.c \
    miniunz.cpp \
    ioapi.c

DISTFILES += \
    ooxml.pro.user

QMAKE_LFLAGS += "-z relro -z now -shared -lz"
