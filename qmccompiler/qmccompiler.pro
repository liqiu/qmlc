#-------------------------------------------------
#
# Project created by QtCreator 2014-07-14T20:44:03
#
#-------------------------------------------------

QT       += qml qml-private core-private
QT       -= gui

TARGET = qmccompiler
TEMPLATE = lib

DEPENDPATH += .
INCLUDEPATH += .
INCLUDEPATH += ../3rdparty/masm
INCLUDEPATH += ../3rdparty/masm/stubs
INCLUDEPATH += ../3rdparty/masm/stubs/wtf
INCLUDEPATH += ../3rdparty/masm/jit
INCLUDEPATH += ../3rdparty/masm/disassembler
INCLUDEPATH += ../include

include(../3rdparty/masm/masm-defs.pri)

DEFINES += ENABLE_JIT ASSERT_DISABLED=1

DEFINES += QMCCOMPILER_LIBRARY

SOURCES += \
    qmlc.cpp \
    qmlcompilation.cpp \
    qmcexporter.cpp \
    compiler.cpp \
    qmctypecompiler.cpp \
    propertycachecreator.cpp \
    defaultpropertymerger.cpp \
    signalhandlerfunctionconverter.cpp \
    enumtyperesolver.cpp \
    aliasannotator.cpp \
    customparserscriptindexer.cpp \
    scriptstringscanner.cpp \
    jsbindingexpressionsimplifier.cpp \
    jscodegenerator.cpp \
    propertyvalidator.cpp \
    componentandaliasresolver.cpp \
    irfunctioncleanser.cpp \
    qmcinstructionselection.cpp \
    scriptc.cpp


HEADERS += qmccompiler_global.h \
    qmlc.h \
    qmlcompilation.h \
    qmcexporter.h \
    compiler.h \
    qmctypecompiler.h \
    propertycachecreator.h \
    defaultpropertymerger.h \
    signalhandlerfunctionconverter.h \
    enumtyperesolver.h \
    aliasannotator.h \
    customparserscriptindexer.h \
    scriptstringscanner.h \
    jsbindingexpressionsimplifier.h \
    jscodegenerator.h \
    propertyvalidator.h \
    componentandaliasresolver.h \
    irfunctioncleanser.h \
    qmcinstructionselection.h \
    scriptc.h


unix {
    target.path = /usr/lib
    INSTALLS += target
}
