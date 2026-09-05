#include "retro_button.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>

RetroButton::RetroButton(const QString &text, std::function<void()> onClick,
                         QWidget *parent, Kind kind, bool active)
    : QWidget(parent)
    , m_text(text)
    , m_glyph(Glyph::None)
    , m_kind(kind)
    , m_onClick(std::move(onClick))
    , m_active(active)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(24);
    m_face = Retro::kFace;
    m_textColor = Retro::kText;
}

RetroButton::RetroButton(Glyph glyph, std::function<void()> onClick, QWidget *parent)
    : QWidget(parent)
    , m_text()
    , m_glyph(glyph)
    , m_kind(Kind::Push)
    , m_onClick(std::move(onClick))
    , m_active(false)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(34, 24);
    m_face = Retro::kFace;
    m_textColor = Retro::kText;
}

void RetroButton::setText(const QString &text)
{
    m_text = text;
    update();
}

void RetroButton::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
    }
}

void RetroButton::setLook(const QColor &face, const QColor &text)
{
    m_face = face;
    m_textColor = text;
    update();
}

void RetroButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    QRect r = rect();
    bool active = m_active;
    bool sunken = m_pressed || active;

    /* Build the face.  A hover makes a Push button a touch lighter. */
    QColor face = m_face;
    if (!sunken && m_hover && m_kind == Kind::Push) {
        face = face.lighter(108);
    } else if (active) {
        face = Retro::kFace.lighter(104);
    }
    p.fillRect(r, face);

    /* Hard bevel: raised normally, sunken when pressed or toggled active. */
    const int t = 2;
    QColor hi = sunken ? Retro::kFaceShadow : Retro::kFaceLight;
    QColor sh = sunken ? Retro::kFaceLight : Retro::kFaceShadow;
    p.fillRect(QRect(r.left(), r.top(), r.width(), t), hi);
    p.fillRect(QRect(r.left(), r.top(), t, r.height()), hi);
    p.fillRect(QRect(r.left() + t, r.bottom() - t + 1, r.width() - t, t), sh);
    p.fillRect(QRect(r.right() - t + 1, r.top() + t, t, r.height() - t), sh);

    if (m_glyph != Glyph::None) {
        Retro::Glyph g = Retro::G_CLOSE;
        switch (m_glyph) {
        case Glyph::Minimize: g = Retro::G_MINIMIZE; break;
        case Glyph::Maximize: g = Retro::G_MAXIMIZE; break;
        case Glyph::Restore:  g = Retro::G_RESTORE;  break;
        case Glyph::Close:    g = Retro::G_CLOSE;    break;
        case Glyph::None:     break;
        }
        QColor glyphColor = active ? Retro::kFaceLight : Retro::kFaceDark;
        Retro::drawGlyph(p, r.adjusted(3, 3, -3, -3), g, glyphColor);
        return;
    }

    p.setPen(m_textColor);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSizeF(9.0);
    p.setFont(f);
    p.drawText(r.adjusted(2, 0, -2, 0), Qt::AlignCenter, m_text);
}

void RetroButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
}

void RetroButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    bool wasPressed = m_pressed;
    bool inside = rect().contains(event->pos());
    m_pressed = false;
    update();
    if (wasPressed && inside && m_onClick) {
        m_onClick();
    }
}

void RetroButton::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hover = true;
    update();
}

void RetroButton::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hover = false;
    m_pressed = false;
    update();
}

RetroPanel::RetroPanel(QWidget *parent)
    : QWidget(parent)
{
}

void RetroPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), Retro::kWindow);
    /* Hard navy outline with a white inner line (classic 3D bevel). */
    const int t = 2;
    p.fillRect(rect(), Retro::kFaceDark);
    p.fillRect(rect().adjusted(t, t, -t, -t), Retro::kFaceLight);
    p.fillRect(rect().adjusted(t * 2, t * 2, -t * 2, -t * 2), Retro::kWindow);
}
