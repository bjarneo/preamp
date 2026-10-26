#include "cli.h"

#include "daemon.h"
#include "ipc.h"
#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace cli {

namespace {

const QStringList Commands{"daemon", "status", "preset", "presets", "bypass", "output", "outputs", "volume",
                           "band", "autoeq", "pin", "unpin", "shell", "skill", "help", "--help", "-h", "version",
                           "--version"};

void out(const QString &text) {
    std::fputs(qPrintable(text + "\n"), stdout);
}

void err(const QString &text) {
    std::fputs(qPrintable("preamp: " + text + "\n"), stderr);
}

QString formatGain(double g) {
    const QString sign = g > 0.04 ? "+" : g < -0.04 ? "-" : "";
    return sign + QString::number(std::abs(g), 'f', 1);
}

// Connects to the daemon, starting it through systemd when it is not up.
bool connectDaemon(ipc::Client &client) {
    client.connectToDaemon();
    if (client.waitForConnected(500))
        return true;
    QProcess::execute("systemctl", {"--user", "start", "preamp.service"});
    for (int i = 0; i < 20; ++i) {
        client.connectToDaemon();
        if (client.waitForConnected(250))
            return true;
        QThread::msleep(100);
    }
    err("the preamp daemon is not running. Start it with: systemctl --user enable --now preamp.service");
    return false;
}

int finish(const QJsonObject &reply, bool json, const QString &done) {
    if (json)
        out(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Indented)).trimmed());
    if (!reply.value("ok").toBool()) {
        if (!json)
            err(reply.value("error").toString("failed"));
        return 1;
    }
    if (!json && !done.isEmpty())
        out(done);
    return 0;
}

QString shellPluginDir() {
    return QDir::homePath() + "/.config/omarchy/plugins/preamp";
}

int installShell(bool json) {
    // The plugin ships inside the binary, under :/shell.
    const QString dir = shellPluginDir();
    QDir().mkpath(dir);
    int written = 0;
    // True when a file the shell may already have compiled changes or goes.
    bool replaced = false;
    QSet<QString> shipped;
    QDirIterator it(":/shell", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString to = dir + from.mid(QStringLiteral(":/shell").size());
        shipped.insert(to);
        QFile in(from);
        if (!in.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = in.readAll();
        // An unchanged file is left alone, so the shell does not reload for it.
        if (QFile current(to); current.open(QIODevice::ReadOnly)) {
            if (current.readAll() == data)
                continue;
            replaced = true;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        QSaveFile file(to);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
            err(QStringLiteral("could not write %1").arg(file.fileName()));
            return 1;
        }
        ++written;
    }
    // A file from an older version can shadow a type the widget uses now.
    QDirIterator old(dir, QDir::Files, QDirIterator::Subdirectories);
    while (old.hasNext()) {
        const QString path = old.next();
        if (!shipped.contains(path) && QFile::remove(path))
            replaced = true;
    }
    // Put the widget on the bar next to the audio widget. When it is already
    // there, the bar keeps it where it is.
    // The shell finds a new plugin a moment after its files land, so try a
    // few times before giving up.
    bool placed = false;
    for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
        if (attempt > 0)
            QThread::msleep(500);
        for (const QStringList &args : {QStringList{"bar", "put", "preamp", "--before", "omarchy.audio"},
                                        QStringList{"bar", "put", "preamp", "--section", "right"}}) {
            QProcess bar;
            bar.start("omarchy", args);
            if (bar.waitForFinished(5000) && bar.exitStatus() == QProcess::NormalExit && bar.exitCode() == 0) {
                placed = true;
                break;
            }
        }
    }
    // The shell compiles a plugin's files once and keeps them until it
    // restarts. Its plugin reload does not drop them, so a changed widget
    // shows only after a restart. The restart fails while the screen is
    // locked.
    bool restarted = false;
    if (replaced) {
        QProcess restart;
        restart.start("omarchy", {"restart", "shell"});
        restarted = restart.waitForFinished(30000) && restart.exitStatus() == QProcess::NormalExit &&
                    restart.exitCode() == 0;
    }
    if (json) {
        out(QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true},
                                                        {"dir", dir},
                                                        {"files", written},
                                                        {"bar", placed},
                                                        {"restarted", restarted}})
                                  .toJson(QJsonDocument::Compact)));
        return 0;
    }
    out(placed ? QStringLiteral("Installed the bar widget in %1 and put it on the bar.").arg(dir)
               : QStringLiteral("Installed the bar widget in %1. Add it to the bar with: omarchy bar put preamp").arg(dir));
    if (restarted)
        out("Restarted the Omarchy shell to load the new widget.");
    else if (replaced)
        out("The shell shows the old widget until it restarts. Run: omarchy restart shell");
    return 0;
}

