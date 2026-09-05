#ifndef PC_SIM_RETRO_STYLE_H
#define PC_SIM_RETRO_STYLE_H

#include <QColor>
#include <QRect>
#include <QString>

class QPainter;

/*
 * Retro palette + hard-edged 3D bevel drawing helpers.
 *
 * "Retro blue + white, hard shadows" style: a light blue-white page surface,
 * a strong blue title bar, and chunky non-antialiased bevels whose shadow is a
 * medium/dark blue (never a soft gradient).  The look is deliberately stiff to
 * match a late-1980s / early-1990s pixel GUI.
 */
namespace Retro {

const QColor kWindow   (0xE8, 0xF0, 0xFA);   /* page / window background. */
const QColor kFace     (0xD9, 0xE5, 0xF4);   /* control face             */
const QColor kFaceLight(0xFF, 0xFF, 0xFF);   /* top/left bevel highlight  */
const QColor kFaceShadow(0x7E, 0x97, 0xB8);  /* bottom/right bevel shadow */
const QColor kFaceDark  (0x00, 0x00, 0x80);  /* dark navy (window frame)  */
const QColor kTitle     (0x00, 0x00, 0xA8);  /* title bar blue            */
const QColor kTitleTop  (0x0A, 0x2A, 0xC8);  /* title bar top highlight    */
const QColor kText      (0x00, 0x00, 0x00);
const QColor kMuted     (0x3A, 0x4A, 0x66);
const QColor kBezel     (0x12, 0x16, 0x2A);  /* dark bezel around the CGA  */
const QColor kBezelInner(0x00, 0x00, 0xA8);  /* navy inner bezel line      */

/* Chunky, hard-edged bevels.  thick defaults to 2 for the crisp 90s look. */
void drawRaised(QPainter &p, const QRect &r, int thick = 2);
void drawSunken(QPainter &p, const QRect &r, int thick = 2);

/* A retro group box: filled face + hard bevel + bold capital label. */
void drawPanel(QPainter &p, const QRect &r, const QString &label);

/* A retro hard frame for the whole window (navy outline + white inner). */
void drawWindowFrame(QPainter &p, const QRect &r);

/* Pixel-art glyphs painted into a small button. */
enum Glyph {
    G_MINIMIZE,
    G_MAXIMIZE,
    G_RESTORE,
    G_CLOSE
};
void drawGlyph(QPainter &p, const QRect &r, Glyph g, const QColor &c);

}  // namespace Retro

#endif  // PC_SIM_RETRO_STYLE_H
