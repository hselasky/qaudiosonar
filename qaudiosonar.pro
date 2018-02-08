TEMPLATE        = app
unix:CONFIG     += qt warn_on release
win32:CONFIG    += windows warn_on release
QT		+= core gui network
greaterThan(QT_MAJOR_VERSION, 4) {
QT		+= widgets
}
HEADERS         += qaudiosonar.h
HEADERS         += qaudiosonar_button.h
HEADERS         += qaudiosonar_buttonmap.h
SOURCES         += qaudiosonar.cpp
SOURCES		+= qaudiosonar_button.cpp
SOURCES		+= qaudiosonar_buttonmap.cpp
SOURCES		+= qaudiosonar_iso.cpp
SOURCES		+= qaudiosonar_midi.cpp
SOURCES		+= qaudiosonar_mul.cpp
SOURCES		+= qaudiosonar_oss.cpp
SOURCES         += qaudiosonar_correlation.cpp
SOURCES         += qaudiosonar_display.cpp
SOURCES         += qaudiosonar_record.cpp
SOURCES         += qaudiosonar_wave.cpp
RESOURCES	+= qaudiosonar.qrc
TARGET          = qaudiosonar
QTDIR_build:REQUIRES="contains(QT_CONFIG, full-config)"
unix:LIBS      += -lpthread -lm

isEmpty(PREFIX) {
PREFIX		= /usr/local
}

target.path	= $${PREFIX}/bin
INSTALLS	+= target

isEmpty(HAVE_BUNDLE_ICONS) {
  icons.path	= $${PREFIX}/share/pixmaps
  icons.files	= qaudiosonar.png
  INSTALLS	+= icons
}

desktop.path	= $${PREFIX}/share/applications
desktop.files	= qaudiosonar.desktop
INSTALLS	+= desktop
