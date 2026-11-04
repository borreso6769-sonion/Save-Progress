#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Datos: niveles seguidos y sus intentos. Se guardan en dos archivos dentro de
// la carpeta de guardado del mod:
//   store.txt     -> formato interno (para recargar al abrir el juego)
//   progress.json -> el que lee la pagina web (se actualiza solo en cada intento)
// ---------------------------------------------------------------------------
namespace {
    struct Run { long long t; int a; int b; bool practice; };
    struct Lvl {
        std::string name;
        bool active = false;
        long long last = 0;
        std::vector<Run> runs;
    };
    std::map<int, Lvl> g_levels;

    long long nowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    std::string clean(std::string s) {
        for (auto& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' ';
        return s;
    }

    std::string jsonEsc(std::string const& s) {
        std::string o;
        for (unsigned char c : s) {
            if (c == '"') o += "\\\"";
            else if (c == '\\') o += "\\\\";
            else if (c < 0x20) o += ' ';
            else o += static_cast<char>(c);
        }
        return o;
    }

    std::vector<std::string> splitBar(std::string const& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == '|') { out.push_back(cur); cur.clear(); }
            else cur += c;
        }
        out.push_back(cur);
        return out;
    }

    int keyFor(GJGameLevel* lvl) {
        int id = lvl->m_levelID.value();
        if (id != 0) return id;
        // niveles locales / del editor: id 0, usamos un hash del nombre
        auto h = std::hash<std::string>{}(std::string(lvl->m_levelName.c_str()));
        return -static_cast<int>(h % 1000000000u) - 1;
    }

    // Copia visible para el navegador: C:\Users\TuUsuario\GeodeProgress\progress.json
    // (el navegador no deja elegir archivos dentro de AppData ni Program Files)
    std::filesystem::path userCopyDir() {
        auto e = std::getenv("USERPROFILE");
        if (!e) return {};
        return std::filesystem::path(e) / "GeodeProgress";
    }

    Lvl& ensureLevel(GJGameLevel* lvl) {
        auto& l = g_levels[keyFor(lvl)];
        l.name = clean(lvl->m_levelName.c_str());
        if (!l.last) l.last = nowMs();
        return l;
    }

    void writeAtomic(std::filesystem::path const& p, std::string const& data) {
        auto tmp = p;
        tmp += ".tmp";
        auto r = file::writeString(tmp, data);
        if (r.isErr()) {
            log::error("No pude escribir {}", tmp.string());
            return;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, p, ec);
        if (ec) (void)file::writeString(p, data);
    }

    void saveAll() {
        auto dir = Mod::get()->getSaveDir();
        std::string store;
        std::string json = "{\"version\":1,\"levels\":{";
        bool firstL = true;
        for (auto& [key, l] : g_levels) {
            store += fmt::format("L|{}|{}|{}|{}\n", key, l.active ? 1 : 0, l.last, clean(l.name));
            if (!firstL) json += ",";
            firstL = false;
            json += fmt::format("\"{}\":{{\"name\":\"{}\",\"active\":{},\"runs\":[",
                key, jsonEsc(l.name), l.active ? "true" : "false");
            bool firstR = true;
            int n = 0;
            for (auto& r : l.runs) {
                store += fmt::format("R|{}|{}|{}|{}|{}\n", key, r.a, r.b, r.t, r.practice ? 1 : 0);
                if (!firstR) json += ",";
                firstR = false;
                json += fmt::format("{{\"id\":\"{}-{}-{}\",\"a\":{},\"b\":{},\"t\":{},\"m\":\"{}\"}}",
                    key, r.t, n++, r.a, r.b, r.t, r.practice ? "Pr\\u00e1ctica" : "Normal");
            }
            json += "]}";
        }
        json += "}}";
        writeAtomic(dir / "store.txt", store);
        writeAtomic(dir / "progress.json", json);

        auto copyDir = userCopyDir();
        if (!copyDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(copyDir, ec);
            if (!ec) writeAtomic(copyDir / "progress.json", json);
        }
    }

    void loadAll() {
        auto res = file::readString(Mod::get()->getSaveDir() / "store.txt");
        if (res.isErr()) return;
        std::string text = res.unwrap();
        size_t pos = 0;
        while (pos < text.size()) {
            auto end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            auto p = splitBar(text.substr(pos, end - pos));
            pos = end + 1;
            try {
                if (p[0] == "L" && p.size() >= 5) {
                    auto& l = g_levels[std::stoi(p[1])];
                    l.active = p[2] == "1";
                    l.last = std::stoll(p[3]);
                    l.name = p[4];
                } else if (p[0] == "R" && p.size() >= 6) {
                    g_levels[std::stoi(p[1])].runs.push_back(
                        {std::stoll(p[4]), std::stoi(p[2]), std::stoi(p[3]), p[5] == "1"});
                }
            } catch (...) {}
        }
    }

    bool isTracked(PlayLayer* pl) {
        auto it = g_levels.find(keyFor(pl->m_level));
        return it != g_levels.end() && it->second.active;
    }

    void recordRun(PlayLayer* pl, int a, int b) {
        if (pl->m_isTestMode) return;
        auto it = g_levels.find(keyFor(pl->m_level));
        if (it == g_levels.end() || !it->second.active) return;
        if (a < 0 || b > 100 || a >= b) return;
        it->second.runs.push_back({nowMs(), a, b, pl->m_isPracticeMode});
        it->second.last = nowMs();
        saveAll();
    }
}

