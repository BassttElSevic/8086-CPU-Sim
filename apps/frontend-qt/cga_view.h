#ifndef PC_SIM_CGA_VIEW_H
#define PC_SIM_CGA_VIEW_H

#include <QImage>
#include <QWidget>

extern "C" {
#include "sim/sim_frontend.h"
}

/*
 * Displays the simulated CGA framebuffer.
 *
 * The buffered frame is a fixed 640x200 RGBA raster.  We copy it into a QImage
 * once per update and paint it scaled with nearest-neighbour integer scaling
 * (QPainter's default fast transform) so the pixels stay sharp and chunky —
 * a clean, non-CRT look.  A hard navy bezel frames the screen.
 */
class CgaView : public QWidget {
public:
    explicit CgaView(QWidget *parent = nullptr);

    void setFrame(const SimCgaRenderFrame &frame);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    bool m_hasImage = false;
};

#endif  // PC_SIM_CGA_VIEW_H
