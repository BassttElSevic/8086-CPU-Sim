#include <QApplication>

#include "main_window.h"

/*
 * 8086-PC-Sim retro frontend entry point.
 *
 * A frameless, self-drawn pixel/blue-white window that drives the simulation
 * engine exclusively through the frontend API (sim/sim_frontend.h).  It is a
 * Qt6 Widgets application (Core/Gui/Widgets only) and deliberately needs no
 * moc/uic step, so it builds from source with a C++ compiler + pkg-config.
 */
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("8086-PC-Sim"));
    app.setOrganizationName(QStringLiteral("8086-CPU-Sim"));

    MainWindow window;
    window.applyArguments(argc, argv);
    window.show();
    return app.exec();
}
