#ifndef PC_SIM_RETRO_BUTTON_H
#define PC_SIM_RETRO_BUTTON_H

#include <functional>
#include <QString>
#include <QWidget>

#include "retro_style.h"

/*
 * A custom-painted, hard-edged retro button.
 *
 * Deliberately avoids Q_OBJECT / moc so the frontend builds with just a C++
 * compiler plus Qt (Core/Gui/Widgets) — no uic/moc step, no resource file.
 * Click handling is done through a std::function callback instead of signals.
 *
 * It can show either a text label (Push / Toggle) or a pixel-art glyph used by
 * the title bar (min/max/restore/close).
 */
class RetroButton : public QWidget {
public:
    enum class Kind { Push, Toggle };
    enum class Glyph { None, Minimize, Maximize, Restore, Close };

    /* Text button.  active is used by Toggle buttons (speed selector). */
    RetroButton(const QString &text, std::function<void()> onClick,
                QWidget *parent = nullptr, Kind kind = Kind::Push,
                bool active = false);

    /* Glyph title-bar button. */
    RetroButton(Glyph glyph, std::function<void()> onClick, QWidget *parent);

    void setText(const QString &text);
    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setLook(const QColor &face, const QColor &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_text;
    Glyph m_glyph;
    Kind m_kind;
    std::function<void()> m_onClick;
    bool m_pressed = false;
    bool m_active;
    bool m_hover = false;
    QColor m_face;
    QColor m_textColor;
};

/*
 * A retro container for the control panel: fills its background with the
 * blue-white surface and draws a hard navy outline so it reads as a separate
 * panel.
 */
class RetroPanel : public QWidget {
public:
    explicit RetroPanel(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif  // PC_SIM_RETRO_BUTTON_H
