TEMPLATE        = app
unix:CONFIG     += qt warn_on release
win32:CONFIG    += windows warn_on release
QT		+= core gui network
greaterThan(QT_MAJOR_VERSION, 4) {
QT		+= widgets
}
HEADERS         = qaudiosonar.h
SOURCES         += qaudiosonar.cpp
SOURCES		+= qaudiosonar_fet.cpp
SOURCES		+= qaudiosonar_filter.cpp
SOURCES		+= qaudiosonar_oss.cpp
SOURCES         += qaudiosonar_record.cpp
RESOURCES	+= qaudiosonar.qrc
TARGET          = qaudiosonar
QTDIR_build:REQUIRES="contains(QT_CONFIG, full-config)"
unix:LIBS      += -lpthread -lm
