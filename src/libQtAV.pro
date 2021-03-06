TEMPLATE = lib
TARGET = QtAV

QT += core gui
greaterThan(QT_MAJOR_VERSION, 4) {
  QT += widgets
}

CONFIG *= qtav-buildlib

#var with '_' can not pass to pri?
STATICLINK = 0
PROJECTROOT = $$PWD/..
!include(libQtAV.pri): error("could not find libQtAV.pri")
preparePaths($$OUT_PWD/../out)


RESOURCES += ../i18n/QtAV.qrc \
    shaders/shaders.qrc

win32 {
    RC_FILE = $${PROJECTROOT}/res/QtAV.rc
#no depends for rc file by default, even if rc includes a header. Makefile target use '/' as default, so not works iwth win cmd
    rc.target = $$clean_path($$RC_FILE) #rc obj depends on clean path target
    rc.depends = $$PWD/QtAV/version.h
#why use multiple rule failed? i.e. add a rule without command
    isEmpty(QMAKE_SH) {
        rc.commands = @copy /B $$system_path($$RC_FILE)+,, #change file time
    } else {
        rc.commands = @touch $$RC_FILE #change file time
    }
    QMAKE_EXTRA_TARGETS += rc
}
OTHER_FILES += $$RC_FILE
TRANSLATIONS = $${PROJECTROOT}/i18n/QtAV_zh_CN.ts

*msvc* {
#link FFmpeg and portaudio which are built by gcc need /SAFESEH:NO
    QMAKE_LFLAGS += /SAFESEH:NO
    INCLUDEPATH += compat/msvc
}
#UINT64_C: C99 math features, need -D__STDC_CONSTANT_MACROS in CXXFLAGS
DEFINES += __STDC_CONSTANT_MACROS

LIBS += -lavcodec -lavformat -lavutil -lswscale
config_swresample {
    DEFINES += QTAV_HAVE_SWRESAMPLE=1
    SOURCES += AudioResamplerFF.cpp
    LIBS += -lswresample
}
config_avresample {
    DEFINES += QTAV_HAVE_AVRESAMPLE=1
    SOURCES += AudioResamplerLibav.cpp
    LIBS += -lavresample
}
ipp-link {
    DEFINES += IPP_LINK
    ICCROOT = $$(IPPROOT)/../compiler
    INCLUDEPATH += $$(IPPROOT)/include
    message("QMAKE_TARGET.arch" $$QMAKE_TARGET.arch)
    *64|contains(QMAKE_TARGET.arch, x86_64)|contains(TARGET_ARCH, x86_64) {
        IPPARCH=intel64
    } else {
        IPPARCH=ia32
    }
    LIBS *= -L$$(IPPROOT)/lib/$$IPPARCH -lippcc -lippcore -lippi \
            -L$$(IPPROOT)/../compiler/lib/$$IPPARCH -lsvml -limf
    #omp for static link. _t is multi-thread static link
}
config_portaudio {
    SOURCES += AudioOutputPortAudio.cpp
    SDK_HEADERS += QtAV/AudioOutputPortAudio.h
    DEFINES *= QTAV_HAVE_PORTAUDIO=1
    LIBS *= -lportaudio
    #win32: LIBS *= -lwinmm #-lksguid #-luuid
}
config_openal {
    SOURCES += AudioOutputOpenAL.cpp
    SDK_HEADERS += QtAV/AudioOutputOpenAL.h
    DEFINES *= QTAV_HAVE_OPENAL=1
    win32: LIBS += -lOpenAL32
    unix:!mac:!blackberry: LIBS += -lopenal
    blackberry: LIBS += -lOpenAL
    mac: LIBS += -framework OpenAL
    mac: DEFINES += HEADER_OPENAL_PREFIX
}
config_gdiplus {
    DEFINES *= QTAV_HAVE_GDIPLUS=1
    SOURCES += GDIRenderer.cpp
    HEADERS += QtAV/private/GDIRenderer_p.h
    SDK_HEADERS += QtAV/GDIRenderer.h
    LIBS += -lgdiplus -lGdi32
}
config_direct2d {
    DEFINES *= QTAV_HAVE_DIRECT2D=1
    !*msvc*: INCLUDEPATH += $$PROJECTROOT/contrib/d2d1headers
    SOURCES += Direct2DRenderer.cpp
    HEADERS += QtAV/private/Direct2DRenderer_p.h
    SDK_HEADERS += QtAV/Direct2DRenderer.h
    #LIBS += -lD2d1
}
config_xv {
    DEFINES *= QTAV_HAVE_XV=1
    SOURCES += XVRenderer.cpp
    HEADERS += QtAV/private/XVRenderer_p.h
    SDK_HEADERS += QtAV/XVRenderer.h
    LIBS += -lXv
}
config_gl {
    QT *= opengl
    DEFINES *= QTAV_HAVE_GL=1
    SOURCES += GLWidgetRenderer.cpp
    HEADERS += QtAV/private/GLWidgetRenderer_p.h
    SDK_HEADERS += QtAV/GLWidgetRenderer.h
    OTHER_FILES += shaders/yuv_rgb.f.glsl
}
config_cuda {
    DEFINES += QTAV_HAVE_CUDA=1
    HEADERS += cuda/helper_cuda.h
    SOURCES += VideoDecoderCUDA.cpp
    INCLUDEPATH += $$(CUDA_PATH)/include $$PWD/cuda
    win32 {
        isEqual(TARGET_ARCH, x86): LIBS += -L$$(CUDA_PATH)/lib/Win32
        else: LIBS += -L$$(CUDA_PATH)/lib/x64
    }
    LIBS += -lnvcuvid -lcuda
}
config_dxva {
    DEFINES *= QTAV_HAVE_DXVA=1
    SOURCES += VideoDecoderDXVA.cpp
    LIBS += -lOle32
}
config_vaapi {
    DEFINES *= QTAV_HAVE_VAAPI=1
    SOURCES += VideoDecoderVAAPI.cpp
    LIBS += -lva -lva-x11 #TODO: dynamic load
}
config_libcedarv {
    DEFINES *= QTAV_HAVE_CEDARV=1
    SOURCES += VideoDecoderCedarv.cpp
    LIBS += -lvecore -lcedarv
}
SOURCES += \
    QtAV_Compat.cpp \
    QtAV_Global.cpp \
    AudioThread.cpp \
    AVThread.cpp \
    AudioDecoder.cpp \
    AudioFormat.cpp \
    AudioFrame.cpp \
    AudioOutput.cpp \
    AudioOutputTypes.cpp \
    AudioResampler.cpp \
    AudioResamplerTypes.cpp \
    AVDecoder.cpp \
    AVDemuxer.cpp \
    AVDemuxThread.cpp \
    Frame.cpp \
    Filter.cpp \
    FilterContext.cpp \
    FilterManager.cpp \
    GraphicsItemRenderer.cpp \
    ImageConverter.cpp \
    ImageConverterFF.cpp \
    ImageConverterIPP.cpp \
    QPainterRenderer.cpp \
    OSD.cpp \
    OSDFilter.cpp \
    Packet.cpp \
    AVError.cpp \
    AVPlayer.cpp \
    VideoCapture.cpp \
    VideoFormat.cpp \
    VideoFrame.cpp \
    VideoRenderer.cpp \
    VideoRendererTypes.cpp \
    VideoOutputEventFilter.cpp \
    WidgetRenderer.cpp \
    AVOutput.cpp \
    OutputSet.cpp \
    AVClock.cpp \
    Statistics.cpp \
    VideoDecoder.cpp \
    VideoDecoderTypes.cpp \
    VideoDecoderFFmpeg.cpp \
    VideoDecoderFFmpegHW.cpp \
    VideoThread.cpp \
    QAVIOContext.cpp \
    CommonTypes.cpp

