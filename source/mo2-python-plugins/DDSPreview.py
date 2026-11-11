# MO2 Revamped: the DDS preview. The renderer (the shaders and DDSWidget, which
# uploads the texture to the graphics card) is Mod Organizer 2's DDS Preview by
# AnyOldName3 (GNU GPL version 3); the window around it is rewritten for
# Revamped. It keeps the plugin name and its settings.

from __future__ import annotations

import enum
import struct
import sys

from PyQt6.QtCore import QCoreApplication, QRectF, QSize, Qt, qDebug
from PyQt6.QtGui import (QColor, QIcon, QMatrix4x4, QOpenGLContext, QPainter, QPainterPath, QPixmap,
                         QSurfaceFormat, QVector4D)
from PyQt6.QtOpenGL import (QOpenGLBuffer, QOpenGLDebugLogger, QOpenGLShader, QOpenGLShaderProgram,
                            QOpenGLVersionFunctionsFactory, QOpenGLVersionProfile, QOpenGLVertexArrayObject)
from PyQt6.QtOpenGLWidgets import QOpenGLWidget
from PyQt6.QtWidgets import (QApplication, QButtonGroup, QColorDialog, QHBoxLayout, QLabel, QToolButton,
                             QVBoxLayout, QWidget)

from DDS import DDSDefinitions
from DDS.DDSFile import DDSFile

if "mobase" not in sys.modules:
    import mobase

# MO2 installs translations/revamped_<language>.qm; the source text is English
_TR_CONTEXT = "RevampedDDSPreview"


def tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


def checker_colours() -> tuple[QColor, QColor]:
    """The two tones of the checkerboard behind transparent pixels."""
    application = QApplication.instance()
    dark_theme = application is not None and application.palette().window().color().lightness() < 128
    return (QColor("#404040"), QColor("#303030")) if dark_theme else (QColor("#FFFFFF"), QColor("#D6D6D6"))


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


class DDSOptions:
    def __init__(self, colour: QColor = QColor(0, 0, 0, 0), channelMatrix: QMatrix4x4 = QMatrix4x4(),
                 channelOffset: QVector4D = QVector4D()):
        # QMatrix4x4 with no arguments is the identity matrix, so no channel transformations
        # declare member variables with None values
        self.backgroundColour = None
        self.channelMatrix = None
        self.channelOffset = None
        # initialize member variables with error checks
        self.setBackgroundColour(colour)
        self.setChannelMatrix(channelMatrix)
        self.setChannelOffset(channelOffset)

    def setBackgroundColour(self, colour: QColor):
        if isinstance(colour, QColor) and colour.isValid():
            self.backgroundColour = colour
        else:
            raise TypeError(str(colour) + " is not a valid QColor object.")

    def getBackgroundColour(self) -> QColor:
        return self.backgroundColour

    def getChannelMatrix(self) -> QMatrix4x4:
        return self.channelMatrix

    def setChannelMatrix(self, matrix):
        self.channelMatrix = QMatrix4x4(matrix)

    def getChannelOffset(self) -> QVector4D:
        return self.channelOffset

    def setChannelOffset(self, vector):
        self.channelOffset = QVector4D(vector)


glVersionProfile = QOpenGLVersionProfile()
glVersionProfile.setVersion(2, 1)


class DDSWidget(QOpenGLWidget):
    def __init__(self, ddsFile, ddsOptions=DDSOptions(), debugContext=False, parent=None, f=Qt.WindowType(0)):
        super(DDSWidget, self).__init__(parent, f)

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
        light, dark = checker_colours()
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


class ColourChannels(enum.Enum):
    """The channels shown; the names are what the "channels" setting saves."""
    RGBA = "RGBA"
    RGB = "RGB"
    R = "R"
    G = "G"
    B = "B"
    A = "A"


def channel_tooltip(channels: ColourChannels) -> str:
    return {
        ColourChannels.RGBA: tr("Color and transparency"),
        ColourChannels.RGB: tr("Color only, without transparency"),
        ColourChannels.R: tr("Red channel, in grayscale"),
        ColourChannels.G: tr("Green channel, in grayscale"),
        ColourChannels.B: tr("Blue channel, in grayscale"),
        ColourChannels.A: tr("Transparency (alpha channel), in grayscale"),
    }[channels]


