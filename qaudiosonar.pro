#
# QMAKE project file for Quick Audio Sonar
#
TEMPLATE        = app
CONFIG          += qt warn_on release
QT		+= core gui widgets

isEmpty(PREFIX) {
PREFIX		= /usr/local
}

HEADERS         += src/qaudiosonar.h
HEADERS         += src/qaudiosonar_button.h
HEADERS         += src/qaudiosonar_buttonmap.h
HEADERS         += src/qaudiosonar_configdlg.h
HEADERS         += src/qaudiosonar_mainwindow.h
HEADERS         += src/qaudiosonar_siggen.h
HEADERS         += src/qaudiosonar_spectrum.h

SOURCES         += src/qaudiosonar.cpp
SOURCES         += src/qaudiosonar_button.cpp
SOURCES         += src/qaudiosonar_buttonmap.cpp
SOURCES         += src/qaudiosonar_configdlg.cpp
SOURCES         += src/qaudiosonar_correlation.cpp
SOURCES         += src/qaudiosonar_display.cpp
SOURCES         += src/qaudiosonar_ftt.cpp
SOURCES         += src/qaudiosonar_iso.cpp
SOURCES         += src/qaudiosonar_mainwindow.cpp
SOURCES         += src/qaudiosonar_midi.cpp
SOURCES         += src/qaudiosonar_mul.cpp
SOURCES         += src/qaudiosonar_oss.cpp
SOURCES         += src/qaudiosonar_siggen.cpp
SOURCES         += src/qaudiosonar_spectrum.cpp
SOURCES         += src/qaudiosonar_wave.cpp

macx {
HEADERS		+= mac/activity.h
SOURCES		+= mac/activity.mm
}

# ASIO audio backend
win32 {
DEFINES         -= UNICODE
SOURCES         += \
        windows/sound_asio.cpp \
        windows/merge.cpp \
        windows/ASIOSDK2/common/asio.cpp \
        windows/ASIOSDK2/host/asiodrivers.cpp \
        windows/ASIOSDK2/host/pc/asiolist.cpp
INCLUDEPATH     += \
        windows/ASIOSDK2/common \
        windows/ASIOSDK2/host \
        windows/ASIOSDK2/host/pc
DEFINES         += HAVE_ASIO_AUDIO
}

# MacOS audio backend
macx {
SOURCES		+= mac/sound_mac.cpp
LIBS+=		-framework AudioUnit
LIBS+=		-framework CoreAudio
LIBS+=		-framework CoreMIDI
DEFINES		+= HAVE_MAC_AUDIO
}

# JACK audio backend
!macx:!win32 {
SOURCES		+= linux/sound_jack.cpp
LIBS		+= -L$${PREFIX}/lib -ljack
DEFINES		+= HAVE_JACK_AUDIO
}

RESOURCES	+= qaudiosonar.qrc

macx {
TARGET          = QuickAudioSonar
} else {
TARGET          = qaudiosonar
}

LIBS            += -lpthread -lm

macx {
QMAKE_ASSET_CATALOGS += mac/Assets.xcassets
QMAKE_INFO_PLIST= mac/qaudiosonar.plist
OTHER_FILES += mac/qaudiosonar.entitlements
}

ios {
QMAKE_ASSET_CATALOGS += mac/Assets.xcassets
QMAKE_INFO_PLIST= ios/qaudiosonar.plist
QMAKE_APPLE_DEVICE_ARCHS= arm64
QMAKE_IOS_DEPLOYMENT_TARGET= 13.0
}

android {
QT += androidextras
QT += gui-private
}

win32 {
CONFIG	+= staticlib
LIBS	+= \
	-lole32 \
	-luser32 \
	-ladvapi32 \
	-lwinmm

QMAKE_CXXFLAGS	+= -include windows.h
INCLUDEPATH	+= windows/include
RC_FILE		= windows/mainicon.rc
}

!macx:!android:!ios:!win32:unix {
INCLUDEPATH+=   $${PREFIX}/include
LIBDIR+=        $${PREFIX}/lib

icons.path	= $${PREFIX}/share/pixmaps
icons.files	= qaudiosonar.png
INSTALLS	+= icons

desktop.path	= $${PREFIX}/share/applications
desktop.files	= qaudiosonar.desktop
INSTALLS	+= desktop
}

target.path	= $${PREFIX}/bin
INSTALLS	+= target