SDK_HEADERS *= \
    QtAV/QtAV.h \
    QtAV/dptr.h \
    QtAV/QtAV_Global.h \
    QtAV/AudioResampler.h \
    QtAV/AudioResamplerTypes.h \
    QtAV/AudioDecoder.h \
    QtAV/AudioFormat.h \
    QtAV/AudioFrame.h \
    QtAV/AudioOutput.h \
    QtAV/AudioOutputTypes.h \
    QtAV/AVDecoder.h \
    QtAV/AVDemuxer.h \
    QtAV/BlockingQueue.h \
    QtAV/Filter.h \
    QtAV/FilterContext.h \
    QtAV/Frame.h \
    QtAV/GraphicsItemRenderer.h \
    QtAV/ImageConverter.h \
    QtAV/ImageConverterTypes.h \
    QtAV/QPainterRenderer.h \
    QtAV/OSD.h \
    QtAV/OSDFilter.h \
    QtAV/Packet.h \
    QtAV/AVError.h \
    QtAV/AVPlayer.h \
    QtAV/VideoCapture.h \
    QtAV/VideoRenderer.h \
    QtAV/VideoRendererTypes.h \
    QtAV/WidgetRenderer.h \
    QtAV/AVOutput.h \
    QtAV/AVClock.h \
    QtAV/VideoDecoder.h \
    QtAV/VideoDecoderTypes.h \
    QtAV/VideoDecoderFFmpeg.h \
    QtAV/VideoDecoderFFmpegHW.h \
    QtAV/VideoFormat.h \
    QtAV/VideoFrame.h \
    QtAV/FactoryDefine.h \
    QtAV/Statistics.h \
    QtAV/version.h


HEADERS *= \
    $$SDK_HEADERS \
    QtAV/prepost.h \
    QtAV/AVDemuxThread.h \
    QtAV/AVThread.h \
    QtAV/AudioThread.h \
    QtAV/VideoThread.h \
    QtAV/VideoOutputEventFilter.h \
    QtAV/OutputSet.h \
    QtAV/QtAV_Compat.h \
    QtAV/singleton.h \
    QtAV/factory.h \
    QtAV/FilterManager.h \
    QtAV/private/AudioOutput_p.h \
    QtAV/private/AudioResampler_p.h \
    QtAV/private/AVThread_p.h \
    QtAV/private/AVDecoder_p.h \
    QtAV/private/AVOutput_p.h \
    QtAV/private/Filter_p.h \
    QtAV/private/Frame_p.h \
    QtAV/private/GraphicsItemRenderer_p.h \
    QtAV/private/ImageConverter_p.h \
    QtAV/private/VideoDecoder_p.h \
    QtAV/private/VideoDecoderFFmpeg_p.h \
    QtAV/private/VideoDecoderFFmpegHW_p.h \
    QtAV/private/VideoRenderer_p.h \
    QtAV/private/QPainterRenderer_p.h \
    QtAV/private/WidgetRenderer_p.h \
    QtAV/QAVIOContext.h \
    QtAV/CommonTypes.h


SDK_INCLUDE_FOLDER = QtAV
include($$PROJECTROOT/deploy.pri)
