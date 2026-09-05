#include "main_window.h"

#include <QCoreApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "cga_view.h"
#include "retro_button.h"
#include "retro_style.h"

/* ------------------------------------------------------------------ */
/* Small static helpers                                                */
/* ------------------------------------------------------------------ */

static QLabel *makeLabel(const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral(
        "color:#000080;font-weight:bold;font-family:'monospace';font-size:12px;"));
    l->setFixedWidth(78);
    l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return l;
}

static QLineEdit *makeEdit(QWidget *parent)
{
    QLineEdit *e = new QLineEdit(parent);
    e->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#ffffff;color:#000000;"
        "border:2px solid;border-top-color:#000080;border-left-color:#000080;"
        "border-bottom-color:#ffffff;border-right-color:#ffffff;"
        "padding:1px 3px;font-family:'monospace';font-size:12px;}"));
    return e;
}

static QWidget *makeRow(const QString &label, QWidget *main, QWidget *right,
                        QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    if (!label.isEmpty()) h->addWidget(makeLabel(label, row));
    h->addWidget(main, 1);
    if (right) h->addWidget(right, 0);
    return row;
}

static QWidget *makeButtonRow(const QList<QWidget *> &buttons, QWidget *parent)
{
    QWidget *row = new QWidget(parent);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    /* Left spacer keeps the two buttons under the path field's right edge. */
    h->addSpacing(84);
    for (QWidget *b : buttons) h->addWidget(b, 1);
    return row;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize(1080, 680);
    setMinimumSize(920, 560);

    /* Left: the CGA display. */
    m_cga = new CgaView(this);

    /* Right: the control panel. */
    RetroPanel *panel = new RetroPanel(this);
    panel->setFixedWidth(kPanelW);
    QVBoxLayout *pv = new QVBoxLayout(panel);
    pv->setContentsMargins(12, 12, 12, 12);
    pv->setSpacing(8);

    /* Header. */
    QLabel *header = new QLabel(QStringLiteral("MACHINE MEDIA"), panel);
    header->setStyleSheet(QStringLiteral(
        "color:#000080;font-weight:bold;font-family:'monospace';font-size:13px;"));
    pv->addWidget(header);

    /* BIOS ROM row. */
    m_biosEdit = makeEdit(panel);
    m_biosBrowse = new RetroButton(QStringLiteral("..."),
                                   [this] { browseBios(); }, panel);
    m_biosBrowse->setFixedWidth(48);
    pv->addWidget(makeRow(QStringLiteral("BIOS ROM"), m_biosEdit,
                          m_biosBrowse, panel));

    /* Floppy A row + load/eject. */
    m_floppyEdit = makeEdit(panel);
    m_floppyBrowse = new RetroButton(QStringLiteral("..."),
                                     [this] { browseFloppy(); }, panel);
    m_floppyBrowse->setFixedWidth(48);
    pv->addWidget(makeRow(QStringLiteral("FLOPPY A:"), m_floppyEdit,
                          m_floppyBrowse, panel));
    m_floppyReload = new RetroButton(QStringLiteral("Reload"),
                                     [this] { reloadFloppy(); }, panel);
    m_floppyEject = new RetroButton(QStringLiteral("Eject"),
                                    [this] { ejectFloppy(); }, panel);
    pv->addWidget(makeButtonRow({ m_floppyReload, m_floppyEject }, panel));

    /* Virtual disk C row + new/eject. */
    m_hddEdit = makeEdit(panel);
    m_hddBrowse = new RetroButton(QStringLiteral("..."),
                                  [this] { browseHdd(); }, panel);
    m_hddBrowse->setFixedWidth(48);
    pv->addWidget(makeRow(QStringLiteral("DISK  C:"), m_hddEdit,
                          m_hddBrowse, panel));
    m_hddNew = new RetroButton(QStringLiteral("New..."),
                               [this] { createNewHdd(); }, panel);
    m_hddEject = new RetroButton(QStringLiteral("Eject"),
                                 [this] { ejectHdd(); }, panel);
    pv->addWidget(makeButtonRow({ m_hddNew, m_hddEject }, panel));

    /* Mount / Restart. */
    m_mount = new RetroButton(QStringLiteral("MOUNT / RESTART"),
                              [this] { applyConfig(); }, panel, RetroButton::Kind::Push);
    m_mount->setFixedHeight(30);
    m_mount->setLook(Retro::kTitle, Retro::kFaceLight);
    pv->addWidget(m_mount);

    /* Speed selector. */
    QLabel *speedHeader = new QLabel(QStringLiteral("SPEED"), panel);
    speedHeader->setStyleSheet(QStringLiteral(
        "color:#000080;font-weight:bold;font-family:'monospace';font-size:13px;"));
    pv->addSpacing(6);
    pv->addWidget(speedHeader);
    QWidget *speedRow = new QWidget(panel);
    QHBoxLayout *sh = new QHBoxLayout(speedRow);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(6);
    m_speed1 = new RetroButton(QStringLiteral("1x"), [this] { setSpeed(1); }, speedRow,
                               RetroButton::Kind::Toggle, true);
    m_speed2 = new RetroButton(QStringLiteral("2x"), [this] { setSpeed(2); }, speedRow,
                               RetroButton::Kind::Toggle);
    m_speed4 = new RetroButton(QStringLiteral("4x"), [this] { setSpeed(4); }, speedRow,
                               RetroButton::Kind::Toggle);
    m_speed8 = new RetroButton(QStringLiteral("8x"), [this] { setSpeed(8); }, speedRow,
                               RetroButton::Kind::Toggle);
    for (RetroButton *b : { m_speed1, m_speed2, m_speed4, m_speed8 }) {
        sh->addWidget(b, 1);
    }
    pv->addWidget(speedRow);

    /* Status box (sunken). */
    m_status = new QLabel(panel);
    m_status->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#ffffff;color:#000000;"
        "border:2px solid;border-top-color:#000080;border-left-color:#000080;"
        "border-bottom-color:#ffffff;border-right-color:#ffffff;"
        "padding:3px;font-family:'monospace';font-size:12px;}"
        "QLabel{background:#ffffff;color:#000000;font-family:'monospace';font-size:12px;}"));
    m_status->setStyleSheet(QStringLiteral(
        "QLabel{background:#ffffff;color:#000000;"
        "border:2px solid;border-top-color:#000080;border-left-color:#000080;"
        "border-bottom-color:#ffffff;border-right-color:#ffffff;"
        "padding:4px;font-family:'monospace';font-size:12px;}"));
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    pv->addWidget(m_status, 1);

    /* Title bar buttons (child widgets, positioned in placeTitleButtons). */
    m_minBtn = new RetroButton(RetroButton::Glyph::Minimize,
                               [this] { showMinimized(); }, this);
    m_maxBtn = new RetroButton(RetroButton::Glyph::Maximize,
                               [this] { toggleMaximized(); }, this);
    m_closeBtn = new RetroButton(RetroButton::Glyph::Close,
                                 [this] { close(); }, this);

    /* Content layout: CGA + panel, below the custom title band. */
    QHBoxLayout *content = new QHBoxLayout(this);
    content->setContentsMargins(kMargin, kTitleH + kMargin, kMargin, kMargin);
    content->setSpacing(kMargin);
    content->addWidget(m_cga, 1);
    content->addWidget(panel, 0);

    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, [this] { runSlice(); });

    m_biosEdit->setText(defaultBiosPath());
    m_floppyEdit->setText(QString());
    m_hddEdit->setText(QString());
    updateSpeedButtons();
    updateStatus();
}