int installSkill(bool json) {
    const QString dir = QDir::homePath() + "/.claude/skills/preamp";
    QDir().mkpath(dir);
    QFile in(":/skill/SKILL.md");
    QSaveFile file(dir + "/SKILL.md");
    if (!in.open(QIODevice::ReadOnly) || !file.open(QIODevice::WriteOnly) || file.write(in.readAll()) < 0 || !file.commit()) {
        err(QStringLiteral("could not write %1/SKILL.md").arg(dir));
        return 1;
    }
    if (json)
        out(QStringLiteral("{\"ok\":true,\"path\":\"%1/SKILL.md\"}").arg(dir));
    else
        out(QStringLiteral("Installed the preamp skill in %1.").arg(dir));
    return 0;
}

int sigFd[2] = {-1, -1};

void onSignal(int) {
    const char c = 1;
    [[maybe_unused]] auto n = ::write(sigFd[0], &c, 1);
}

} // namespace

bool handles(const QString &command) { return Commands.contains(command); }

void printHelp() {
    out(QStringLiteral(
        "preamp: system audio equalizer for Omarchy\n"
        "\n"
        "Usage:\n"
        "  preamp                    open the window\n"
        "  preamp daemon             run the audio service (systemd starts it)\n"
        "  preamp status             show what plays where\n"
        "  preamp presets            list presets\n"
        "  preamp preset NAME        apply a preset\n"
        "  preamp bypass [on|off]    switch the equalizer off and on\n"
        "  preamp outputs            list outputs\n"
        "  preamp output NAME        play through an output\n"
        "  preamp volume [0-150]     show or set the output volume\n"
        "  preamp band N [--freq F] [--gain G] [--q Q]\n"
        "  preamp autoeq QUERY       apply the AutoEq profile for a headphone\n"
        "  preamp pin | unpin        pin the preset to the output, or drop the pin\n"
        "  preamp shell install      put the preamp widget on the Omarchy bar\n"
        "  preamp skill install      teach coding agents to drive preamp\n"
        "\n"
        "Add --json to any command for machine-readable output."));
}

int runDaemon() {
    // Notifications speak the Omarchy language, like the window.
    Translator translator;
    QCoreApplication::installTranslator(&translator);

    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/preamp";
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/preamp";
    Daemon daemon(config, cache);
    QString error;
    if (!daemon.start(&error)) {
        err(error);
        return 1;
    }
    // Stop cleanly on SIGTERM and SIGINT, so the system output goes back to
    // the device preamp was playing into.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) == 0) {
        auto *notifier = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &daemon);
        QObject::connect(notifier, &QSocketNotifier::activated, &daemon, [&daemon] {
            char c;
            [[maybe_unused]] auto n = ::read(sigFd[1], &c, 1);
            daemon.stop();
            QCoreApplication::quit();
        });
        std::signal(SIGTERM, onSignal);
        std::signal(SIGINT, onSignal);
    }
    return QCoreApplication::exec();
}