def apply_channels(options: DDSOptions, channels: ColourChannels) -> None:
    """What the shader multiplies the texture by to show these channels."""
    matrix = QMatrix4x4()
    offset = QVector4D()
    if channels == ColourChannels.RGB:
        # color with the alpha forced to 1
        matrix[3, 3] = 0
        offset.setW(1.0)
    elif channels != ColourChannels.RGBA:
        # one channel copied to red, green and blue, alpha forced to 1
        index = {ColourChannels.R: 0, ColourChannels.G: 1, ColourChannels.B: 2, ColourChannels.A: 3}[channels]
        row = [0.0, 0.0, 0.0, 0.0]
        row[index] = 1.0
        matrix = QMatrix4x4(*(row * 3 + [0.0, 0.0, 0.0, 0.0]))
        offset = QVector4D(0, 0, 0, 1)
    options.setChannelMatrix(matrix)
    options.setChannelOffset(offset)


# the backgrounds offered at once; an alpha of 0 shows the checkerboard
BACKGROUNDS = [
    ("checker", QColor(0, 0, 0, 0)),
    ("black", QColor(0, 0, 0, 255)),
    ("white", QColor(255, 255, 255, 255)),
]


def background_name(key: str) -> str:
    return {"checker": tr("Checkerboard (shows transparency)"), "black": tr("Black"),
            "white": tr("White")}[key]


def _swatch_icon(colour: QColor, checker: bool) -> QIcon:
    pixmap = QPixmap(32, 32)
    pixmap.setDevicePixelRatio(2.0)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setPen(QColor(128, 128, 128, 160))
    if checker:
        light, dark = checker_colours()
        painter.save()
        path = QPainterPath()
        path.addRoundedRect(QRectF(1, 1, 14, 14), 3, 3)
        painter.setClipPath(path)
        painter.fillRect(QRectF(1, 1, 14, 14), light)
        for x in range(2):
            for y in range(2):
                if (x + y) % 2:
                    painter.fillRect(QRectF(1 + x * 7, 1 + y * 7, 7, 7), dark)
        painter.restore()
        painter.setBrush(Qt.BrushStyle.NoBrush)
    else:
        painter.setBrush(colour)
    painter.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3)
    painter.end()
    return QIcon(pixmap)


def _segment(button: QToolButton) -> QToolButton:
    """A toggle button the themes draw as a segment: the checked one has the
    accent outline."""
    button.setCheckable(True)
    button.setAutoRaise(True)
    button.setProperty("fluentSegment", True)
    return button


class DDSPreviewWidget(QWidget):
    """The texture, a line about the file, and under it the channels shown
    and the background behind transparent pixels."""

    def __init__(self, plugin: "DDSPreview", dds_file: DDSFile, log_gl: bool):
        super().__init__()
        self._plugin = plugin
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        description = describe(dds_file)
        self._view = DDSWidget(dds_file, plugin.options, log_gl)
        self._view.setAccessibleName(tr("Texture"))
        self._view.setAccessibleDescription(description)
        self._view.setMinimumSize(160, 160)
        layout.addWidget(self._view, 1)

        info = QLabel(description, self)
        info.setObjectName("fluentSettingDescription")
        info.setWordWrap(True)
        info.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse
                                     | Qt.TextInteractionFlag.TextSelectableByKeyboard)
        info.setToolTip(dds_file.getDescription())
        layout.addWidget(info)

        bar = QHBoxLayout()
        bar.setSpacing(2)
        # the channels: one of six, like Windows 11's segmented control
        self._channels = QButtonGroup(self)
        self._channels.setExclusive(True)
        for channels in ColourChannels:
            button = _segment(QToolButton(self))
            button.setText(channels.value)
            button.setToolTip(channel_tooltip(channels))
            button.setAccessibleName(channel_tooltip(channels))
            button.setProperty("channels", channels.name)
            self._channels.addButton(button)
            bar.addWidget(button)
        self._channels.buttonClicked.connect(
            lambda button: self._set_channels(ColourChannels[button.property("channels")]))
        bar.addStretch(1)

        # the background: three at once, any other from the color picker
        self._backgrounds = QButtonGroup(self)
        self._backgrounds.setExclusive(True)
        for key, colour in BACKGROUNDS:
            button = _segment(QToolButton(self))
            button.setIcon(_swatch_icon(colour, key == "checker"))
            button.setToolTip(background_name(key))
            button.setAccessibleName(background_name(key))
            button.setProperty("colour", colour)
            self._backgrounds.addButton(button)
            bar.addWidget(button)
        self._custom = _segment(QToolButton(self))
        self._custom.setToolTip(tr("Custom background color…"))
        self._custom.setAccessibleName(tr("Custom background color…"))
        self._backgrounds.addButton(self._custom)
        bar.addWidget(self._custom)
        self._backgrounds.buttonClicked.connect(self._background_clicked)
        layout.addLayout(bar)
        self._sync()

    def showEvent(self, event) -> None:
        # the choices are shared: another preview may have changed them
        self._sync()
        super().showEvent(event)

    def _sync(self) -> None:
        for button in self._channels.buttons():
            button.setChecked(button.property("channels") == self._plugin.channels.name)
        current = self._plugin.options.getBackgroundColour()
        preset = next((b for b in self._backgrounds.buttons()
                       if b is not self._custom and b.property("colour") == current), None)
        (preset or self._custom).setChecked(True)
        self._custom.setIcon(_swatch_icon(current if preset is None else QColor(128, 128, 128), False))

    def _set_channels(self, channels: ColourChannels) -> None:
        self._plugin.set_channels(channels)
        self._view.update()

    def _background_clicked(self, button: QToolButton) -> None:
        if button is self._custom:
            colour = QColorDialog.getColor(self._plugin.options.getBackgroundColour(), self,
                                           tr("Background color"), QColorDialog.ColorDialogOption.ShowAlphaChannel)
            if not colour.isValid():
                self._sync()
                return
        else:
            colour = button.property("colour")
        self._plugin.set_background(colour)
        self._sync()
        self._view.update()