MainWindow::~MainWindow()
{
    m_timer.stop();
    if (m_fe) {
        sim_fe_destroy(m_fe);
        m_fe = nullptr;
    }
}

/* ------------------------------------------------------------------ */
/* Keyboard mapping                                                    */
/* ------------------------------------------------------------------ */

bool MainWindow::mapSet1(int key, bool &extended, uint8_t &code)
{
    extended = false;

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        code = static_cast<uint8_t>(0x02 + (key - Qt::Key_0));
        return true;
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        static const uint8_t letters[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24,
            0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14,
            0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
        };
        code = letters[key - Qt::Key_A];
        return true;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F10) {
        code = static_cast<uint8_t>(0x3B + (key - Qt::Key_F1));
        return true;
    }
    if (key == Qt::Key_F11) { code = 0x57; return true; }
    if (key == Qt::Key_F12) { code = 0x58; return true; }

    switch (key) {
    case Qt::Key_Escape:     code = 0x01; return true;
    case Qt::Key_Backspace:  code = 0x0E; return true;
    case Qt::Key_Tab:        code = 0x0F; return true;
    case Qt::Key_Return:     code = 0x1C; return true;
    case Qt::Key_Enter:      code = 0x1C; return true;
    case Qt::Key_Space:      code = 0x39; return true;
    case Qt::Key_CapsLock:   code = 0x3A; return true;
    case Qt::Key_NumLock:    code = 0x45; return true;
    case Qt::Key_ScrollLock: code = 0x46; return true;
    case Qt::Key_Shift:      code = 0x2A; return true;
    case Qt::Key_Control:    code = 0x1D; return true;
    case Qt::Key_Alt:        code = 0x38; return true;

    case Qt::Key_Minus:      code = 0x0C; return true;
    case Qt::Key_Equal:      code = 0x0D; return true;
    case Qt::Key_BracketLeft:  code = 0x1A; return true;
    case Qt::Key_BracketRight: code = 0x1B; return true;
    case Qt::Key_Backslash:  code = 0x2B; return true;
    case Qt::Key_Semicolon:  code = 0x27; return true;
    case Qt::Key_Apostrophe: code = 0x28; return true;
    case Qt::Key_QuoteLeft:  code = 0x29; return true;
    case Qt::Key_Comma:      code = 0x33; return true;
    case Qt::Key_Period:     code = 0x34; return true;
    case Qt::Key_Slash:      code = 0x35; return true;

    /* Extended set (prefixed by 0xE0). */
    case Qt::Key_Home:     extended = true; code = 0x47; return true;
    case Qt::Key_End:      extended = true; code = 0x4F; return true;
    case Qt::Key_PageUp:   extended = true; code = 0x49; return true;
    case Qt::Key_PageDown: extended = true; code = 0x51; return true;
    case Qt::Key_Insert:   extended = true; code = 0x52; return true;
    case Qt::Key_Delete:   extended = true; code = 0x53; return true;
    case Qt::Key_Left:     extended = true; code = 0x4B; return true;
    case Qt::Key_Up:       extended = true; code = 0x48; return true;
    case Qt::Key_Right:    extended = true; code = 0x4D; return true;
    case Qt::Key_Down:     extended = true; code = 0x50; return true;

    default:
        return false;
    }
}

