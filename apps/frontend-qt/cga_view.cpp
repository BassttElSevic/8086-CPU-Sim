#include "cga_view.h"

#include <QPainter>

#include "retro_style.h"

CgaView::CgaView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
}

void CgaView::setFrame(const SimCgaRenderFrame &frame)
{
    /* Share the pixel buffer as an RGB32 image, then take an owned copy so the
     * engine can reuse its internal buffer on the next slice. */
    m_image = QImage(reinterpret_cast<const uchar *>(frame.pixels),
                     SIM_CGA_FRAME_WIDTH, SIM_CGA_FRAME_HEIGHT,
                     SIM_CGA_FRAME_WIDTH * 4, QImage::Format_RGB32)
                  .copy();
    m_hasImage = true;
    update();
}

void CgaView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    /* Dark screen bezel with a hard navy inner line. */
    p.fillRect(rect(), Retro::kBezel);
    p.fillRect(QRect(4, 4, width() - 8, height() - 8), Retro::kBezelInner);
    p.fillRect(QRect(6, 6, width() - 12, height() - 12), Retro::kBezel);

    if (!m_hasImage) {
        p.setPen(Retro::kMuted);
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = p.font();
        f.setBold(true);
        f.setPointSizeF(10.0);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("CGA"));
        return;
    }

    /* The CGA raster is 640x200 but its pixels are non-square: a real CGA
     * monitor presented that raster at ~4:3 (each pixel taller than wide).
     * Stretch the 640x200 buffer into a 4:3 rectangle (nearest-neighbour). */
    QRect avail = rect().adjusted(10, 10, -10, -10);
    if (avail.width() <= 0 || avail.height() <= 0) return;
    int w = avail.width();
    int h = (w * 3) / 4;   /* 4:3 physical aspect. */
    if (h > avail.height()) {
        h = avail.height();
        w = (h * 4) / 3;
    }
    QRect target(avail.left() + (avail.width() - w) / 2,
                 avail.top() + (avail.height() - h) / 2, w, h);
    p.drawImage(target, m_image);
}
