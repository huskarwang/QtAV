/******************************************************************************
    QtAV:  Media play library based on Qt and FFmpeg
    Copyright (C) 2012-2014 Wang Bin <wbsecg1@gmail.com>

*   This file is part of QtAV

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
******************************************************************************/

#include "QtAV/GLWidgetRenderer.h"
#include "private/GLWidgetRenderer_p.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QResizeEvent>
#include <QtOpenGL/QGLShaderProgram>
#include <QtOpenGL/QGLShader>
//TODO: vsync http://stackoverflow.com/questions/589064/how-to-enable-vertical-sync-in-opengl
//TODO: check gl errors
//glEGLImageTargetTexture2DOES:http://software.intel.com/en-us/articles/using-opengl-es-to-accelerate-apps-with-legacy-2d-guis
/*
//GL_BGRA is available in OpenGL >= 1.2
#ifndef GL_BGRA
#ifndef GL_BGRA_EXT
#if defined QT_OPENGL_ES_2
#include <GLES2/gl2ext.h>
#elif defined QT_OPENGL_ES
#include <GLES/glext.h>
#else
#include <GL/glext.h> //GL_BGRA_EXT for OpenGL<=1.1 //TODO Apple include <OpenGL/xxx>
#endif
#endif //GL_BGRA_EXT
//TODO: glPixelStorei(GL_PACK_SWAP_BYTES, ) to swap rgba?
#ifndef GL_BGRA //it may be defined in glext.h
#define GL_BGRA GL_BGRA_EXT
#define GL_BGR GL_BGR_EXT
#endif //GL_BGRA
#endif //GL_BGRA
*/
#ifdef QT_OPENGL_ES_2
#define FMT_INTERNAL GL_BGRA //why BGRA?
#define FMT GL_BGRA
#else //QT_OPENGL_ES_2
#define FMT_INTERNAL GL_RGBA //why? why 3 works?
#define FMT GL_BGRA
#endif //QT_OPENGL_ES_2

#include <QtAV/FilterContext.h>
#include <QtAV/OSDFilter.h>

#define UPLOAD_ROI 0
#define ROI_TEXCOORDS 1

//TODO: QGLfunctions?
namespace QtAV {


const GLfloat kVertices[] = {
    -1, 1,
    1, 1,
    1, -1,
    -1, -1,
};

static inline void checkGlError(const char* op = 0) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return;
    qWarning("GL error %s (%#x): %s", op, error, glGetString(error));
}

#define CHECK_GL_ERROR(FUNC) \
    FUNC; \
    checkGlError(#FUNC);


static const char kVertexShader[] =
    "attribute vec4 a_Position;\n"
    "attribute vec2 a_TexCoords;\n"
    "uniform mat4 u_MVP_matrix;\n"
    "varying vec2 v_TexCoords;\n"
    "void main() {\n"
    "  gl_Position = u_MVP_matrix * a_Position;\n"
    "  v_TexCoords = a_TexCoords; \n"
    "}\n";

static const char kFragmentShader[] =
#ifdef QT_OPENGL_ES_2
    "precision mediump float;\n"
#endif
    "uniform sampler2D u_Texture0;\n"
    "varying vec2 v_TexCoords;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_Texture0, v_TexCoords);\n"
    "}\n";


//http://www.opengl.org/wiki/GLSL#Error_Checking
// TODO: use QGLShaderProgram for better compatiblity
GLuint GLWidgetRendererPrivate::loadShader(GLenum shaderType, const char* pSource) {
    if (!hasGLSL)
        return 0;
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_FALSE) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*)malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    qWarning("Could not compile shader %d:\n%s\n", shaderType, buf);
                    free(buf);
                }
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

GLuint GLWidgetRendererPrivate::createProgram(const char* pVertexSource, const char* pFragmentSource) {
    if (!hasGLSL)
        return 0;
    program = glCreateProgram(); //TODO: var name conflict. temp var is better
    if (!program)
        return 0;
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }
    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        glDeleteShader(vertexShader);
        return 0;
    }
    glAttachShader(program, vertexShader);
    glAttachShader(program, pixelShader);
    glLinkProgram(program);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
        if (bufLength) {
            char* buf = (char*)malloc(bufLength);
            if (buf) {
                glGetProgramInfoLog(program, bufLength, NULL, buf);
                qWarning("Could not link program:\n%s\n", buf);
                free(buf);
            }
        }
        glDetachShader(program, vertexShader);
        glDeleteShader(vertexShader);
        glDetachShader(program, pixelShader);
        glDeleteShader(pixelShader);
        glDeleteProgram(program);
        program = 0;
        return 0;
    }
    //Always detach shaders after a successful link.
    glDetachShader(program, vertexShader);
    glDetachShader(program, pixelShader);
    vert = vertexShader;
    frag = pixelShader;
    return program;
}

