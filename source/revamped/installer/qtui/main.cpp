#include <QAbstractButton>
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QVector>
#include <QWidget>
#include <QtMath>
#include <utility>

namespace {

struct ModeInfo {
    int id;
    QString title;
    QString description;
    QString glyph;
};

struct Theme {
    bool dark = true;
    QColor page, surface, input, border, text, muted, heading;
    QColor accent, accentHover, selected, hover, danger, dangerHover, success, tile, tileInk;
};

Theme makeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.page = QColor(30, 35, 40); t.surface = QColor(37, 43, 49); t.input = QColor(29, 34, 39);
        t.border = QColor(62, 73, 82); t.text = QColor(211, 221, 228); t.muted = QColor(150, 167, 179);
        t.heading = QColor(228, 238, 245); t.accent = QColor(38, 128, 178); t.accentHover = QColor(52, 144, 195);
        t.selected = QColor(42, 62, 76); t.hover = QColor(44, 52, 59); t.danger = QColor(196, 84, 78);
        t.dangerHover = QColor(214, 99, 92); t.success = QColor(110, 196, 146);
        t.tile = QColor(47, 58, 67); t.tileInk = QColor(132, 196, 230);
    } else {
        t.page = QColor(243, 246, 249); t.surface = Qt::white; t.input = QColor(249, 251, 253);
        t.border = QColor(208, 219, 228); t.text = QColor(41, 56, 71); t.muted = QColor(88, 110, 128);
        t.heading = QColor(30, 66, 94); t.accent = QColor(30, 122, 176); t.accentHover = QColor(24, 108, 158);
        t.selected = QColor(223, 237, 248); t.hover = QColor(238, 244, 249); t.danger = QColor(184, 64, 58);
        t.dangerHover = QColor(162, 52, 47); t.success = QColor(38, 128, 78);
        t.tile = QColor(226, 238, 247); t.tileInk = QColor(30, 110, 160);
    }
    return t;
}

QColor mix(const QColor &from, const QColor &to, qreal amount) {
    const qreal t = qBound<qreal>(0.0, amount, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QColor transparent(const QColor &color) {
    QColor result(color);
    result.setAlpha(0);
    return result;
}

// Arrow head at `tip`, pointing along `direction`.
void addArrowHead(QPainterPath &path, const QPointF &tip, QPointF direction, qreal size) {
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length <= 0.0) return;
    direction /= length;
    const QPointF normal(-direction.y(), direction.x());
    path.moveTo(tip - direction * size + normal * size * 0.8);
    path.lineTo(tip);
    path.lineTo(tip - direction * size - normal * size * 0.8);
}

QPointF onCircle(const QPointF &center, qreal radius, qreal degrees) {
    const qreal angle = qDegreesToRadians(degrees);
    return {center.x() + radius * std::cos(angle), center.y() - radius * std::sin(angle)};
}

// Direction of travel for an arc that grows counter-clockwise (Qt angles).
QPointF ccwTangent(qreal degrees) {
    const qreal angle = qDegreesToRadians(degrees);
    return {-std::sin(angle), -std::cos(angle)};
}

// Line icons share one 24-unit grid, a 1.9 unit stroke and rounded joins so
// they read as a single family next to the layered brand mark.
void drawGlyph(QPainter &p, const QString &kind, const QRectF &box, const QColor &ink, const QColor &fill) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.topLeft());
    p.scale(box.width() / 24.0, box.height() / 24.0);
    QPen pen(ink, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    if (kind == QStringLiteral("install")) {
        path.moveTo(12, 3.5); path.lineTo(12, 14);
        addArrowHead(path, QPointF(12, 14.2), QPointF(0, 1), 4.2);
        path.moveTo(4, 13.5); path.lineTo(4, 18);
        path.quadTo(4, 20.5, 6.5, 20.5); path.lineTo(17.5, 20.5);
        path.quadTo(20, 20.5, 20, 18); path.lineTo(20, 13.5);
        p.drawPath(path);
    } else if (kind == QStringLiteral("update")) {
        const QPointF c(12, 12);
        const QRectF circle(c.x() - 7.8, c.y() - 7.8, 15.6, 15.6);
        path.arcMoveTo(circle, 25); path.arcTo(circle, 25, 135);
        addArrowHead(path, onCircle(c, 7.8, 160), ccwTangent(160), 3.4);
        path.arcMoveTo(circle, 205); path.arcTo(circle, 205, 135);
        addArrowHead(path, onCircle(c, 7.8, 340), ccwTangent(340), 3.4);
        p.drawPath(path);
    } else if (kind == QStringLiteral("copy")) {
        p.drawRoundedRect(QRectF(9, 3.5, 11, 13.5), 2.2, 2.2);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(4, 7.5, 11, 13.5), 2.2, 2.2);
        p.setBrush(Qt::NoBrush);
        path.moveTo(7, 12.5); path.lineTo(12, 12.5);
        path.moveTo(7, 16.5); path.lineTo(10.5, 16.5);
        p.drawPath(path);
    } else if (kind == QStringLiteral("restore")) {
        const QPointF c(12, 12.5);
        const QRectF circle(c.x() - 8, c.y() - 8, 16, 16);
        path.arcMoveTo(circle, 165); path.arcTo(circle, 165, -300);
        addArrowHead(path, onCircle(c, 8, 165), ccwTangent(165), 3.4);
        path.moveTo(12, 8.5); path.lineTo(12, 12.8); path.lineTo(15, 14.6);
        p.drawPath(path);
    } else if (kind == QStringLiteral("remove")) {
        path.moveTo(4.5, 6.5); path.lineTo(19.5, 6.5);
        path.moveTo(9.5, 6.5); path.lineTo(9.5, 5);
        path.quadTo(9.5, 3.5, 11, 3.5); path.lineTo(13, 3.5);
        path.quadTo(14.5, 3.5, 14.5, 5); path.lineTo(14.5, 6.5);
        path.moveTo(6.5, 6.5); path.lineTo(7.4, 18.6);
        path.quadTo(7.6, 20.5, 9.5, 20.5); path.lineTo(14.5, 20.5);
        path.quadTo(16.4, 20.5, 16.6, 18.6); path.lineTo(17.5, 6.5);
        path.moveTo(10.2, 10.5); path.lineTo(10.2, 16.5);
        path.moveTo(13.8, 10.5); path.lineTo(13.8, 16.5);
        p.drawPath(path);
    } else if (kind == QStringLiteral("globe")) {
        p.drawEllipse(QRectF(3.5, 3.5, 17, 17));
        p.drawEllipse(QRectF(8.4, 3.5, 7.2, 17));
        path.moveTo(4.4, 9.3); path.lineTo(19.6, 9.3);
        path.moveTo(4.4, 14.7); path.lineTo(19.6, 14.7);
        p.drawPath(path);
    } else if (kind == QStringLiteral("folder")) {
        path.moveTo(3.5, 17.5); path.lineTo(3.5, 7);
        path.quadTo(3.5, 5.5, 5, 5.5); path.lineTo(9.3, 5.5);
        path.lineTo(11.3, 7.5); path.lineTo(19, 7.5);
        path.quadTo(20.5, 7.5, 20.5, 9); path.lineTo(20.5, 17.5);
        path.quadTo(20.5, 19, 19, 19); path.lineTo(5, 19);
        path.quadTo(3.5, 19, 3.5, 17.5);
        path.moveTo(3.5, 10.5); path.lineTo(20.5, 10.5);
        p.drawPath(path);
    }
    p.restore();
}

QIcon glyphIcon(const QString &kind, const QColor &ink, int size = 18) {
    QIcon icon;
    for (const qreal ratio : {1.0, 1.25, 1.5, 2.0}) {
        QPixmap pixmap(qRound(size * ratio), qRound(size * ratio));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        drawGlyph(painter, kind, QRectF(0, 0, size, size), ink, Qt::transparent);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString languageName(const QString &code) {
    if (code == QStringLiteral("en")) return QStringLiteral("English");
    if (code == QStringLiteral("fr")) return QStringLiteral("Français");
    if (code == QStringLiteral("de")) return QStringLiteral("Deutsch");
    if (code == QStringLiteral("it")) return QStringLiteral("Italiano");
    if (code == QStringLiteral("pt-BR")) return QStringLiteral("Português (Brasil)");
    return QStringLiteral("Español");
}

void fadeIn(QWidget *widget, int duration = 190) {
    auto *effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(0.0);
    widget->setGraphicsEffect(effect);
    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(duration);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    // Drop the effect afterwards so text renders with full ClearType quality.
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [widget] { widget->setGraphicsEffect(nullptr); });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

class BrandMark final : public QWidget {
public:
    explicit BrandMark(QWidget *parent = nullptr) : QWidget(parent), image_(QStringLiteral(":/brand/icon.png")) { setFixedSize(46, 46); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!image_.isNull()) {
            painter.drawPixmap(rect(), image_);
            return;
        }
        const QPointF c(width() / 2.0, height() / 2.0);
        const QColor colors[] = {QColor(150, 166, 184), QColor(24, 64, 128), QColor(30, 190, 240)};
        for (int i = 0; i < 3; ++i) {
            const qreal y = 9.0 - i * 8.0;
            QPolygonF layer{c + QPointF(0, y + 7), c + QPointF(18, y), c + QPointF(0, y - 7), c + QPointF(-18, y)};
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors[i]);
            painter.drawPolygon(layer);
        }
    }
private:
    QPixmap image_;
};

class LanguageButton final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *event) override {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor ink = palette().color(QPalette::ButtonText);
        if (!isEnabled()) ink.setAlpha(115);
        const qreal x = width() - 18.0;
        const qreal y = height() / 2.0;
        QPainterPath chevron;
        chevron.moveTo(x - 3.5, y - 1.8);
        chevron.lineTo(x, y + 1.8);
        chevron.lineTo(x + 3.5, y - 1.8);
        painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(chevron);
    }
};