void MainWindow::injectKey(int key, bool down)
{
    if (!m_fe) return;
    bool extended;
    uint8_t code;
    if (!mapSet1(key, extended, code)) return;
    if (extended) sim_fe_inject_scancode(m_fe, 0xE0);
    sim_fe_inject_scancode(m_fe, down ? code : static_cast<uint8_t>(code | 0x80));
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    injectKey(event->key(), true);
    QWidget::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    injectKey(event->key(), false);
    QWidget::keyReleaseEvent(event);
}

/* ------------------------------------------------------------------ */
/* Engine driving                                                      */
/* ------------------------------------------------------------------ */

QString MainWindow::defaultBiosPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("pc_compat_bios.bin")),
        QDir(appDir).filePath(QStringLiteral("firmware/pc_compat_bios.bin")),
        QDir(appDir).filePath(QStringLiteral("../firmware/pc_compat_bios.bin")),
        QDir::current().filePath(QStringLiteral("firmware/pc_compat_bios.bin"))
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) return QDir::cleanPath(c);
    }
    return QDir::cleanPath(candidates.last());
}

void MainWindow::applyConfig()
{
    m_timer.stop();
    if (m_fe) {
        sim_fe_destroy(m_fe);   /* flush a writable disk first. */
        m_fe = nullptr;
    }
    m_running = false;
    m_error.clear();

    const QString bios = m_biosEdit->text().trimmed();
    if (bios.isEmpty() || !QFileInfo::exists(bios)) {
        showError(QStringLiteral("Select a valid BIOS ROM file."));
        updateStatus();
        return;
    }

    /* sim_fe_create() copies the path strings, but only while it runs, so the
     * UTF-8 byte buffers must stay alive until just after that call. */
    QByteArray biosUtf8 = bios.toUtf8();
    QByteArray floppyUtf8 = m_floppyEdit->text().trimmed().toUtf8();
    QByteArray hddUtf8 = m_hddEdit->text().trimmed().toUtf8();

    SimFeConfig cfg = {};
    cfg.bios_path = biosUtf8.constData();
    cfg.floppy_path = floppyUtf8.isEmpty() ? nullptr : floppyUtf8.constData();
    cfg.hdd_path = hddUtf8.isEmpty() ? nullptr : hddUtf8.constData();
    cfg.create_hdd = m_createHdd;
    cfg.hdd_cylinders = m_hddCylinders;
    cfg.hdd_heads = m_hddHeads;
    cfg.hdd_sectors = m_hddSectors;
    cfg.speed_multiplier = m_speed;
    cfg.trace_enabled = false;
    SimRamConfig ram = {};
    cfg.ram_config = ram;

    m_fe = sim_fe_create(&cfg);
    m_createHdd = false;   /* one-shot: only for the next create. */
    if (!m_fe) {
        showError(QStringLiteral("Unable to initialize the simulated PC."));
        updateStatus();
        return;
    }
    sim_fe_set_speed(m_fe, m_speed);
    SimFeStatus status = {};
    sim_fe_status(m_fe, &status);
    m_running = status.running;
    if (m_running) m_timer.start();
    else showError(QStringLiteral("Mount a floppy image, a disk image, or both."));
    updateStatus();
}

