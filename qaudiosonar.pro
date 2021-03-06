TEMPLATE        = app
CONFIG          += qt warn_on release
QT		+= core gui widgets
HEADERS         += qaudiosonar.h
HEADERS         += qaudiosonar_button.h
HEADERS         += qaudiosonar_buttonmap.h
SOURCES         += qaudiosonar.cpp
SOURCES		+= qaudiosonar_button.cpp
SOURCES		+= qaudiosonar_buttonmap.cpp
SOURCES		+= qaudiosonar_ftt.cpp
SOURCES		+= qaudiosonar_iso.cpp
SOURCES		+= qaudiosonar_midi.cpp
SOURCES		+= qaudiosonar_mul.cpp
SOURCES		+= qaudiosonar_oss.cpp
SOURCES         += qaudiosonar_correlation.cpp
SOURCES         += qaudiosonar_display.cpp
SOURCES         += qaudiosonar_wave.cpp
RESOURCES	+= qaudiosonar.qrc

macx {
TARGET          = QuickAudioSonar
} else {
TARGET          = qaudiosonar
}

LIBS            += -lpthread -lm

isEmpty(PREFIX) {
PREFIX		= /usr/local
}

isEmpty(PORTAUDIOPATH) {
PORTAUDIOPATH= ../portaudio
}

macx {
icons.path= $${DESTDIR}/Contents/Resources
icons.files= qaudiosonar.icns
QMAKE_BUNDLE_DATA+= icons
QMAKE_INFO_PLIST= qaudiosonar_osx.plist
OTHER_FILES+= qaudiosonar.entitlements
HAVE_STATIC_PORTAUDIO=YES
}

ios {
icons.path	= $${PREFIX}
icons.files	= qaudiosonar_144x144.png qaudiosonar_152x152.png qaudiosonar_72x72.png qaudiosonar_76x76.png
QMAKE_BUNDLE_DATA += icons
QMAKE_INFO_PLIST= qaudiosonar_ios.plist
QMAKE_APPLE_DEVICE_ARCHS= armv7 arm64
QMAKE_IOS_DEPLOYMENT_TARGET= 9.2
HAVE_STATIC_PORTAUDIO=YES
}

!isEmpty(HAVE_STATIC_PORTAUDIO) {
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
DEFINES+= HAVE_CLOCK_GETTIME=1
DEFINES+= HAVE_NANOSLEEP=1

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
macx {
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core_utilities.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core_blocking.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio/pa_mac_core.c
DEFINES+= PA_USE_COREAUDIO=1
LIBS+=  -framework Carbon
LIBS+=  -framework AudioUnit
}
ios {
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio_ios/pa_ios_core_utilities.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio_ios/pa_ios_core_blocking.c
SOURCES+= $${PORTAUDIOPATH}/src/hostapi/coreaudio_ios/pa_ios_core.c
DEFINES+= PA_USE_COREAUDIO_IOS=1
}
SOURCES+= $${PORTAUDIOPATH}/src/os/unix/pa_unix_hostapis.c
SOURCES+= $${PORTAUDIOPATH}/src/os/unix/pa_unix_util.c

LIBS+=  -framework CoreAudio
LIBS+=  -framework AudioToolbox
}

android {
QT += androidextras
QT += gui-private
}

!macx:!android:!ios:unix {
INCLUDEPATH+=   $${PREFIX}/include
LIBDIR+=        $${PREFIX}/lib
LIBS+=          -lportaudio

icons.path	= $${PREFIX}/share/pixmaps
icons.files	= qaudiosonar.png
INSTALLS	+= icons

desktop.path	= $${PREFIX}/share/applications
desktop.files	= qaudiosonar.desktop
INSTALLS	+= desktop
}

target.path	= $${PREFIX}/bin
INSTALLS	+= target
