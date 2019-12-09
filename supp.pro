QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    core/Bundle.cpp \
    core/DiagnosticEmitter.cpp \
    core/ExecutionContext.cpp \
    core/Expression.cpp \
    core/IRValidate.cpp \
    core/OutputHandler.cpp \
    core/Parser.cpp \
    core/Task.cpp \
    core/XML_IR.cpp \
    ui/DocumentEdit.cpp \
    ui/DocumentWidget.cpp \
    ui/PlainTextDocumentWidget.cpp \
    ui/main.cpp \
    ui/MainWindow.cpp

HEADERS += \
    core/CLIDriver.h \
    core/Parser.h \
    core/XML.h \
    ui/PlainTextDocumentWidget.h \
    util/ADT.h \
    core/Bundle.h \
    core/ExecutionContext.h \
    core/Expression.h \
    core/IR.h \
    core/OutputHandlerBase.h \
    core/Task.h \
    core/Value.h \
    core/DiagnosticEmitter.h \
    ui/DocumentEdit.h \
    ui/DocumentWidget.h \
    ui/MainWindow.h \


FORMS += \
    ui/MainWindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
