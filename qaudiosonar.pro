TEMPLATE        = app
CONFIG          += qt warn_on release
QT		+= core gui widgets
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
icons.path= $${DESTDIR}/Contents/Resources
icons.files= qaudiosonar.icns
QMAKE_BUNDLE_DATA+= icons
QMAKE_INFO_PLIST= qaudiosonar_osx.plist
OTHER_FILES+= qaudiosonar.entitlements
PORTAUDIOPATH= ../portaudio

INCLUDEPATH+= $${PORTAUDIOPATH}/include
INCLUDEPATH+= $${PORTAUDIOPATH}/src/common
INCLUDEPATH+= $${PORTAUDIOPATH}/src/os/unix

DEFINES+= PA_LITTLE_ENDIAN
DEFINES+= PACKAGE_NAME=\\\"\\\"
DEFINES+= PACKAGE_TARNAME=\\\"\\\"
DEFINES+= PACKAGE_VERSION=\\\"\\\"
DEFINES+= PACKAGE_STRING=\\\"\\\"
DEFINES+= PACKAGE_BUGREPORT=\\\"\\\"
DEFINES+= PACKAGE_URL=\\\"\\\"
DEFINES+= STDC_HEADERS=1
DEFINES+= HAVE_SYS_TYPES_H=1
DEFINES+= HAVE_SYS_STAT_H=1
DEFINES+= HAVE_STDLIB_H=1
DEFINES+= HAVE_STRING_H=1
DEFINES+= HAVE_MEMORY_H=1
DEFINES+= HAVE_STRINGS_H=1
DEFINES+= HAVE_INTTYPES_H=1
DEFINES+= HAVE_STDINT_H=1
DEFINES+= HAVE_UNISTD_H=1
DEFINES+= HAVE_DLFCN_H=1
DEFINES+= SIZEOF_SHORT=2
DEFINES+= SIZEOF_INT=4
DEFINES+= SIZEOF_LONG=8
DEFINES+= HAVE_CLOCK_GETTIME=1
DEFINES+= HAVE_NANOSLEEP=1
DEFINES+= PA_USE_COREAUDIO=1

SOURCES+= $${PORTAUDIOPATH}/src/common/pa_debugprint.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_ringbuffer.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_front.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_process.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_allocation.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_dither.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_cpuload.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_stream.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_trace.c
SOURCES+= $${PORTAUDIOPATH}/src/common/pa_converters.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/skeleton/pa_hostapi_skeleton.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core_utilities.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core_blocking.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core.c
SOURCES+= $${PORTAUDIOPATH}/src/os/unix/pa_unix_hostapis.c
SOURCES+= $${PORTAUDIOPATH}/src/os/unix/pa_unix_util.c

LIBS+=  -framework Carbon
LIBS+=  -framework CoreAudio
LIBS+=  -framework AudioUnit
LIBS+=  -framework AudioToolBox
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