class GlyphTile final : public QWidget {
public:
    GlyphTile(QString glyph, int size, const Theme &theme, QWidget *parent = nullptr)
        : QWidget(parent), glyph_(std::move(glyph)), theme_(theme) {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setGlyph(const QString &glyph) { glyph_ = glyph; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient gradient(r.topLeft(), r.bottomRight());
        gradient.setColorAt(0.0, theme_.accentHover);
        gradient.setColorAt(1.0, theme_.accent.darker(118));
        p.setPen(Qt::NoPen);
        p.setBrush(gradient);
        p.drawRoundedRect(r, r.width() * 0.28, r.height() * 0.28);
        const qreal inset = r.width() * 0.24;
        drawGlyph(p, glyph_, r.adjusted(inset, inset, -inset, -inset), Qt::white, theme_.accent);
    }
private:
    QString glyph_;
    Theme theme_;
};

// Sidebar task with eased hover and selection transitions; stylesheets cannot
// animate, so the card paints itself.
class TaskCard final : public QAbstractButton {
public:
    TaskCard(int mode, QString glyph, const Theme &theme, QWidget *parent = nullptr)
        : QAbstractButton(parent), mode_(mode), glyph_(std::move(glyph)), theme_(theme) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setMinimumHeight(58);
        hoverAnimation_ = makeAnimation(&hover_);
        checkAnimation_ = makeAnimation(&check_);
        connect(this, &QAbstractButton::toggled, this, [this](bool on) { animateTo(checkAnimation_, check_, on ? 1.0 : 0.0); });
    }
    int mode() const { return mode_; }
    // A second, muted line under the title; used to mark unavailable tasks.
    void setBadge(const QString &badge) { badge_ = badge; update(); }
    QSize sizeHint() const override { return {236, 58}; }
protected:
    void enterEvent(QEnterEvent *event) override { animateTo(hoverAnimation_, hover_, 1.0); QAbstractButton::enterEvent(event); }
    void leaveEvent(QEvent *event) override { animateTo(hoverAnimation_, hover_, 0.0); QAbstractButton::leaveEvent(event); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal hover = isEnabled() ? hover_ : 0.0;
        QColor background = mix(transparent(theme_.hover), theme_.hover, hover);
        background = mix(background, theme_.selected, check_);
        QColor outline = mix(transparent(theme_.border), theme_.border, hover * (1.0 - check_));
        QColor activeOutline(theme_.accent);
        activeOutline.setAlphaF(0.55);
        outline = mix(outline, activeOutline, check_);
        p.setPen(outline.alpha() > 0 ? QPen(outline, 1.0) : Qt::NoPen);
        p.setBrush(background);
        p.drawRoundedRect(r, 9, 9);

        if (check_ > 0.01) {
            const qreal barHeight = (r.height() - 26.0) * check_;
            p.setPen(Qt::NoPen);
            p.setBrush(theme_.accent);
            p.drawRoundedRect(QRectF(r.left() + 3.5, r.center().y() - barHeight / 2.0, 3.0, barHeight), 1.5, 1.5);
        }

        const QRectF tile(r.left() + 14.0, r.center().y() - 18.0, 36.0, 36.0);
        const QColor tileColor = mix(theme_.tile, theme_.accent, check_);
        const QColor ink = mix(theme_.tileInk, Qt::white, check_);
        p.setPen(Qt::NoPen);
        p.setBrush(tileColor);
        p.drawEllipse(tile);
        drawGlyph(p, glyph_, tile.adjusted(8.5, 8.5, -8.5, -8.5), ink, tileColor);

        QFont titleFont = font();
        titleFont.setWeight(QFont::DemiBold);
        p.setFont(titleFont);
        QColor textColor = mix(theme_.text, theme_.heading, qMax(hover, check_));
        if (!isEnabled()) textColor = theme_.muted;
        p.setPen(textColor);
        const QRectF textRect(tile.right() + 12.0, r.top() + 4.0, r.right() - tile.right() - 22.0, r.height() - 8.0);
        if (badge_.isEmpty()) {
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text());
        } else {
            const qreal half = textRect.height() / 2.0;
            p.drawText(QRectF(textRect.left(), textRect.top(), textRect.width(), half + 1.0),
                       Qt::AlignBottom | Qt::AlignLeft, p.fontMetrics().elidedText(text(), Qt::ElideRight, qRound(textRect.width())));
            QFont badgeFont = font();
            badgeFont.setPointSizeF(badgeFont.pointSizeF() - 1.0);
            p.setFont(badgeFont);
            p.setPen(theme_.muted);
            p.drawText(QRectF(textRect.left(), textRect.top() + half + 2.0, textRect.width(), half - 2.0),
                       Qt::AlignTop | Qt::AlignLeft, p.fontMetrics().elidedText(badge_, Qt::ElideRight, qRound(textRect.width())));
        }

        if (hasFocus()) {
            p.setPen(QPen(theme_.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
        }
    }
private:
    QVariantAnimation *makeAnimation(qreal *target) {
        auto *animation = new QVariantAnimation(this);
        animation->setDuration(170);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, [this, target](const QVariant &value) { *target = value.toReal(); update(); });
        return animation;
    }
    static void animateTo(QVariantAnimation *animation, qreal current, qreal target) {
        animation->stop();
        animation->setStartValue(current);
        animation->setEndValue(target);
        animation->start();
    }
    int mode_;
    QString glyph_;
    QString badge_;
    Theme theme_;
    qreal hover_ = 0.0;
    qreal check_ = 0.0;
    QVariantAnimation *hoverAnimation_ = nullptr;
    QVariantAnimation *checkAnimation_ = nullptr;
};

class SetupCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        if (!isChecked()) return;
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRectF indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor ink(Qt::white);
        if (!isEnabled()) ink.setAlpha(140);
        painter.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath check;
        check.moveTo(indicator.left() + indicator.width() * 0.24, indicator.top() + indicator.height() * 0.52);
        check.lineTo(indicator.left() + indicator.width() * 0.43, indicator.top() + indicator.height() * 0.70);
        check.lineTo(indicator.left() + indicator.width() * 0.77, indicator.top() + indicator.height() * 0.31);
        painter.drawPath(check);
    }
};

class SetupWindow final : public QMainWindow {
public:
    SetupWindow(QString host, QString language, QString fixedMode, QString fixedTarget, bool isolated, const QString &themeOverride)
        : host_(std::move(host)), language_(std::move(language)), fixedTarget_(std::move(fixedTarget)), isolated_(isolated) {
        if (fixedMode == QStringLiteral("uninstall")) fixedMode_ = 5;
        else if (fixedMode == QStringLiteral("restore")) fixedMode_ = 4;
        detectTheme(themeOverride);
        if (languageName(language_) == QStringLiteral("Español") && language_ != QStringLiteral("es"))
            language_ = QStringLiteral("es");
        loadTranslations(language_);
        setWindowTitle(tr(QStringLiteral("Instalador de MO2 Revamped")));
        // small enough for a 1366x768 screen at 150 %
        setMinimumSize(800, 560);
        resize(1080, 760);
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QSize available = screen->availableGeometry().size();
            resize(qMin(1080, available.width() - 40), qMin(760, available.height() - 40));
        }
        buildShell();

        if (fixedMode_ >= 0) {
            setMode(fixedMode_);
            if (sidebar_) sidebar_->hide();
        } else {
            setMode(1);
        }
    }

    ~SetupWindow() override {
        if (inspectProcess_) { inspectProcess_->disconnect(); inspectProcess_->kill(); inspectProcess_->waitForFinished(1000); }
        if (!inspectFolder_.isEmpty()) QDir(inspectFolder_).removeRecursively();
        if (!jobDirectory_.isEmpty()) QDir(jobDirectory_).removeRecursively();
    }

    void showLanguageMenuSnapshot(const QString &path) {
        if (!languageButton_ || !languageMenu_) return;
        languageMenu_->popup(languageButton_->mapToGlobal(QPoint(0, languageButton_->height() + 4)));
        QTimer::singleShot(400, languageMenu_, [this, path] {
            languageMenu_->grab().save(path);
            QApplication::quit();
        });
    }

    void showModeSnapshot(int mode, const QString &path) {
        setMode(mode);
        QTimer::singleShot(700, this, [this, path] { grab().save(path); QApplication::quit(); });
    }

