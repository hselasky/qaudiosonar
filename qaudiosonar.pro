TEMPLATE        = app
CONFIG          += qt warn_on release
QT		+= core gui widgets multimedia
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
SOURCES         += qaudiosonar_wave.cpp
RESOURCES	+= qaudiosonar.qrc
TARGET          = qaudiosonar
LIBS            += -lpthread -lm

macx {
icons.path	= $${DESTDIR}/Contents/Resources
icons.files	= qaudiosonar.icns
QMAKE_BUNDLE_DATA += icons
QMAKE_INFO_PLIST= qaudiosonar_osx.plist
OTHER_FILES += qaudiosonar.entitlements
}

ios {
icons.path	= $${PREFIX}
icons.files	= qaudiosonar.png
QMAKE_BUNDLE_DATA += icons
QMAKE_INFO_PLIST= qaudiosonar_ios.plist
QMAKE_APPLE_DEVICE_ARCHS= armv7 arm64
QMAKE_IOS_DEPLOYMENT_TARGET= 9.2
}

android {
QT += androidextras
QT += gui-private
}

unix {
  icons.path	= $${PREFIX}/share/pixmaps
  icons.files	= qaudiosonar.png
  INSTALLS	+= icons

  desktop.path	= $${PREFIX}/share/applications
  desktop.files	= qaudiosonar.desktop
  INSTALLS	+= desktop
}

isEmpty(PREFIX) {
PREFIX		= /usr/local
}

target.path	= $${PREFIX}/bin
INSTALLS	+= target
