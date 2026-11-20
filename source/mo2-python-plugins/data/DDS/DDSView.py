# MO2 Revamped: the OpenGL view of the DDS preview, moved out of DDSPreview.py
# so that QtOpenGL and QtOpenGLWidgets load with the first texture shown and
# not with MO2's plugins. The shaders and DDSWidget are Mod Organizer 2's DDS
# Preview by AnyOldName3 (GNU GPL version 3).

import struct

from PyQt6.QtCore import QCoreApplication, Qt, qDebug
from PyQt6.QtGui import QColor, QOpenGLContext, QSurfaceFormat
from PyQt6.QtOpenGL import (QOpenGLBuffer, QOpenGLDebugLogger, QOpenGLShader, QOpenGLShaderProgram,
                            QOpenGLVersionFunctionsFactory, QOpenGLVersionProfile, QOpenGLVertexArrayObject)
from PyQt6.QtOpenGLWidgets import QOpenGLWidget

# the same context as DDSPreview.py: revamped_<language>.qm translates both
_TR_CONTEXT = "RevampedDDSPreview"


def tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


vertexShader2D = """
#version 150

uniform float aspectRatioRatio;

in vec4 position;
in vec2 texCoordIn;

out vec2 texCoord;

void main()
{
    texCoord = texCoordIn;
    gl_Position = position;
    if (aspectRatioRatio >= 1.0)
        gl_Position.y /= aspectRatioRatio;
    else
        gl_Position.x *= aspectRatioRatio;
}
"""

vertexShaderCube = """
#version 150

uniform float aspectRatioRatio;

in vec4 position;
in vec2 texCoordIn;

out vec2 texCoord;

void main()
{
    texCoord = texCoordIn;
    gl_Position = position;
}
"""

fragmentShaderFloat = """
#version 150

uniform sampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    gl_FragData[0] = channelMatrix *  texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderUInt = """
#version 150

uniform usampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    // autofilled alpha is 1, so if we have a scaling factor, we need separate ones for luminance and alpha
    gl_FragData[0] = channelMatrix * texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderSInt = """
#version 150

uniform isampler2D aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

void main()
{
    // autofilled alpha is 1, so if we have a scaling factor and offset, we need separate ones for luminance and alpha
    gl_FragData[0] = channelMatrix * texture(aTexture, texCoord) + channelOffset;
}
"""

fragmentShaderCube = """
#version 150

uniform samplerCube aTexture;
uniform mat4 channelMatrix;
uniform vec4 channelOffset;

in vec2 texCoord;

const float PI = 3.1415926535897932384626433832795;

void main()
{
    float theta = -2.0 * PI * texCoord.x;
    float phi = PI * texCoord.y;
    gl_FragData[0] = channelMatrix * texture(aTexture, vec3(sin(theta) * sin(phi), cos(theta) * sin(phi), cos(phi))) + channelOffset;
}
"""

transparencyVS = """
#version 150

in vec4 position;

void main()
{
    gl_Position = position;
}
"""

transparencyFS = """
#version 150

uniform vec4 backgroundColour;
uniform vec4 checkerLight;
uniform vec4 checkerDark;

void main()
{
    float x = gl_FragCoord.x;
    float y = gl_FragCoord.y;
    x = mod(x, 16.0);
    y = mod(y, 16.0);
    gl_FragData[0] = x < 8.0 ^^ y < 8.0 ? checkerDark : checkerLight;
    gl_FragData[0].rgb = backgroundColour.rgb * backgroundColour.a + gl_FragData[0].rgb * (1.0 - backgroundColour.a);
}
"""

vertices = [
    # vertex coordinates        texture coordinates
    -1.0, -1.0, 0.5, 1.0, 0.0, 1.0,
    -1.0, 1.0, 0.5, 1.0, 0.0, 0.0,
    1.0, 1.0, 0.5, 1.0, 1.0, 0.0,

    -1.0, -1.0, 0.5, 1.0, 0.0, 1.0,
    1.0, 1.0, 0.5, 1.0, 1.0, 0.0,
    1.0, -1.0, 0.5, 1.0, 1.0, 1.0,
]


glVersionProfile = QOpenGLVersionProfile()
glVersionProfile.setVersion(2, 1)