def describe(dds_file: DDSFile) -> str:
    """One line about the texture: its format, size, kind and mip levels."""
    header = dds_file.header
    format_name = ""
    if dds_file.dxt10Header is not None:
        format_name = dds_file.dxt10Header.dxgiFormat.name.replace("DXGI_FORMAT_", "")
    elif header.ddspf.dwFlags & DDSDefinitions.DDS_PIXELFORMAT.Flags.DDPF_FOURCC:
        dxgi = DDSDefinitions.fourCCToDXGI(header.ddspf.dwFourCC)
        format_name = dxgi.name.replace("DXGI_FORMAT_", "") if dxgi is not None \
            else bytes(header.ddspf.dwFourCC).decode("ascii", "replace")
    else:
        format_name = tr("Uncompressed")
    kind = tr("Cubemap") if dds_file.isCubemap else tr("2D texture")
    levels = max(1, dds_file.mipLevels())
    mipmaps = tr("no mipmaps") if levels == 1 else tr("{count} mip levels").format(count=levels)
    return f"{format_name} · {header.dwWidth} × {header.dwHeight} · {kind} · {mipmaps}"


class CantShow(Exception):
    """A file the preview can't show, with a message for the user."""


# the most a texture can be: 16384 pixels a side, so 15 mip levels
_MAX_SIDE = 16384
_MAX_LEVELS = 15


def check_header(data: bytes) -> None:
    """Refuses what isn't a DDS file, or claims sizes no texture has, before
    the reader loops over them."""
    if len(data) < 128 or data[:4] != DDSDefinitions.DDS_MAGIC_NUMBER:
        raise CantShow(tr("The file is not a DDS texture, or it is cut short ({size} bytes).")
                       .format(size=len(data)))
    flags, height, width, _pitch, _depth, levels = struct.unpack_from("<6I", data, 8)
    if not flags & 0x20000:  # DDSD_MIPMAPCOUNT: without it the count means nothing
        levels = 1
    if not (0 < width <= _MAX_SIDE and 0 < height <= _MAX_SIDE) or levels > _MAX_LEVELS:
        raise CantShow(tr("The file's header gives a size no texture has ({width} × {height}, "
                          "{levels} mip levels).").format(width=width, height=height, levels=levels))