private:
    QString tr(const QString &key) const { return translations_.value(key, key); }

    void detectTheme(const QString &themeOverride) {
        bool dark;
        if (themeOverride == QStringLiteral("dark")) dark = true;
        else if (themeOverride == QStringLiteral("light")) dark = false;
        else {
            QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), QSettings::NativeFormat);
            dark = settings.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
        }
        theme_ = makeTheme(dark);
    }

    QString tempFile(const QString &pattern) const {
        const QString path = QDir::temp().filePath(QStringLiteral("MO2 Revamped Setup/") +
                                                   pattern.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(QFileInfo(path).absolutePath());
        return path;
    }

    QJsonObject runHost(const QStringList &arguments, const QString &output, int timeout = 8000) const {
        QProcess process;
        process.start(host_, arguments);
        QJsonObject result;
        if (process.waitForStarted(3000) && process.waitForFinished(timeout)) {
            QFile file(output);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonParseError error;
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error == QJsonParseError::NoError && document.isObject()) result = document.object();
            }
        } else {
            process.kill();
            process.waitForFinished(1000);
        }
        QFile::remove(output);
        return result;
    }

    void loadTranslations(const QString &language) {
        const QString output = tempFile(QStringLiteral("localization/%1.json"));
        const QJsonObject loaded = runHost({QStringLiteral("--translations"), language, output}, output);
        if (loaded.isEmpty()) return;
        translations_.clear();
        for (auto it = loaded.constBegin(); it != loaded.constEnd(); ++it) translations_.insert(it.key(), it.value().toString());
    }

    QVector<ModeInfo> modes() const {
        if (isolated_) return {{1, QStringLiteral("Instalar Elden Ring y Nightreign aislado"), QStringLiteral("Crea una copia portable dedicada a Elden Ring y Nightreign."), QStringLiteral("install")},
                               {2, QStringLiteral("Actualizar instalación aislada"), QStringLiteral("Actualiza solo la copia aislada y conserva sus datos."), QStringLiteral("update")},
                               {4, QStringLiteral("Restaurar copia anterior"), QStringLiteral("Recupera la copia previa de una actualización."), QStringLiteral("restore")},
                               {5, QStringLiteral("Desinstalar instalación aislada"), QStringLiteral("Retira esta copia; puedes conservar o borrar sus datos."), QStringLiteral("remove")}};
        return {{1, QStringLiteral("Instalar Elden Ring y Nightreign aislado"), QStringLiteral("Crea una copia portable dedicada a Elden Ring y Nightreign."), QStringLiteral("install")},
                {0, QStringLiteral("Nueva instalación"), QStringLiteral("Instala MO2 Revamped. Después podrás crear tus instancias desde la aplicación."), QStringLiteral("install")},
                {2, QStringLiteral("Actualizar MO2 Revamped"), QStringLiteral("Actualiza una copia reconocida y crea una copia de seguridad."), QStringLiteral("update")},
                {3, QStringLiteral("Copiar datos"), QStringLiteral("Elige qué datos copiar; el origen permanece intacto."), QStringLiteral("copy")},
                {4, QStringLiteral("Restaurar copia anterior"), QStringLiteral("Recupera la copia previa de una actualización."), QStringLiteral("restore")},
                {5, QStringLiteral("Desinstalar MO2 Revamped"), QStringLiteral("Quita Revamped y conserva o elimina sus datos."), QStringLiteral("remove")}};
    }

    // The standard multi-instance setup (and "Copiar datos", which installs the
    // same Complete payload) stays unavailable until its support is extended;
    // the isolated Elden Ring edition is the only new installation for now.
    bool modeBlocked(int id) const { return !isolated_ && (id == 0 || id == 3); }

    void applyBlockedTiles() {
        for (TaskCard *tile : taskTiles_) {
            if (!modeBlocked(tile->mode())) continue;
            tile->setEnabled(false);
            tile->setBadge(tr(QStringLiteral("Próximamente")));
            tile->setToolTip(tr(QStringLiteral("La instalación estándar con varias instancias estará disponible más adelante. Por ahora, instala la versión aislada de Elden Ring y Nightreign.")));
        }
    }

    ModeInfo currentModeInfo() const {
        for (const ModeInfo &info : modes()) if (info.id == mode_) return info;
        // Fixed modes opened by the uninstaller are always available.
        if (mode_ == 4) return {4, QStringLiteral("Restaurar copia anterior"), QStringLiteral("Recupera la copia previa de una actualización."), QStringLiteral("restore")};
        return {5, QStringLiteral("Desinstalar MO2 Revamped"), QStringLiteral("Quita Revamped y conserva o elimina sus datos."), QStringLiteral("remove")};
    }

    void buildShell() {
        auto *central = new QWidget;
        central->setObjectName(QStringLiteral("central"));
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(20, 14, 20, 14);
        root->setSpacing(14);

        auto *header = new QFrame;
        header->setObjectName(QStringLiteral("header"));
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 14);
        headerLayout->setSpacing(13);
        headerLayout->addWidget(new BrandMark, 0, Qt::AlignVCenter);
        auto *brandText = new QVBoxLayout;
        brandText->setSpacing(1);
        auto *brandTitle = new QLabel(QStringLiteral("MO2 Revamped"));
        brandTitle->setObjectName(QStringLiteral("brandTitle"));
        brandSub_ = new QLabel(brandSubtitle());
        brandSub_->setObjectName(QStringLiteral("brandSub"));
        brandText->addStretch();
        brandText->addWidget(brandTitle);
        brandText->addWidget(brandSub_);
        brandText->addStretch();
        headerLayout->addLayout(brandText);
        headerLayout->addStretch();

        languageButton_ = new LanguageButton;
        languageButton_->setObjectName(QStringLiteral("languageBox"));
        languageButton_->setText(languageName(language_));
        languageButton_->setIcon(glyphIcon(QStringLiteral("globe"), theme_.tileInk, 20));
        languageButton_->setIconSize(QSize(20, 20));
        languageButton_->setFixedSize(206, 40);
        languageButton_->setCursor(Qt::PointingHandCursor);
        languageButton_->setAccessibleName(tr(QStringLiteral("Idioma")));
        languageMenu_ = new QMenu(languageButton_);
        languageMenu_->setObjectName(QStringLiteral("languageMenu"));
        // Rounded corners need a translucent, frameless popup.
        languageMenu_->setWindowFlags(languageMenu_->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        languageMenu_->setAttribute(Qt::WA_TranslucentBackground);
        languageMenu_->setMinimumWidth(206);
        connect(languageMenu_, &QMenu::aboutToShow, this, [this] {
            languageMenu_->setWindowOpacity(0.0);
            auto *animation = new QPropertyAnimation(languageMenu_, "windowOpacity", languageMenu_);
            animation->setDuration(140);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        });
        const QPair<QString, QString> languages[] = {
            {QStringLiteral("es"), QStringLiteral("Español")},
            {QStringLiteral("en"), QStringLiteral("English")},
            {QStringLiteral("fr"), QStringLiteral("Français")},
            {QStringLiteral("de"), QStringLiteral("Deutsch")},
            {QStringLiteral("it"), QStringLiteral("Italiano")},
            {QStringLiteral("pt-BR"), QStringLiteral("Português (Brasil)")}
        };
        for (const auto &language : languages) {
            QAction *action = languageMenu_->addAction(language.second);
            action->setData(language.first);
            action->setCheckable(true);
            action->setChecked(language.first == language_);
            connect(action, &QAction::triggered, this, [this, action] {
                const QString nextLanguage = action->data().toString();
                if (busy_ || nextLanguage == language_) {
                    action->setChecked(nextLanguage == language_);
                    return;
                }
                savePageState();
                language_ = nextLanguage;
                languageButton_->setText(languageName(language_));
                for (QAction *item : languageMenu_->actions())
                    item->setChecked(item->data().toString() == language_);
                QApplication::setOverrideCursor(Qt::WaitCursor);
                loadTranslations(language_);
                QApplication::restoreOverrideCursor();
                rebuildShellText();
                setMode(mode_, true);
            });
        }
        languageButton_->setMenu(languageMenu_);
        headerLayout->addWidget(languageButton_, 0, Qt::AlignVCenter);
        root->addWidget(header);

        auto *workspace = new QHBoxLayout;
        workspace->setSpacing(16);
        auto *sidebar = new QFrame;
        sidebar_ = sidebar;
        sidebar->setObjectName(QStringLiteral("sidebar"));
        sidebar->setFixedWidth(268);
        auto *sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(10, 14, 10, 12);
        sideLayout->setSpacing(4);
        sideTitle_ = new QLabel(tr(QStringLiteral("¿Qué quieres hacer?")));
        sideTitle_->setObjectName(QStringLiteral("sectionTitle"));
        sideTitle_->setContentsMargins(6, 0, 6, 0);
        sideHint_ = new QLabel(tr(QStringLiteral("Selecciona una tarea para ver sus opciones y la ubicación afectada.")));
        sideHint_->setObjectName(QStringLiteral("secondary"));
        sideHint_->setWordWrap(true);
        sideHint_->setContentsMargins(6, 0, 6, 0);
        sideLayout->addWidget(sideTitle_);
        sideLayout->addWidget(sideHint_);
        sideLayout->addSpacing(10);
        for (const ModeInfo &info : modes()) {
            auto *tile = new TaskCard(info.id, info.glyph, theme_);
            tile->setText(tr(info.title));
            tile->setAccessibleDescription(tr(info.description));
            sideLayout->addWidget(tile);
            taskTiles_.append(tile);
            connect(tile, &QAbstractButton::clicked, this, [this, tile, id = info.id] {
                if (mode_ == id) tile->setChecked(true);
                else setMode(id);
            });
        }
        applyBlockedTiles();
        sideLayout->addStretch();
        workspace->addWidget(sidebar);

        scroll_ = new QScrollArea;
        scroll_->setObjectName(QStringLiteral("pageScroll"));
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        workspace->addWidget(scroll_, 1);
        root->addLayout(workspace, 1);

        auto *footer = new QFrame;
        footer->setObjectName(QStringLiteral("footer"));
        auto *footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(0, 12, 0, 0);
        footerLayout->setSpacing(10);
        statusLabel_ = new QLabel(tr(QStringLiteral("Listo. Revisa la carpeta seleccionada antes de continuar.")));
        statusLabel_->setObjectName(QStringLiteral("statusLabel"));
        statusLabel_->setWordWrap(true);
        footerLayout->addWidget(statusLabel_, 1);
        footerLayout->addWidget(closeButton_ = new QPushButton(tr(QStringLiteral("Cerrar"))));
        footerLayout->addWidget(actionButton_ = new QPushButton(tr(QStringLiteral("Instalar"))));
        closeButton_->setObjectName(QStringLiteral("secondaryButton"));
        closeButton_->setMinimumWidth(112);
        actionButton_->setMinimumWidth(172);
        actionButton_->setObjectName(QStringLiteral("primaryButton"));
        actionButton_->setDefault(true);
        closeButton_->setCursor(Qt::PointingHandCursor);
        actionButton_->setCursor(Qt::PointingHandCursor);
        root->addWidget(footer);
        setCentralWidget(central);

        connect(closeButton_, &QPushButton::clicked, this, [this] { if (!busy_) close(); });
        connect(actionButton_, &QPushButton::clicked, this, [this] { beginOperation(); });
        // a main window has no default button: Enter starts the task (which
        // still asks to confirm) and Esc closes while nothing runs
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
            auto *enter = new QShortcut(QKeySequence(key), this);
            connect(enter, &QShortcut::activated, this, [this] {
                if (!busy_ && actionButton_ && actionButton_->isVisible() && actionButton_->isEnabled())
                    actionButton_->click();
            });
        }
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(escape, &QShortcut::activated, this, [this] { if (!busy_) close(); });
        applyStyle();
    }

    QString brandSubtitle() const {
        QString text = tr(QStringLiteral("Instalación y mantenimiento seguro"));
#ifdef MO2R_VERSION
        text += QStringLiteral(" · v") + QStringLiteral(MO2R_VERSION);
#endif
        return text;
    }

    void rebuildShellText() {
        setWindowTitle(tr(QStringLiteral("Instalador de MO2 Revamped")));
        if (brandSub_) brandSub_->setText(brandSubtitle());
        if (sideTitle_) sideTitle_->setText(tr(QStringLiteral("¿Qué quieres hacer?")));
        if (sideHint_) sideHint_->setText(tr(QStringLiteral("Selecciona una tarea para ver sus opciones y la ubicación afectada.")));
        if (closeButton_) closeButton_->setText(tr(QStringLiteral("Cerrar")));
        for (TaskCard *tile : taskTiles_) {
            for (const ModeInfo &info : modes()) if (info.id == tile->mode()) {
                tile->setText(tr(info.title));
                tile->setAccessibleDescription(tr(info.description));
            }
        }
        applyBlockedTiles();
    }

    QFrame *card(const QString &title, const QString &description = QString()) {
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("contentCard"));
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(18, 15, 18, 16);
        layout->setSpacing(10);
        auto *heading = new QLabel(title);
        heading->setObjectName(QStringLiteral("cardTitle"));
        layout->addWidget(heading);
        if (!description.isEmpty()) {
            auto *sub = new QLabel(description);
            sub->setObjectName(QStringLiteral("secondary"));
            sub->setWordWrap(true);
            layout->addWidget(sub);
        }
        return frame;
    }

    QFrame *heroCard(const ModeInfo &info, const QString &title) {
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("heroCard"));
        auto *layout = new QHBoxLayout(frame);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(15);
        layout->addWidget(new GlyphTile(info.glyph, 46, theme_), 0, Qt::AlignVCenter);
        auto *copy = new QVBoxLayout;
        copy->setSpacing(3);
        auto *heading = new QLabel(title);
        heading->setObjectName(QStringLiteral("heroTitle"));
        heading->setWordWrap(true);
        auto *description = new QLabel(tr(info.description));
        description->setObjectName(QStringLiteral("secondary"));
        description->setWordWrap(true);
        copy->addWidget(heading);
        copy->addWidget(description);
        layout->addLayout(copy, 1);
        return frame;
    }

    QLabel *noteLabel(const QString &text) {
        auto *note = new QLabel(text);
        note->setObjectName(QStringLiteral("secondary"));
        note->setWordWrap(true);
        return note;
    }

    QLabel *groupLabel(const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName(QStringLiteral("groupLabel"));
        return label;
    }

    QString defaultTarget(int id) const {
        if (!fixedTarget_.isEmpty() && fixedMode_ >= 0) return QDir::toNativeSeparators(fixedTarget_);
        if (id == 1) return QStringLiteral("C:\\MO2 Revamped\\Isolated\\Elden Ring");
        if (id == 3) return QStringLiteral("C:\\MO2 Revamped\\Portable");
        return QStringLiteral("C:\\MO2 Revamped");
    }

    void savePageState() {
        if (target_) savedPaths_[mode_][QStringLiteral("target")] = target_->text();
        if (sourceInstall_) savedPaths_[mode_][QStringLiteral("sourceInstall")] = sourceInstall_->text();
        if (sourceData_) savedPaths_[mode_][QStringLiteral("sourceData")] = sourceData_->text();
        for (auto it = checks_.begin(); it != checks_.end(); ++it) savedChecks_[mode_][it.key()] = it.value()->isChecked();
    }

    QStringList installCandidates() {
        if (!registeredLoaded_) {
            registeredLoaded_ = true;
            const QString output = tempFile(QStringLiteral("installations-%1.json"));
            const QJsonObject result = runHost({QStringLiteral("--installations"), output}, output);
            for (const QJsonValue &value : result.value(QStringLiteral("paths")).toArray())
                if (!value.toString().isEmpty()) registered_ << QDir::toNativeSeparators(value.toString());
        }
        QStringList candidates = registered_;
        for (const QString &known : {QStringLiteral("C:\\MO2 Revamped"), QStringLiteral("C:\\MO2 Revamped\\Full"),
                                     QStringLiteral("C:\\MO2 Revamped\\MO2 Revamped"), QStringLiteral("C:\\MO2 Revamped\\Portable"),
                                     QStringLiteral("C:\\MO2 Revamped\\Isolated\\Elden Ring")}) {
            if (!candidates.contains(known, Qt::CaseInsensitive)) candidates << known;
        }
        return candidates;
    }

    void setMode(int id, bool force = false) {
        if (busy_ || (hasCompleted_ && !force) || modeBlocked(id)) return;
        const bool changed = id != mode_ || !pageBuilt_;
        if (pageBuilt_ && !force) savePageState();
        mode_ = id;
        for (TaskCard *tile : taskTiles_) tile->setChecked(tile->mode() == mode_);
        buildPage(changed || force);
        pageBuilt_ = true;
        if (target_ && (mode_ == 2 || mode_ == 4 || mode_ == 5) && fixedMode_ < 0 &&
            !savedPaths_.value(mode_).contains(QStringLiteral("target"))) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            for (const QString &candidate : installCandidates()) {
                if (!QFileInfo(QDir(candidate).filePath(QStringLiteral("ModOrganizer.exe"))).isFile()) continue;
                const QJsonObject found = inspect(candidate);
                bool eligible = found.value(QStringLiteral("recognized")).toBool();
                if (mode_ == 2) eligible = eligible && found.value(QStringLiteral("compatibleVersion")).toBool();
                if (mode_ == 4) eligible = eligible && found.value(QStringLiteral("pendingRestore")).toBool() && found.value(QStringLiteral("validBackup")).toBool();
                if (eligible) {
                    const QSignalBlocker blocker(target_);
                    target_->setText(QDir::toNativeSeparators(candidate));
                    break;
                }
            }
            QApplication::restoreOverrideCursor();
        }
        if (target_) refreshInspection();
    }

    void updateModeLabels() {
        const QString action = mode_ == 5 ? QStringLiteral("Desinstalar") :
                               mode_ == 4 ? QStringLiteral("Restaurar copia anterior") :
                               mode_ == 2 ? (isolated_ ? QStringLiteral("Actualizar instalación aislada") : QStringLiteral("Actualizar MO2 Revamped")) :
                               mode_ == 3 ? QStringLiteral("Copiar datos") : QStringLiteral("Instalar");
        actionButton_->setText(tr(action));
        actionButton_->setObjectName(mode_ == 5 ? QStringLiteral("dangerButton") : QStringLiteral("primaryButton"));
        repolish(actionButton_);
        setStatus(tr(QStringLiteral("Listo. Revisa la carpeta seleccionada antes de continuar.")), QString());
    }

    static void repolish(QWidget *widget) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    void setStatus(const QString &text, const QString &state) {
        statusLabel_->setText(text);
        statusLabel_->setProperty("state", state);
        repolish(statusLabel_);
    }

    void addPathRow(QVBoxLayout *layout, const QString &label, QLineEdit **out, bool browse, const QString &stateKey) {
        auto *row = new QWidget;
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        auto *caption = new QLabel(label);
        caption->setObjectName(QStringLiteral("fieldLabel"));
        auto *controls = new QHBoxLayout;
        controls->setSpacing(8);
        auto *edit = new QLineEdit;
        edit->setClearButtonEnabled(!(fixedMode_ >= 0 && stateKey == QStringLiteral("target")));
        edit->setText(savedPaths_.value(mode_).value(stateKey, stateKey == QStringLiteral("target") ? defaultTarget(mode_) : QString()));
        caption->setBuddy(edit);
        controls->addWidget(edit, 1);
        if (browse) {
            auto *button = new QPushButton(tr(QStringLiteral("Examinar…")));
            button->setObjectName(QStringLiteral("secondaryButton"));
            button->setIcon(glyphIcon(QStringLiteral("folder"), theme_.tileInk, 18));
            button->setIconSize(QSize(18, 18));
            button->setMinimumWidth(122);
            button->setCursor(Qt::PointingHandCursor);
            controls->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, edit] {
                const QString current = edit->text().trimmed();
                const QString start = !current.isEmpty() && QDir(current).exists() ? current
                                    : !current.isEmpty() && QDir(QFileInfo(current).absolutePath()).exists() ? QFileInfo(current).absolutePath()
                                    : QDir::homePath();
                const QString path = QFileDialog::getExistingDirectory(this, tr(QStringLiteral("Selecciona una carpeta")), start,
                                                                       QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
                if (!path.isEmpty()) edit->setText(QDir::toNativeSeparators(path));
            });
        }
        rowLayout->addWidget(caption);
        rowLayout->addLayout(controls);
        layout->addWidget(row);
        *out = edit;
        if (stateKey == QStringLiteral("target")) connect(edit, &QLineEdit::textChanged, this, [this] { scheduleInspect(); });
        if (stateKey == QStringLiteral("sourceInstall")) connect(edit, &QLineEdit::textChanged, this, [this, edit] {
            if (sourceData_ && QFileInfo(QDir(edit->text().trimmed()).filePath(QStringLiteral("ModOrganizer.ini"))).isFile())
                sourceData_->setText(edit->text().trimmed());
        });
    }

    QCheckBox *addCheck(QGridLayout *layout, const QString &key, const QString &label, bool fallback, int row, int column, int span = 1) {
        auto *check = new SetupCheckBox(tr(label));
        check->setCursor(Qt::PointingHandCursor);
        const auto saved = savedChecks_.value(mode_);
        check->setChecked(saved.contains(key) ? saved.value(key) : fallback);
        layout->addWidget(check, row, column, 1, span);
        checks_.insert(key, check);
        return check;
    }

    QGridLayout *newGrid(QVBoxLayout *parent) {
        auto *gridWidget = new QWidget;
        auto *grid = new QGridLayout(gridWidget);
        grid->setContentsMargins(0, 2, 0, 0);
        grid->setHorizontalSpacing(28);
        grid->setVerticalSpacing(9);
        parent->addWidget(gridWidget);
        return grid;
    }

    void buildPage(bool animate) {
        target_ = sourceInstall_ = sourceData_ = nullptr;
        appDataCheck_ = nullptr;
        checks_.clear();
        inspection_ = QJsonObject();
        auto *page = new QWidget;
        page->setObjectName(QStringLiteral("setupPage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 8, 6);
        pageLayout->setSpacing(12);

        const ModeInfo info = currentModeInfo();
        QString title = tr(info.title);
        if (fixedMode_ == 5 && isolated_) title = tr(QStringLiteral("Desinstalar instalación aislada"));
        pageLayout->addWidget(heroCard(info, title));

        locationCard_ = card(tr(QStringLiteral("Ubicación")));
        auto *locationLayout = qobject_cast<QVBoxLayout *>(locationCard_->layout());
        if (mode_ == 0 || mode_ == 1) {
            addPathRow(locationLayout, tr(QStringLiteral("Carpeta de destino")), &target_, true, QStringLiteral("target"));
        } else if (mode_ == 2) {
            addPathRow(locationLayout, tr(QStringLiteral("Instalación MO2 Revamped que se actualizará")), &target_, true, QStringLiteral("target"));
        } else if (mode_ == 3) {
            addPathRow(locationLayout, tr(QStringLiteral("Instalación MO2 de origen")), &sourceInstall_, true, QStringLiteral("sourceInstall"));
            addPathRow(locationLayout, tr(QStringLiteral("Carpeta de datos del origen")), &sourceData_, true, QStringLiteral("sourceData"));
            addPathRow(locationLayout, tr(QStringLiteral("Nueva carpeta de destino")), &target_, true, QStringLiteral("target"));
        } else if (mode_ == 4) {
            addPathRow(locationLayout, tr(QStringLiteral("Instalación de MO2 Revamped con copia de seguridad")), &target_, fixedMode_ < 0, QStringLiteral("target"));
        } else {
            addPathRow(locationLayout, tr(QStringLiteral("Carpeta de instalación de MO2 Revamped")), &target_, fixedMode_ < 0, QStringLiteral("target"));
        }
        if (fixedMode_ >= 0 && target_) target_->setReadOnly(true);
        pageLayout->addWidget(locationCard_);

        optionsCard_ = card(tr(QStringLiteral("Opciones")));
        auto *optionsLayout = qobject_cast<QVBoxLayout *>(optionsCard_->layout());
        if (mode_ == 0 || mode_ == 1 || mode_ == 2) {
            if (mode_ == 1 && !isolated_) optionsLayout->addWidget(noteLabel(tr(QStringLiteral("Esta edición solo incluye Elden Ring y Nightreign, usa una única instancia portable y guarda sus datos junto a la carpeta de instalación. La instalación estándar con varias instancias está desactivada temporalmente mientras se amplía su soporte."))));
            if (mode_ == 2) optionsLayout->addWidget(noteLabel(tr(QStringLiteral("Los archivos reemplazados se guardan en una copia previa. Perfiles, mods, descargas y plugins adicionales se mantienen. Los accesos existentes se conservan; las casillas añaden los que falten."))));
            QGridLayout *grid = newGrid(optionsLayout);
            addCheck(grid, QStringLiteral("startMenu"), QStringLiteral("Acceso en menú Inicio"), true, 0, 0);
            addCheck(grid, QStringLiteral("desktop"), QStringLiteral("Acceso en escritorio"), false, 0, 1);
            addCheck(grid, QStringLiteral("launch"), QStringLiteral("Abrir MO2 al terminar"), true, 0, 2);
            grid->setColumnStretch(3, 1);
        } else if (mode_ == 3) {
            optionsLayout->addWidget(groupLabel(tr(QStringLiteral("Datos que se copiarán"))));
            QGridLayout *data = newGrid(optionsLayout);
            addCheck(data, QStringLiteral("profiles"), QStringLiteral("Perfiles y partidas"), true, 0, 0);
            addCheck(data, QStringLiteral("mods"), QStringLiteral("Mods instalados"), true, 0, 1);
            addCheck(data, QStringLiteral("downloads"), QStringLiteral("Descargas"), true, 0, 2);
            addCheck(data, QStringLiteral("overwrite"), QStringLiteral("Overwrite"), true, 1, 0);
            addCheck(data, QStringLiteral("settings"), QStringLiteral("Preferencias MO2"), true, 1, 1);
            addCheck(data, QStringLiteral("categories"), QStringLiteral("Categorías y mapa Nexus"), true, 1, 2);
            data->setColumnStretch(3, 1);
            optionsLayout->addSpacing(4);
            optionsLayout->addWidget(groupLabel(tr(QStringLiteral("Tras la instalación"))));
            QGridLayout *after = newGrid(optionsLayout);
            addCheck(after, QStringLiteral("startMenu"), QStringLiteral("Acceso en menú Inicio"), true, 0, 0);
            addCheck(after, QStringLiteral("desktop"), QStringLiteral("Acceso en escritorio"), false, 0, 1);
            addCheck(after, QStringLiteral("launch"), QStringLiteral("Abrir MO2 al terminar"), true, 0, 2);
            after->setColumnStretch(3, 1);
        } else if (mode_ == 4) {
            optionsLayout->addWidget(noteLabel(tr(QStringLiteral("Recupera la versión anterior guardada al actualizar. Mantiene perfiles, mods, descargas y cualquier archivo que haya cambiado después."))));
        } else {
            optionsLayout->addWidget(noteLabel(tr(QStringLiteral("Por defecto solo se quitan los archivos del programa y se conservan tus datos: perfiles, mods, descargas y overwrite. Las opciones de borrado eliminan también esos datos y no se pueden deshacer."))));
            QGridLayout *grid = newGrid(optionsLayout);
            grid->setVerticalSpacing(10);
            addCheck(grid, QStringLiteral("forceClose"), QStringLiteral("Forzar el cierre de MO2 en esta carpeta si sigue abierto"), false, 0, 0);
            QCheckBox *removeAll = addCheck(grid, QStringLiteral("removeAll"), QStringLiteral("Borrar todos los datos y la carpeta seleccionada"), false, 1, 0);
            removeAll->setProperty("danger", true);
            appDataCheck_ = addCheck(grid, QStringLiteral("removeAppData"), QStringLiteral("Borrar también los datos globales de MO2 Revamped (AppData\\Local)"), false, 2, 0);
            appDataCheck_->setProperty("danger", true);
            appDataCheck_->setEnabled(false);
        }
        pageLayout->addWidget(optionsCard_);

        progressCard_ = card(tr(QStringLiteral("Estado")));
        auto *progressLayout = qobject_cast<QVBoxLayout *>(progressCard_->layout());
        progressStatus_ = new QLabel;
        progressStatus_->setObjectName(QStringLiteral("secondary"));
        progressStatus_->setWordWrap(true);
        progressLayout->addWidget(progressStatus_);
        progress_ = new QProgressBar;
        progress_->setRange(0, 1000);
        progress_->setValue(0);
        progress_->setTextVisible(false);
        progress_->setFixedHeight(8);
        progressLayout->addWidget(progress_);
        progressAnimation_ = new QPropertyAnimation(progress_, "value", progress_);
        progressAnimation_->setDuration(320);
        progressAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        detailsButton_ = new QToolButton;
        detailsButton_->setObjectName(QStringLiteral("linkButton"));
        detailsButton_->setText(tr(QStringLiteral("Ver detalles")));
        detailsButton_->setCursor(Qt::PointingHandCursor);
        detailsButton_->setCheckable(true);
        progressLayout->addWidget(detailsButton_, 0, Qt::AlignLeft);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(400);
        log_->setMinimumHeight(110);
        log_->setMaximumHeight(170);
        log_->setPlaceholderText(tr(QStringLiteral("El proceso y los avisos aparecerán aquí.")));
        log_->hide();
        progressLayout->addWidget(log_);
        connect(detailsButton_, &QToolButton::toggled, this, [this](bool on) {
            log_->setVisible(on);
            detailsButton_->setText(tr(on ? QStringLiteral("Ocultar detalles") : QStringLiteral("Ver detalles")));
            if (on) QTimer::singleShot(0, this, [this] { scroll_->ensureWidgetVisible(log_); });
        });
        progressCard_->hide();
        pageLayout->addWidget(progressCard_);
        pageLayout->addStretch();

        QWidget *previous = scroll_->takeWidget();
        scroll_->setWidget(page);
        if (previous) previous->deleteLater();
        if (animate) fadeIn(page);
        updateModeLabels();
    }

    void applyStyle() {
        QString qss = QStringLiteral(
            "QWidget { font-family:'Segoe UI Variable Text','Segoe UI'; font-size:9pt; color:%1; }"
            "QMainWindow,QWidget#central,QWidget#qt_scrollarea_viewport,QWidget#setupPage { background:%2; }"
            "QFrame#header { background:transparent; border:none; border-bottom:1px solid %3; }"
            "QLabel#brandTitle { color:%4; font-size:16pt; font-weight:600; }"
            "QLabel#brandSub,QLabel#secondary { color:%5; }"
            "QLabel#sectionTitle { color:%4; font-size:10.5pt; font-weight:600; }"
            "QFrame#sidebar,QFrame#contentCard,QFrame#heroCard { background:%6; border:1px solid %3; border-radius:10px; }"
            "QFrame#heroCard { border-color:%13; }"
            "QLabel#heroTitle { color:%4; font-size:13pt; font-weight:600; }"
            "QLabel#cardTitle { color:%4; font-size:10.5pt; font-weight:600; }"
            "QLabel#groupLabel { color:%5; font-size:8pt; font-weight:600; letter-spacing:0.4px; text-transform:uppercase; }"
            "QLabel#fieldLabel { color:%4; font-weight:500; }"
            "QLabel#statusLabel { color:%5; }"
            "QLabel#statusLabel[state='success'] { color:%14; font-weight:600; }"
            "QLabel#statusLabel[state='error'] { color:%15; font-weight:600; }"
            "QLabel#statusLabel[state='busy'] { color:%1; }"
            "QLineEdit,QPlainTextEdit { background:%10; border:1px solid %3; border-radius:7px; padding:7px 10px; selection-background-color:%11; selection-color:white; }"
            "QLineEdit:hover { border-color:%13; }"
            "QLineEdit:focus,QPlainTextEdit:focus { border:1px solid %11; }"
            "QLineEdit:read-only { background:%2; color:%5; }"
            "QPlainTextEdit { font-family:'Cascadia Mono','Consolas'; font-size:8.5pt; }"
            "QPushButton { background:%6; border:1px solid %3; border-radius:7px; padding:8px 16px; min-height:20px; }"
            "QPushButton:hover { background:%7; border-color:%13; }"
            "QPushButton:pressed { background:%8; }"
            "QPushButton:disabled { color:%5; background:%2; border-color:%3; }"
            "QPushButton:focus { border-color:%11; }"
            "QPushButton#primaryButton { background:%11; color:white; border:1px solid %11; font-weight:600; }"
            "QPushButton#primaryButton:hover { background:%12; border-color:%12; }"
            "QPushButton#primaryButton:pressed { background:%11; }"
            "QPushButton#primaryButton:disabled,QPushButton#dangerButton:disabled { background:%3; color:%5; border-color:%3; }"
            "QPushButton#dangerButton { background:%15; color:white; border:1px solid %15; font-weight:600; }"
            "QPushButton#dangerButton:hover { background:%16; border-color:%16; }"
            "QPushButton#languageBox { text-align:left; background:%6; border:1px solid %3; border-radius:12px; padding:4px 34px 4px 10px; font-weight:500; }"
            "QPushButton#languageBox:hover { background:%7; border-color:%13; }"
            "QPushButton#languageBox:pressed { background:%8; }"
            "QPushButton#languageBox::menu-indicator { image:none; width:0; }"
            "QMenu#languageMenu { background:%6; border:1px solid %3; border-radius:12px; padding:6px; }"
            "QMenu#languageMenu::item { background:transparent; color:%1; padding:8px 14px 8px 32px; margin:1px 0; border-radius:7px; }"
            "QMenu#languageMenu::item:selected { background:%7; color:%4; }"
            "QMenu#languageMenu::item:checked { background:%8; color:%4; font-weight:600; }"
            "QMenu#languageMenu::indicator { width:14px; height:14px; left:10px; }"
            "QToolButton#linkButton { background:transparent; border:none; color:%11; padding:0; font-weight:600; }"
            "QToolButton#linkButton:hover { color:%12; text-decoration:underline; }"
            "QCheckBox { spacing:9px; min-height:22px; }"
            "QCheckBox:disabled { color:%5; }"
            "QCheckBox::indicator { width:17px; height:17px; border:1px solid %13; border-radius:5px; background:%10; }"
            "QCheckBox::indicator:hover { border-color:%11; }"
            "QCheckBox::indicator:checked { background:%11; border-color:%11; }"
            "QCheckBox[danger='true']::indicator:checked { background:%15; border-color:%15; }"
            "QCheckBox::indicator:disabled { background:%2; border-color:%3; }"
            "QProgressBar { background:%10; border:none; border-radius:4px; }"
            "QProgressBar::chunk { border-radius:4px; background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %11, stop:1 %12); }"
            "QScrollBar:vertical { background:transparent; width:10px; margin:2px 0; }"
            "QScrollBar::handle:vertical { background:%3; min-height:30px; border-radius:4px; margin:0 2px; }"
            "QScrollBar::handle:vertical:hover { background:%13; }"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }"
            "QFrame#footer { background:transparent; border:none; border-top:1px solid %3; }"
            "QMessageBox { background:%2; } QMessageBox QLabel { color:%1; }"
            "QToolTip { background:%6; color:%1; border:1px solid %3; padding:4px 6px; }"
        );
        const QColor strongBorder = mix(theme_.border, theme_.muted, 0.35);
        const QString colors[] = {theme_.text.name(), theme_.page.name(), theme_.border.name(), theme_.heading.name(),
                                  theme_.muted.name(), theme_.surface.name(), theme_.hover.name(), theme_.selected.name(),
                                  theme_.accent.name(), theme_.input.name(), theme_.accent.name(), theme_.accentHover.name(),
                                  strongBorder.name(), theme_.success.name(), theme_.danger.name(), theme_.dangerHover.name()};
        for (int i = 16; i >= 1; --i) qss.replace(QStringLiteral("%") + QString::number(i), colors[i - 1]);
        setStyleSheet(qss);
        QPalette p = palette();
        p.setColor(QPalette::Window, theme_.page); p.setColor(QPalette::WindowText, theme_.text);
        p.setColor(QPalette::Base, theme_.input); p.setColor(QPalette::Text, theme_.text);
        p.setColor(QPalette::Button, theme_.surface); p.setColor(QPalette::ButtonText, theme_.text);
        p.setColor(QPalette::Highlight, theme_.accent); p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, theme_.muted);
        setPalette(p);
    }

    QJsonObject inspect(const QString &path) {
        const QString output = tempFile(QStringLiteral("inspect-%1.json"));
        return runHost({QStringLiteral("--inspect"), path, output, language_}, output, 7000);
    }

    void scheduleInspect() {
        if (!inspectTimer_) {
            inspectTimer_ = new QTimer(this);
            inspectTimer_->setSingleShot(true);
            connect(inspectTimer_, &QTimer::timeout, this, [this] { refreshInspection(); });
        }
        inspectTimer_->start(300);
    }

    // Asynchronous so typing a path never freezes the window.
    void refreshInspection() {
        if (!target_ || busy_) return;
        if (inspectTimer_) inspectTimer_->stop();
        if (inspectProcess_) {
            inspectProcess_->disconnect();
            inspectProcess_->kill();
            inspectProcess_->waitForFinished(500);
            inspectProcess_->deleteLater();
            inspectProcess_ = nullptr;
        }
        if (!inspectFolder_.isEmpty()) QDir(inspectFolder_).removeRecursively();
        inspectFolder_ = QFileInfo(tempFile(QStringLiteral("inspect-%1/result.json"))).absolutePath();
        const QString output = QDir(inspectFolder_).filePath(QStringLiteral("result.json"));
        const QString inspected = target_->text().trimmed();
        inspectProcess_ = new QProcess(this);
        connect(inspectProcess_, &QProcess::finished, this, [this, output, inspected] {
            QJsonObject result;
            QFile file(output);
            if (file.open(QIODevice::ReadOnly)) result = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            QDir(inspectFolder_).removeRecursively();
            inspectFolder_.clear();
            inspectProcess_->deleteLater();
            inspectProcess_ = nullptr;
            if (!target_ || target_->text().trimmed() != inspected) return;
            inspection_ = result;
            applyInspection();
        });
        inspectProcess_->start(host_, {QStringLiteral("--inspect"), inspected, output, language_});
    }

    void applyInspection() {
        if (mode_ != 5) return;
        if (QCheckBox *removeAll = checks_.value(QStringLiteral("removeAll"), nullptr)) {
            removeAll->setText(tr(inspection_.value(QStringLiteral("hasBackupDirectory")).toBool()
                ? QStringLiteral("Borrar todos los datos y la carpeta seleccionada, incluida la copia previa")
                : QStringLiteral("Borrar todos los datos y la carpeta seleccionada")));
        }
        if (appDataCheck_) {
            const bool available = inspection_.value(QStringLiteral("globalData")).toBool();
            if (!available) appDataCheck_->setChecked(false);
            appDataCheck_->setEnabled(available && !busy_ && !hasCompleted_);
            appDataCheck_->setText(tr(available
                ? QStringLiteral("Borrar también los datos globales de MO2 Revamped (AppData\\Local)")
                : QStringLiteral("Solo disponible para instalaciones no portables de MO2 Revamped")));
        }
    }

    bool pathsOverlap(QString left, QString right) const {
        // cleanPath() always yields '/' separators.
        left = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
        right = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
        const auto within = [](const QString &a, const QString &b) {
            const QString prefix = b.endsWith(QLatin1Char('/')) ? b : b + QLatin1Char('/');
            return a.compare(b, Qt::CaseInsensitive) == 0 || a.startsWith(prefix, Qt::CaseInsensitive);
        };
        return within(left, right) || within(right, left);
    }

    bool validate() {
        const QString target = target_ ? QDir::cleanPath(target_->text().trimmed()) : QString();
        if (target.isEmpty() || !QDir::isAbsolutePath(target) || target.size() < 3)
            return showError(tr(QStringLiteral("Falta la ubicación")), tr(QStringLiteral("Indica una carpeta válida.")));
        const bool fresh = mode_ == 0 || mode_ == 1;
        if (mode_ == 3) {
            const QString source = sourceInstall_->text().trimmed();
            const QString data = sourceData_->text().trimmed();
            if (source.isEmpty() || !QFileInfo(QDir(source).filePath(QStringLiteral("ModOrganizer.exe"))).isFile())
                return showError(tr(QStringLiteral("Origen no encontrado")), tr(QStringLiteral("Selecciona la carpeta de origen que contiene ModOrganizer.exe.")));
            if (data.isEmpty() || !QDir(data).exists())
                return showError(tr(QStringLiteral("Datos de origen no válidos")), tr(QStringLiteral("Selecciona una carpeta de datos válida y marca al menos una categoría que exista.")));
            int mask = 0;
            const QStringList keys = {QStringLiteral("profiles"), QStringLiteral("mods"), QStringLiteral("downloads"),
                                      QStringLiteral("overwrite"), QStringLiteral("settings"), QStringLiteral("categories")};
            for (int i = 0; i < keys.size(); ++i) if (checks_.value(keys.at(i))->isChecked()) mask |= (1 << i);
            const QString output = tempFile(QStringLiteral("migration-%1.json"));
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QJsonObject check = runHost({QStringLiteral("--migration-data"), data, QString::number(mask), output, language_}, output, 15000);
            QApplication::restoreOverrideCursor();
            if (!check.value(QStringLiteral("hasData")).toBool())
                return showError(tr(QStringLiteral("Datos de origen no válidos")), tr(QStringLiteral("Selecciona una carpeta de datos válida y marca al menos una categoría que exista.")));
            if (pathsOverlap(target, source) || pathsOverlap(target, data))
                return showError(tr(QStringLiteral("Carpetas solapadas")), tr(QStringLiteral("El origen y el destino deben ser carpetas separadas.")));
        }
        if (fresh || mode_ == 3) {
            if (QFileInfo(target).exists() && !QFileInfo(target).isDir())
                return showError(tr(QStringLiteral("Ubicación no válida")), tr(QStringLiteral("La ruta indicada no es válida.")));
            if (QDir(target).exists()) {
                if (QFileInfo(QDir(target).filePath(QStringLiteral("ModOrganizer.exe"))).isFile())
                    return showError(tr(QStringLiteral("La carpeta ya contiene MO2")), tr(QStringLiteral("Esta carpeta ya contiene MO2. Elige «Actualizar MO2 Revamped» si está marcada como Revamped o crea otra carpeta.")));
                if (!QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty())
                    return showError(tr(QStringLiteral("El destino no está vacío")), tr(QStringLiteral("El destino contiene archivos. Elige una carpeta vacía para evitar sobrescribirlos.")));
            }
        }
        if (mode_ == 2 || mode_ == 4 || mode_ == 5) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            inspection_ = inspect(target);
            QApplication::restoreOverrideCursor();
            applyInspection();
            if (mode_ == 2) {
                if (!inspection_.value(QStringLiteral("recognized")).toBool())
                    return showError(tr(QStringLiteral("Instalación no reconocida")), tr(QStringLiteral("La actualización solo se admite en una instalación identificada de MO2 Revamped. No se modificó la carpeta seleccionada.")));
                if (!inspection_.value(QStringLiteral("compatibleVersion")).toBool())
                    return showError(tr(QStringLiteral("Versión no compatible")), tr(QStringLiteral("La actualización directa admite MO2 Revamped basado en MO2 2.5.0 a 2.5.2. Usa «Copiar datos» para pasar tus datos a una instalación nueva.")));
            } else if (!inspection_.value(QStringLiteral("recognized")).toBool()) {
                return showError(tr(QStringLiteral("Instalación no reconocida")), tr(QStringLiteral("Selecciona una carpeta que contenga una instalación identificada de MO2 Revamped. Las instalaciones originales de MO2 no se modifican.")));
            } else if (mode_ == 4 && (!inspection_.value(QStringLiteral("pendingRestore")).toBool() || !inspection_.value(QStringLiteral("validBackup")).toBool())) {
                return showError(tr(QStringLiteral("Copia previa no encontrada")), tr(QStringLiteral("Esta instalación no tiene una copia previa válida. «Restaurar copia anterior» solo está disponible después de una actualización con copia de seguridad.")));
            }
        }
        return true;
    }

    bool showError(const QString &title, const QString &body) {
        QMessageBox box(QMessageBox::Warning, title, body, QMessageBox::Ok, this);
        if (QAbstractButton *ok = box.button(QMessageBox::Ok)) ok->setText(tr(QStringLiteral("Cerrar")));
        box.setStyleSheet(styleSheet());
        box.exec();
        return false;
    }

    QString selectedCategories() const {
        QStringList selected;
        if (checks_.value(QStringLiteral("profiles"))->isChecked()) selected << tr(QStringLiteral("perfiles"));
        if (checks_.value(QStringLiteral("mods"))->isChecked()) selected << tr(QStringLiteral("mods"));
        if (checks_.value(QStringLiteral("downloads"))->isChecked()) selected << tr(QStringLiteral("descargas"));
        if (checks_.value(QStringLiteral("overwrite"))->isChecked()) selected << tr(QStringLiteral("Overwrite"));
        if (checks_.value(QStringLiteral("settings"))->isChecked()) selected << tr(QStringLiteral("preferencias"));
        if (checks_.value(QStringLiteral("categories"))->isChecked()) selected << tr(QStringLiteral("categorías"));
        return selected.isEmpty() ? tr(QStringLiteral("ninguna")) : selected.join(QStringLiteral(", "));
    }

    bool confirm() {
        const QString target = QDir::toNativeSeparators(target_->text().trimmed());
        QString title;
        QString body;
        bool destructive = false;
        if (mode_ == 0) {
            title = tr(QStringLiteral("Instalación nueva"));
            body = tr(QStringLiteral("MO2 Revamped se instalará en:")) + QStringLiteral("\n\n") + target + tr(QStringLiteral("\n\n¿Continuar?"));
        } else if (mode_ == 1) {
            title = tr(QStringLiteral("Instalación portable solo para Elden Ring y Nightreign"));
            body = tr(QStringLiteral("La edición portable aislada de MO2 se instalará en:")) + QStringLiteral("\n\n") + target +
                   tr(QStringLiteral("\n\nSolo incluirá soporte para Elden Ring y Nightreign y no leerá ni modificará las instancias globales de otras copias de MO2. ¿Continuar?"));
        } else if (mode_ == 2) {
            title = tr(isolated_ ? QStringLiteral("Actualizar instalación aislada") : QStringLiteral("Actualizar MO2 Revamped"));
            body = tr(QStringLiteral("Se actualizarán los archivos del programa en:")) + QStringLiteral("\n\n") + target +
                   tr(QStringLiteral("\n\nLos archivos previos se guardarán para poder restaurarlos. ¿Continuar?"));
        } else if (mode_ == 3) {
            title = tr(QStringLiteral("Copiar datos"));
            body = tr(QStringLiteral("Se creará una instalación portable en:")) + QStringLiteral("\n\n") + target +
                   tr(QStringLiteral("\n\nEl origen se conservará. Categorías seleccionadas: ")) + selectedCategories() + tr(QStringLiteral(". ¿Continuar?"));
        } else if (mode_ == 4) {
            title = tr(QStringLiteral("Restaurar copia anterior"));
            body = tr(QStringLiteral("Se recuperarán los archivos originales guardados para:")) + QStringLiteral("\n\n") + target +
                   tr(QStringLiteral("\n\nSe quitarán los accesos directos que añadió la actualización. Los perfiles, mods, descargas y archivos modificados se conservarán. ¿Continuar?"));
        } else {
            title = tr(isolated_ ? QStringLiteral("Desinstalar instalación aislada") : QStringLiteral("Desinstalar MO2 Revamped"));
            body = tr(QStringLiteral("Se desinstalará esta instalación de MO2 Revamped:")) + QStringLiteral("\n\n") + target;
            if (checks_.value(QStringLiteral("removeAll"))->isChecked()) {
                destructive = true;
                body += tr(QStringLiteral("\n\nSe borrará todo lo que haya dentro de la carpeta seleccionada, incluidos perfiles, mods, descargas, ajustes, guardados compartidos, copias de seguridad de guardados y archivos añadidos. También se quitará la carpeta seleccionada y las contenedoras que queden vacías, hasta llegar a una carpeta con contenido o protegida por Windows. Los datos fuera de esta ruta no se tocarán. Esta acción no se puede deshacer."));
            } else {
                body += tr(QStringLiteral("\n\nSe quitarán las carpetas de componentes reconocidas y se conservarán los datos de usuario, como perfiles, mods, descargas, overwrite y ajustes. Los demás archivos fuera de esas carpetas también se mantienen. ")) +
                        (inspection_.value(QStringLiteral("pendingRestore")).toBool() && inspection_.value(QStringLiteral("validBackup")).toBool()
                            ? tr(QStringLiteral("Si hay una copia previa, se restaurará la versión original."))
                            : tr(QStringLiteral("Los datos de usuario se mantendrán en su ubicación actual.")));
            }
            if (checks_.value(QStringLiteral("removeAppData"))->isChecked()) {
                destructive = true;
                QString dataWarning = tr(QStringLiteral("\n\nSe borrará por completo la carpeta global de MO2 Revamped:\n{0}\nIncluye todas las instancias globales y sus perfiles, mods, descargas y ajustes. Se comparte entre instalaciones no portables de Revamped."));
                dataWarning.replace(QStringLiteral("{0}"), QDir::toNativeSeparators(inspection_.value(QStringLiteral("globalDataPath")).toString()));
                body += dataWarning;
            }
            if (checks_.value(QStringLiteral("forceClose"))->isChecked())
                body += tr(QStringLiteral("\n\nSi MO2 sigue abierto, se intentará cerrarlo y se forzará el cierre de esta instalación si no responde; podrías perder cambios sin guardar."));
            body += tr(QStringLiteral("\n\n¿Continuar?"));
        }
        QMessageBox box(destructive ? QMessageBox::Warning : QMessageBox::Question, title, body, QMessageBox::Yes | QMessageBox::No, this);
        if (QAbstractButton *yes = box.button(QMessageBox::Yes)) {
            yes->setText(tr(QStringLiteral("Sí")));
            yes->setObjectName(destructive ? QStringLiteral("dangerButton") : QStringLiteral("primaryButton"));
        }
        if (QAbstractButton *no = box.button(QMessageBox::No)) no->setText(tr(QStringLiteral("No")));
        box.setDefaultButton(QMessageBox::No);
        box.setStyleSheet(styleSheet());
        return box.exec() == QMessageBox::Yes;
    }

    void beginOperation() {
        if (busy_ || hasCompleted_ || !target_ || modeBlocked(mode_) || !validate() || !confirm()) return;
        savePageState();
        if (!jobDirectory_.isEmpty()) QDir(jobDirectory_).removeRecursively();
        jobDirectory_ = QFileInfo(tempFile(QStringLiteral("job-%1/request.json"))).absolutePath();
        requestPath_ = QDir(jobDirectory_).filePath(QStringLiteral("request.json"));
        eventsPath_ = QDir(jobDirectory_).filePath(QStringLiteral("events.jsonl"));
        resultPath_ = QDir(jobDirectory_).filePath(QStringLiteral("result.json"));
        QFile events(eventsPath_);
        if (!events.open(QIODevice::WriteOnly)) { showError(tr(QStringLiteral("Estado")), events.errorString()); return; }
        events.close();

        QJsonObject request;
        request.insert(QStringLiteral("Mode"), mode_);
        request.insert(QStringLiteral("InstanceKind"), (mode_ == 1 || mode_ == 3) ? 0 : 1);
        request.insert(QStringLiteral("TargetDirectory"), QDir::toNativeSeparators(QDir::cleanPath(target_->text().trimmed())));
        request.insert(QStringLiteral("SourceInstallDirectory"), sourceInstall_ ? QDir::toNativeSeparators(sourceInstall_->text().trimmed()) : QString());
        request.insert(QStringLiteral("SourceDataDirectory"), sourceData_ ? QDir::toNativeSeparators(sourceData_->text().trimmed()) : QString());
        const auto checked = [this](const QString &key) { return checks_.contains(key) && checks_.value(key)->isChecked(); };
        request.insert(QStringLiteral("CopyProfiles"), checked(QStringLiteral("profiles")));
        request.insert(QStringLiteral("CopyMods"), checked(QStringLiteral("mods")));
        request.insert(QStringLiteral("CopyDownloads"), checked(QStringLiteral("downloads")));
        request.insert(QStringLiteral("CopyOverwrite"), checked(QStringLiteral("overwrite")));
        request.insert(QStringLiteral("CopySettings"), checked(QStringLiteral("settings")));
        request.insert(QStringLiteral("CopyCategories"), checked(QStringLiteral("categories")));
        request.insert(QStringLiteral("StartMenuShortcut"), checked(QStringLiteral("startMenu")));
        request.insert(QStringLiteral("DesktopShortcut"), checked(QStringLiteral("desktop")));
        request.insert(QStringLiteral("LaunchWhenDone"), checked(QStringLiteral("launch")));
        request.insert(QStringLiteral("ForceCloseMo2"), checked(QStringLiteral("forceClose")));
        request.insert(QStringLiteral("RemoveAllRemainingContents"), checked(QStringLiteral("removeAll")));
        request.insert(QStringLiteral("RemoveRevampedAppData"), checked(QStringLiteral("removeAppData")));
        QFile requestFile(requestPath_);
        if (!requestFile.open(QIODevice::WriteOnly)) { showError(tr(QStringLiteral("Estado")), requestFile.errorString()); return; }
        requestFile.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
        requestFile.close();

        busy_ = true;
        setInputsEnabled(false);
        progressCard_->show();
        fadeIn(progressCard_, 220);
        log_->clear();
        progressAnimation_->stop();
        progress_->setValue(0);
        progressTarget_ = 0;
        const QString preparing = tr(mode_ == 5 ? QStringLiteral("Preparando la desinstalación…") :
                                     mode_ == 4 ? QStringLiteral("Preparando la restauración…") : QStringLiteral("Preparando la instalación…"));
        setStatus(preparing, QStringLiteral("busy"));
        progressStatus_->setText(preparing);
        closeButton_->setEnabled(false);
        actionButton_->setEnabled(false);
        QTimer::singleShot(0, this, [this] { scroll_->ensureWidgetVisible(progressCard_); });
        eventOffset_ = 0;
        if (!eventTimer_) { eventTimer_ = new QTimer(this); connect(eventTimer_, &QTimer::timeout, this, [this] { readEvents(); }); }
        eventTimer_->start(150);
        backend_ = new QProcess(this);
        connect(backend_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
            completeOperation(exitCode, exitStatus == QProcess::NormalExit);
        });
        backend_->start(host_, {QStringLiteral("--backend"), requestPath_, eventsPath_, resultPath_, language_});
        if (!backend_->waitForStarted(5000)) {
            const QString startError = backend_->errorString();
            if (eventTimer_) eventTimer_->stop();
            disconnect(backend_, nullptr, this, nullptr);
            backend_->deleteLater();
            backend_ = nullptr;
            busy_ = false;
            setInputsEnabled(true);
            closeButton_->setEnabled(true);
            actionButton_->setEnabled(true);
            progressCard_->hide();
            setStatus(tr(QStringLiteral("La operación no pudo completarse.")), QStringLiteral("error"));
            showError(tr(QStringLiteral("Estado")), startError);
        }
    }

    void setInputsEnabled(bool enabled) {
        const bool active = enabled && !hasCompleted_;
        for (TaskCard *tile : taskTiles_) tile->setEnabled(active && !modeBlocked(tile->mode()));
        if (languageButton_) languageButton_->setEnabled(active);
        if (locationCard_) locationCard_->setEnabled(active);
        if (optionsCard_) optionsCard_->setEnabled(active);
        if (active) applyInspection();
    }

    void closeEvent(QCloseEvent *event) override {
        if (busy_) {
            QMessageBox box(QMessageBox::Warning, tr(QStringLiteral("Proceso en curso")),
                            tr(QStringLiteral("Espera a que termine el proceso antes de cerrar el asistente.")),
                            QMessageBox::Ok, this);
            if (QAbstractButton *ok = box.button(QMessageBox::Ok)) ok->setText(tr(QStringLiteral("Cerrar")));
            box.setStyleSheet(styleSheet());
            box.exec();
            event->ignore();
            return;
        }
        event->accept();
    }

    void animateProgress(int percent) {
        // Stages report their own ranges; never let the bar move backwards.
        const int target = qBound(0, percent, 100) * 10;
        if (target <= progressTarget_) return;
        progressTarget_ = target;
        progressAnimation_->stop();
        progressAnimation_->setStartValue(progress_->value());
        progressAnimation_->setEndValue(target);
        progressAnimation_->start();
    }

    void readEvents() {
        QFile file(eventsPath_);
        if (!file.open(QIODevice::ReadOnly)) return;
        if (!file.seek(eventOffset_)) return;
        const QByteArray data = file.readAll();
        // Only consume complete lines; the backend may still be writing the last one.
        const int end = data.lastIndexOf('\n');
        if (end < 0) return;
        eventOffset_ += end + 1;
        for (const QByteArray &line : data.left(end).split('\n')) {
            const QJsonObject item = QJsonDocument::fromJson(line.trimmed()).object();
            if (item.value(QStringLiteral("type")).toString() != QStringLiteral("progress")) continue;
            animateProgress(item.value(QStringLiteral("percent")).toInt());
            const QString message = item.value(QStringLiteral("message")).toString();
            if (message.isEmpty()) continue;
            setStatus(message, QStringLiteral("busy"));
            progressStatus_->setText(message);
            log_->appendPlainText(message);
        }
    }

    bool launchMo2(const QString &path) {
        // The setup runs elevated. The host asks the desktop shell to start
        // ModOrganizer.exe with the user's normal token, so drag and drop and
        // file ownership behave. explorer.exe is not used: it can open the
        // installation folder instead of running the program.
        QProcess launcher;
        launcher.start(host_, {QStringLiteral("--launch"), QDir::toNativeSeparators(path)});
        if (launcher.waitForStarted(3000) && launcher.waitForFinished(15000) &&
            launcher.exitStatus() == QProcess::NormalExit && launcher.exitCode() == 0)
            return true;
        if (launcher.state() != QProcess::NotRunning) { launcher.kill(); launcher.waitForFinished(1000); }
        // no fallback from this elevated window: MO2, its games and mods
        // would all run as administrator
        return false;
    }

    void completeOperation(int exitCode, bool normalExit) {
        readEvents();
        if (eventTimer_) eventTimer_->stop();
        QFile resultFile(resultPath_);
        QJsonObject result;
        if (resultFile.open(QIODevice::ReadOnly)) result = QJsonDocument::fromJson(resultFile.readAll()).object();
        resultFile.close();
        if (backend_) { backend_->deleteLater(); backend_ = nullptr; }
        const bool success = normalExit && exitCode == 0 && result.value(QStringLiteral("success")).toBool();
        const QString launchPath = result.value(QStringLiteral("launchPath")).toString();
        QString message = result.value(QStringLiteral("message")).toString();
        if (message.isEmpty()) message = result.value(QStringLiteral("error")).toString();
        if (message.isEmpty()) message = tr(success ? QStringLiteral("Operación completada.") : QStringLiteral("La operación no pudo completarse."));
        const QString details = result.value(QStringLiteral("details")).toString();
        progressStatus_->setText(message);
        log_->appendPlainText(QString());
        log_->appendPlainText(message);
        if (!details.isEmpty()) log_->appendPlainText(details);
        busy_ = false;
        closeButton_->setEnabled(true);
        actionButton_->setEnabled(true);

        if (success) {
            progressAnimation_->stop();
            progressTarget_ = 0;
            animateProgress(100);
            hasCompleted_ = true;
            setInputsEnabled(false);
            setStatus(message, QStringLiteral("success"));
            disconnect(actionButton_, nullptr, this, nullptr);
            actionButton_->hide();
            closeButton_->setObjectName(QStringLiteral("primaryButton"));
            repolish(closeButton_);
            closeButton_->setFocus();
            if (!details.isEmpty() && !detailsButton_->isChecked()) progressStatus_->setText(message + QStringLiteral("\n") + details);
            if (!launchPath.isEmpty() && QFileInfo(launchPath).isFile() && !launchMo2(launchPath)) {
                actionButton_->setText(tr(QStringLiteral("Abrir MO2")));
                actionButton_->setObjectName(QStringLiteral("secondaryButton"));
                repolish(actionButton_);
                actionButton_->show();
                connect(actionButton_, &QPushButton::clicked, this, [this, launchPath] {
                    if (launchMo2(launchPath)) close();
                    else showError(tr(QStringLiteral("Estado")), tr(QStringLiteral("No se pudo abrir Mod Organizer 2.")));
                });
            }
        } else {
            // Keep the window usable so the user can fix the cause and retry.
            setInputsEnabled(true);
            setStatus(message, QStringLiteral("error"));
            actionButton_->setText(tr(QStringLiteral("Reintentar")));
            detailsButton_->setChecked(true);
        }
    }

    QString host_;
    QString language_;
    QString fixedTarget_;
    bool isolated_ = false;
    bool busy_ = false;
    bool hasCompleted_ = false;
    bool pageBuilt_ = false;
    bool registeredLoaded_ = false;
    int fixedMode_ = -1;
    int mode_ = 0;
    int progressTarget_ = 0;
    Theme theme_;
    QStringList registered_;
    QHash<QString, QString> translations_;
    QHash<int, QHash<QString, QString>> savedPaths_;
    QHash<int, QHash<QString, bool>> savedChecks_;
    QHash<QString, QCheckBox *> checks_;
    QJsonObject inspection_;
    QVector<TaskCard *> taskTiles_;
    QLabel *brandSub_ = nullptr;
    QLabel *sideTitle_ = nullptr;
    QLabel *sideHint_ = nullptr;
    QFrame *sidebar_ = nullptr;
    LanguageButton *languageButton_ = nullptr;
    QMenu *languageMenu_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *progressStatus_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *actionButton_ = nullptr;
    QToolButton *detailsButton_ = nullptr;
    QLineEdit *target_ = nullptr;
    QLineEdit *sourceInstall_ = nullptr;
    QLineEdit *sourceData_ = nullptr;
    QCheckBox *appDataCheck_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPropertyAnimation *progressAnimation_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QFrame *locationCard_ = nullptr;
    QFrame *optionsCard_ = nullptr;
    QFrame *progressCard_ = nullptr;
    QTimer *inspectTimer_ = nullptr;
    QTimer *eventTimer_ = nullptr;
    QProcess *backend_ = nullptr;
    QProcess *inspectProcess_ = nullptr;
    QString inspectFolder_;
    QString jobDirectory_, requestPath_, eventsPath_, resultPath_;
    qint64 eventOffset_ = 0;
};

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MO2 Revamped"));
    QCoreApplication::setApplicationName(QStringLiteral("Setup"));
    const QStringList args = app.arguments();
    QString host, language = QStringLiteral("en"), fixedMode, fixedTarget, snapshot, iconPath, theme;
    int snapshotMode = -1;
    bool isolated = false;
    bool snapshotLanguageMenu = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == QStringLiteral("--host") && i + 1 < args.size()) host = args.at(++i);
        else if (arg == QStringLiteral("--language") && i + 1 < args.size()) language = args.at(++i);
        else if (arg == QStringLiteral("--fixed-mode") && i + 1 < args.size()) fixedMode = args.at(++i);
        else if (arg == QStringLiteral("--target") && i + 1 < args.size()) fixedTarget = args.at(++i);
        else if (arg == QStringLiteral("--icon") && i + 1 < args.size()) iconPath = args.at(++i);
        else if (arg == QStringLiteral("--theme") && i + 1 < args.size()) theme = args.at(++i);
        else if (arg == QStringLiteral("--snapshot") && i + 1 < args.size()) snapshot = args.at(++i);
        else if (arg == QStringLiteral("--snapshot-mode") && i + 1 < args.size()) snapshotMode = args.at(++i).toInt();
        else if (arg == QStringLiteral("--snapshot-language-menu")) snapshotLanguageMenu = true;
        else if (arg == QStringLiteral("--elden-ring-only")) isolated = true;
    }
    app.setWindowIcon(!iconPath.isEmpty() && QFileInfo::exists(iconPath) ? QIcon(iconPath) : QIcon(QStringLiteral(":/brand/icon.png")));
    if (host.isEmpty() || !QFileInfo::exists(host)) {
        const bool spanish = language.startsWith(QStringLiteral("es"));
        QMessageBox::critical(nullptr, QStringLiteral("MO2 Revamped"),
                              spanish ? QStringLiteral("No se pudo localizar el motor del instalador.")
                                      : QStringLiteral("The setup engine could not be found."));
        return 2;
    }
    SetupWindow window(host, language, fixedMode, fixedTarget, isolated, theme);
    window.show();
    if (!snapshot.isEmpty()) {
        if (snapshotLanguageMenu)
            QTimer::singleShot(900, &window, [&window, snapshot] { window.showLanguageMenuSnapshot(snapshot); });
        else if (snapshotMode >= 0)
            QTimer::singleShot(300, &window, [&window, snapshot, snapshotMode] { window.showModeSnapshot(snapshotMode, snapshot); });
        else
            QTimer::singleShot(900, &window, [&window, &app, snapshot] { window.grab().save(snapshot); app.quit(); });
    }
    return app.exec();
}
