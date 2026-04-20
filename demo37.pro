QT += core widgets gui
CONFIG += c++17
TARGET = APP121411
TEMPLATE = app
RC_FILE = version.rc
RESOURCES = resources.qrc

SOURCES = main.cpp
HEADERS = main.h

win32: DEFINES += _WIN32_WINNT=0x0601