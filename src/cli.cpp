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
#include <optional>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace cli {

namespace {

// Every action of the window and the bar widget has a command here. The
// daemon's socket takes the same actions; see docs/ipc.md.
const QStringList Commands{"daemon", "status", "watch", "preset", "presets", "save", "delete", "search", "autoeq",
                           "pin", "unpin", "bypass", "bands", "band", "tone", "channel", "output", "outputs",
                           "volume", "balance", "width", "room", "apps", "app", "plugins", "plugin", "rules",
                           "rule", "shell", "skill", "help", "--help", "-h", "version", "--version"};

void out(const QString &text) {
    std::fputs(qPrintable(text + "\n"), stdout);
}

void err(const QString &text) {
    std::fputs(qPrintable("preamp: " + text + "\n"), stderr);
}

QString pretty(const QJsonValue &value) {
    const QJsonDocument doc = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();
}

QString formatGain(double g) {
    const QString sign = g > 0.04 ? "+" : g < -0.04 ? "-" : "";
    return sign + QString::number(std::abs(g), 'f', 1);
}

QString formatFreq(double f) {
    return f < 1000 ? QStringLiteral("%1 Hz").arg(qRound(f)) : QStringLiteral("%1 kHz").arg(QString::number(f / 1000, 'g', 3));
}

QString formatBalance(int b) {
    return b == 0 ? QStringLiteral("center") : QStringLiteral("%1 %2").arg(std::abs(b)).arg(b < 0 ? "left" : "right");
}

// Band types by the names people use, and by the short names in the JSON.
QString bandType(const QString &name) {
    if (name == "peak" || name == "pk")
        return "pk";
    if (name == "lowshelf" || name == "ls")
        return "ls";
    if (name == "highshelf" || name == "hs")
        return "hs";
    return {};
}

QString bandTypeLabel(const QString &t) {
    return t == "ls" ? "lowshelf" : t == "hs" ? "highshelf" : "peak";
}

// Takes "--name VALUE" out of the arguments.
std::optional<QString> takeOption(QStringList &args, const QString &name) {
    const qsizetype i = args.indexOf(name);
    if (i < 0)
        return std::nullopt;
    const QString value = args.value(i + 1);
    args.remove(i, std::min<qsizetype>(2, args.size() - i));
    return value;
}

bool takeFlag(QStringList &args, const QString &name) {
    return args.removeAll(name) > 0;
}

// Reads a number in a range, or says what the range is.
bool number(const QString &text, double low, double high, const QString &what, double *value) {
    bool ok = false;
    *value = QString(text).remove('%').toDouble(&ok);
    if (ok && *value >= low && *value <= high)
        return true;
    err(QStringLiteral("%1 is %2 to %3").arg(what).arg(low).arg(high));
    return false;
}

// A control of an effect, as `preamp plugin params` lists it. A side
// without a bound comes as +/-FLT_MAX.
bool bounded(double v) { return std::abs(v) < 1e38; }

QString formatParam(const QJsonObject &param, double value) {
    if (param.value("toggled").toBool())
        return value > 0 ? "on" : "off";
    for (const auto &o : param.value("options").toArray())
        if (o.toObject().value("value").toDouble() == value)
            return o.toObject().value("label").toString();
    return QString::number(value, 'g', 5);
}

QString paramRange(const QJsonObject &param) {
    if (param.value("toggled").toBool())
        return "on or off";
    const QJsonArray options = param.value("options").toArray();
    if (!options.isEmpty()) {
        QStringList labels;
        for (const auto &o : options)
            labels << o.toObject().value("label").toString();
        return "(" + labels.join(", ") + ")";
    }
    const double min = param.value("min").toDouble(), max = param.value("max").toDouble();
    return QStringLiteral("(%1 to %2)")
        .arg(bounded(min) ? QString::number(min, 'g', 5) : "any", bounded(max) ? QString::number(max, 'g', 5) : "any");
}

// A number, on or off for a switch, or the label of a named value.
bool paramValue(const QJsonObject &param, const QString &text, double *value) {
    if (param.value("toggled").toBool()) {
        const QString t = text.toLower();
        if (t == "on" || t == "true" || t == "1" || t == "off" || t == "false" || t == "0") {
            *value = (t == "on" || t == "true" || t == "1") ? 1 : 0;
            return true;
        }
        err(QStringLiteral("%1 is on or off").arg(param.value("name").toString()));
        return false;
    }
    const QJsonArray options = param.value("options").toArray();
    QStringList labels;
    for (const auto &o : options) {
        if (o.toObject().value("label").toString().compare(text, Qt::CaseInsensitive) == 0) {
            *value = o.toObject().value("value").toDouble();
            return true;
        }
        labels << o.toObject().value("label").toString();
    }
    // A list of named values takes only those names.
    if (param.value("integer").toBool() && !labels.isEmpty()) {
        err(QStringLiteral("%1 is %2 or %3").arg(param.value("name").toString(), labels.mid(0, labels.size() - 1).join(", "), labels.last()));
        return false;
    }
    const double min = param.value("min").toDouble(), max = param.value("max").toDouble();
    if (bounded(min) && bounded(max))
        return number(text, min, max, param.value("name").toString(), value);
    bool ok = false;
    *value = text.toDouble(&ok);
    if (!ok || (bounded(min) && *value < min) || (bounded(max) && *value > max)) {
        err(QStringLiteral("%1 is a number %2").arg(param.value("name").toString(), paramRange(param)));
        return false;
    }
    return true;
}

// Places in the chain and rules count from 1 on the command line.
bool place(const QString &text, int count, const QString &what, int *index) {
    bool ok = false;
    const int n = text.toInt(&ok);
    if (ok && n >= 1 && n <= count) {
        *index = n - 1;
        return true;
    }
    err(count ? QStringLiteral("%1 is 1 to %2").arg(what).arg(count) : QStringLiteral("there is no %1 yet").arg(what));
    return false;
}

// An output by its label or its node name.
QString outputName(const QJsonObject &state, const QString &wanted) {
    for (const auto &v : state.value("outputs").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("name").toString() == wanted || o.value("label").toString().compare(wanted, Qt::CaseInsensitive) == 0)
            return o.value("name").toString();
    }
    return {};
}

