// AXIOM Launcher : installs, updates (GitHub Releases) and starts the game.
//
// Folder layout next to the launcher:
//   AxiomLauncher(.exe)
//   launcher.ini            repo=owner/name   (pre-filled by the CI build)
//   game/AxiomGame(.exe)
//   game/AxiomServer(.exe)
//   game/version.txt
//
// Update flow: GitHub API (latest release) -> pick the asset for this OS -> curl download ->
// extract (tar on Windows 10+, unzip/python elsewhere) -> swap the game folder atomically.
#include "raylib.h"
#include "proc.h"
#include "../src/font_data.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

#ifndef AXIOM_VERSION_STR
#define AXIOM_VERSION_STR "1.0.0"
#endif
#ifndef AXIOM_REPO
#define AXIOM_REPO ""
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static const char* kGameExe = "AxiomGame.exe";
static const char* kLauncherExe = "AxiomLauncher.exe";
static const char* kPlatformTag = "windows";
#else
static const char* kGameExe = "AxiomGame";
static const char* kLauncherExe = "AxiomLauncher";
static const char* kPlatformTag = "linux";
#endif

// ---------------------------------------------------------------------------
// theme / drawing
// ---------------------------------------------------------------------------
static const Color C_BG = {8, 11, 17, 255}, C_PANEL = {16, 20, 30, 235}, C_TEXT = {232, 238, 248, 255}, C_DIM = {140, 152, 172, 255},
                   C_ACCENT = {41, 227, 255, 255}, C_ACCENT2 = {255, 70, 110, 255}, C_GOOD = {80, 230, 140, 255}, C_BAD = {255, 84, 84, 255},
                   C_GOLD = {255, 196, 60, 255};
static Font g_med[3], g_bold[3];
static const int kSizes[3] = {18, 28, 64};

static const Font& pick(float s, bool bold) {
    for (int i = 0; i < 3; i++) if (kSizes[i] >= s * 0.92f) return bold ? g_bold[i] : g_med[i];
    return bold ? g_bold[2] : g_med[2];
}
static void txt(const std::string& s, float x, float y, float size, Color c, bool bold = false, int align = 0) {
    const Font& f = pick(size, bold);
    if (align) {
        float w = MeasureTextEx(f, s.c_str(), size, size * 0.02f).x;
        x -= align == 1 ? w / 2 : w;
    }
    DrawTextEx(f, s.c_str(), {floorf(x), floorf(y)}, size, size * 0.02f, c);
}
static float txtW(const std::string& s, float size, bool bold = false) { return MeasureTextEx(pick(size, bold), s.c_str(), size, size * 0.02f).x; }

static bool button(Rectangle r, const char* label, bool primary, bool enabled = true) {
    bool h = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color bg = !enabled ? Color{30, 34, 44, 255} : primary ? (h ? Color{130, 240, 255, 255} : C_ACCENT) : (h ? Color{40, 50, 70, 255} : Color{26, 32, 46, 255});
    DrawRectangleRec(r, bg);
    if (h && !primary) DrawRectangle((int)r.x, (int)r.y, 4, (int)r.height, C_ACCENT);
    float fs = std::min(r.height * 0.42f, 26.f);
    txt(label, r.x + r.width / 2, r.y + (r.height - fs) / 2 - 1, fs, primary ? Color{5, 12, 20, 255} : (enabled ? C_TEXT : C_DIM), true, 1);
    if (h) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ---------------------------------------------------------------------------
// tiny JSON helpers (enough for the GitHub releases API)
// ---------------------------------------------------------------------------
static size_t findKey(const std::string& j, const char* key, size_t from) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos) return p;
    p = j.find(':', p + k.size());
    return p == std::string::npos ? p : p + 1;
}
static std::string jsonStr(const std::string& j, const char* key, size_t from = 0, size_t* end = nullptr) {
    size_t p = findKey(j, key, from);
    if (p == std::string::npos) return "";
    while (p < j.size() && (j[p] == ' ' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t')) p++;
    if (p >= j.size() || j[p] != '"') return "";
    std::string out;
    for (p++; p < j.size() && j[p] != '"'; p++) {
        if (j[p] == '\\' && p + 1 < j.size()) {
            char c = j[++p];
            if (c == 'n') out += '\n';
            else if (c == 'r') {}
            else if (c == 't') out += ' ';
            else if (c == 'u' && p + 4 < j.size()) {
                unsigned cp = (unsigned)strtoul(j.substr(p + 1, 4).c_str(), nullptr, 16);
                p += 4;
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
                else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
            } else out += c;
        } else out += j[p];
    }
    if (end) *end = p;
    return out;
}
static long long jsonNum(const std::string& j, const char* key, size_t from = 0) {
    size_t p = findKey(j, key, from);
    if (p == std::string::npos) return -1;
    return atoll(j.c_str() + p);
}

static std::vector<int> parseVer(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v = v.substr(1);
    std::vector<int> out;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, '.')) out.push_back(atoi(part.c_str()));
    while (out.size() < 3) out.push_back(0);
    return out;
}
static bool newer(const std::string& a, const std::string& b) { return parseVer(a) > parseVer(b); }

