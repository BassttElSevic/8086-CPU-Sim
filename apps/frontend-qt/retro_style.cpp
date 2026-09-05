#include "retro_style.h"

#include <QFontMetrics>
#include <QPainter>

namespace Retro {

void drawRaised(QPainter &p, const QRect &r, int thick)
{
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(r, kFace);
    p.fillRect(QRect(r.left(), r.top(), r.width(), thick), kFaceLight);
    p.fillRect(QRect(r.left(), r.top(), thick, r.height()), kFaceLight);
    p.fillRect(QRect(r.left() + thick, r.bottom() - thick + 1,
                     r.width() - thick, thick), kFaceShadow);
    p.fillRect(QRect(r.right() - thick + 1, r.top() + thick,
                     thick, r.height() - thick), kFaceShadow);
}

void drawSunken(QPainter &p, const QRect &r, int thick)
{
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(r, kFace);
    p.fillRect(QRect(r.left(), r.top(), r.width(), thick), kFaceShadow);
    p.fillRect(QRect(r.left(), r.top(), thick, r.height()), kFaceShadow);
    p.fillRect(QRect(r.left() + thick, r.bottom() - thick + 1,
                     r.width() - thick, thick), kFaceLight);
    p.fillRect(QRect(r.right() - thick + 1, r.top() + thick,
                     thick, r.height() - thick), kFaceLight);
}

void drawWindowFrame(QPainter &p, const QRect &r)
{
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, false);
    /* Outer navy, then a white inner line, then the navy core line. */
    p.fillRect(r, kFaceDark);
    p.fillRect(r.adjusted(1, 1, -1, -1), kFaceLight);
    p.fillRect(r.adjusted(2, 2, -2, -2), kFaceDark);
}

void drawPanel(QPainter &p, const QRect &r, const QString &label)
{
    QRect inner = r.adjusted(4, 4, -4, -4);
    drawRaised(p, r, 2);
    p.fillRect(inner, kWindow);
    p.setPen(kTitle);
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSizeF(9.0);
    p.setFont(f);
    p.drawText(QRect(inner.left() + 6, inner.top() + 2,
                     inner.width() - 10, 18),
               Qt::AlignLeft | Qt::AlignVCenter, label);
    p.setPen(Qt::NoPen);
    p.fillRect(QRect(inner.left(), inner.top() + 22, inner.width(), 1), kMuted);
}

/* 8x8 pixel-art glyph bitmaps.  '1' = pixel drawn. */
static const char *const kClose[8] = {
    "10000001", "01000010", "00100100", "00011000",
    "00011000", "00100100", "01000010", "10000001"
};
static const char *const kMinimize[8] = {
    "00000000", "00000000", "00000000", "00000000",
    "00000000", "00000011", "11111111", "00000000"
};
static const char *const kMaximize[8] = {
    "11111111", "10000001", "10000001", "10000001",
    "10000001", "10000001", "10000001", "11111111"
};
static const char *const kRestore[8] = {
    "11111000", "10001000", "11111111", "10000001",
    "10000001", "10000001", "10000001", "11111111"
};

void drawGlyph(QPainter &p, const QRect &r, Glyph g, const QColor &c)
{
    const char *const *rows = kClose;
    int top = 7;
    int left = 7;

    switch (g) {
    case G_MINIMIZE: rows = kMinimize; break;
    case G_MAXIMIZE: rows = kMaximize; break;
    case G_RESTORE:  rows = kRestore;  break;
    case G_CLOSE:    rows = kClose;    break;
    }

    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing, false);
    /* Scale the 8x8 glyph to a square cell that fits the button. */
    int scale = qMin(r.width(), r.height()) / 8;
    if (scale < 1) scale = 1;
    int total = scale * 8;
    int x0 = r.left() + (r.width() - total) / 2;
    int y0 = r.top() + (r.height() - total) / 2;
    p.fillRect(r, Qt::transparent);
    p.setBrush(c);
    for (int row = 0; row < 8; ++row) {
        const char *s = rows[row];
        for (int col = 0; col < 8; ++col) {
            if (s[col] == '1') {
                p.fillRect(x0 + col * scale, y0 + row * scale, scale, scale, c);
            }
        }
    }
    (void)top;
    (void)left;
}

}  // namespace Retro