bool GLWidgetRendererPrivate::releaseResource()
{
    pixel_fmt = VideoFormat::Format_Invalid;
    texture0Size = QSize();
    glDeleteTextures(textures.size(), textures.data());
    qDebug("delete %d textures", textures.size());
    textures.clear();
    if (vert) {
        if (program)
            glDetachShader(program, vert);
        glDeleteShader(vert);
    }
    if (frag) {
        if (program)
            glDetachShader(program, frag);
        glDeleteShader(frag);
    }
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
    return true;
}

bool GLWidgetRendererPrivate::initTexture(GLuint tex, GLint internalFormat, GLenum format, int width, int height)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    setupQuality();
    // This is necessary for non-power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D
                 , 0                //level
                 , internalFormat               //internal format. 4? why GL_RGBA? GL_RGB?
                 , width
                 , height
                 , 0                //border, ES not support
                 , format          //format, must the same as internal format?
                 , GL_UNSIGNED_BYTE
                 , NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool GLWidgetRendererPrivate::prepareShaderProgram(const VideoFormat &fmt, int width, int height)
{
    // isSupported(pixfmt)
    if (!fmt.isValid())
        return false;
    releaseResource();
    pixel_fmt = fmt.pixelFormat();
    texture0Size = QSize(width, height);

    textures.resize(fmt.planeCount());
    glGenTextures(textures.size(), textures.data());

    //http://www.berkelium.com/OpenGL/GDC99/internalformat.html
    //TODO: check channels
    //NV12: UV is 1 plane. 16 bits as a unit. GL_LUMINANCE4, 8, 16, ... 32?
    //GL_LUMINANCE, GL_LUMINANCE_ALPHA are deprecated in GL3, removed in GL3.1
    //replaced by GL_RED, GL_RG, GL_RGB, GL_RGBA? for 1, 2, 3, 4 channel image
    //http://www.gamedev.net/topic/634850-do-luminance-textures-still-exist-to-opengl/
    //https://github.com/kivy/kivy/issues/1738: GL_LUMINANCE does work on a Galaxy Tab 2. LUMINANCE_ALPHA very slow on Linux
     //ALPHA: vec4(0,0,0,A), LUMINANCE: (L,L,L,1), LUMINANCE_ALPHA: (L,L,L,A)
    /*
     * To support both planar and packed use GL_ALPHA and in shader use r,g,a like xbmc does.
     * or use Swizzle_mask to layout the channels: http://www.opengl.org/wiki/Texture#Swizzle_mask
     * GL ES2 support: GL_RGB, GL_RGBA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_ALPHA
     * http://stackoverflow.com/questions/18688057/which-opengl-es-2-0-texture-formats-are-color-depth-or-stencil-renderable
     */
    internal_format = QVector<GLint>(fmt.planeCount(), FMT_INTERNAL);
    data_format = QVector<GLenum>(fmt.planeCount(), FMT);
    if (fmt.isRGB()) {
        if (fmt.isPlanar()) {

        }
    } else {
        //uyvy, nv12, yv12
        if (fmt.isPlanar()) {
            //why luminance?
            internal_format[0] = internal_format[1] = GL_LUMINANCE; //vec4(L,L,L,0)
            data_format[0] = data_format[1] = GL_LUMINANCE; //or GL_RED
            internal_format[2] = GL_ALPHA;
            data_format[2] = GL_ALPHA;
            //channels == 2: GL_RG, GL_LUMINANCE_ALPHA
        } else {
            //check channles
        }
    }

    if (!hasGLSL) {
        // more than 1?
        initTexture(textures[0], internal_format[0], data_format[0], width, height);
        qWarning("Does not support GLSL!");
        return false;
    }
    // FIXME
    if (fmt.isRGB()) {
        program = createProgram(kVertexShader, kFragmentShader);
        if (!program) {
            qWarning("Could not create shader program.");
            return false;
        }
    } else if (fmt == VideoFormat::Format_YUV420P) {
        QString shader_file = "/shaders/yuv_rgb.f.glsl";
        QFile f(qApp->applicationDirPath() + shader_file);
        if (!f.exists()) {
            f.setFileName(":" + shader_file);
        }
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning("Can not load shader %s: %s", f.fileName().toUtf8().constData(), f.errorString().toUtf8().constData());
            return false;
        }
        program = createProgram(kVertexShader, f.readAll().constData());
        f.close();
        if (!program) {
            qWarning("Could not create shader program.");
            return false;
        }
    } else {
        return false;
    }
    // vertex shader
    CHECK_GL_ERROR(a_Position = glGetAttribLocation(program, "a_Position"));
    a_TexCoords = glGetAttribLocation(program, "a_TexCoords");
    qDebug("glGetAttribLocation(\"a_TexCoords\") = %d\n", a_TexCoords);
    u_matrix = glGetUniformLocation(program, "u_MVP_matrix");
    qDebug("glGetUniformLocation(\"u_MVP_matrix\") = %d\n", u_matrix);

    // fragment shader
    u_Texture.resize(fmt.planeCount());
    for (int i = 0; i < textures.size(); ++i) {
        QString tex_var = QString("u_Texture%1").arg(i);
        u_Texture[i] = glGetUniformLocation(program, tex_var.toUtf8().constData());
        qDebug("glGetUniformLocation(\"%s\") = %d\n", tex_var.toUtf8().constData(), u_Texture[i]);
        if (i == 0) {
            width = fmt.chromaWidth(width);
            height = fmt.chromaHeight(height);
        }
        initTexture(textures[i], internal_format[i], data_format[i], width, height);
    }
    return true;
}