int run(const QStringList &argv) {
    QStringList args = argv;
    const bool json = args.removeAll("--json") > 0;
    const QString command = args.value(0);

    if (command == "help" || command == "--help" || command == "-h") {
        printHelp();
        return 0;
    }
    if (command == "version" || command == "--version") {
        out(QCoreApplication::applicationVersion());
        return 0;
    }
    if (command == "daemon")
        return runDaemon();
    if (command == "shell") {
        if (args.value(1) != "install") {
            err("usage: preamp shell install");
            return 1;
        }
        return installShell(json);
    }
    if (command == "skill") {
        if (args.value(1) != "install") {
            err("usage: preamp skill install");
            return 1;
        }
        return installSkill(json);
    }

    ipc::Client client;
    if (!connectDaemon(client))
        return 1;
    auto send = [&](const QJsonObject &m) { return client.request(m); };
    const QJsonObject state = send({{"cmd", "state"}}).value("state").toObject();

    if (command == "status") {
        if (json) {
            out(QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Indented)).trimmed());
            return 0;
        }
        out(QStringLiteral("preset   %1%2").arg(state.value("preset").toString(), state.value("dirty").toBool() ? " (edited)" : ""));
        out(QStringLiteral("eq       %1").arg(state.value("bypass").toBool() ? "bypassed" : "active"));
        out(QStringLiteral("preamp   %1 dB").arg(formatGain(state.value("preamp").toDouble())));
        out(QStringLiteral("output   %1").arg(state.value("outputLabel").toString()));
        out(QStringLiteral("volume   %1%").arg(state.value("volume").toInt()));
        out(QStringLiteral("room     %1, width %2%").arg(state.value("room").toString()).arg(state.value("width").toInt()));
        return 0;
    }
    if (command == "presets") {
        if (json) {
            out(QString::fromUtf8(QJsonDocument(state.value("presets").toArray()).toJson(QJsonDocument::Indented)).trimmed());
            return 0;
        }
        for (const auto &p : state.value("presets").toArray()) {
            const QString name = p.toObject().value("name").toString();
            out((name == state.value("preset").toString() ? "* " : "  ") + name);
        }
        return 0;
    }
    if (command == "preset") {
        const QString name = args.mid(1).join(' ');
        if (name.isEmpty()) {
            err("usage: preamp preset NAME");
            return 1;
        }
        return finish(send({{"cmd", "applyPreset"}, {"name", name}}), json, QStringLiteral("Applied %1.").arg(name));
    }
    if (command == "bypass") {
        QJsonObject m{{"cmd", "setBypass"}};
        if (args.value(1) == "on")
            m.insert("on", true);
        else if (args.value(1) == "off")
            m.insert("on", false);
        const QJsonObject r = send(m);
        return finish(r, json, r.value("bypass").toBool() ? "The equalizer is bypassed." : "The equalizer is on.");
    }
    if (command == "outputs") {
        if (json) {
            out(QString::fromUtf8(QJsonDocument(state.value("outputs").toArray()).toJson(QJsonDocument::Indented)).trimmed());
            return 0;
        }
        for (const auto &v : state.value("outputs").toArray()) {
            const QJsonObject o = v.toObject();
            const bool current = o.value("name").toString() == state.value("output").toString();
            out(QStringLiteral("%1%2  %3 · %4  (%5)")
                    .arg(current ? "* " : "  ", o.value("label").toString(), o.value("kind").toString(),
                         o.value("rate").toString(), o.value("name").toString()));
        }
        return 0;
    }
    if (command == "output") {
        const QString wanted = args.mid(1).join(' ');
        QString name;
        for (const auto &v : state.value("outputs").toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value("name").toString() == wanted || o.value("label").toString().compare(wanted, Qt::CaseInsensitive) == 0)
                name = o.value("name").toString();
        }
        if (name.isEmpty()) {
            err(QStringLiteral("no output named %1. List them with: preamp outputs").arg(wanted));
            return 1;
        }
        return finish(send({{"cmd", "setOutput"}, {"name", name}}), json, QStringLiteral("Playing through %1.").arg(wanted));
    }
    if (command == "volume") {
        if (args.size() < 2) {
            if (json)
                out(QStringLiteral("{\"volume\":%1}").arg(state.value("volume").toInt()));
            else
                out(QStringLiteral("%1%").arg(state.value("volume").toInt()));
            return 0;
        }
        bool ok = false;
        const double v = QString(args[1]).remove('%').toDouble(&ok);
        if (!ok || v < 0 || v > 150) {
            err("volume is 0 to 150");
            return 1;
        }
        return finish(send({{"cmd", "setVolume"}, {"v", v}}), json, QStringLiteral("Volume %1%.").arg(v));
    }
    if (command == "band") {
        bool ok = false;
        const int n = args.value(1).toInt(&ok);
        if (!ok || n < 1 || n > 10) {
            err("usage: preamp band 1-10 [--freq HZ] [--gain DB] [--q Q]");
            return 1;
        }
        QJsonObject m{{"cmd", "setBand"}, {"i", n - 1}};
        for (int i = 2; i + 1 < args.size(); i += 2) {
            const QString key = args[i] == "--freq" ? "f" : args[i] == "--gain" ? "g" : args[i] == "--q" ? "q" : QString();
            if (key.isEmpty()) {
                err(QStringLiteral("unknown option %1").arg(args[i]));
                return 1;
            }
            m.insert(key, args[i + 1].toDouble());
        }
        return finish(send(m), json, QStringLiteral("Band %1 set.").arg(n));
    }
    if (command == "pin" || command == "unpin")
        return finish(send({{"cmd", command}}), json,
                      command == "pin" ? QStringLiteral("Pinned %1 to %2.").arg(state.value("preset").toString(),
                                                                                  state.value("outputLabel").toString())
                                       : QStringLiteral("Dropped the pin on %1.").arg(state.value("outputLabel").toString()));
    if (command == "autoeq") {
        const QString query = args.mid(1).join(' ');
        if (query.isEmpty()) {
            err("usage: preamp autoeq QUERY");
            return 1;
        }
        // Search replies arrive as messages; wait for one without "pending".
        QJsonObject found;
        QEventLoop loop;
        QObject::connect(&client, &ipc::Client::message, &loop, [&](const QJsonObject &m) {
            if (m.value("type").toString() != "search" || m.value("pending").toBool())
                return;
            found = m;
            loop.quit();
        });
        QTimer::singleShot(25000, &loop, &QEventLoop::quit);
        client.send({{"cmd", "search"}, {"q", query}});
        loop.exec();
        QJsonObject pick;
        for (const auto &v : found.value("results").toArray())
            if (pick.isEmpty() && v.toObject().value("s").toString() == "autoeq")
                pick = v.toObject();
        if (pick.isEmpty()) {
            const QString why = found.value("error").toString();
            err(why.isEmpty() ? QStringLiteral("no AutoEq profile matches %1").arg(query)
                              : QStringLiteral("could not reach AutoEq: %1").arg(why));
            return 1;
        }
        if (!pick.contains("path"))
            return finish(send({{"cmd", "applyPreset"}, {"name", pick.value("n").toString()}}), json,
                          QStringLiteral("Applied %1.").arg(pick.value("n").toString()));
        const QJsonObject r = client.request({{"cmd", "autoeq"}, {"path", pick.value("path").toString()}}, 30000);
        return finish(r, json, QStringLiteral("Applied %1.").arg(r.value("preset").toString()));
    }
    err(QStringLiteral("unknown command %1").arg(command));
    printHelp();
    return 1;
}

} // namespace cli