QJsonObject findApp(const QJsonObject &state, const QString &wanted) {
    for (const auto &v : state.value("apps").toArray()) {
        const QJsonObject a = v.toObject();
        if (a.value("key").toString() == wanted || a.value("name").toString().compare(wanted, Qt::CaseInsensitive) == 0)
            return a;
    }
    return {};
}

bool hasPreset(const QJsonObject &state, const QString &name) {
    for (const auto &v : state.value("presets").toArray())
        if (v.toObject().value("name").toString() == name)
            return true;
    return false;
}

// The rules after the pins, which the pin commands own.
QJsonArray switchRules(const QJsonObject &state) {
    QJsonArray list;
    for (const auto &v : state.value("rules").toArray())
        if (v.toObject().value("kind").toString() != "output")
            list.append(v);
    return list;
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

// Search replies arrive as messages. This waits for the one that is no
// longer pending.
QJsonObject search(ipc::Client &client, const QString &query) {
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
    return found;
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
        "  preamp [open]                       open the window\n"
        "  preamp daemon                       run the audio service (systemd starts it)\n"
        "  preamp status                       show what plays where\n"
        "  preamp watch [--spectrum]           print each change, and the analyzer, as JSON lines\n"
        "\n"
        "Presets:\n"
        "  preamp presets                      list presets; keys 1 to 8 show their number\n"
        "  preamp preset NAME                  apply a preset\n"
        "  preamp preset --next | --prev       apply the next or previous preset on keys 1 to 8\n"
        "  preamp save NAME                    save the curve as a preset\n"
        "  preamp delete NAME                  delete a saved preset\n"
        "  preamp search QUERY                 find presets and AutoEq profiles\n"
        "  preamp autoeq QUERY                 apply the AutoEq profile for a headphone\n"
        "  preamp pin [PRESET] [--output OUT]  pin a preset to an output\n"
        "  preamp unpin [--output OUT]         drop the pin on an output\n"
        "\n"
        "Equalizer:\n"
        "  preamp bypass [on|off]              switch the equalizer off and on\n"
        "  preamp bands                        list the ten bands\n"
        "  preamp band N [--freq HZ] [--gain DB] [--q Q] [--type peak|lowshelf|highshelf]\n"
        "  preamp tone [--bass DB] [--mid DB] [--treble DB]\n"
        "                                      show or set bass, mid and treble\n"
        "  preamp channel [L+R|L|R | --next]   show or pick the channel to edit\n"
        "\n"
        "Output:\n"
        "  preamp outputs                      list outputs\n"
        "  preamp output NAME | --next         play through an output\n"
        "  preamp volume [0..150]              show or set the output volume\n"
        "  preamp balance [-100..100]          show or set the balance; below 0 is left\n"
        "  preamp width [0..200]               show or set the stereo width in percent\n"
        "  preamp room [ROOM | --next]         show or set the room: off, small room, studio, hall\n"
        "\n"
        "Apps:\n"
        "  preamp apps                         list the apps that play\n"
        "  preamp app NAME [--volume 0..100] [--mute | --unmute]\n"
        "\n"
        "Effects:\n"
        "  preamp plugins [--available]        list the effects in the chain, or the installed ones\n"
        "  preamp plugin add ID|NAME           add an effect at the end of the chain\n"
        "  preamp plugin on|off|remove N       switch on, switch off or remove effect N\n"
        "  preamp plugin move N TO             move effect N to place TO\n"
        "  preamp plugin params N              list the controls of effect N\n"
        "  preamp plugin set N NAME VALUE|default\n"
        "                                      set a control of effect N, or reset it\n"
        "\n"
        "Rules:\n"
        "  preamp rules                        list the pins and the auto-switch rules\n"
        "  preamp rule add call|fullscreen APP [--preset P] [--room ROOM]\n"
        "  preamp rule set N [--kind call|fullscreen] [--app APP] [--preset P] [--room ROOM]\n"
        "  preamp rule remove N\n"
        "\n"
        "Setup:\n"
        "  preamp shell install                put the preamp widget on the Omarchy bar\n"
        "  preamp skill install                teach coding agents to drive preamp\n"
        "  preamp version                      print the version\n"
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
        out(json ? QStringLiteral("{\"version\":\"%1\"}").arg(QCoreApplication::applicationVersion())
                 : QCoreApplication::applicationVersion());
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
            out(pretty(state));
            return 0;
        }
        out(QStringLiteral("preset   %1%2").arg(state.value("preset").toString(), state.value("dirty").toBool() ? " (edited)" : ""));
        out(QStringLiteral("eq       %1").arg(state.value("bypass").toBool() ? "bypassed" : "active"));
        out(QStringLiteral("preamp   %1 dB").arg(formatGain(state.value("preamp").toDouble())));
        out(QStringLiteral("channel  %1").arg(state.value("link").toBool() ? "L+R" : state.value("channel").toString()));
        out(QStringLiteral("output   %1").arg(state.value("outputLabel").toString()));
        out(QStringLiteral("volume   %1%").arg(state.value("volume").toInt()));
        out(QStringLiteral("balance  %1").arg(formatBalance(state.value("balance").toInt())));
        out(QStringLiteral("room     %1, width %2%").arg(state.value("room").toString()).arg(state.value("width").toInt()));
        return 0;
    }
    if (command == "watch") {
        // One JSON object per line: the state after each change, and the
        // analyzer about 30 times a second with --spectrum.
        QObject::connect(&client, &ipc::Client::message, [](const QJsonObject &m) {
            out(QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
            std::fflush(stdout);
        });
        QObject::connect(&client, &ipc::Client::disconnected, QCoreApplication::instance(), &QCoreApplication::quit);
        client.send({{"cmd", "subscribe"}, {"state", true}, {"spectrum", takeFlag(args, "--spectrum")}});
        QCoreApplication::exec();
        return 0;
    }

    // Presets.
    if (command == "presets") {
        const QJsonArray library = send({{"cmd", "library"}}).value("presets").toArray();
        if (json) {
            out(pretty(library));
            return 0;
        }
        const QStringList keys = state.value("slots").toVariant().toStringList();
        for (const auto &v : library) {
            const QJsonObject p = v.toObject();
            const QString name = p.value("name").toString();
            const qsizetype key = keys.indexOf(name);
            const QString kind = p.value("kind").toString();
            out(QStringLiteral("%1%2  %3%4")
                    .arg(name == state.value("preset").toString() ? "* " : "  ", key >= 0 ? QString::number(key + 1) : " ", name,
                         kind == "builtin" ? QString() : "  · " + kind));
        }
        return 0;
    }
    if (command == "preset") {
        const bool next = takeFlag(args, "--next");
        const bool prev = takeFlag(args, "--prev");
        if (next || prev) {
            const QJsonObject r = send({{"cmd", next ? "nextPreset" : "prevPreset"}});
            const QString now = send({{"cmd", "state"}}).value("state").toObject().value("preset").toString();
            return finish(r, json, QStringLiteral("Applied %1.").arg(now));
        }
        const QString name = args.mid(1).join(' ');
        if (name.isEmpty()) {
            err("usage: preamp preset NAME | --next | --prev");
            return 1;
        }
        return finish(send({{"cmd", "applyPreset"}, {"name", name}}), json, QStringLiteral("Applied %1.").arg(name));
    }
    if (command == "save" || command == "delete") {
        const QString name = args.mid(1).join(' ');
        if (name.isEmpty()) {
            err(QStringLiteral("usage: preamp %1 NAME").arg(command));
            return 1;
        }
        return command == "save" ? finish(send({{"cmd", "savePreset"}, {"name", name}}), json, QStringLiteral("Saved %1.").arg(name))
                                 : finish(send({{"cmd", "deletePreset"}, {"name", name}}), json, QStringLiteral("Deleted %1.").arg(name));
    }
    if (command == "search") {
        const QString query = args.mid(1).join(' ');
        if (query.isEmpty()) {
            err("usage: preamp search QUERY");
            return 1;
        }
        const QJsonObject found = search(client, query);
        if (json) {
            out(pretty(found));
            return found.value("error").toString().isEmpty() ? 0 : 1;
        }
        const QJsonArray results = found.value("results").toArray();
        for (const auto &v : results)
            out(QStringLiteral("%1  %2").arg(v.toObject().value("s").toString().leftJustified(6), v.toObject().value("t").toString()));
        if (!found.value("error").toString().isEmpty()) {
            err(QStringLiteral("could not reach AutoEq: %1").arg(found.value("error").toString()));
            return 1;
        }
        if (results.isEmpty()) {
            err(QStringLiteral("nothing matches %1").arg(query));
            return 1;
        }
        return 0;
    }
    if (command == "autoeq") {
        const QString query = args.mid(1).join(' ');
        if (query.isEmpty()) {
            err("usage: preamp autoeq QUERY");
            return 1;
        }
        const QJsonObject found = search(client, query);
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
    if (command == "pin" || command == "unpin") {
        QJsonObject m{{"cmd", command}};
        QString label = state.value("outputLabel").toString();
        if (const auto wanted = takeOption(args, "--output")) {
            // A pin can stay for an output that is not connected now.
            const QString name = outputName(state, *wanted);
            m.insert("output", name.isEmpty() ? *wanted : name);
            label = *wanted;
        }
        const QString preset = args.mid(1).join(' ');
        if (command == "unpin")
            return finish(send(m), json, QStringLiteral("Dropped the pin on %1.").arg(label));
        if (!preset.isEmpty())
            m.insert("preset", preset);
        return finish(send(m), json,
                      QStringLiteral("Pinned %1 to %2.").arg(preset.isEmpty() ? state.value("preset").toString() : preset, label));
    }

    // Equalizer.
    if (command == "bypass") {
        QJsonObject m{{"cmd", "setBypass"}};
        if (args.value(1) == "on")
            m.insert("on", true);
        else if (args.value(1) == "off")
            m.insert("on", false);
        const QJsonObject r = send(m);
        return finish(r, json, r.value("bypass").toBool() ? "The equalizer is bypassed." : "The equalizer is on.");
    }
    if (command == "bands") {
        const QJsonObject bands = state.value("bands").toObject();
        if (json) {
            out(pretty(QJsonObject{{"link", state.value("link")}, {"channel", state.value("channel")}, {"bands", bands}}));
            return 0;
        }
        const bool link = state.value("link").toBool();
        for (const QString &side : link ? QStringList{"L"} : QStringList{"L", "R"}) {
            out(link ? QStringLiteral("L+R") : side);
            const QJsonArray list = bands.value(side).toArray();
            for (int i = 0; i < list.size(); ++i) {
                const QJsonObject b = list[i].toObject();
                out(QStringLiteral("%1  %2  %3  %4 dB  q %5")
                        .arg(i + 1, 2)
                        .arg(bandTypeLabel(b.value("t").toString()).leftJustified(9), formatFreq(b.value("f").toDouble()).rightJustified(8),
                             formatGain(b.value("g").toDouble()).rightJustified(5), QString::number(b.value("q").toDouble(), 'f', 2)));
            }
        }
        return 0;
    }
    if (command == "band") {
        bool ok = false;
        const int n = args.value(1).toInt(&ok);
        if (!ok || n < 1 || n > 10) {
            err("usage: preamp band 1-10 [--freq HZ] [--gain DB] [--q Q] [--type peak|lowshelf|highshelf]");
            return 1;
        }
        QJsonObject m{{"cmd", "setBand"}, {"i", n - 1}};
        double v = 0;
        if (const auto f = takeOption(args, "--freq")) {
            if (!number(*f, 20, 20000, "the frequency", &v))
                return 1;
            m.insert("f", v);
        }
        if (const auto g = takeOption(args, "--gain")) {
            if (!number(*g, -12, 12, "the gain", &v))
                return 1;
            m.insert("g", v);
        }
        if (const auto q = takeOption(args, "--q")) {
            if (!number(*q, 0.3, 10, "q", &v))
                return 1;
            m.insert("q", v);
        }
        if (const auto t = takeOption(args, "--type")) {
            if (bandType(*t).isEmpty()) {
                err("the type is peak, lowshelf or highshelf");
                return 1;
            }
            m.insert("t", bandType(*t));
        }
        if (args.size() > 2) {
            err(QStringLiteral("unknown option %1").arg(args[2]));
            return 1;
        }
        return finish(send(m), json, QStringLiteral("Band %1 set.").arg(n));
    }
    if (command == "tone") {
        // Bass, mid and treble each move a group of bands, as in the basic
        // mode of the window.
        const QStringList names{"bass", "mid", "treble"};
        bool changedAny = false;
        for (int k = 0; k < names.size(); ++k) {
            const auto g = takeOption(args, "--" + names[k]);
            if (!g)
                continue;
            double v = 0;
            if (!number(*g, -12, 12, names[k], &v))
                return 1;
            const QJsonObject r = send({{"cmd", "setMacro"}, {"k", k}, {"v", v}});
            if (!r.value("ok").toBool())
                return finish(r, json, {});
            changedAny = true;
        }
        if (args.size() > 1) {
            err(QStringLiteral("unknown option %1").arg(args[1]));
            return 1;
        }
        const QJsonArray macros = (changedAny ? send({{"cmd", "state"}}).value("state").toObject() : state).value("macros").toArray();
        if (json) {
            out(pretty(QJsonObject{{"bass", macros[0]}, {"mid", macros[1]}, {"treble", macros[2]}}));
            return 0;
        }
        for (int k = 0; k < names.size(); ++k)
            out(QStringLiteral("%1 %2 dB").arg(names[k].leftJustified(7), formatGain(macros[k].toDouble())));
        return 0;
    }
    if (command == "channel") {
        if (takeFlag(args, "--next")) {
            const QJsonObject r = send({{"cmd", "cycleChannel"}});
            const QJsonObject now = send({{"cmd", "state"}}).value("state").toObject();
            return finish(r, json, QStringLiteral("Editing %1.").arg(now.value("link").toBool() ? "L+R" : now.value("channel").toString()));
        }
        if (args.size() < 2) {
            const QString ch = state.value("link").toBool() ? "L+R" : state.value("channel").toString();
            out(json ? QStringLiteral("{\"channel\":\"%1\"}").arg(ch) : ch);
            return 0;
        }
        return finish(send({{"cmd", "setChannel"}, {"channel", args[1].toUpper()}}), json,
                      QStringLiteral("Editing %1.").arg(args[1].toUpper()));
    }

    // Output.
    if (command == "outputs") {
        if (json) {
            out(pretty(state.value("outputs")));
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
        if (takeFlag(args, "--next")) {
            const QJsonObject r = send({{"cmd", "nextOutput"}});
            const QString now = send({{"cmd", "state"}}).value("state").toObject().value("outputLabel").toString();
            return finish(r, json, QStringLiteral("Playing through %1.").arg(now));
        }
        const QString wanted = args.mid(1).join(' ');
        const QString name = outputName(state, wanted);
        if (name.isEmpty()) {
            err(QStringLiteral("no output named %1. List them with: preamp outputs").arg(wanted));
            return 1;
        }
        return finish(send({{"cmd", "setOutput"}, {"name", name}}), json, QStringLiteral("Playing through %1.").arg(wanted));
    }
    if (command == "volume" || command == "balance" || command == "width") {
        const int now = state.value(command).toInt();
        if (args.size() < 2) {
            if (json)
                out(QStringLiteral("{\"%1\":%2}").arg(command).arg(now));
            else
                out(command == "balance" ? formatBalance(now) : QStringLiteral("%1%").arg(now));
            return 0;
        }
        double v = 0;
        if (command == "volume") {
            if (!number(args[1], 0, 150, "volume", &v))
                return 1;
            return finish(send({{"cmd", "setVolume"}, {"v", v}}), json, QStringLiteral("Volume %1%.").arg(v));
        }
        if (command == "balance") {
            if (!number(args[1], -100, 100, "balance", &v))
                return 1;
            return finish(send({{"cmd", "setBalance"}, {"v", v}}), json, QStringLiteral("Balance %1.").arg(formatBalance(qRound(v))));
        }
        if (!number(args[1], 0, 200, "width", &v))
            return 1;
        return finish(send({{"cmd", "setWidth"}, {"v", v}}), json, QStringLiteral("Width %1%.").arg(qRound(v)));
    }
    if (command == "room") {
        if (args.size() < 2) {
            out(json ? QStringLiteral("{\"room\":\"%1\"}").arg(state.value("room").toString()) : state.value("room").toString());
            return 0;
        }
        if (takeFlag(args, "--next")) {
            const QJsonObject r = send({{"cmd", "nextRoom"}});
            const QString now = send({{"cmd", "state"}}).value("state").toObject().value("room").toString();
            return finish(r, json, QStringLiteral("Room %1.").arg(now));
        }
        const QString room = args.mid(1).join(' ');
        return finish(send({{"cmd", "setRoom"}, {"room", room}}), json, QStringLiteral("Room %1.").arg(room));
    }

    // Apps.
    if (command == "apps") {
        if (json) {
            out(pretty(state.value("apps")));
            return 0;
        }
        for (const auto &v : state.value("apps").toArray()) {
            const QJsonObject a = v.toObject();
            out(QStringLiteral("%1  %2%  %3%4")
                    .arg(a.value("name").toString())
                    .arg(a.value("volume").toInt())
                    .arg(a.value("state").toString(), a.value("muted").toBool() ? ", muted" : ""));
        }
        return 0;
    }
    if (command == "app") {
        const auto volume = takeOption(args, "--volume");
        const bool mute = takeFlag(args, "--mute");
        const bool unmute = takeFlag(args, "--unmute");
        const QString wanted = args.mid(1).join(' ');
        const QJsonObject app = findApp(state, wanted);
        if (wanted.isEmpty() || app.isEmpty()) {
            err(QStringLiteral("no app named %1 plays now. List them with: preamp apps").arg(wanted));
            return 1;
        }
        const QString key = app.value("key").toString();
        bool muted = app.value("muted").toBool();
        if (volume) {
            double v = 0;
            if (!number(*volume, 0, 100, "the app volume", &v))
                return 1;
            const QJsonObject r = send({{"cmd", "setAppVolume"}, {"key", key}, {"v", v}});
            if (!r.value("ok").toBool())
                return finish(r, json, {});
            muted = false;
        }
        if ((mute && !muted) || (unmute && muted)) {
            const QJsonObject r = send({{"cmd", "toggleAppMute"}, {"key", key}});
            if (!r.value("ok").toBool())
                return finish(r, json, {});
        }
        if (!volume && !mute && !unmute) {
            out(json ? pretty(app)
                     : QStringLiteral("%1  %2%  %3").arg(app.value("name").toString()).arg(app.value("volume").toInt()).arg(app.value("state").toString()));
            return 0;
        }
        return finish({{"ok", true}}, json, QStringLiteral("Set %1.").arg(app.value("name").toString()));
    }

    // Effects.
    if (command == "plugins") {
        if (takeFlag(args, "--available")) {
            const QJsonObject r = send({{"cmd", "plugins"}});
            if (json) {
                out(pretty(r.value("plugins")));
                return 0;
            }
            for (const auto &v : r.value("plugins").toArray()) {
                const QJsonObject p = v.toObject();
                out(QStringLiteral("%1  %2  %3").arg(p.value("name").toString(), p.value("kind").toString(), p.value("id").toString()));
            }
            return 0;
        }
        const QJsonArray chain = state.value("plugins").toArray();
        if (json) {
            out(pretty(chain));
            return 0;
        }
        if (chain.isEmpty())
            out("No effects in the chain. List the installed ones with: preamp plugins --available");
        for (int i = 0; i < chain.size(); ++i) {
            const QJsonObject p = chain[i].toObject();
            const QString error = p.value("error").toString();
            out(QStringLiteral("%1  %2  %3  %4%5")
                    .arg(i + 1)
                    .arg(p.value("on").toBool() ? "on " : "off", p.value("name").toString(), p.value("kind").toString(),
                         error.isEmpty() ? QString() : "  " + error));
        }
        return 0;
    }
    if (command == "plugin") {
        const QString action = args.value(1);
        const QJsonArray chain = state.value("plugins").toArray();
        if (action == "add") {
            const QString wanted = args.mid(2).join(' ');
            QString id, name;
            for (const auto &v : send({{"cmd", "plugins"}}).value("plugins").toArray()) {
                const QJsonObject p = v.toObject();
                if (p.value("id").toString() == wanted || p.value("name").toString().compare(wanted, Qt::CaseInsensitive) == 0) {
                    id = p.value("id").toString();
                    name = p.value("name").toString();
                }
            }
            if (id.isEmpty()) {
                err(QStringLiteral("no installed effect named %1. List them with: preamp plugins --available").arg(wanted));
                return 1;
            }
            return finish(send({{"cmd", "addPlugin"}, {"plugin", id}}), json, QStringLiteral("Added %1.").arg(name));
        }
        int i = 0;
        if ((action == "on" || action == "off" || action == "remove" || action == "move" || action == "params" ||
             action == "set") &&
            !place(args.value(2), int(chain.size()), "effect", &i))
            return 1;
        if (action == "params" || action == "set") {
            const QJsonObject r = send({{"cmd", "pluginParams"}, {"i", i}});
            if (!r.value("ok").toBool())
                return finish(r, json, {});
            const QJsonArray params = r.value("params").toArray();
            if (action == "params") {
                if (json) {
                    out(pretty(params));
                    return 0;
                }
                if (params.isEmpty())
                    out(QStringLiteral("Effect %1 has no controls.").arg(i + 1));
                for (const auto &v : params) {
                    const QJsonObject p = v.toObject();
                    out(QStringLiteral("%1  %2  %3  %4")
                            .arg(p.value("symbol").toString(), formatParam(p, p.value("value").toDouble()), paramRange(p),
                                 p.value("name").toString()));
                }
                return 0;
            }
            const QString wanted = args.value(3);
            const QString text = args.mid(4).join(' ');
            QJsonObject param;
            for (const auto &v : params) {
                const QJsonObject p = v.toObject();
                if (p.value("symbol").toString() == wanted || p.value("name").toString().compare(wanted, Qt::CaseInsensitive) == 0)
                    param = p;
            }
            if (param.isEmpty() || text.isEmpty()) {
                err(param.isEmpty() ? QStringLiteral("effect %1 has no control named %2. List them with: preamp plugin params %1").arg(i + 1).arg(wanted)
                                    : QStringLiteral("usage: preamp plugin set N NAME VALUE|default"));
                return 1;
            }
            QJsonObject m{{"cmd", "setPluginParam"}, {"i", i}, {"param", param.value("symbol").toString()}};
            if (text != "default") {
                double value = 0;
                if (!paramValue(param, text, &value))
                    return 1;
                m.insert("value", value);
            }
            const QJsonObject reply = send(m);
            return finish(reply, json,
                          QStringLiteral("Set %1 of effect %2 to %3.")
                              .arg(param.value("name").toString())
                              .arg(i + 1)
                              .arg(formatParam(param, reply.value("value").toDouble())));
        }
        if (action == "on" || action == "off") {
            if (chain[i].toObject().value("on").toBool() == (action == "on"))
                return finish({{"ok", true}}, json, QStringLiteral("Effect %1 is already %2.").arg(i + 1).arg(action));
            return finish(send({{"cmd", "togglePlugin"}, {"i", i}}), json, QStringLiteral("Switched effect %1 %2.").arg(i + 1).arg(action));
        }
        if (action == "remove")
            return finish(send({{"cmd", "removePlugin"}, {"i", i}}), json, QStringLiteral("Removed effect %1.").arg(i + 1));
        if (action == "move") {
            int to = 0;
            if (!place(args.value(3), int(chain.size()), "the new place", &to))
                return 1;
            return finish(send({{"cmd", "movePlugin"}, {"i", i}, {"to", to}}), json,
                          QStringLiteral("Moved effect %1 to place %2.").arg(i + 1).arg(to + 1));
        }
        err("usage: preamp plugin add ID|NAME | on|off|remove|params N | move N TO | set N NAME VALUE");
        return 1;
    }

    // Rules.
    if (command == "rules") {
        if (json) {
            out(pretty(state.value("rules")));
            return 0;
        }
        for (const auto &v : state.value("rules").toArray()) {
            const QJsonObject r = v.toObject();
            const QString kind = r.value("kind").toString();
            const QString head = kind == "output" ? QStringLiteral("pin  output is")
                                                  : QStringLiteral("%1    %2").arg(r.value("index").toInt() + 1)
                                                        .arg(kind == "call" ? "app in call" : "app fullscreen");
            const QString room = r.value("room").toString();
            out(QStringLiteral("%1  %2 → %3%4%5")
                    .arg(head, r.value("when").toString(), r.value("then").toString(), room.isEmpty() ? QString() : ", room " + room,
                         r.value("active").toBool() ? "  active" : ""));
        }
        return 0;
    }
    if (command == "rule") {
        const QString action = args.value(1);
        const QJsonArray rules = switchRules(state);
        QJsonObject m;
        if (const auto preset = takeOption(args, "--preset")) {
            if (!hasPreset(state, *preset)) {
                err(QStringLiteral("no preset named %1. List them with: preamp presets").arg(*preset));
                return 1;
            }
            m.insert("then", *preset);
        }
        if (const auto room = takeOption(args, "--room")) {
            const QJsonArray rooms = state.value("rooms").toArray();
            if (!rooms.contains(*room)) {
                err("the room is off, small room, studio or hall");
                return 1;
            }
            m.insert("room", *room == "off" ? QString() : *room);
        }
        if (const auto kind = takeOption(args, "--kind"))
            m.insert("kind", *kind);
        if (const auto app = takeOption(args, "--app"))
            m.insert("when", *app);
        if (action == "add") {
            const QString kind = args.value(2);
            const QString app = args.mid(3).join(' ');
            if ((kind != "call" && kind != "fullscreen") || app.isEmpty()) {
                err("usage: preamp rule add call|fullscreen APP [--preset P] [--room ROOM]");
                return 1;
            }
            m.insert("cmd", "addRule");
            m.insert("kind", kind);
            m.insert("when", app);
            return finish(send(m), json, QStringLiteral("Added rule %1.").arg(rules.size() + 1));
        }
        int i = 0;
        if ((action == "set" || action == "remove") && !place(args.value(2), int(rules.size()), "rule", &i))
            return 1;
        if (action == "set") {
            m.insert("cmd", "updateRule");
            m.insert("index", i);
            return finish(send(m), json, QStringLiteral("Changed rule %1.").arg(i + 1));
        }
        if (action == "remove")
            return finish(send({{"cmd", "removeRule"}, {"index", i}}), json, QStringLiteral("Removed rule %1.").arg(i + 1));
        err("usage: preamp rule add call|fullscreen APP | set N ... | remove N");
        return 1;
    }
    err(QStringLiteral("unknown command %1").arg(command));
    printHelp();
    return 1;
}

} // namespace cli