void GLWidgetRendererPrivate::upload(const QRect &roi)
{
    const VideoFormat fmt = video_frame.format();
#if UPLOAD_ROI
    if (fmt != pixel_fmt || roi.size() != texture0Size) {
        qDebug("update texture: %dx%d, %s", roi.width(), roi.height(), video_frame.format().name().toUtf8().constData());
        if (!prepareShaderProgram(fmt, roi.width(), roi.height())) {
#else
    if (fmt != pixel_fmt || video_frame.size() != texture0Size) {
        qDebug("update texture: %dx%d, %s", video_frame.width(), video_frame.height(), video_frame.format().name().toUtf8().constData());
        if (!prepareShaderProgram(fmt, video_frame.width(), video_frame.height())) {
#endif //UPLOAD_ROI
            qWarning("shader program create error...");
            return;
        } else {
            qDebug("shader program created!!!");
        }
    }
    //glPixelStorei(GL_UNPACK_ALIGNMENT,1); //xbmc: nv12 use bpp
    for (int i = 0; i < video_frame.planeCount(); ++i) {
        uploadPlane(i, internal_format[i], data_format[i], roi);
    }
}

void GLWidgetRendererPrivate::uploadPlane(int p, GLint internalFormat, GLenum format, const QRect& roi)
{
    if (hasGLSL) {
        glActiveTexture(GL_TEXTURE0 + p); //TODO: can remove??
    }
    glBindTexture(GL_TEXTURE_2D, textures[p]);
    setupQuality();
    //qDebug("bpl[%d]=%d", p, video_frame.bytesPerLine(p));
    // This is necessary for non-power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //uploading part of image eats less gpu memory, but may be more cpu(gles)
    //FIXME: more cpu usage then qpainter. FBO, VBO?
    //roi for planes?
    if (ROI_TEXCOORDS || roi.size() == video_frame.size()) {
        glTexSubImage2D(GL_TEXTURE_2D
                     , 0                //level
                     , 0                // xoffset
                     , 0                // yoffset
                     , video_frame.planeWidth(p)
                     , video_frame.planeHeight(p)
                     , format          //format, must the same as internal format?
                     , GL_UNSIGNED_BYTE
                     , video_frame.bits(p));
    } else {
        int roi_x = roi.x();
        int roi_y = roi.y();
        int roi_w = roi.width();
        int roi_h = roi.height();
        int plane_w = video_frame.planeWidth(p);
        VideoFormat fmt = video_frame.format();
        if (p == 0) {
            texture0Size = QSize(roi_w, roi_h);
        } else {
            roi_x = fmt.chromaWidth(roi_x);
            roi_y = fmt.chromaHeight(roi_y);
            roi_w = fmt.chromaWidth(roi_w);
            roi_h = fmt.chromaHeight(roi_h);
        }
        qDebug("roi: %d, %d %dx%d", roi_x, roi_y, roi_w, roi_h);
#if 0// defined(GL_UNPACK_ROW_LENGTH) && defined(GL_UNPACK_SKIP_PIXELS)
// http://stackoverflow.com/questions/205522/opengl-subtexturing
        glPixelStorei(GL_UNPACK_ROW_LENGTH, plane_w);
        //glPixelStorei or compute pointer
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, roi_x);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, roi_y);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, roi_w, roi_h, 0, format, GL_UNSIGNED_BYTE, video_frame.bits(p));
        //glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, roi_w, roi_h, format, GL_UNSIGNED_BYTE, video_frame.bits(p));
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#else // GL ES
//define it? or any efficient way?
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, roi_w, roi_h, 0, format, GL_UNSIGNED_BYTE, NULL);
        const char *src = (char*)video_frame.bits(p) + roi_y*plane_w + roi_x*fmt.bytesPerPixel(p);