$on_mod(Loaded) {
    loadAll();
}

// ---------------------------------------------------------------------------
// Menu: lista de niveles con un check para activar el seguimiento
// ---------------------------------------------------------------------------
class TrackerPopup : public geode::Popup {
protected:
    bool init() {
        if (!Popup::init(330.f, 240.f)) return false;
        this->setTitle("Progress Tracker");

        if (auto pl = PlayLayer::get()) {
            ensureLevel(pl->m_level);
            saveAll();
        }

        auto hint = CCLabelBMFont::create("Activa un nivel para guardar sus intentos", "chatFont.fnt");
        hint->setScale(.6f);
        hint->setOpacity(170);
        m_mainLayer->addChildAtPosition(hint, Anchor::Center, {0.f, m_size.height / 2 - 40.f});

        std::vector<std::pair<int, Lvl*>> v;
        for (auto& [k, l] : g_levels) v.push_back({k, &l});
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.second->last > y.second->last; });

        float top = m_size.height / 2 - 72.f;
        int shown = 0;
        for (auto& [k, l] : v) {
            if (shown >= 4) break;
            float y = top - shown * 34.f;
            std::string name = l->name.size() > 20 ? l->name.substr(0, 19) + ".." : l->name;

            auto lbl = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
            lbl->setScale(.45f);
            lbl->setAnchorPoint({0.f, .5f});
            m_mainLayer->addChildAtPosition(lbl, Anchor::Center, {-m_size.width / 2 + 22.f, y + 7.f});

            auto sub = CCLabelBMFont::create(fmt::format("{} intentos", l->runs.size()).c_str(), "chatFont.fnt");
            sub->setScale(.6f);
            sub->setOpacity(160);
            sub->setAnchorPoint({0.f, .5f});
            m_mainLayer->addChildAtPosition(sub, Anchor::Center, {-m_size.width / 2 + 22.f, y - 9.f});

            auto tog = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(TrackerPopup::onToggle), .7f);
            tog->setTag(k);
            tog->toggle(l->active);
            m_buttonMenu->addChildAtPosition(tog, Anchor::Center, {m_size.width / 2 - 34.f, y});
            ++shown;
        }

        auto spr = ButtonSprite::create("Abrir carpeta", "bigFont.fnt", "GJ_button_01.png", .6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(TrackerPopup::onFolder));
        m_buttonMenu->addChildAtPosition(btn, Anchor::Bottom, {0.f, 24.f});
        return true;
    }

    void onToggle(CCObject* sender) {
        auto t = static_cast<CCMenuItemToggler*>(sender);
        bool on = !t->isToggled();
        auto it = g_levels.find(t->getTag());
        if (it == g_levels.end()) return;
        it->second.active = on;
        saveAll();
        Notification::create(on ? "Seguimiento activado" : "Seguimiento desactivado",
            NotificationIcon::Success, .8f)->show();
    }

    void onFolder(CCObject*) {
        auto d = userCopyDir();
        if (!d.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(d, ec);
            file::openFolder(d);
        } else {
            file::openFolder(Mod::get()->getSaveDir());
        }
    }

