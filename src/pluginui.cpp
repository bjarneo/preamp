#include "pluginui.h"

#include "ipc.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <xcb/xcb.h>

namespace pluginui {

namespace {

void err(const QString &text) {
    std::fputs(qPrintable("preamp: " + text + "\n"), stderr);
}

// The features a UI may require. A UI that requires anything else is skipped.
const QSet<QString> Supported{LV2_URID__map,          LV2_URID__unmap,      LV2_UI__parent,     LV2_UI__resize,
                              LV2_UI__idleInterface, LV2_OPTIONS__options, LV2_UI__portMap,    LV2_UI__noUserResize,
                              LV2_UI__fixedSize,     LV2_CORE__isLive};

// Only the UI thread maps, so the map needs no lock.
class UridMap {
public:
    LV2_URID map(const char *uri) {
        auto it = m_ids.find(uri);
        if (it != m_ids.end())
            return it->second;
        m_uris.emplace_back(uri);
        const LV2_URID id = LV2_URID(m_uris.size());
        m_ids.emplace(uri, id);
        return id;
    }
    const char *unmap(LV2_URID id) const { return id > 0 && id <= m_uris.size() ? m_uris[id - 1].c_str() : nullptr; }

private:
    std::vector<std::string> m_uris;
    std::unordered_map<std::string, LV2_URID> m_ids;
};

// A key per effect, so a second `preamp plugin ui` for the same effect
// raises the open window instead of opening another.
QString lockName(const QString &id) {
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QDir::tempPath();
    const QByteArray hash = QCryptographicHash::hash((ipc::socketPath() + '\n' + id).toUtf8(), QCryptographicHash::Sha1);
    return dir + "/preamp-ui-" + QString::fromLatin1(hash.toHex().left(16)) + ".sock";
}

class Host : public QObject {
public:
    ~Host() override {
        if (m_ui && m_descriptor && m_descriptor->cleanup)
            m_descriptor->cleanup(m_ui);
        if (m_conn) {
            if (m_window)
                xcb_destroy_window(m_conn, m_window);
            xcb_disconnect(m_conn);
        }
        // The UI library stays loaded: some register atexit handlers or
        // threads that would run in unmapped code.
        if (m_world)
            lilv_world_free(m_world);
    }

    int start(int index) {
        m_clock.start();
        m_index = index;
        m_client.connectToDaemon();
        if (!m_client.waitForConnected(1000)) {
            err("the preamp daemon is not running");
            return 1;
        }
        const QJsonArray chain = m_client.request({{"cmd", "state"}}).value("state").toObject().value("plugins").toArray();
        if (index < 0 || index >= chain.size()) {
            err(QStringLiteral("no effect at place %1").arg(index + 1));
            return 1;
        }
        const QJsonObject effect = chain[index].toObject();
        m_id = effect.value("id").toString();
        m_name = effect.value("name").toString();
        if (effect.value("kind").toString() != "lv2") {
            err(QStringLiteral("%1 has no window of its own. Set its controls with: preamp plugin set %2 NAME VALUE")
                    .arg(m_name)
                    .arg(index + 1));
            return 1;
        }

        // One window per effect.
        const QString lock = lockName(m_id);
        {
            QLocalSocket other;
            other.connectToServer(lock);
            if (other.waitForConnected(200)) {
                other.write("raise\n");
                other.waitForBytesWritten(200);
                return 0;
            }
        }
        QLocalServer::removeServer(lock);
        m_lock.setSocketOptions(QLocalServer::UserAccessOption);
        m_lock.listen(lock);
        connect(&m_lock, &QLocalServer::newConnection, this, [this] {
            QLocalSocket *s = m_lock.nextPendingConnection();
            connect(s, &QLocalSocket::readyRead, s, [this, s] {
                s->readAll();
                raise();
                s->deleteLater();
            });
        });

        if (!loadParams())
            return 1;
        QString error;
        if (!openUi(&error)) {
            err(error);
            return 1;
        }

        connect(&m_client, &ipc::Client::message, this, [this](const QJsonObject &m) {
            if (m.value("type").toString() == "state")
                onState(m);
        });
        connect(&m_client, &ipc::Client::disconnected, qApp, &QCoreApplication::quit);
        m_client.send({{"cmd", "subscribe"}, {"state", true}, {"spectrum", false}});
        return -1; // keeps running
    }

private:
    struct Control {
        QString symbol;
        uint32_t port = 0;
        float value = 0;
        qint64 writtenAt = -1; // when the window last set it, in ms
    };