class DDSWidget(QOpenGLWidget):
    def __init__(self, ddsFile, ddsOptions, debugContext=False, checkerColours=None, parent=None,
                 f=Qt.WindowType(0)):
        super(DDSWidget, self).__init__(parent, f)

        self.checkerColours = checkerColours

        self.ddsFile = ddsFile

        self.ddsOptions = ddsOptions

        self.clean = True

        self.logger = None

        self.program = None
        self.transparecyProgram = None
        self.texture = None
        self.vbo = None
        self.vao = None

        if debugContext:
            format = QSurfaceFormat()
            format.setOption(QSurfaceFormat.FormatOption.DebugContext)
            self.setFormat(format)
            self.logger = QOpenGLDebugLogger(self)

    def __del__(self):
        # the C++ widget can be gone already when Python collects this
        try:
            self.cleanup()
        except RuntimeError:
            pass

    def __dtor__(self):
        try:
            self.cleanup()
        except RuntimeError:
            pass

    def initializeGL(self):
        if self.logger:
            self.logger.initialize()
            self.logger.messageLogged.connect(
                lambda message: qDebug(self.tr("OpenGL debug message: {0}").format(message.message())))
            self.logger.startLogging()

        gl = QOpenGLVersionFunctionsFactory.get(glVersionProfile)
        QOpenGLContext.currentContext().aboutToBeDestroyed.connect(self.cleanup)

        self.clean = False

        fragmentShader = None
        vertexShader = vertexShader2D
        if self.ddsFile.isCubemap:
            fragmentShader = fragmentShaderCube
            vertexShader = vertexShaderCube
            if QOpenGLContext.currentContext().hasExtension(b"GL_ARB_seamless_cube_map"):
                GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F
                gl.glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)
        elif self.ddsFile.glFormat.samplerType == "F":
            fragmentShader = fragmentShaderFloat
        elif self.ddsFile.glFormat.samplerType == "UI":
            fragmentShader = fragmentShaderUInt
        else:
            fragmentShader = fragmentShaderSInt

        self.program = QOpenGLShaderProgram(self)
        self.program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, vertexShader)
        self.program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, fragmentShader)
        self.program.bindAttributeLocation("position", 0)
        self.program.bindAttributeLocation("texCoordIn", 1)
        self.program.link()

        self.transparecyProgram = QOpenGLShaderProgram(self)
        self.transparecyProgram.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, transparencyVS)
        self.transparecyProgram.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, transparencyFS)
        self.transparecyProgram.bindAttributeLocation("position", 0)
        self.transparecyProgram.link()

        self.vao = QOpenGLVertexArrayObject(self)
        vaoBinder = QOpenGLVertexArrayObject.Binder(self.vao)

        self.vbo = QOpenGLBuffer(QOpenGLBuffer.Type.VertexBuffer)
        self.vbo.create()
        self.vbo.bind()

        theBytes = struct.pack("%sf" % len(vertices), *vertices)
        self.vbo.allocate(theBytes, len(theBytes))

        gl.glEnableVertexAttribArray(0)
        gl.glEnableVertexAttribArray(1)
        gl.glVertexAttribPointer(0, 4, gl.GL_FLOAT, False, 6 * 4, 0)
        gl.glVertexAttribPointer(1, 2, gl.GL_FLOAT, False, 6 * 4, 4 * 4)

        self.texture = self.ddsFile.asQOpenGLTexture(gl, QOpenGLContext.currentContext())

    def resizeGL(self, w, h):
        aspectRatioTex = self.texture.width() / self.texture.height() if self.texture else 1.0
        aspectRatioWidget = w / h
        ratioRatio = aspectRatioTex / aspectRatioWidget

        self.program.bind()
        self.program.setUniformValue("aspectRatioRatio", ratioRatio)
        self.program.release()

    def paintGL(self):
        # nothing to draw when initializeGL() could not set the texture up
        if self.program is None or self.transparecyProgram is None:
            return
        gl = QOpenGLVersionFunctionsFactory.get(glVersionProfile)

        vaoBinder = QOpenGLVertexArrayObject.Binder(self.vao)

        # Draw checkerboard so transparency is obvious
        self.transparecyProgram.bind()

        backgroundColour = self.ddsOptions.getBackgroundColour()
        if backgroundColour and backgroundColour.isValid():
            self.transparecyProgram.setUniformValue("backgroundColour", backgroundColour)
        # MO2 Revamped: the checkerboard in the theme's tones, not white on a dark theme
        light, dark = (self.checkerColours() if self.checkerColours
                       else (QColor("#FFFFFF"), QColor("#D6D6D6")))
        self.transparecyProgram.setUniformValue("checkerLight", light)
        self.transparecyProgram.setUniformValue("checkerDark", dark)

        gl.glDrawArrays(gl.GL_TRIANGLES, 0, 6)

        self.transparecyProgram.release()

        self.program.bind()

        if self.texture:
            self.texture.bind()

        gl.glEnable(gl.GL_BLEND)
        gl.glBlendFunc(gl.GL_SRC_ALPHA, gl.GL_ONE_MINUS_SRC_ALPHA)

        self.program.setUniformValue("channelMatrix", self.ddsOptions.getChannelMatrix())
        self.program.setUniformValue("channelOffset", self.ddsOptions.getChannelOffset())

        gl.glDrawArrays(gl.GL_TRIANGLES, 0, 6)

        if self.texture:
            self.texture.release()
        self.program.release()

    def cleanup(self):
        if not self.clean:
            self.makeCurrent()

            self.program = None
            self.transparecyProgram = None
            if self.texture:
                self.texture.destroy()
            self.texture = None
            self.vbo.destroy()
            self.vbo = None
            self.vao.destroy()
            self.vao = None

            self.doneCurrent()
            self.clean = True

    def tr(self, text):
        return tr(text)