void MainWindow::runSlice()
{
    if (!m_fe || !m_running) return;
    SimFeRunResult result = sim_fe_run_slice(m_fe, 2048);
    if (result == SIM_FE_OK) {
        const SimCgaRenderFrame *frame = sim_fe_frame(m_fe);
        if (frame && m_cga) m_cga->setFrame(*frame);
        updateStatus();
    } else if (result == SIM_FE_CPU_FAULT) {
        m_running = false;
        m_timer.stop();
        showError(QStringLiteral("CPU faulted."));
        updateStatus();
    } else {
        m_running = false;
        m_timer.stop();
        m_error = QString::fromUtf8(sim_fe_last_error(m_fe));
        updateStatus();
    }
}

void MainWindow::updateStatus()
{
    if (!m_status) return;
    SimFeStatus status = {};
    if (m_fe) sim_fe_status(m_fe, &status);

    QString s;
    s += QStringLiteral("A: %1   C: %2\n")
             .arg(status.floppy_present ? QStringLiteral("mounted") : QStringLiteral("empty"))
             .arg(status.hdd_present ? QStringLiteral("mounted") : QStringLiteral("empty"));
    s += QStringLiteral("Running: %1\n").arg(m_running ? QStringLiteral("yes") : QStringLiteral("no"));
    s += QStringLiteral("Speed: %1x\n").arg(m_speed);
    if (status.int13_result != 0xFF) {
        const char *rs = status.int13_result == 0 ? "ok" :
                         status.int13_result == 1 ? "fail" : "pending";
        s += QStringLiteral("INT13h %1  AH %2  AL %3\nC=%4 H=%5 S=%6  DL %7\n")
                 .arg(QString::fromLatin1(rs))
                 .arg(status.int13_ah, 2, 16, QChar('0'))
                 .arg(status.int13_al, 2, 16, QChar('0'))
                 .arg(status.int13_ch)
                 .arg(status.int13_dh)
                 .arg(status.int13_cl)
                 .arg(status.int13_dl);
    }
    if (!m_error.isEmpty()) {
        s += QStringLiteral("\nError: %1").arg(m_error);
    }
    m_status->setText(s);
}

void MainWindow::showError(const QString &message)
{
    m_error = message;
    updateStatus();
}

/* ------------------------------------------------------------------ */
/* Media operations                                                    */
/* ------------------------------------------------------------------ */

void MainWindow::browseBios()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select BIOS ROM"), m_biosEdit->text(),
        QStringLiteral("BIOS ROM (*.bin *.rom);;All files (*)"));
    if (!path.isEmpty()) m_biosEdit->setText(path);
}

void MainWindow::browseFloppy()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select floppy image"), m_floppyEdit->text(),
        QStringLiteral("Floppy images (*.img *.ima *.vfd);;All files (*)"));
    if (!path.isEmpty()) m_floppyEdit->setText(path);
}

void MainWindow::browseHdd()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select raw disk image"), m_hddEdit->text(),
        QStringLiteral("Raw disk images (*.img *.ima);;All files (*)"));
    if (!path.isEmpty()) m_hddEdit->setText(path);
}

