// render.h : 3D world renderer, viewmodel and visual effects
#pragma once
#include "world.h"

namespace ax {

struct Particle { Vector3 pos, vel; float life, maxLife, size, gravity; Color col; bool additive; };
struct Decal { Vector3 pos, normal; float time; Material mat; };
struct Tracer { Vector3 a, b; float time; bool local; };

struct ViewState {
    Vector3 eye{};
    float yaw = 0, pitch = 0;      // includes visual punch
    float fovY = 73.7f;
    int localId = -1;              // player whose viewmodel is drawn (-1 none)
    bool thirdPerson = false;
    float alpha = 1;               // interpolation factor
    float frameTime = 0;
};

class Renderer {
public:
    void init();
    void shutdown();
    void onMapLoaded(const Map& m);
    void render(const World& w, const ViewState& v);        // 3D pass (world + viewmodel)
    void handleEvent(const World& w, const GameEvent& e, int localId);
    void update(float dt, const World& w);
    void addViewKick(float k) { vmKick += k; }
    void notifyMouse(float dx, float dy) { swayX += dx; swayY += dy; }
    void clearEffects() { particles.clear(); decals.clear(); tracers.clear(); }
    Camera3D lastCamera{};
    std::function<void()> extra3D;          // custom 3D drawing hook (aim trainer, grenade preview)
    void drawSphere(Vector3 c, float r, Color col, float emissive = 0);

    // shared draw helpers (also used by menus)
    void drawBox(const Matrix& m, Color c, float emissive = 0);
    void drawWeaponModel(WeaponId w, const Matrix& base, Color tint, float t, bool viewmodel);

private:
    Shader shader{};
    int locViewPos = -1, locSun = -1, locFogColor = -1, locFogDensity = -1, locBrightness = -1, locFlash = -1,
        locEmissive = -1, locHeight = -1, locMapInfo = -1, locShadows = -1;
    Model mapModel{};
    bool mapLoaded = false;
    Mesh cube{}, sphere{};
    ::Material mat{};
    Texture2D heightTex{};
    Vector3 sunDir = {-0.42f, -0.78f, -0.46f};
    float mapW = 0, mapH = 0;

    std::vector<Particle> particles;
    std::vector<Decal> decals;
    std::vector<Tracer> tracers;
    float muzzleFlashTime = -10;
    Vector3 muzzleFlashPos{};
    float worldTime = 0;
    float renderTime = 0;
    float baseEmissive = 0;

    // viewmodel animation state
    float vmKick = 0, vmBobPhase = 0, vmBobAmp = 0, swayX = 0, swayY = 0, vmSwayX = 0, vmSwayY = 0;
    float vmLandDip = 0;
    float lastShotAnim = -10;
    float knifeSwingTime = -10;
    bool knifeHeavy = false;
    float throwAnimTime = -10;

    void setEmissive(float e);
    void drawPlayers(const World& w, const ViewState& v);
    void drawPlayer(const World& w, const Player& p, const ViewState& v, bool enemy);
    void drawViewmodel(const World& w, const ViewState& v);
    void drawEffects(const World& w, const ViewState& v);
    void drawSmokes(const World& w, const ViewState& v);
    void spawnBurst(Vector3 pos, Vector3 normal, Color c, int n, float speed, float life, float size, float gravity, bool additive);
};

extern Renderer g_renderer;

} // namespace ax
