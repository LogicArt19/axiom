// render.h : top-down 3D renderer for the battlefield
#pragma once
#include "sim.h"

namespace ax {

struct Particle { Vector3 pos, vel; float life, maxLife, size, gravity; Color col; bool additive; };
struct Beam { Vector3 a, b; float time, dur; Color col; float width; };
struct GroundMark { Vector2 pos; float radius; float time, dur; Color col; };

struct RenderView {
    Camera3D cam{};
    int team = 0;                 // perspective for fog of war (-1 = everything)
    int player = 0;
    float alpha = 1;              // interpolation between ticks
    const std::vector<uint16_t>* selection = nullptr;
    uint16_t hovered = 0;
    // placement / targeting previews
    int placeType = 0; Vector2 placePos{}; bool placeOk = false;
    bool aiming = false; Vector2 aimFrom{}, aimAt{}; float aimRange = 0, aimRadius = 0; int aimMode = 0;
};

class Renderer {
public:
    void init();
    void shutdown();
    void onMapLoaded(const World& w);
    void render(const World& w, const RenderView& v);
    void handleEvent(const World& w, const GameEvent& e);
    void update(float dt);
    void clearEffects() { particles.clear(); beams.clear(); marks.clear(); }
    void clickMarker(Vector2 p, bool attack) { marks.push_back({p, 0.9f, clock, 0.45f, attack ? Color{255, 80, 70, 255} : Color{90, 230, 120, 255}}); }
    float clock = 0;

private:
    Shader shader{};
    int locViewPos = -1, locSun = -1, locFogColor = -1, locFogDensity = -1, locBrightness = -1, locEmissive = -1, locMapInfo = -1,
        locShadows = -1, locTime = -1, locUseFog = -1;
    Mesh cube{}, sphere{}, terrain{};
    bool terrainLoaded = false;
    ::Material mat{};
    Texture2D heightTex{}, fogTex{};
    Image fogImg{};
    int mapVersion = -1;
    float baseEmissive = 0;
    std::vector<Particle> particles;
    std::vector<Beam> beams;
    std::vector<GroundMark> marks;

    void setEmissive(float e);
    void box(const Matrix& m, Color c, float em = 0);
    void ball(Vector3 c, float r, Color col, float em = 0);
    void ring(Vector2 c, float r, float thick, Color col, float y = 0.05f);
    void disc(Vector2 c, float r, Color col, float y = 0.04f);
    void drawEntity(const World& w, const Entity& e, const RenderView& v);
    void drawUnitModel(const Entity& e, Vector3 pos, float facing, Color team, Color accent, float t, float moveAmt, float attackT, float scale);
    void drawBuildingModel(const World& w, const Entity& e, Color team, Color accent);
    void updateHeightTex(const World& w);
    void updateFogTex(const World& w, int team);
    void burst(Vector3 p, Color c, int n, float speed, float life, float size, float gravity, bool additive);
};

extern Renderer g_renderer;

} // namespace ax