void MainWindow::reloadFloppy()
{
    if (!m_fe) { showError(QStringLiteral("Start the machine first.")); return; }
    const QString path = m_floppyEdit->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        showError(QStringLiteral("Select an existing floppy image."));
        return;
    }
    if (!sim_fe_mount_floppy(m_fe, path.toUtf8().constData())) {
        m_error = QString::fromUtf8(sim_fe_last_error(m_fe));
    }
    updateStatus();
}

void MainWindow::ejectFloppy()
{
    if (!m_fe) { showError(QStringLiteral("Start the machine first.")); return; }
    if (!sim_fe_eject_floppy(m_fe)) m_error = QString::fromUtf8(sim_fe_last_error(m_fe));
    m_floppyEdit->clear();
    updateStatus();
}

void MainWindow::ejectHdd()
{
    if (!m_fe) { showError(QStringLiteral("Start the machine first.")); return; }
    if (!sim_fe_eject_hdd(m_fe)) m_error = QString::fromUtf8(sim_fe_last_error(m_fe));
    m_hddEdit->clear();
    updateStatus();
}

void MainWindow::createNewHdd()
{
    if (!m_fe) { showError(QStringLiteral("Start the machine first.")); return; }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Create new virtual disk"), m_hddEdit->text(),
        QStringLiteral("Raw disk images (*.img);;All files (*)"));
    if (path.isEmpty()) return;
    if (!sim_fe_create_hdd(m_fe, path.toUtf8().constData(),
                           m_hddCylinders, m_hddHeads, m_hddSectors)) {
        showError(QString::fromUtf8(sim_fe_last_error(m_fe)));
    } else {
        m_hddEdit->setText(path);
        updateStatus();
    }
}

void MainWindow::setSpeed(unsigned multiplier)
{
    m_speed = multiplier;
    if (m_fe) sim_fe_set_speed(m_fe, m_speed);
    updateSpeedButtons();
    updateStatus();
}

void MainWindow::updateSpeedButtons()
{
    if (m_speed1) m_speed1->setActive(m_speed == 1);
    if (m_speed2) m_speed2->setActive(m_speed == 2);
    if (m_speed4) m_speed4->setActive(m_speed == 4);
    if (m_speed8) m_speed8->setActive(m_speed == 8);
}

/* ------------------------------------------------------------------ */
/* Window chrome                                                       */
/* ------------------------------------------------------------------ */

void MainWindow::placeTitleButtons()
{
    const int y = 4;
    const int h = kTitleH - 2 * y;
    const int w = 34;
    const int right = width();
    if (m_closeBtn) m_closeBtn->setGeometry(right - kMargin - w, y, w, h);
    if (m_maxBtn)   m_maxBtn->setGeometry(right - kMargin - 2 * w - 2, y, w, h);
    if (m_minBtn)   m_minBtn->setGeometry(right - kMargin - 3 * w - 4, y, w, h);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    /* Page background. */
    p.fillRect(rect(), Retro::kWindow);

    /* Title band. */
    const QRect band(0, 0, width(), kTitleH);
    p.fillRect(band, Retro::kTitle);
    p.fillRect(QRect(0, 0, width(), 2), Retro::kTitleTop);
    p.fillRect(QRect(0, kTitleH - 2, width(), 2), Retro::kFaceDark);

    /* Tiny pixel-art logo. */
    const QRect logo(kMargin + 8, (kTitleH - 16) / 2, 16, 16);
    p.fillRect(logo, Retro::kTitleTop);
    p.fillRect(logo.adjusted(2, 2, -2, -2), Retro::kFaceLight);
    p.fillRect(logo.adjusted(4, 4, -4, -4), Retro::kFaceDark);

    /* Title text. */
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setFamily(QStringLiteral("monospace"));
    f.setPointSizeF(11.0);
    p.setFont(f);
    p.drawText(QRect(logo.right() + 8, 0, 280, kTitleH),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("8086 PC SIM"));

    /* Hard window frame. */
    Retro::drawWindowFrame(p, rect());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    placeTitleButtons();
    QWidget::resizeEvent(event);
}

/* ------------------------------------------------------------------ */
/* Drag / resize / maximize                                            */
/* ------------------------------------------------------------------ */