public:
    static TrackerPopup* create() {
        auto ret = new TrackerPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(PTPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto win = CCDirector::get()->getWinSize();

        auto spr = ButtonSprite::create("Progreso", "bigFont.fnt", "GJ_button_01.png", .7f);
        spr->setScale(.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PTPauseLayer::onTracker));

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        // esquina superior izquierda, debajo del boton que ya tienes ahi
        btn->setPosition({52.f, win.height - 80.f});
        menu->addChild(btn);
        this->addChild(menu);
    }

    void onTracker(CCObject*) {
        if (auto p = TrackerPopup::create()) p->show();
    }
};

// ---------------------------------------------------------------------------
// Registro automatico de intentos: inicio% -> fin% (al morir o al completar)
// ---------------------------------------------------------------------------
class $modify(PTPlayLayer, PlayLayer) {
    struct Fields {
        int startPct = 0;
        float lastX = 0.f;
        bool gotStart = false;
        bool recorded = false;
        bool cheat = false;
    };

    static void onModify(auto& self) {
        // Nuestro hook envuelve a los de otros mods: asi vemos el resultado final
        // (si un noclip impidio la muerte, o si otro mod movio al jugador al inicio).
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::VeryEarly);
        (void)self.setHookPriority("PlayLayer::postUpdate", Priority::VeryEarly);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->startPct = static_cast<int>(this->getCurrentPercent());
        m_fields->lastX = m_player1 ? m_player1->getPositionX() : 0.f;
        m_fields->gotStart = false;
        m_fields->recorded = false;
        m_fields->cheat = false;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!m_player1) return;

        // Speedhack: la escala de tiempo no coincide con la del propio juego
        // (los triggers de time warp legitimos si cuadran con m_timeWarp).
        float ts = CCDirector::get()->getScheduler()->getTimeScale();
        float tw = m_gameState.m_timeWarp;
        if (std::fabs(ts - 1.f) > 0.001f && std::fabs(ts - tw) > 0.01f) {
            m_fields->cheat = true;
        }

        // Porcentaje de inicio: el de la primera vez que el jugador se mueve tras el reinicio.
        // (Asi cuenta tambien cuando otro mod te coloca en un startpos despues del reset.)
        if (!m_fields->gotStart && !m_player1->m_isDead) {
            float x = m_player1->getPositionX();
            if (std::fabs(x - m_fields->lastX) > 0.001f) {
                m_fields->gotStart = true;
                m_fields->startPct = static_cast<int>(this->getCurrentPercent() + 0.05f);
            }
            m_fields->lastX = x;
        }
    }

    void finishRun(int a, int b) {
        if (!isTracked(this)) return;
        if (m_fields->cheat && Mod::get()->getSettingValue<bool>("detect-cheats")) {
            Notification::create("Intento no guardado (noclip o speedhack detectado)",
                NotificationIcon::Warning, 1.5f)->show();
            return;
        }
        recordRun(this, a, b);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        int end = static_cast<int>(this->getCurrentPercent());
        bool already = m_fields->recorded;
        bool wasDead = player->m_isDead;
        bool ending = m_levelEndAnimationStarted;
        PlayLayer::destroyPlayer(player, object);

        // Noclip: el juego pidio matar al jugador pero sigue vivo.
        if (!wasDead && !ending && object != m_anticheatSpike && !player->m_isDead) {
            m_fields->cheat = true;
            return;
        }
        if (already) return;
        if ((m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead)) {
            m_fields->recorded = true;
            finishRun(m_fields->startPct, end);
        }
    }

    void levelComplete() {
        if (!m_fields->recorded) {
            m_fields->recorded = true;
            finishRun(m_fields->startPct, 100);
        }
        PlayLayer::levelComplete();
    }
};