#define UPLOAD_LINE 1
#if UPLOAD_LINE
        for (int y = 0; y < roi_h; y++) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, roi_w, 1, format, GL_UNSIGNED_BYTE, src);
        }
#else
        int line_size = roi_w*fmt.bytesPerPixel(p);
        char *sub = (char*)malloc(roi_h*line_size);
        char *dst = sub;
        for (int y = 0; y < roi_h; y++) {
            memcpy(dst, src, line_size);
            src += video_frame.bytesPerLine(p);
            dst += line_size;
        }
        // FIXME: crash
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, roi_w, roi_h, format, GL_UNSIGNED_BYTE, sub);
        free(sub);
#endif //UPLOAD_LINE
#endif //GL_UNPACK_ROW_LENGTH
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}


GLWidgetRenderer::GLWidgetRenderer(QWidget *parent, const QGLWidget* shareWidget, Qt::WindowFlags f):
    QGLWidget(parent, shareWidget, f),VideoRenderer(*new GLWidgetRendererPrivate())
{
    DPTR_INIT_PRIVATE(GLWidgetRenderer);
    DPTR_D(GLWidgetRenderer);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    /* To rapidly update custom widgets that constantly paint over their entire areas with
     * opaque content, e.g., video streaming widgets, it is better to set the widget's
     * Qt::WA_OpaquePaintEvent, avoiding any unnecessary overhead associated with repainting the
     * widget's background
     */
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    //default: swap in qpainter dtor. we should swap before QPainter.endNativePainting()
    setAutoBufferSwap(false);
    setAutoFillBackground(false);
    d.painter = new QPainter();
    d.filter_context = FilterContext::create(FilterContext::QtPainter);
    QPainterFilterContext *ctx = static_cast<QPainterFilterContext*>(d.filter_context);
    ctx->paint_device = this;
    ctx->painter = d.painter;
    setOSDFilter(new OSDFilterQPainter());
}

bool GLWidgetRenderer::receiveFrame(const VideoFrame& frame)
{
    DPTR_D(GLWidgetRenderer);
    QMutexLocker locker(&d.img_mutex);
    Q_UNUSED(locker);
    d.video_frame = frame;

    update(); //can not call updateGL() directly because no event and paintGL() will in video thread
    return true;
}

bool GLWidgetRenderer::needUpdateBackground() const
{
    return true;
}

void GLWidgetRenderer::drawBackground()
{
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLWidgetRenderer::drawFrame()
{
    DPTR_D(GLWidgetRenderer);
    QRect roi = realROI();
    d.upload(roi);

    // shader program may not ready before upload
    if (d.hasGLSL) {
        glUseProgram(d.program); //qpainter need
    }
    glDisable(GL_DEPTH_TEST);
    for (int i = 0; i < d.textures.size(); ++i) {
        glEnable(GL_TEXTURE_2D);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, d.textures[i]); //we've bind 0 after upload()
        // use glUniform1i to swap planes. swap uv: i => (3-i)%3
        glUniform1i(d.u_Texture[i], i);
    }
    //glActiveTexture(GL_TEXTURE0); //? vlc
    //TODO: compute kTexCoords only if roi changed
#if ROI_TEXCOORDS
    const GLfloat kTexCoords[] = {
            (GLfloat)roi.x()/(GLfloat)d.video_frame.width(), (GLfloat)roi.y()/(GLfloat)d.video_frame.height(),
            (GLfloat)(roi.x() + roi.width())/(GLfloat)d.video_frame.width(), (GLfloat)roi.y()/(GLfloat)d.video_frame.height(),
            (GLfloat)(roi.x() + roi.width())/(GLfloat)d.video_frame.width(), (GLfloat)(roi.y()+roi.height())/(GLfloat)d.video_frame.height(),
            (GLfloat)roi.x()/(GLfloat)d.video_frame.width(), (GLfloat)(roi.y()+roi.height())/(GLfloat)d.video_frame.height(),
    };
///        glVertexAttribPointer(d.a_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, kTexCoords);
///        glEnableVertexAttribArray(d.a_TexCoords);
#else
    const GLfloat kTexCoords[] = {
            0, 0,
            1, 0,
            1, 1,
            0, 1,
    };
#endif //ROI_TEXCOORDS
#ifndef QT_OPENGL_ES_2
    //GL_XXX may not defined in ES2. so macro is required
    if (!d.hasGLSL) {
        //qpainter will reset gl state, so need glMatrixMode and clear color(in drawBackground())
        //TODO: study what state will be reset
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glPushMatrix();
        d.setupAspectRatio(); //TODO: can we avoid calling this every time but only in resize event?
        glVertexPointer(2, GL_FLOAT, 0, kVertices);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, kTexCoords);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glPopMatrix();
    }