def check_data(dds_file: DDSFile) -> None:
    """Refuses a texture whose pixel data is shorter than its header says:
    the graphics card would read past its end, which can take MO2 down."""
    if dds_file.glFormat is None:
        raise CantShow(tr("The preview can't draw this texture's format."))
    header = dds_file.header
    flags = DDSDefinitions.DDS_PIXELFORMAT.Flags
    uncompressed = header.ddspf.dwFlags & (flags.DDPF_ALPHA | flags.DDPF_RGB | flags.DDPF_YUV | flags.DDPF_LUMINANCE)
    if dds_file.dxt10Header is not None:
        dxgi = dds_file.dxt10Header.dxgiFormat
    elif header.ddspf.dwFlags & flags.DDPF_FOURCC:
        dxgi = DDSDefinitions.fourCCToDXGI(header.ddspf.dwFourCC)
    else:
        dxgi = None
    levels = max(1, dds_file.mipLevels())
    expected = []
    width, height = header.dwWidth, header.dwHeight
    for _level in range(levels):
        if uncompressed:
            expected.append(width * height * ((header.ddspf.dwRGBBitCount + 7) // 8))
        elif dxgi is not None:
            expected.append(DDSDefinitions.sizeFromFormat(dxgi, width, height))
        width, height = max(width // 2, 1), max(height // 2, 1)
    faces = len(dds_file.data) // levels if dds_file.data else 0
    if not dds_file.data or faces < 1 or any(
            len(chunk) < expected[index % levels] for index, chunk in enumerate(dds_file.data)):
        raise CantShow(tr("The texture is cut short: the file has less data than its header says."))


def failure_widget(message: str) -> QWidget:
    """What the preview shows for a file it can't show."""
    widget = QWidget()
    layout = QVBoxLayout(widget)
    layout.addStretch(1)
    title = QLabel(tr("This DDS file can't be shown"), widget)
    title.setObjectName("fluentCardTitle")
    title.setAlignment(Qt.AlignmentFlag.AlignCenter)
    text = QLabel(message, widget)
    text.setObjectName("fluentSettingDescription")
    text.setAlignment(Qt.AlignmentFlag.AlignCenter)
    text.setWordWrap(True)
    text.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    layout.addWidget(title)
    layout.addWidget(text)
    layout.addStretch(1)
    return widget


class DDSPreview(mobase.IPluginPreview):

    def __init__(self):
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self.options = DDSOptions()
        self.channels = ColourChannels.RGBA

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        try:
            colour = QColor(*(int(self._setting(f"background {c}")) for c in "rgba"))
        except (TypeError, ValueError):
            colour = QColor(0, 0, 0, 0)
        self.options = DDSOptions(colour if colour.isValid() else QColor(0, 0, 0, 0))
        # MO2 2.5.2 saved the enum's names too (RGBA, RGB, A, R, G, B)
        try:
            self.channels = ColourChannels[str(self._setting("channels"))]
        except KeyError:
            self.channels = ColourChannels.RGBA
        apply_channels(self.options, self.channels)
        return True

    def _setting(self, name: str):
        return self._organizer.pluginSetting(self.name(), name)

    def _set_setting(self, name: str, value) -> None:
        self._organizer.setPluginSetting(self.name(), name, value)

    def set_channels(self, channels: ColourChannels) -> None:
        self.channels = channels
        apply_channels(self.options, channels)
        self._set_setting("channels", channels.name)

    def set_background(self, colour: QColor) -> None:
        self.options.setBackgroundColour(colour)
        for component, value in zip("rgba", (colour.red(), colour.green(), colour.blue(), colour.alpha())):
            self._set_setting(f"background {component}", value)

    def name(self) -> str:
        return "DDS Preview Plugin"

    def localizedName(self) -> str:
        return tr("DDS preview")

    def author(self) -> str:
        return "AnyOldName3, MO2 Revamped"

    def description(self) -> str:
        return tr("Shows DDS textures as the graphics card draws them in the game.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(2, 0, 0, mobase.ReleaseType.FINAL)

    def settings(self) -> list:
        return [
            mobase.PluginSetting("log gl errors", tr("Write OpenGL's errors and debug messages to MO2's log. "
                                                     "It can slow the preview down."), False),
            mobase.PluginSetting("background r", tr("Red component of the background color (0–255)."), 0),
            mobase.PluginSetting("background g", tr("Green component of the background color (0–255)."), 0),
            mobase.PluginSetting("background b", tr("Blue component of the background color (0–255)."), 0),
            mobase.PluginSetting("background a", tr("Opacity of the background color (0–255); 0 shows only the "
                                                    "checkerboard."), 0),
            mobase.PluginSetting("channels", tr("The channels shown: RGBA, RGB, R, G, B or A."), ColourChannels.RGBA.name),
        ]

    def supportedExtensions(self) -> set[str]:
        return {"dds"}

    def supportsArchives(self) -> bool:
        return True

    def genFilePreview(self, fileName: str, maxSize: QSize) -> QWidget:
        return self._guarded(lambda: DDSFile.fromFile(fileName))

    def genDataPreview(self, fileData: bytes, fileName: str, maxSize: QSize) -> QWidget:
        return self._guarded(lambda: DDSFile(fileData, fileName))

    def _guarded(self, make) -> QWidget:
        # a damaged file must not take MO2 down
        try:
            return self._preview(make())
        except CantShow as problem:
            return failure_widget(str(problem))
        except Exception as error:
            qDebug(f"DDS preview: {type(error).__name__}: {error}")
            return failure_widget(tr("It may be damaged, or in a format the preview doesn't read."))

    def _preview(self, dds_file: DDSFile) -> QWidget:
        check_header(dds_file.fileData or b"")
        dds_file.load()
        check_data(dds_file)
        return DDSPreviewWidget(self, dds_file, bool(self._setting("log gl errors")))


def createPlugin() -> mobase.IPlugin:
    return DDSPreview()