    // ------------------------------------------------------------ daemon

    bool loadParams() {
        const QJsonObject r = m_client.request({{"cmd", "pluginParams"}, {"i", m_index}});
        if (!r.value("ok").toBool()) {
            err(r.value("error").toString());
            return false;
        }
        m_controls.clear();
        for (const auto &v : r.value("params").toArray()) {
            const QJsonObject p = v.toObject();
            Control c;
            c.symbol = p.value("symbol").toString();
            c.value = float(p.value("value").toDouble());
            m_controls.insert(c.symbol, c);
        }
        return true;
    }

    void onState(const QJsonObject &state) {
        const QJsonArray chain = state.value("plugins").toArray();
        // The effect may have moved. Follow it, and close when it is gone.
        if (m_index >= chain.size() || chain[m_index].toObject().value("id").toString() != m_id) {
            int found = -1;
            for (int i = 0; i < chain.size() && found < 0; ++i)
                if (chain[i].toObject().value("id").toString() == m_id)
                    found = i;
            if (found < 0) {
                QCoreApplication::quit();
                return;
            }
            m_index = found;
        }
        fetch();
    }

    // One request at a time. A state that comes in meanwhile asks again once
    // the answer arrives, so no change is lost.
    void fetch() {
        if (m_fetching) {
            m_fetchAgain = true;
            return;
        }
        m_fetching = true;
        m_client.send({{"cmd", "pluginParams"}, {"i", m_index}}, [this](const QJsonObject &r) {
            m_fetching = false;
            if (m_fetchAgain) {
                m_fetchAgain = false;
                fetch();
            }
            if (!r.value("ok").toBool())
                return;
            for (const auto &v : r.value("params").toArray()) {
                const QJsonObject p = v.toObject();
                auto it = m_controls.find(p.value("symbol").toString());
                if (it == m_controls.end())
                    continue;
                const float value = float(p.value("value").toDouble());
                // A value the window set a moment ago wins over an older one
                // still on its way back, so a knob does not jump while it moves.
                if (it->writtenAt >= 0 && m_clock.elapsed() - it->writtenAt < 500)
                    continue;
                if (value != it->value) {
                    it->value = value;
                    portEvent(*it);
                }
            }
        });
    }

    // From the UI: a knob moved.
    void write(uint32_t port, uint32_t size, uint32_t protocol, const void *buffer) {
        // Some UIs write their own defaults while they open. The values in
        // preamp win, so writes count only once the UI has them.
        if (!m_ready || protocol != 0 || size != sizeof(float))
            return; // atom messages need forwarding to the daemon first
        const QString symbol = m_symbols.value(port);
        auto it = m_controls.find(symbol);
        if (it == m_controls.end())
            return;
        const float value = *static_cast<const float *>(buffer);
        it->writtenAt = m_clock.elapsed();
        if (value == it->value)
            return;
        it->value = value;
        m_client.send({{"cmd", "setPluginParam"}, {"i", m_index}, {"param", symbol}, {"value", double(value)}});
    }

    // Meters and the other values the effect reports, at the UI's rate. One
    // request at a time, so a slow daemon gets fewer, not a queue.
    void pollOutputs() {
        if (m_polling || !m_client.isConnected())
            return;
        m_polling = true;
        m_client.send({{"cmd", "pluginOutputs"}, {"i", m_index}}, [this](const QJsonObject &r) {
            m_polling = false;
            if (!r.value("ok").toBool() || !m_descriptor || !m_descriptor->port_event)
                return;
            for (const auto &v : r.value("outputs").toArray()) {
                const QJsonObject o = v.toObject();
                if (!o.contains("value"))
                    continue;
                const uint32_t port = m_ports.value(o.value("symbol").toString(), LV2UI_INVALID_PORT_INDEX);
                if (port == LV2UI_INVALID_PORT_INDEX)
                    continue;
                const float value = float(o.value("value").toDouble());
                auto it = m_outputs.find(port);
                if (it != m_outputs.end() && it.value() == value)
                    continue;
                m_outputs.insert(port, value);
                m_descriptor->port_event(m_ui, port, sizeof(float), 0, &value);
            }
        });
    }

    void portEvent(const Control &c) {
        if (m_descriptor && m_descriptor->port_event)
            m_descriptor->port_event(m_ui, c.port, sizeof(float), 0, &c.value);
    }

    // ---------------------------------------------------------------- UI