MainWindow::Drag MainWindow::hitTest(const QPoint &pos)
{
    const int e = 6;
    const bool left = pos.x() < e;
    const bool right = pos.x() >= width() - e;
    const bool top = pos.y() < e;
    const bool bottom = pos.y() >= height() - e;

    if (top && left) return Drag::TopLeft;
    if (top && right) return Drag::TopRight;
    if (bottom && left) return Drag::BottomLeft;
    if (bottom && right) return Drag::BottomRight;
    if (top) return Drag::Top;
    if (bottom) return Drag::Bottom;
    if (left) return Drag::Left;
    if (right) return Drag::Right;
    if (pos.y() < kTitleH) return Drag::Move;
    return Drag::None;
}

void MainWindow::updateCursor(const QPoint &pos)
{
    switch (hitTest(pos)) {
    case Drag::Left:
    case Drag::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case Drag::Top:
    case Drag::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case Drag::TopLeft:
    case Drag::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case Drag::TopRight:
    case Drag::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case Drag::Move:
        setCursor(Qt::SizeAllCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_drag = hitTest(event->pos());
        if (m_drag != Drag::None) {
            if (m_drag == Drag::Move && m_maximized) toggleMaximized();
            m_dragStart = event->globalPosition().toPoint();
            m_dragStartGeom = geometry();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == Drag::None) {
        updateCursor(event->pos());
        return;
    }
    const QPoint global = event->globalPosition().toPoint();
    const QPoint delta = global - m_dragStart;

    if (m_drag == Drag::Move) {
        move(m_dragStartGeom.topLeft() + delta);
        return;
    }

    QRect g = m_dragStartGeom;
    switch (m_drag) {
    case Drag::Left:
    case Drag::TopLeft:
    case Drag::BottomLeft:
        g.setLeft(m_dragStartGeom.left() + delta.x());
        break;
    case Drag::Right:
    case Drag::TopRight:
    case Drag::BottomRight:
        g.setRight(m_dragStartGeom.right() + delta.x());
        break;
    default:
        break;
    }
    switch (m_drag) {
    case Drag::Top:
    case Drag::TopLeft:
    case Drag::TopRight:
        g.setTop(m_dragStartGeom.top() + delta.y());
        break;
    case Drag::Bottom:
    case Drag::BottomLeft:
    case Drag::BottomRight:
        g.setBottom(m_dragStartGeom.bottom() + delta.y());
        break;
    default:
        break;
    }
    setGeometry(g.normalized());
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_drag = Drag::None;
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton &&
        event->pos().y() < kTitleH && hitTest(event->pos()) == Drag::Move) {
        toggleMaximized();
    }
}

void MainWindow::toggleMaximized()
{
    if (!m_maximized) {
        m_restoreGeom = geometry();
        setGeometry(screen()->availableGeometry());
        m_maximized = true;
    } else {
        setGeometry(m_restoreGeom);
        m_maximized = false;
    }
}

/* ------------------------------------------------------------------ */
/* Shutdown                                                            */
/* ------------------------------------------------------------------ */

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_timer.stop();
    if (m_fe) {
        sim_fe_destroy(m_fe);   /* flush a writable disk image. */
        m_fe = nullptr;
    }
    QWidget::closeEvent(event);
}

/* ------------------------------------------------------------------ */
/* Command-line arguments                                              */
/* ------------------------------------------------------------------ */

void MainWindow::applyArguments(int argc, char **argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QStringLiteral("--bios") && i + 1 < args.size()) {
            m_biosEdit->setText(args.at(++i));
        } else if (a == QStringLiteral("--floppy") && i + 1 < args.size()) {
            m_floppyEdit->setText(args.at(++i));
        } else if (a == QStringLiteral("--hdd") && i + 1 < args.size()) {
            m_hddEdit->setText(args.at(++i));
        } else if (a == QStringLiteral("--create-hdd") && i + 1 < args.size()) {
            m_hddEdit->setText(args.at(++i));
            m_createHdd = true;
        } else if (a == QStringLiteral("--run")) {
            m_runOnStart = true;
        } else if (!a.startsWith(QLatin1Char('-'))) {
            if (m_floppyEdit->text().isEmpty()) m_floppyEdit->setText(a);
        }
    }
    if (m_runOnStart) applyConfig();
}
