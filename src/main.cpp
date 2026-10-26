// preamp: a system audio equalizer for Omarchy.
//
// One binary: with no arguments it opens the window, `preamp daemon` runs the
// audio service, and the other commands drive that service from a terminal.

#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "cli.h"
#include "controller.h"
#include "graph.h"
#include "theme.h"
#include "translator.h"

namespace {

const char *Version = "0.1.1";

QString windowSocket() {
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return dir + "/preamp-window.sock";
}

// A second `preamp` brings the open window forward instead of opening
// another, passing its activation token so the compositor allows it.
bool forwardToRunningWindow() {
    QLocalSocket socket;
    socket.connectToServer(windowSocket());
    if (!socket.waitForConnected(200))
        return false;
    socket.write(qgetenv("XDG_ACTIVATION_TOKEN") + "\n");
    socket.waitForBytesWritten(200);
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    // --json can come before the command too.
    const QString command = args.value(args.value(0) == "--json" ? 1 : 0);

    if (cli::handles(command)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("preamp");
        app.setApplicationVersion(Version);
        return cli::run(args);
    }
    if (!command.isEmpty() && command != "open") {
        QCoreApplication app(argc, argv);
        cli::printHelp();
        return 1;
    }
    if (forwardToRunningWindow())
        return 0;

    QGuiApplication app(argc, argv);
    app.setOrganizationName("preamp");
    app.setApplicationName("preamp");
    app.setApplicationVersion(Version);
    // Matches the window to preamp.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("preamp");
    app.setWindowIcon(QIcon::fromTheme("preamp"));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    // PREAMP_LANGUAGE_FILE stands in for Omarchy's language file, for
    // screenshots in another language.
    Translator translator(qEnvironmentVariable("PREAMP_LANGUAGE_FILE"));
    app.installTranslator(&translator);

    // PREAMP_THEME_DIR points at another Omarchy state directory, for
    // screenshots that should not depend on the current theme.
    Theme theme(qEnvironmentVariable("PREAMP_THEME_DIR"));
    QFont font(theme.font());
    font.setPixelSize(12);
    app.setFont(font);

    Controller controller;

    // Everything QML reads is made before the engine, so it outlives it.
    qmlRegisterType<GraphItem>("Preamp", 1, 0, "Graph");
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("preamp", &controller);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Screenshots for the README and the design comparison. PREAMP_VIEW picks
    // the view, for example studio, modes:graphic or chain. PREAMP_SEL picks
    // the band. PREAMP_SCREENSHOT saves the window to that file and quits.
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (const QString view = qEnvironmentVariable("PREAMP_VIEW"); !view.isEmpty() && window) {
        if (QObject *root = window->findChild<QObject *>("app")) {
            root->setProperty("view", view.section(':', 0, 0));
            if (view.contains(':'))
                root->setProperty("mode", view.section(':', 1));
            if (qEnvironmentVariableIsSet("PREAMP_SEL"))
                root->setProperty("sel", qEnvironmentVariableIntValue("PREAMP_SEL"));
        }
    }
    if (const QString shot = qEnvironmentVariable("PREAMP_SCREENSHOT"); !shot.isEmpty() && window) {
        QTimer::singleShot(qEnvironmentVariableIsSet("PREAMP_SCREENSHOT_DELAY") ? qEnvironmentVariableIntValue("PREAMP_SCREENSHOT_DELAY") : 1500,
                           window, [window, shot] {
                               window->grabWindow().save(shot);
                               QCoreApplication::quit();
                           });
    }

    QLocalServer server;
    QLocalServer::removeServer(windowSocket());
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(windowSocket());
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        QLocalSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [&engine, socket] {
            const QByteArray token = socket->readLine().trimmed();
            if (!token.isEmpty())
                qputenv("XDG_ACTIVATION_TOKEN", token);
            if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
                window->show();
                window->raise();
                window->requestActivate();
            }
            socket->deleteLater();
        });
    });

    return app.exec();
}