#endif //QT_OPENGL_ES_2
    if (d.hasGLSL) {
        d.setupAspectRatio(); //TODO: can we avoid calling this every time but only in resize event?
        //qpainter need. TODO: VBO?
        glVertexAttribPointer(d.a_Position, 2, GL_FLOAT, GL_FALSE, 0, kVertices);
        glEnableVertexAttribArray(d.a_Position);
        glVertexAttribPointer(d.a_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, kTexCoords);
        glEnableVertexAttribArray(d.a_TexCoords);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableVertexAttribArray(d.a_TexCoords);
        glDisableVertexAttribArray(d.a_Position);
    }

    for (int i = 0; i < d.textures.size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + i); //??
        glDisable(GL_TEXTURE_2D); //??
    }
}

void GLWidgetRenderer::initializeGL()
{
    DPTR_D(GLWidgetRenderer);
    makeCurrent();
    //const QByteArray extensions(reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS)));
    d.hasGLSL = QGLShaderProgram::hasOpenGLShaderPrograms();
    qDebug("OpenGL version: %d.%d  hasGLSL: %d", format().majorVersion(), format().minorVersion(), d.hasGLSL);
    initializeGLFunctions();
    d.initializeGLFunctions();

    glEnable(GL_TEXTURE_2D);
#ifndef QT_OPENGL_ES_2
    if (!d.hasGLSL) {
        glShadeModel(GL_SMOOTH); //setupQuality?
        glClearDepth(1.0f);
        //d.textures.resize(1);
        //glGenTextures(d.textures.size(), d.textures.data());
    }
#endif //QT_OPENGL_ES_2
    glClearColor(0.0, 0.0, 0.0, 0.0);
    d.setupQuality();
}

void GLWidgetRenderer::paintGL()
{
    DPTR_D(GLWidgetRenderer);
    /* we can mix gl and qpainter.
     * QPainter painter(this);
     * painter.beginNativePainting();
     * gl functions...
     * painter.endNativePainting();
     * swapBuffers();
     */
    handlePaintEvent();
    swapBuffers();
    if (d.painter && d.painter->isActive())
        d.painter->end();
}

void GLWidgetRenderer::resizeGL(int w, int h)
{
    DPTR_D(GLWidgetRenderer);
    qDebug("%s @%d %dx%d", __FUNCTION__, __LINE__, d.out_rect.width(), d.out_rect.height());
    glViewport(0, 0, w, h);
    d.setupAspectRatio();
#ifndef QT_OPENGL_ES_2
    //??
    if (!d.hasGLSL) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
#endif //QT_OPENGL_ES_2
}

void GLWidgetRenderer::resizeEvent(QResizeEvent *e)
{
    DPTR_D(GLWidgetRenderer);
    d.update_background = true;
    resizeRenderer(e->size());
    QGLWidget::resizeEvent(e); //will call resizeGL(). TODO:will call paintEvent()?
}

//TODO: out_rect not correct when top level changed
void GLWidgetRenderer::showEvent(QShowEvent *)
{
    DPTR_D(GLWidgetRenderer);
    d.update_background = true;
    /*
     * Do something that depends on widget below! e.g. recreate render target for direct2d.
     * When Qt::WindowStaysOnTopHint changed, window will hide first then show. If you
     * don't do anything here, the widget content will never be updated.
     */
}
} //namespace QtAV