    bool openUi(QString *error) {
        m_world = lilv_world_new();
        lilv_world_load_all(m_world);
        LilvNode *uri = lilv_new_uri(m_world, m_id.mid(4).toUtf8().constData());
        const LilvPlugin *plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(m_world), uri);
        lilv_node_free(uri);
        if (!plugin) {
            *error = QStringLiteral("%1 is not installed").arg(m_name);
            return false;
        }

        for (uint32_t i = 0; i < lilv_plugin_get_num_ports(plugin); ++i) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plugin, i);
            const QString symbol = QString::fromUtf8(lilv_node_as_string(lilv_port_get_symbol(plugin, port)));
            m_symbols.insert(i, symbol);
            m_ports.insert(symbol, i);
            if (auto it = m_controls.find(symbol); it != m_controls.end())
                it->port = i;
        }

        const LilvUI *ui = pickUi(plugin, error);
        if (!ui)
            return false;
        const std::string uiUri = lilv_node_as_uri(lilv_ui_get_uri(ui));
        char *binary = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_binary_uri(ui)), nullptr);
        char *bundle = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_bundle_uri(ui)), nullptr);
        const std::string binaryPath = binary ? binary : "", bundlePath = bundle ? bundle : "";
        lilv_free(binary);
        lilv_free(bundle);

        void *lib = dlopen(binaryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            *error = QStringLiteral("could not load the window of %1: %2").arg(m_name, QString::fromLocal8Bit(dlerror()));
            return false;
        }
        auto entry = reinterpret_cast<LV2UI_DescriptorFunction>(dlsym(lib, "lv2ui_descriptor"));
        for (uint32_t i = 0; entry && !m_descriptor; ++i) {
            const LV2UI_Descriptor *d = entry(i);
            if (!d)
                break;
            if (uiUri == d->URI)
                m_descriptor = d;
        }
        if (!m_descriptor) {
            *error = QStringLiteral("the window of %1 is not in %2").arg(m_name, QString::fromStdString(binaryPath));
            return false;
        }

        if (!openWindow(error))
            return false;

        m_mapFeature = {this, [](LV2_URID_Map_Handle h, const char *u) { return static_cast<Host *>(h)->m_urids.map(u); }};
        m_unmapFeature = {this, [](LV2_URID_Unmap_Handle h, LV2_URID id) { return static_cast<Host *>(h)->m_urids.unmap(id); }};
        m_resizeFeature = {this, [](LV2UI_Feature_Handle h, int w, int hgt) {
                               static_cast<Host *>(h)->resize(w, hgt);
                               return 0;
                           }};
        m_portMapFeature = {this, [](LV2UI_Feature_Handle h, const char *symbol) {
                                return static_cast<Host *>(h)->m_ports.value(QString::fromUtf8(symbol),
                                                                             uint32_t(LV2UI_INVALID_PORT_INDEX));
                            }};
        m_rate = 48000;
        m_updateRate = 30;
        m_options = {
            {LV2_OPTIONS_INSTANCE, 0, m_urids.map(LV2_PARAMETERS__sampleRate), sizeof(float), m_urids.map(LV2_ATOM__Float), &m_rate},
            {LV2_OPTIONS_INSTANCE, 0, m_urids.map(LV2_UI__updateRate), sizeof(float), m_urids.map(LV2_ATOM__Float), &m_updateRate},
            {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr},
        };
        m_features = {
            {LV2_URID__map, &m_mapFeature},
            {LV2_URID__unmap, &m_unmapFeature},
            {LV2_UI__parent, reinterpret_cast<void *>(uintptr_t(m_window))},
            {LV2_UI__resize, &m_resizeFeature},
            {LV2_UI__portMap, &m_portMapFeature},
            {LV2_UI__idleInterface, nullptr},
            {LV2_OPTIONS__options, m_options.data()},
        };
        for (const LV2_Feature &f : m_features)
            m_featureList.push_back(&f);
        m_featureList.push_back(nullptr);

        const QByteArray pluginUri = m_id.mid(4).toUtf8();
        LV2UI_Widget widget = nullptr;
        m_ui = m_descriptor->instantiate(
            m_descriptor, pluginUri.constData(), bundlePath.c_str(),
            [](LV2UI_Controller c, uint32_t port, uint32_t size, uint32_t protocol, const void *buffer) {
                static_cast<Host *>(c)->write(port, size, protocol, buffer);
            },
            this, &widget, m_featureList.data());
        if (!m_ui) {
            *error = QStringLiteral("the window of %1 did not open").arg(m_name);
            return false;
        }
        if (m_descriptor->extension_data)
            m_idle = static_cast<const LV2UI_Idle_Interface *>(m_descriptor->extension_data(LV2_UI__idleInterface));

        for (const Control &c : std::as_const(m_controls))
            portEvent(c);
        m_ready = true;

        // The UI made its window inside ours. Watch it for its size.
        m_child = xcb_window_t(uintptr_t(widget));
        if (m_child) {
            const uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
            xcb_change_window_attributes(m_conn, m_child, XCB_CW_EVENT_MASK, &mask);
            if (auto *g = xcb_get_geometry_reply(m_conn, xcb_get_geometry(m_conn, m_child), nullptr)) {
                if (g->width > 1 && g->height > 1 && m_width <= 1)
                    resize(g->width, g->height);
                free(g);
            }
        }
        if (m_width <= 1)
            resize(640, 400);
        // Without a position the compositor puts an X11 window in the top
        // left corner, so it opens in the middle of the screen.
        const uint32_t at[] = {uint32_t(std::max(0, (m_screen->width_in_pixels - m_width) / 2)),
                               uint32_t(std::max(0, (m_screen->height_in_pixels - m_height) / 2))};
        xcb_configure_window(m_conn, m_window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, at);
        xcb_map_window(m_conn, m_window);
        xcb_flush(m_conn);

        auto *notifier = new QSocketNotifier(xcb_get_file_descriptor(m_conn), QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Host::pumpX);
        connect(&m_idleTimer, &QTimer::timeout, this, [this] {
            pumpX();
            if (m_idle && m_idle->idle(m_ui) != 0)
                QCoreApplication::quit();
            pollOutputs();
        });
        m_idleTimer.start(1000 / 30);
        return true;
    }

    const LilvUI *pickUi(const LilvPlugin *plugin, QString *error) {
        LilvNode *x11 = lilv_new_uri(m_world, LV2_UI__X11UI);
        LilvNode *required = lilv_new_uri(m_world, LV2_CORE__requiredFeature);
        const LilvUI *picked = nullptr;
        QStringList missing;
        LilvUIs *uis = lilv_plugin_get_uis(plugin);
        LILV_FOREACH (uis, it, uis) {
            const LilvUI *ui = lilv_uis_get(uis, it);
            if (!lilv_ui_is_a(ui, x11))
                continue;
            QStringList lacks;
            LilvNodes *features = lilv_world_find_nodes(m_world, lilv_ui_get_uri(ui), required, nullptr);
            LILV_FOREACH (nodes, f, features) {
                const QString feature = QString::fromUtf8(lilv_node_as_uri(lilv_nodes_get(features, f)));
                if (!Supported.contains(feature))
                    lacks << feature;
            }
            lilv_nodes_free(features);
            if (lacks.isEmpty()) {
                picked = ui;
                break;
            }
            missing << lacks;
        }
        // The UIs stay owned by the plugin, so the list may go.
        lilv_node_free(required);
        lilv_node_free(x11);
        if (!picked)
            *error = missing.isEmpty() ? QStringLiteral("%1 has no X11 window of its own").arg(m_name)
                                       : QStringLiteral("the window of %1 needs %2").arg(m_name, missing.join(", "));
        return picked;
    }

    xcb_atom_t atom(const char *name) {
        auto *r = xcb_intern_atom_reply(m_conn, xcb_intern_atom(m_conn, 0, uint16_t(strlen(name)), name), nullptr);
        const xcb_atom_t a = r ? r->atom : xcb_atom_t(XCB_ATOM_NONE);
        free(r);
        return a;
    }

    void setProperty(xcb_atom_t property, xcb_atom_t type, uint8_t format, uint32_t count, const void *data) {
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, m_window, property, type, format, count, data);
    }

    bool openWindow(QString *error) {
        int screenNumber = 0;
        m_conn = xcb_connect(nullptr, &screenNumber);
        if (xcb_connection_has_error(m_conn)) {
            *error = "plugin windows need X11. Check that XWayland runs and DISPLAY is set";
            return false;
        }
        xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(m_conn));
        for (int i = 0; i < screenNumber; ++i)
            xcb_screen_next(&it);
        m_screen = it.data;
        m_window = xcb_generate_id(m_conn);
        const uint32_t values[] = {m_screen->black_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY};
        xcb_create_window(m_conn, XCB_COPY_FROM_PARENT, m_window, m_screen->root, 0, 0, 1, 1, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, m_screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);

        const QByteArray title = (m_name + " · preamp").toUtf8();
        setProperty(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, uint32_t(title.size()), title.constData());
        setProperty(atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8, uint32_t(title.size()), title.constData());
        // Instance and class, so a window rule can match "preamp-plugin".
        const char wmClass[] = "preamp-plugin\0preamp-plugin";
        setProperty(XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);
        m_deleteAtom = atom("WM_DELETE_WINDOW");
        setProperty(atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1, &m_deleteAtom);
        // A dialog floats in Hyprland without a window rule.
        const xcb_atom_t dialog = atom("_NET_WM_WINDOW_TYPE_DIALOG");
        setProperty(atom("_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &dialog);
        const uint32_t pid = uint32_t(QCoreApplication::applicationPid());
        setProperty(atom("_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1, &pid);
        return true;
    }

    void resize(int width, int height) {
        if (width <= 0 || height <= 0 || (width == m_width && height == m_height))
            return;
        m_width = width;
        m_height = height;
        const uint32_t size[] = {uint32_t(width), uint32_t(height)};
        xcb_configure_window(m_conn, m_window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
        // WM_NORMAL_HINTS with the minimum and the maximum at the UI's size:
        // the window takes the size the plugin drew for, and floats.
        uint32_t hints[18] = {};
        hints[0] = 16 | 32; // PMinSize | PMaxSize
        hints[5] = hints[7] = uint32_t(width);
        hints[6] = hints[8] = uint32_t(height);
        setProperty(XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
        xcb_flush(m_conn);
    }

    void raise() {
        if (!m_conn)
            return;
        xcb_map_window(m_conn, m_window);
        xcb_client_message_event_t ev = {};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = m_window;
        ev.type = atom("_NET_ACTIVE_WINDOW");
        ev.data.data32[0] = 2; // from a pager, which the compositor trusts
        xcb_send_event(m_conn, 0, m_screen->root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                       reinterpret_cast<const char *>(&ev));
        xcb_flush(m_conn);
    }

    void pumpX() {
        while (xcb_generic_event_t *e = xcb_poll_for_event(m_conn)) {
            switch (e->response_type & 0x7f) {
            case XCB_CLIENT_MESSAGE: {
                auto *m = reinterpret_cast<xcb_client_message_event_t *>(e);
                if (m->data.data32[0] == m_deleteAtom)
                    QCoreApplication::quit();
                break;
            }
            case XCB_CONFIGURE_NOTIFY: {
                auto *c = reinterpret_cast<xcb_configure_notify_event_t *>(e);
                if (c->window == m_child)
                    resize(c->width, c->height);
                break;
            }
            default:
                break;
            }
            free(e);
        }
        if (xcb_connection_has_error(m_conn))
            QCoreApplication::quit();
    }

    ipc::Client m_client;
    QLocalServer m_lock;
    int m_index = 0;
    QString m_id, m_name;
    QHash<QString, Control> m_controls;
    QHash<uint32_t, QString> m_symbols;
    QHash<QString, uint32_t> m_ports;
    QHash<uint32_t, float> m_outputs; // the values the UI has seen
    bool m_polling = false;
    bool m_fetching = false, m_fetchAgain = false;
    bool m_ready = false;
    QElapsedTimer m_clock;

    LilvWorld *m_world = nullptr;
    const LV2UI_Descriptor *m_descriptor = nullptr;
    LV2UI_Handle m_ui = nullptr;
    const LV2UI_Idle_Interface *m_idle = nullptr;
    UridMap m_urids;
    LV2_URID_Map m_mapFeature{};
    LV2_URID_Unmap m_unmapFeature{};
    LV2UI_Resize m_resizeFeature{};
    LV2UI_Port_Map m_portMapFeature{};
    float m_rate = 48000, m_updateRate = 30;
    std::vector<LV2_Options_Option> m_options;
    std::vector<LV2_Feature> m_features;
    std::vector<const LV2_Feature *> m_featureList;
    QTimer m_idleTimer;

    xcb_connection_t *m_conn = nullptr;
    xcb_screen_t *m_screen = nullptr;
    xcb_window_t m_window = 0, m_child = 0;
    xcb_atom_t m_deleteAtom = XCB_ATOM_NONE;
    int m_width = 0, m_height = 0;
};

} // namespace

int run(int index) {
    Host host;
    const int code = host.start(index);
    if (code >= 0)
        return code;
    return QCoreApplication::exec();
}

} // namespace pluginui