static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a);
}
static std::string q(const fs::path& p) { return "\"" + p.string() + "\""; }

// ---------------------------------------------------------------------------
// updater state machine (runs commands on a worker thread)
// ---------------------------------------------------------------------------
enum State { ST_IDLE, ST_CHECKING, ST_UPTODATE, ST_AVAILABLE, ST_DOWNLOADING, ST_INSTALLING, ST_DONE, ST_ERROR, ST_NOREPO };

struct Updater {
    fs::path appDir, gameDir, tmpDir;
    std::string repo;
    std::string installed;               // installed version ("" if none)
    std::atomic<int> state{ST_IDLE};
    std::mutex mtx;
    std::string latest, notes, assetUrl, assetName, error, publishedAt;
    long long assetSize = 0;
    std::atomic<long long> downloaded{0};
    bool launcherReplaced = false;
    std::thread worker;
    bool closeOnPlay = true;

    void loadConfig() {
        repo = AXIOM_REPO;
        std::string ini = readFile(appDir / "launcher.ini");
        std::stringstream ss(ini);
        std::string line;
        while (std::getline(ss, line)) {
            line = trim(line);
            if (line.rfind("repo=", 0) == 0 && line.size() > 5) repo = trim(line.substr(5));
            if (line.rfind("close_on_play=", 0) == 0) closeOnPlay = line.substr(14) == "1";
        }
        installed = trim(readFile(gameDir / "version.txt"));
    }
    void saveConfig() {
        std::ofstream f(appDir / "launcher.ini");
        f << "# AXIOM launcher\nrepo=" << repo << "\nclose_on_play=" << (closeOnPlay ? 1 : 0) << "\n";
    }
    bool gameInstalled() const { return fs::exists(gameDir / kGameExe); }

    void join() { if (worker.joinable()) worker.join(); }

