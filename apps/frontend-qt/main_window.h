#ifndef PC_SIM_MAIN_WINDOW_H
#define PC_SIM_MAIN_WINDOW_H

#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

extern "C" {
#include "sim/sim_frontend.h"
}

class QLineEdit;
class QLabel;
class RetroButton;
class RetroPanel;
class CgaView;

/*
 * Retro pixel-art main window.
 *
 * Frameless Qt window with its own hard-edged blue/white chrome so the
 * minimise / maximise / close controls can be drawn as pixel art.  It owns the
 * SimFrontend engine handle, drives it from a QTimer, maps Qt keys to 8042
 * scancode-set-1 bytes, and lays out the media controls on the right panel.
 *
 * Everything talks to the engine only through sim_frontend.h.
 */
class MainWindow : public QWidget {
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /* Apply command-line arguments before the window is shown. */
    void applyArguments(int argc, char **argv);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Drag {
        None, Move, Left, Right, Top, Bottom,
        TopLeft, TopRight, BottomLeft, BottomRight
    };

    void placeTitleButtons();
    void applyConfig();
    void runSlice();
    void updateStatus();
    void updateSpeedButtons();

    /* Media operations. */
    void browseBios();
    void browseFloppy();
    void browseHdd();
    void ejectFloppy();
    void reloadFloppy();
    void ejectHdd();
    void createNewHdd();
    void setSpeed(unsigned multiplier);
    void showError(const QString &message);

    /* Window chrome helpers. */
    Drag hitTest(const QPoint &pos);
    void updateCursor(const QPoint &pos);
    void toggleMaximized();

    /* Keyboard. */
    void injectKey(int key, bool down);
    static bool mapSet1(int key, bool &extended, uint8_t &code);

    static QString defaultBiosPath();

    /* Engine handle + configuration. */
    SimFrontend *m_fe = nullptr;
    bool m_createHdd = false;
    unsigned m_speed = 1;
    uint16_t m_hddCylinders = 256;
    uint8_t m_hddHeads = 16;
    uint8_t m_hddSectors = 63;

    bool m_running = false;
    QString m_error;
    bool m_runOnStart = false;

    /* Children. */
    QTimer m_timer;
    RetroButton *m_minBtn = nullptr;
    RetroButton *m_maxBtn = nullptr;
    RetroButton *m_closeBtn = nullptr;
    CgaView *m_cga = nullptr;
    QLineEdit *m_biosEdit = nullptr;
    QLineEdit *m_floppyEdit = nullptr;
    QLineEdit *m_hddEdit = nullptr;
    RetroButton *m_biosBrowse = nullptr;
    RetroButton *m_floppyBrowse = nullptr;
    RetroButton *m_floppyEject = nullptr;
    RetroButton *m_floppyReload = nullptr;
    RetroButton *m_hddBrowse = nullptr;
    RetroButton *m_hddNew = nullptr;
    RetroButton *m_hddEject = nullptr;
    RetroButton *m_mount = nullptr;
    RetroButton *m_speed1 = nullptr;
    RetroButton *m_speed2 = nullptr;
    RetroButton *m_speed4 = nullptr;
    RetroButton *m_speed8 = nullptr;
    RetroButton *m_speed16 = nullptr;
    RetroButton *m_speed32 = nullptr;
    RetroButton *m_speed64 = nullptr;
    QLabel *m_status = nullptr;

    /* Window dragging / resizing state. */
    Drag m_drag = Drag::None;
    QPoint m_dragStart;
    QRect m_dragStartGeom;
    bool m_maximized = false;
    QRect m_restoreGeom;

    static const int kTitleH = 32;
    static const int kPanelW = 360;
    static const int kMargin = 8;
};

#endif  // PC_SIM_MAIN_WINDOW_H