    void check() {
        join();
        if (repo.empty() || repo.find('/') == std::string::npos) { state = ST_NOREPO; return; }
        state = ST_CHECKING;
        worker = std::thread([this]() {
            std::error_code ec;
            fs::create_directories(tmpDir, ec);
            fs::path out = tmpDir / "latest.json";
            fs::remove(out, ec);
            const char* override_ = getenv("AXIOM_LAUNCHER_API");
            std::string url = override_ ? override_ : "https://api.github.com/repos/" + repo + "/releases/latest";
            std::string cmd = "curl -sSL --max-time 25 -H \"Accept: application/vnd.github+json\" -H \"User-Agent: AXIOM-Launcher\" -o " + q(out) + " \"" + url + "\"";
            int rc = axl::runHidden(cmd);
            std::string j = readFile(out);
            std::lock_guard<std::mutex> lk(mtx);
            if (rc != 0 || j.empty()) { error = "Impossible de joindre GitHub (connexion ? curl ?)"; state = ST_ERROR; return; }
            if (j.find("\"tag_name\"") == std::string::npos) {
                error = j.find("Not Found") != std::string::npos ? "Aucune release publiee sur " + repo : "Reponse GitHub inattendue";
                state = ST_ERROR; return;
            }
            latest = jsonStr(j, "tag_name");
            notes = jsonStr(j, "body");
            publishedAt = jsonStr(j, "published_at");
            assetUrl.clear(); assetName.clear(); assetSize = 0;
            size_t p = j.find("\"assets\"");
            while (p != std::string::npos) {
                size_t namePos = findKey(j, "name", p);
                if (namePos == std::string::npos) break;
                size_t e;
                std::string name = jsonStr(j, "name", p, &e);
                long long size = jsonNum(j, "size", e);
                std::string dl = jsonStr(j, "browser_download_url", e, &e);
                if (dl.empty()) break;
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.find(kPlatformTag) != std::string::npos && lower.find(".zip") != std::string::npos) { assetUrl = dl; assetName = name; assetSize = size; }
                p = e;
            }
            if (assetUrl.empty()) { error = std::string("La release ") + latest + " ne contient pas de paquet " + kPlatformTag; state = ST_ERROR; return; }
            bool need = !gameInstalled() || installed.empty() || newer(latest, installed);
            state = need ? ST_AVAILABLE : ST_UPTODATE;
        });
    }

    void update() {
        join();
        state = ST_DOWNLOADING;
        downloaded = 0;
        worker = std::thread([this]() {
            std::error_code ec;
            fs::path zip = tmpDir / "update.zip";
            fs::path ext = tmpDir / "extract";
            fs::remove(zip, ec);
            fs::remove_all(ext, ec);
            fs::create_directories(ext, ec);
            std::string url, name;
            { std::lock_guard<std::mutex> lk(mtx); url = assetUrl; name = assetName; }
            std::atomic<bool> dlDone{false};
            std::thread poll([&]() {
                while (!dlDone) {
                    std::error_code e2;
                    if (fs::exists(zip, e2)) downloaded = (long long)fs::file_size(zip, e2);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
            int rc = axl::runHidden("curl -sSL --fail --retry 3 -H \"User-Agent: AXIOM-Launcher\" -o " + q(zip) + " \"" + url + "\"");
            dlDone = true;
            poll.join();
            if (rc != 0 || !fs::exists(zip)) { std::lock_guard<std::mutex> lk(mtx); error = "Telechargement echoue"; state = ST_ERROR; return; }
            downloaded = (long long)fs::file_size(zip, ec);
            state = ST_INSTALLING;
#ifdef _WIN32
            rc = axl::runHidden("tar -xf " + q(zip) + " -C " + q(ext));
#else
            rc = axl::runHidden("unzip -o -q " + q(zip) + " -d " + q(ext) + " 2>/dev/null");
            if (rc != 0) rc = axl::runHidden("python3 -m zipfile -e " + q(zip) + " " + q(ext));
            if (rc != 0) rc = axl::runHidden("bsdtar -xf " + q(zip) + " -C " + q(ext));
#endif
            if (rc != 0) { std::lock_guard<std::mutex> lk(mtx); error = "Extraction impossible"; state = ST_ERROR; return; }
            // locate the 'game' folder inside the archive
            fs::path newGame;
            for (auto it = fs::recursive_directory_iterator(ext, ec); it != fs::recursive_directory_iterator(); ++it) {
                if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                if (it->is_directory() && it->path().filename() == "game" && fs::exists(it->path() / kGameExe)) { newGame = it->path(); break; }
            }
            if (newGame.empty()) { std::lock_guard<std::mutex> lk(mtx); error = "Archive invalide (dossier game introuvable)"; state = ST_ERROR; return; }
#ifndef _WIN32
            fs::permissions(newGame / kGameExe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
            if (fs::exists(newGame / "AxiomServer")) fs::permissions(newGame / "AxiomServer", fs::perms::owner_exec, fs::perm_options::add, ec);
#endif
            fs::path old = appDir / "game_old";
            fs::remove_all(old, ec);
            if (fs::exists(gameDir)) {
                fs::rename(gameDir, old, ec);
                if (ec) { std::lock_guard<std::mutex> lk(mtx); error = "Fermez le jeu avant de mettre a jour"; state = ST_ERROR; return; }
            }
            fs::rename(newGame, gameDir, ec);
            if (ec) {
                fs::copy(newGame, gameDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (ec) { fs::rename(old, gameDir, ec); std::lock_guard<std::mutex> lk(mtx); error = "Installation impossible : " + ec.message(); state = ST_ERROR; return; }
            }
            fs::remove_all(old, ec);
            {
                std::lock_guard<std::mutex> lk(mtx);
                std::ofstream(gameDir / "version.txt") << (latest.size() && (latest[0] == 'v' || latest[0] == 'V') ? latest.substr(1) : latest) << "\n";
                installed = trim(readFile(gameDir / "version.txt"));
            }
            // self-update of the launcher (rename the running binary, drop the new one in place)
            fs::path newLauncher = newGame.parent_path() / kLauncherExe;
            fs::path me = appDir / kLauncherExe;
            if (fs::exists(newLauncher) && fs::exists(me) && fs::file_size(newLauncher, ec) != fs::file_size(me, ec)) {
                fs::path bak = appDir / (std::string(kLauncherExe) + ".old");
                fs::remove(bak, ec);
                fs::rename(me, bak, ec);
                if (!ec) { fs::copy_file(newLauncher, me, fs::copy_options::overwrite_existing, ec); launcherReplaced = !ec; }
            }
            fs::remove_all(tmpDir, ec);
            state = ST_DONE;
        });
    }
};

// ---------------------------------------------------------------------------
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1100, 660, "AXIOM Launcher");
    SetTargetFPS(60);
    {
        Image icon = GenImageColor(64, 64, Color{10, 14, 22, 255});
        ImageDrawTriangle(&icon, {32, 6}, {6, 58}, {58, 58}, C_ACCENT);
        ImageDrawTriangle(&icon, {32, 26}, {20, 50}, {44, 50}, Color{10, 14, 22, 255});
        ImageDrawRectangle(&icon, 14, 40, 36, 5, C_ACCENT2);
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 160; c < 256; c++) cps.push_back(c);
    cps.push_back(0x2019); cps.push_back(0x2026); cps.push_back(0x2022);
    for (int i = 0; i < 3; i++) {
        g_med[i] = LoadFontFromMemory(".ttf", kFontMedium, (int)kFontMediumSize, kSizes[i], cps.data(), (int)cps.size());
        g_bold[i] = LoadFontFromMemory(".ttf", kFontBold, (int)kFontBoldSize, kSizes[i], cps.data(), (int)cps.size());
        SetTextureFilter(g_med[i].texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(g_bold[i].texture, TEXTURE_FILTER_BILINEAR);
    }

    Updater up;
    up.appDir = fs::path(GetApplicationDirectory());
    up.gameDir = up.appDir / "game";
    up.tmpDir = up.appDir / "_update";
    std::error_code ec;
    fs::remove(up.appDir / (std::string(kLauncherExe) + ".old"), ec);
    up.loadConfig();
    up.check();

    char repoBuf[128];
    snprintf(repoBuf, sizeof repoBuf, "%s", up.repo.c_str());
    bool editingRepo = false;
    float notesScroll = 0;
    float t = 0;
    bool quit = false;
    std::string toast;
    float toastTime = -10;

    const char* autotest = getenv("AXIOM_LAUNCHER_TEST");
    int frameNo = 0;
    while (!WindowShouldClose() && !quit) {
        float dt = GetFrameTime();
        if (autotest) {
            frameNo++;
            if (up.state == ST_AVAILABLE && frameNo > 30) { TakeScreenshot("l_available.png"); up.update(); }
            if (up.state == ST_DONE && frameNo % 30 == 0) { TakeScreenshot("l_done.png"); quit = true; }
            if (up.state == ST_ERROR && frameNo > 60) { TakeScreenshot("l_error.png"); quit = true; }
            if (frameNo > 1800) quit = true;
        }
        t += dt;
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
        int st = up.state;
        BeginDrawing();
        ClearBackground(C_BG);
        // animated backdrop
        for (int i = 0; i < 14; i++) {
            float y = fmodf(i * 53.f + t * (12 + i), H + 200) - 100;
            DrawLineEx({0, y}, {W, y - 220}, 1, Color{41, 227, 255, (unsigned char)(10 + i)});
        }
        DrawRectangleGradientH(0, 0, (int)(W * 0.55f), (int)H, Color{6, 9, 15, 250}, Color{6, 9, 15, 0});
        // left: brand
        txt("AXIOM", 60, 60, 92, C_TEXT, true);
        DrawRectangle(62, 160, 120, 6, C_ACCENT);
        DrawRectangle(188, 160, 40, 6, C_ACCENT2);
        txt("TACTICAL COMPETITIVE SHOOTER", 62, 180, 18, C_DIM, true);
        txt(std::string("Launcher ") + AXIOM_VERSION_STR, 62, H - 34, 16, C_DIM);

        // status card
        Rectangle card = {60, 240, 460, 300};
        DrawRectangleRec(card, C_PANEL);
        DrawRectangle((int)card.x, (int)card.y, 4, (int)card.height, C_ACCENT);
        txt("VERSION INSTALLÉE", card.x + 28, card.y + 24, 16, C_DIM, true);
        txt(up.gameInstalled() ? (up.installed.empty() ? "inconnue" : up.installed) : "non installé", card.x + 28, card.y + 46, 28, C_TEXT, true);
        txt("DERNIÈRE VERSION", card.x + 250, card.y + 24, 16, C_DIM, true);
        {
            std::lock_guard<std::mutex> lk(up.mtx);
            txt(up.latest.empty() ? "-" : up.latest, card.x + 250, card.y + 46, 28, C_ACCENT, true);
        }
        std::string status; Color sc = C_TEXT;
        switch (st) {
        case ST_CHECKING: status = "Recherche de mises à jour…"; break;
        case ST_UPTODATE: status = "Le jeu est à jour."; sc = C_GOOD; break;
        case ST_AVAILABLE: status = up.gameInstalled() ? "Mise à jour disponible !" : "Prêt à installer."; sc = C_GOLD; break;
        case ST_DOWNLOADING: status = "Téléchargement…"; break;
        case ST_INSTALLING: status = "Installation…"; break;
        case ST_DONE: status = up.launcherReplaced ? "Installé. Redémarrez le launcher." : "Installation terminée."; sc = C_GOOD; break;
        case ST_ERROR: { std::lock_guard<std::mutex> lk(up.mtx); status = up.error; sc = C_BAD; break; }
        case ST_NOREPO: status = "Renseignez le dépôt GitHub ci-dessous."; sc = C_GOLD; break;
        default: status = ""; break;
        }
        txt(status, card.x + 28, card.y + 100, 18, sc, true);
        // progress
        if (st == ST_DOWNLOADING || st == ST_INSTALLING) {
            float frac = st == ST_INSTALLING ? 1.f : (up.assetSize > 0 ? (float)up.downloaded / (float)up.assetSize : 0.f);
            Rectangle bar = {card.x + 28, card.y + 134, card.width - 56, 10};
            DrawRectangleRec(bar, Color{40, 46, 60, 255});
            DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * std::min(1.f, frac)), (int)bar.height, C_ACCENT);
            txt(TextFormat("%.1f / %.1f Mo", up.downloaded / 1048576.0, up.assetSize / 1048576.0), card.x + 28, card.y + 152, 16, C_DIM);
        }
        bool busy = st == ST_CHECKING || st == ST_DOWNLOADING || st == ST_INSTALLING;
        // main action
        Rectangle play = {card.x + 28, card.y + card.height - 96, card.width - 56, 68};
        if (st == ST_AVAILABLE) {
            if (button(play, up.gameInstalled() ? "METTRE À JOUR" : "INSTALLER", true)) up.update();
        } else {
            bool can = up.gameInstalled() && !busy;
            if (button(play, "JOUER", true, can)) {
                fs::path exe = up.gameDir / kGameExe;
                if (axl::launchDetached(exe.string(), up.gameDir.string())) {
                    toast = "Lancement d'AXIOM…"; toastTime = t;
                    if (up.closeOnPlay) quit = true;
                } else { toast = "Impossible de lancer le jeu"; toastTime = t; }
            }
        }
        // secondary actions
        float by = card.y + card.height + 18;
        if (button({card.x, by, 150, 44}, "VÉRIFIER", false, !busy)) up.check();
        if (button({card.x + 160, by, 150, 44}, "RÉPARER", false, !busy && st != ST_NOREPO && st != ST_ERROR)) up.update();
        if (button({card.x + 320, by, 140, 44}, "DOSSIER", false)) axl::openShell(up.gameInstalled() ? up.gameDir.string() : up.appDir.string());

        // right: patch notes
        Rectangle notes = {W - 520, 60, 460, H - 200};
        DrawRectangleRec(notes, C_PANEL);
        txt("NOTES DE VERSION", notes.x + 24, notes.y + 20, 16, C_ACCENT, true);
        {
            std::lock_guard<std::mutex> lk(up.mtx);
            std::string body = up.notes.empty() ? (st == ST_CHECKING ? "Chargement…" : "Les notes de la dernière release GitHub s'afficheront ici.") : up.notes;
            BeginScissorMode((int)notes.x, (int)(notes.y + 56), (int)notes.width, (int)(notes.height - 70));
            if (CheckCollisionPointRec(GetMousePosition(), notes)) notesScroll -= GetMouseWheelMove() * 40;
            if (notesScroll < 0) notesScroll = 0;
            float y = notes.y + 60 - notesScroll;
            std::stringstream ss(body);
            std::string line;
            while (std::getline(ss, line)) {
                line = trim(line);
                bool head = false;
                while (!line.empty() && line[0] == '#') { line.erase(0, 1); head = true; }
                line = trim(line);
                if (!line.empty() && (line[0] == '*' || line[0] == '-')) line = "• " + trim(line.substr(1));
                line.erase(std::remove(line.begin(), line.end(), '*'), line.end());
                // naive word wrap
                float fs = head ? 20.f : 17.f;
                std::string cur;
                std::stringstream ws(line);
                std::string word;
                while (ws >> word) {
                    std::string trial = cur.empty() ? word : cur + " " + word;
                    if (txtW(trial, fs, head) > notes.width - 48 && !cur.empty()) { txt(cur, notes.x + 24, y, fs, head ? C_TEXT : C_DIM, head); y += fs + 8; cur = word; }
                    else cur = trial;
                }
                txt(cur, notes.x + 24, y, fs, head ? C_TEXT : C_DIM, head);
                y += fs + (line.empty() ? 4 : 10);
            }
            EndScissorMode();
        }
        // repo settings
        Rectangle rr = {W - 520, H - 124, 460, 44};
        txt("DÉPÔT GITHUB (owner/nom)", rr.x, rr.y - 24, 15, C_DIM, true);
        bool hov = CheckCollisionPointRec(GetMousePosition(), rr);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) editingRepo = hov;
        DrawRectangleRec(rr, editingRepo ? Color{30, 40, 60, 255} : Color{20, 26, 38, 255});
        DrawRectangleLinesEx(rr, editingRepo ? 2.f : 1.f, editingRepo ? C_ACCENT : Color{60, 70, 90, 255});
        if (editingRepo) {
            int c;
            while ((c = GetCharPressed()) > 0) { size_t l = strlen(repoBuf); if (c >= 32 && c < 127 && l < sizeof repoBuf - 1) { repoBuf[l] = (char)c; repoBuf[l + 1] = 0; } }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && strlen(repoBuf)) repoBuf[strlen(repoBuf) - 1] = 0;
            if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) { const char* cb = GetClipboardText(); if (cb) strncat(repoBuf, cb, sizeof repoBuf - strlen(repoBuf) - 1); }
            if (IsKeyPressed(KEY_ENTER)) { editingRepo = false; up.repo = trim(repoBuf); up.saveConfig(); up.check(); }
        }
        txt(repoBuf[0] ? repoBuf : "ex : monpseudo/axiom", rr.x + 12, rr.y + 12, 18, repoBuf[0] ? C_TEXT : C_DIM);
        if (button({rr.x + rr.width - 110, rr.y + 50, 110, 34}, "VALIDER", false)) { editingRepo = false; up.repo = trim(repoBuf); up.saveConfig(); up.check(); }
        bool cop = up.closeOnPlay;
        Rectangle tg = {rr.x, rr.y + 54, 26, 26};
        DrawRectangleRec(tg, cop ? C_ACCENT : Color{40, 46, 60, 255});
        if (CheckCollisionPointRec(GetMousePosition(), {tg.x, tg.y, 300, 26}) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { up.closeOnPlay = !up.closeOnPlay; up.saveConfig(); }
        txt("Fermer le launcher au lancement", tg.x + 36, tg.y + 3, 16, C_DIM);
        if (t - toastTime < 2.5f) txt(toast, W / 2, H - 40, 18, C_GOLD, true, 1);
        EndDrawing();
    }
    up.join();
    for (int i = 0; i < 3; i++) { UnloadFont(g_med[i]); UnloadFont(g_bold[i]); }
    CloseWindow();
    return 0;
}
