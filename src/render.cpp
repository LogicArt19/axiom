#include "render.h"
#include "config.h"
#include "rlgl.h"

namespace ax {

Renderer g_renderer;

static const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* kFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform sampler2D texture0;     // tile height map (r = height / 8)
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float brightness;
uniform vec4 flashLight;
uniform float emissive;
uniform vec4 mapInfo;           // tiles w, tiles h, tile size, unused
uniform float shadows;
out vec4 finalColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float cellHeight(ivec2 c) {
    if (c.x < 0 || c.y < 0 || c.x >= int(mapInfo.x) || c.y >= int(mapInfo.y)) return 8.0;
    return texelFetch(texture0, c, 0).r * 8.0;
}
// exact sun visibility: walk the tile grid along the light ray (ray always rises)
float sunVisibility(vec3 p, vec3 d) {
    vec2 g = p.xz / mapInfo.z + mapInfo.xy * 0.5;
    vec2 dd = d.xz / mapInfo.z;
    ivec2 cell = ivec2(floor(g));
    ivec2 st = ivec2(dd.x > 0.0 ? 1 : -1, dd.y > 0.0 ? 1 : -1);
    vec2 tDelta = vec2(abs(dd.x) > 1e-6 ? abs(1.0 / dd.x) : 1e9, abs(dd.y) > 1e-6 ? abs(1.0 / dd.y) : 1e9);
    vec2 fr = g - floor(g);
    vec2 tMax = vec2(dd.x > 0.0 ? (1.0 - fr.x) * tDelta.x : fr.x * tDelta.x, dd.y > 0.0 ? (1.0 - fr.y) * tDelta.y : fr.y * tDelta.y);
    float t = 0.0;
    for (int i = 0; i < 48; i++) {
        float y = p.y + d.y * t;
        if (y > 7.0) return 1.0;
        if (y < cellHeight(cell) - 0.001) return 0.0;
        if (tMax.x < tMax.y) { t = tMax.x; tMax.x += tDelta.x; cell.x += st.x; }
        else { t = tMax.y; tMax.y += tDelta.y; cell.y += st.y; }
    }
    return 1.0;
}
void main() {
    vec3 n = normalize(fragNormal);
    vec4 base = fragColor * colDiffuse;
    float pid = fragColor.a;
    vec3 col = base.rgb;
    vec3 an = abs(n);
    vec2 uv = an.y > 0.5 ? fragPos.xz : (an.x > 0.5 ? fragPos.zy : fragPos.xy);
    if (pid < 0.99) {
        if (pid > 0.95) {            // concrete blocks
            vec2 g = uv / vec2(2.0, 1.0);
            g.x += floor(g.y) * 0.5;
            vec2 f = fract(g);
            float edge = min(min(f.x, 1.0 - f.x) * 2.0, min(f.y, 1.0 - f.y));
            float groove = smoothstep(0.0, 0.025, edge);
            float nn = vnoise(uv * 2.7) * 0.10 + vnoise(uv * 13.0) * 0.05 + hash(floor(g)) * 0.06;
            col *= (0.78 + 0.22 * groove) * (0.92 + nn);
            if (an.y < 0.5) col *= mix(0.72, 1.0, smoothstep(0.0, 0.9, fragPos.y));
        } else if (pid > 0.7) {      // wood planks
            float plank = floor(uv.y * 5.0);
            float grain = vnoise(vec2(uv.x * 1.5 + plank * 7.0, uv.y * 40.0)) * 0.18;
            float seam = smoothstep(0.0, 0.04, abs(fract(uv.y * 5.0) - 0.5) * 2.0 - 0.9 + 0.9);
            col *= (0.82 + grain + hash(vec2(plank, 3.0)) * 0.12) * (0.85 + 0.15 * seam);
            vec2 fe = fract(uv / 2.0);
            float frame = min(min(fe.x, 1.0 - fe.x), min(fe.y, 1.0 - fe.y));
            col *= mix(0.7, 1.0, smoothstep(0.02, 0.05, frame));
        } else if (pid > 0.5) {      // floor tiles
            vec2 f = fract(uv);
            float e = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
            col *= 0.88 + 0.12 * smoothstep(0.0, 0.02, e);
            col *= 0.9 + vnoise(uv * 0.6) * 0.14 + vnoise(uv * 9.0) * 0.04 + hash(floor(uv)) * 0.05;
        } else if (pid > 0.3) {      // metal
            col *= 0.85 + 0.15 * smoothstep(0.45, 0.5, abs(fract(uv.x * 4.0) - 0.5));
        } else {                     // site floor : stripes + tiles
            vec2 f = fract(uv);
            float e = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
            col *= 0.86 + 0.14 * smoothstep(0.0, 0.025, e);
            col *= 0.92 + vnoise(uv * 0.8) * 0.12;
        }
    }
    float diff = max(dot(n, -sunDir), 0.0);
    // sun shadows by marching the tile height field
    float sh = 1.0;
    if (shadows > 0.5 && diff > 0.0) sh = sunVisibility(fragPos + n * 0.02, -sunDir);
    float hemi = 0.55 + 0.45 * n.y;
    vec3 light = vec3(1.0, 0.95, 0.86) * diff * 0.72 * sh + vec3(0.58, 0.66, 0.80) * hemi * 0.62;
    if (an.y < 0.5) light *= mix(0.75, 1.0, smoothstep(0.0, 1.0, fragPos.y));
    if (flashLight.w > 0.0) {
        vec3 L = flashLight.xyz - fragPos;
        float d = length(L);
        light += vec3(1.0, 0.72, 0.38) * flashLight.w * max(dot(n, L / max(d, 0.001)), 0.0) / (1.0 + d * d * 0.35);
    }
    col *= light * brightness;
    col = mix(col, base.rgb * 1.35, emissive);
    float dist = length(viewPos - fragPos);
    float fog = 1.0 - exp(-pow(dist * fogDensity, 1.5));
    col = mix(col, fogColor, clamp(fog, 0.0, 1.0));
    finalColor = vec4(col, colDiffuse.a);
}
)";

static Matrix mul(const Matrix& a, const Matrix& b) { return MatrixMultiply(a, b); }

static Color teamColor(Team t) {
    if (g_settings.colorblindMode > 0 && t == TEAM_ATK) return {232, 196, 52, 255};
    return t == TEAM_ATK ? Color{222, 118, 56, 255} : Color{62, 138, 222, 255};
}

void Renderer::init() {
    shader = LoadShaderFromMemory(kVS, kFS);
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader, "matNormal");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    locViewPos = GetShaderLocation(shader, "viewPos");
    locSun = GetShaderLocation(shader, "sunDir");
    locFogColor = GetShaderLocation(shader, "fogColor");
    locFogDensity = GetShaderLocation(shader, "fogDensity");
    locBrightness = GetShaderLocation(shader, "brightness");
    locFlash = GetShaderLocation(shader, "flashLight");
    locEmissive = GetShaderLocation(shader, "emissive");
    locMapInfo = GetShaderLocation(shader, "mapInfo");
    locShadows = GetShaderLocation(shader, "shadows");
    cube = GenMeshCube(1, 1, 1);
    sphere = GenMeshSphere(1, 10, 14);
    mat = LoadMaterialDefault();
    mat.shader = shader;
    Vector3 sd = norm3(sunDir);
    SetShaderValue(shader, locSun, &sd, SHADER_UNIFORM_VEC3);
}

void Renderer::shutdown() {
    if (mapLoaded) { UnloadMesh(mapModel.meshes[0]); UnloadTexture(heightTex); }
    UnloadMesh(cube); UnloadMesh(sphere);
    UnloadShader(shader);
}

void Renderer::onMapLoaded(const Map& m) {
    if (mapLoaded) { UnloadMesh(mapModel.meshes[0]); MemFree(mapModel.meshes); UnloadTexture(heightTex); }
    Mesh mesh = m.buildMesh();
    mapModel = Model{};
    mapModel.meshCount = 1;
    mapModel.meshes = (Mesh*)MemAlloc(sizeof(Mesh));
    mapModel.meshes[0] = mesh;
    Image img = GenImageColor(m.w, m.h, BLACK);
    for (int z = 0; z < m.h; z++)
        for (int x = 0; x < m.w; x++) {
            unsigned char v = (unsigned char)clampf(m.at(x, z).height / 8.f * 255.f + 0.5f, 0, 255);
            ImageDrawPixel(&img, x, z, Color{v, v, v, 255});
        }
    heightTex = LoadTextureFromImage(img);
    SetTextureFilter(heightTex, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = heightTex;
    float info[4] = {(float)m.w, (float)m.h, Map::TILE, 0};
    SetShaderValue(shader, locMapInfo, info, SHADER_UNIFORM_VEC4);
    mapLoaded = true;
    mapW = (float)m.w; mapH = (float)m.h;
    clearEffects();
}

void Renderer::drawSphere(Vector3 c, float r, Color col, float emissive) {
    setEmissive(emissive);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = col;
    DrawMesh(sphere, mat, mul(MatrixScale(r, r, r), MatrixTranslate(c.x, c.y, c.z)));
    setEmissive(0);
}

void Renderer::setEmissive(float e) { SetShaderValue(shader, locEmissive, &e, SHADER_UNIFORM_FLOAT); }

void Renderer::drawBox(const Matrix& m, Color c, float emissive) {
    float e = std::max(emissive, baseEmissive);
    if (e != 0) setEmissive(e);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(cube, mat, m);
    if (e != 0) setEmissive(0);
}

static Matrix part(Vector3 size, Vector3 center) {
    return MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(center.x, center.y, center.z));
}

// Gun models in local space: x right, y up, -z forward (muzzle)
void Renderer::drawWeaponModel(WeaponId w, const Matrix& base, Color tint, float t, bool viewmodel) {
    (void)t; (void)viewmodel;
    const WeaponDef& d = weaponDef(w);
    Color metal = {46, 48, 54, 255}, dark = {30, 31, 35, 255}, acc = d.color;
    acc = mixColor(acc, tint, 0.15f);
    auto B = [&](Vector3 s, Vector3 c, Color col, float em = 0) { drawBox(mul(part(s, c), base), col, em); };
    switch (d.cls) {
    case WC_KNIFE:
        B({0.028f, 0.03f, 0.11f}, {0, 0, 0.02f}, dark);
        B({0.034f, 0.012f, 0.012f}, {0, 0, -0.04f}, metal);
        B({0.006f, 0.034f, 0.17f}, {0, 0.004f, -0.13f}, Color{190, 196, 205, 255}, 0.15f);
        B({0.004f, 0.012f, 0.16f}, {0, 0.02f, -0.125f}, acc);
        break;
    case WC_PISTOL: {
        float L = w == W_HORNET ? 0.24f : 0.19f;
        B({0.034f, 0.042f, L}, {0, 0.03f, -L * 0.5f + 0.04f}, w == W_HORNET ? Color{150, 152, 160, 255} : Color{86, 90, 100, 255});
        B({0.036f, 0.006f, L * 0.9f}, {0, 0.052f, -L * 0.5f + 0.04f}, acc);
        B({0.03f, 0.03f, L * 0.8f}, {0, 0.0f, -L * 0.4f + 0.03f}, dark);
        drawBox(mul(mul(part({0.032f, 0.11f, 0.05f}, {0, -0.05f, 0}), MatrixRotateX(-0.25f)), mul(MatrixTranslate(0, -0.01f, 0.035f), base)), acc);
        B({0.008f, 0.012f, 0.01f}, {0, 0.056f, -L + 0.06f}, dark);
        break;
    }
    case WC_GRENADE: {
        Color c = d.color;
        B({0.06f, 0.09f, 0.06f}, {0, 0, 0}, c);
        B({0.045f, 0.02f, 0.045f}, {0, 0.055f, 0}, metal);
        B({0.012f, 0.05f, 0.012f}, {0.02f, 0.07f, 0}, Color{200, 200, 200, 255});
        break;
    }
    case WC_BOMB:
        B({0.22f, 0.1f, 0.16f}, {0, 0, 0}, Color{60, 58, 52, 255});
        B({0.12f, 0.02f, 0.08f}, {0, 0.055f, 0.01f}, Color{20, 22, 20, 255});
        B({0.03f, 0.012f, 0.03f}, {0.07f, 0.055f, -0.05f}, Color{230, 40, 30, 255}, 0.9f);
        B({0.2f, 0.03f, 0.03f}, {0, 0.01f, -0.09f}, Color{150, 40, 30, 255});
        break;
    default: {
        // rifles / smgs / snipers / shotgun / lmg
        float len = 0.62f, barrel = 0.26f, magH = 0.15f, stockL = 0.2f;
        if (d.cls == WC_SMG) { len = 0.45f; barrel = 0.12f; magH = 0.16f; stockL = 0.14f; }
        if (d.cls == WC_SHOTGUN) { len = 0.6f; barrel = 0.36f; magH = 0; stockL = 0.22f; }
        if (d.cls == WC_SNIPER) { len = 0.72f; barrel = 0.42f; magH = 0.08f; stockL = 0.26f; }
        if (d.cls == WC_LMG) { len = 0.7f; barrel = 0.32f; magH = 0.12f; stockL = 0.22f; }
        // receiver
        B({0.065f, 0.095f, len * 0.5f}, {0, 0, -len * 0.18f}, acc);
        B({0.06f, 0.03f, len * 0.5f}, {0, 0.06f, -len * 0.18f}, metal);
        // handguard
        B({0.058f, 0.07f, len * 0.3f}, {0, -0.005f, -len * 0.55f}, dark);
        // barrel
        B({0.024f, 0.024f, barrel}, {0, 0.02f, -len * 0.7f - barrel * 0.5f + 0.05f}, metal);
        B({0.034f, 0.034f, 0.05f}, {0, 0.02f, -len * 0.7f - barrel + 0.04f}, dark);
        // magazine
        if (magH > 0) {
            if (d.cls == WC_LMG) B({0.12f, 0.12f, 0.12f}, {-0.02f, -0.1f, -len * 0.25f}, Color{70, 70, 60, 255});
            else drawBox(mul(mul(part({0.045f, magH, 0.07f}, {0, -magH * 0.5f, 0}), MatrixRotateX(w == W_VANGUARD || w == W_RAPTOR ? 0.25f : 0.08f)),
                             mul(MatrixTranslate(0, -0.045f, -len * 0.25f), base)), w == W_VANGUARD ? Color{120, 80, 40, 255} : dark);
        } else {
            B({0.05f, 0.05f, 0.24f}, {0, -0.05f, -len * 0.45f}, Color{110, 74, 46, 255});   // shotgun pump
        }
        // grip
        drawBox(mul(mul(part({0.04f, 0.1f, 0.05f}, {0, -0.05f, 0}), MatrixRotateX(-0.3f)), mul(MatrixTranslate(0, -0.04f, 0.0f), base)), dark);
        // stock
        B({0.05f, 0.09f, stockL}, {0, -0.01f, stockL * 0.5f + 0.02f}, w == W_VANGUARD || w == W_BREACH ? Color{120, 80, 45, 255} : dark);
        // sights / scope
        if (d.cls == WC_SNIPER) {
            B({0.055f, 0.055f, 0.28f}, {0, 0.115f, -len * 0.22f}, dark);
            B({0.07f, 0.07f, 0.04f}, {0, 0.115f, -len * 0.22f - 0.15f}, metal);
            B({0.05f, 0.05f, 0.01f}, {0, 0.115f, -len * 0.22f - 0.172f}, Color{80, 160, 220, 255}, 0.6f);
        } else {
            B({0.012f, 0.03f, 0.012f}, {0, 0.09f, -len * 0.62f}, dark);
            B({0.03f, 0.025f, 0.03f}, {0, 0.085f, 0.0f}, dark);
            if (w == W_SPECTER) B({0.04f, 0.04f, 0.07f}, {0, 0.1f, -len * 0.25f}, dark);   // red dot
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
void Renderer::spawnBurst(Vector3 pos, Vector3 normal, Color c, int n, float speed, float life, float size, float gravity, bool additive) {
    if (!g_settings.impactParticles) return;
    static Rng r(31337);
    for (int i = 0; i < n; i++) {
        Vector3 d = norm3(normal + V3(r.range(-0.8f, 0.8f), r.range(-0.8f, 0.8f), r.range(-0.8f, 0.8f)));
        Particle p;
        p.pos = pos; p.vel = d * (speed * r.range(0.4f, 1.0f));
        p.maxLife = p.life = life * r.range(0.6f, 1.2f);
        p.size = size * r.range(0.6f, 1.3f);
        p.gravity = gravity; p.col = c; p.additive = additive;
        particles.push_back(p);
    }
    if (particles.size() > 1500) particles.erase(particles.begin(), particles.begin() + (particles.size() - 1500));
}

struct Corpse { Vector3 pos; float yaw; Team team; float time; };
static std::vector<Corpse> g_corpses;
static float g_anim[MAX_PLAYERS];

void Renderer::handleEvent(const World& w, const GameEvent& e, int localId) {
    switch (e.type) {
    case EV_SHOT: {
        bool local = e.a == localId;
        if (local) { vmKick += 1.0f; lastShotAnim = worldTime; muzzleFlashTime = worldTime; }
        else { muzzleFlashTime = worldTime; muzzleFlashPos = e.pos + norm3(e.pos2 - e.pos) * 0.8f; }
        if (local) muzzleFlashPos = e.pos + norm3(e.pos2 - e.pos) * 0.6f;
        if (g_settings.bulletTracers && (!local || (w.players[e.a].shotsFired % 3 == 1) || weaponDef((WeaponId)e.weapon).cls == WC_SNIPER)) {
            Vector3 start = e.pos + norm3(e.pos2 - e.pos) * (local ? 1.2f : 0.7f) - V3(0, local ? 0.12f : 0.15f, 0);
            tracers.push_back({start, e.pos2, worldTime, local});
        }
        break;
    }
    case EV_IMPACT: {
        Material m = (Material)e.value;
        Vector3 n = e.pos2;
        if (!(e.flags & 1)) {
            decals.push_back({e.pos + n * 0.005f, n, worldTime, m});
            if (decals.size() > 300) decals.erase(decals.begin());
        }
        Color c = m == MAT_WOOD ? Color{150, 110, 70, 255} : (m == MAT_METAL ? Color{255, 220, 150, 255} : Color{170, 165, 155, 255});
        spawnBurst(e.pos + n * 0.02f, n, c, 6, 2.5f, 0.45f, 0.035f, 6.0f, false);
        if (m != MAT_WOOD) spawnBurst(e.pos + n * 0.02f, n, Color{255, 200, 120, 255}, 3, 6.f, 0.15f, 0.015f, 9.0f, true);
        break;
    }
    case EV_HIT: {
        Vector3 d = norm3(e.pos - e.pos2);
        spawnBurst(e.pos, d, Color{170, 20, 25, 255}, (e.flags & 1) ? 12 : 6, 2.2f, 0.4f, 0.04f, 8.0f, false);
        if (e.flags & 1 && (e.flags & 4)) spawnBurst(e.pos, d * -1.0f, Color{255, 240, 200, 255}, 6, 4.f, 0.2f, 0.02f, 6.0f, true);
        break;
    }
    case EV_KILL:
        if (e.b >= 0) g_corpses.push_back({w.players[e.b].pos, w.players[e.b].yaw, w.players[e.b].team, worldTime});
        break;
    case EV_ROUND_START: case EV_RESPAWN:
        if (e.type == EV_ROUND_START) { g_corpses.clear(); decals.clear(); }
        else for (size_t i = 0; i < g_corpses.size(); i++) if (worldTime - g_corpses[i].time > 6) { g_corpses.erase(g_corpses.begin() + i); i--; }
        break;
    case EV_FRAG_BOOM:
        spawnBurst(e.pos + V3(0, 0.2f, 0), V3(0, 1, 0), Color{255, 170, 60, 255}, 40, 9.f, 0.35f, 0.08f, 2.f, true);
        spawnBurst(e.pos + V3(0, 0.2f, 0), V3(0, 1, 0), Color{90, 85, 80, 255}, 30, 4.f, 1.4f, 0.18f, -0.5f, false);
        break;
    case EV_FLASH_POP:
        spawnBurst(e.pos, V3(0, 1, 0), Color{255, 255, 255, 255}, 25, 7.f, 0.25f, 0.06f, 0.f, true);
        break;
    case EV_BOMB_EXPLODE:
        spawnBurst(e.pos + V3(0, 0.5f, 0), V3(0, 1, 0), Color{255, 160, 50, 255}, 200, 22.f, 1.2f, 0.3f, 1.f, true);
        spawnBurst(e.pos + V3(0, 0.5f, 0), V3(0, 1, 0), Color{70, 65, 60, 255}, 120, 8.f, 3.5f, 0.6f, -0.4f, false);
        break;
    case EV_KNIFE_SWING:
        if (e.a == localId) { knifeSwingTime = worldTime; knifeHeavy = e.flags != 0; }
        break;
    case EV_NADE_THROW:
        if (e.a == localId) throwAnimTime = worldTime;
        break;
    case EV_LAND:
        if (e.a == localId) vmLandDip = std::min(1.0f, e.value / 60.f);
        break;
    default: break;
    }
}

void Renderer::update(float dt, const World& w) {
    worldTime += dt;
    renderTime = w.time;
    for (auto& p : particles) {
        p.life -= dt;
        p.vel.y -= p.gravity * dt;
        p.vel = p.vel * (1.0f - 1.5f * dt);
        p.pos += p.vel * dt;
        if (p.pos.y < 0.01f && p.gravity > 0) { p.pos.y = 0.01f; p.vel = p.vel * 0.3f; }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }), particles.end());
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(), [&](const Tracer& t) { return worldTime - t.time > 0.12f; }), tracers.end());
    decals.erase(std::remove_if(decals.begin(), decals.end(), [&](const Decal& d) { return worldTime - d.time > 40.f; }), decals.end());
    vmKick = approach(vmKick * expf(-14.f * dt), 0, dt * 0.5f);
    vmSwayX = lerpf(vmSwayX, clampf(swayX * 0.004f, -1.5f, 1.5f), clampf(12 * dt, 0, 1));
    vmSwayY = lerpf(vmSwayY, clampf(swayY * 0.004f, -1.5f, 1.5f), clampf(12 * dt, 0, 1));
    swayX = swayY = 0;
    vmLandDip = approach(vmLandDip, 0, dt * 4.f);
    for (auto& p : w.players) if (p.active) g_anim[p.id] += len2xz(p.vel) * dt * 2.2f;
}

// ---------------------------------------------------------------------------
void Renderer::drawPlayer(const World& w, const Player& p, const ViewState& v, bool enemy) {
    Vector3 pos = lerp3(p.prevPos, p.pos, v.alpha);
    float duck = lerpf(p.prevDuck, p.duck, v.alpha);
    float s = lerpf(STAND_HEIGHT, CROUCH_HEIGHT, duck) / STAND_HEIGHT;
    Color tc = teamColor(p.team);
    if (w.mode == MODE_DEATHMATCH) tc = enemy ? Color{220, 70, 60, 255} : tc;
    float em = 0.12f;
    if (enemy && g_settings.enemyHighlight == 2) { tc = Color{250, 225, 40, 255}; em = 0.45f; }
    Color body = mixColor(tc, Color{46, 50, 58, 255}, 0.35f);
    Color dark = {54, 58, 66, 255};
    Matrix world = mul(MatrixRotateY(-p.yaw * DEG), MatrixTranslate(pos.x, pos.y, pos.z));
    float speed = len2xz(p.vel);
    float swing = sinf(g_anim[p.id]) * clampf(speed / 5.f, 0, 1) * (duck > 0.5f ? 0.3f : 0.65f);
    auto B = [&](Vector3 sz, Vector3 c, Color col, float e = 0) { drawBox(mul(part(sz, c), world), col, e); };
    // legs (pivot at hip)
    float hip = 0.88f * s;
    for (int side = -1; side <= 1; side += 2) {
        float a = swing * side;
        Matrix leg = mul(mul(part({0.15f, hip, 0.18f}, {0, -hip * 0.5f, 0}), MatrixRotateX(a)), mul(MatrixTranslate(0.1f * side, hip, 0), world));
        drawBox(leg, dark);
        Matrix boot = mul(mul(part({0.16f, 0.1f, 0.26f}, {0, -hip + 0.05f, -0.04f}), MatrixRotateX(a)), mul(MatrixTranslate(0.1f * side, hip, 0), world));
        drawBox(boot, Color{24, 24, 26, 255});
    }
    // torso
    float chestY = hip + 0.3f * s;
    B({0.46f, 0.6f * s, 0.28f}, {0, chestY, 0}, body, 0.05f);
    B({0.48f, 0.12f * s, 0.3f}, {0, chestY + 0.1f * s, 0}, tc, em);      // team band
    if (p.armor > 0) B({0.4f, 0.4f * s, 0.06f}, {0, chestY + 0.02f, -0.16f}, Color{60, 66, 60, 255});
    if (p.hasBomb) B({0.3f, 0.32f * s, 0.16f}, {0, chestY + 0.02f, 0.22f}, Color{150, 45, 35, 255}, 0.2f);
    else B({0.28f, 0.3f * s, 0.1f}, {0, chestY, 0.18f}, dark);
    // head with pitch
    float headY = p.eyeHeight() + 0.06f - (STAND_EYE - 1.64f);
    headY = lerpf(STAND_EYE, CROUCH_EYE, duck) + 0.07f;
    Matrix headM = mul(MatrixRotateX(clampf(p.pitch, -30, 30) * DEG * 0.4f), mul(MatrixTranslate(0, headY, 0), world));
    drawBox(mul(part({0.25f, 0.27f, 0.27f}, {0, 0, 0}), headM), Color{86, 90, 98, 255});
    drawBox(mul(part({0.27f, 0.08f, 0.28f}, {0, 0.1f, 0}), headM), p.helmet ? Color{80, 88, 80, 255} : body);
    drawBox(mul(part({0.21f, 0.07f, 0.02f}, {0, 0.0f, -0.14f}), headM), tc, 0.8f);   // visor
    // arms + gun (aim pose)
    Matrix aim = mul(MatrixRotateX(p.pitch * DEG), mul(MatrixTranslate(0.0f, chestY + 0.2f * s, 0), world));
    drawBox(mul(part({0.11f, 0.11f, 0.42f}, {0.2f, -0.06f, -0.16f}), aim), body);
    drawBox(mul(part({0.1f, 0.1f, 0.48f}, {-0.12f, -0.07f, -0.24f}), aim), body);
    drawBox(mul(part({0.11f, 0.11f, 0.1f}, {0.14f, -0.08f, -0.36f}), aim), dark);
    if (p.activeW != W_NONE) {
        Matrix gun = mul(MatrixScale(1.1f, 1.1f, 1.1f), mul(MatrixTranslate(0.1f, -0.04f, -0.36f), aim));
        drawWeaponModel(p.activeW, gun, tc, 0, false);
    }
    // defuse / plant indicator light
    if (p.defusing || p.planting) B({0.05f, 0.05f, 0.05f}, {0, chestY + 0.5f, 0}, Color{255, 60, 40, 255}, 1.0f);
    (void)v;
}

void Renderer::drawPlayers(const World& w, const ViewState& v) {
    for (const Player& p : w.players) {
        if (!p.active || !p.alive) continue;
        if (p.id == v.localId && !v.thirdPerson) continue;
        bool enemy = v.localId >= 0 ? w.isEnemy(v.localId, p.id) : false;
        drawPlayer(w, p, v, enemy);
    }
    // corpses
    for (auto& c : g_corpses) {
        float t = clampf((worldTime - c.time) / 0.45f, 0, 1);
        float fall = t * t * 1.5708f;
        Matrix world = mul(MatrixRotateY(-c.yaw * DEG), MatrixTranslate(c.pos.x, c.pos.y, c.pos.z));
        Matrix body = mul(MatrixRotateX(fall), world);
        Color tc = mixColor(teamColor(c.team), Color{30, 30, 34, 255}, 0.55f);
        drawBox(mul(part({0.44f, 1.15f, 0.3f}, {0, 0.6f, 0}), body), tc);
        drawBox(mul(part({0.24f, 0.26f, 0.26f}, {0, 1.32f, 0}), body), Color{55, 57, 62, 255});
        drawBox(mul(part({0.46f, 0.12f, 0.31f}, {0, 0.95f, 0}), body), teamColor(c.team), 0.1f);
    }
}

void Renderer::drawSmokes(const World& w, const ViewState& v) {
    for (auto& s : w.smokes) {
        float age = w.time - s.start, left = s.end - w.time;
        float grow = clampf(age / 0.7f, 0, 1);
        float fade = clampf(left / 1.5f, 0, 1);
        float r = 3.5f * grow * fade;
        if (r < 0.05f) continue;
        Rng rr((uint32_t)(s.start * 1000) + 17);
        for (int i = 0; i < 34; i++) {
            Vector3 o = V3(rr.range(-1, 1), rr.range(-0.2f, 1), rr.range(-1, 1));
            float l = len3(o);
            if (l > 1) o = o * (1.0f / l);
            Vector3 c = s.pos + V3(o.x * r * 0.75f, 0.4f + (o.y * 0.5f + 0.5f) * r * 0.75f, o.z * r * 0.75f);
            c.x += sinf(renderTime * 0.3f + i) * 0.15f;
            float pr = r * rr.range(0.32f, 0.5f);
            unsigned char g = (unsigned char)(196 + rr.range(-12, 12));
            mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{g, g, (unsigned char)(g + 4), 255};
            setEmissive(0.35f);
            DrawMesh(sphere, mat, mul(MatrixScale(pr, pr * 0.85f, pr), MatrixTranslate(c.x, c.y, c.z)));
        }
        setEmissive(0);
    }
    (void)v;
}

void Renderer::drawEffects(const World& w, const ViewState& v) {
    // fires
    for (auto& f : w.fires) {
        float grow = clampf((w.time - f.start) / 0.5f, 0.3f, 1.0f);
        Rng rr((uint32_t)(f.start * 1000) + 5);
        for (int i = 0; i < 26; i++) {
            float a = rr.range(0, 6.283f), d = sqrtf(rr.f01()) * f.radius * grow;
            float h = (0.3f + 0.5f * fabsf(sinf(renderTime * 9 + i * 1.7f))) * rr.range(0.6f, 1.3f);
            Vector3 c = f.pos + V3(cosf(a) * d, h * 0.5f, sinf(a) * d);
            Color col = i % 3 == 0 ? Color{255, 210, 90, 255} : Color{255, 110, 30, 255};
            setEmissive(1.0f);
            mat.maps[MATERIAL_MAP_DIFFUSE].color = col;
            DrawMesh(sphere, mat, mul(MatrixScale(0.22f, h * 0.6f, 0.22f), MatrixTranslate(c.x, c.y, c.z)));
        }
        setEmissive(0);
        if (rr.f01() < 0.5f) spawnBurst(f.pos + V3(rr.range(-2, 2), 0.2f, rr.range(-2, 2)), V3(0, 1, 0), Color{255, 150, 40, 255}, 1, 2.5f, 0.8f, 0.03f, -1.5f, true);
    }
    // projectiles
    for (auto& pr : w.projectiles) {
        Vector3 p = lerp3(pr.prevPos, pr.pos, v.alpha);
        Matrix m = mul(MatrixRotateY(renderTime * 6), MatrixTranslate(p.x, p.y, p.z));
        drawWeaponModel(pr.type, mul(MatrixScale(1.3f, 1.3f, 1.3f), m), WHITE, 0, false);
    }
    // dropped weapons
    for (auto& d : w.drops) {
        Matrix m = mul(mul(MatrixRotateZ(1.5708f), MatrixRotateY(d.spawnTime * 3.f)), MatrixTranslate(d.pos.x, d.pos.y + 0.03f, d.pos.z));
        drawWeaponModel(d.w, m, WHITE, 0, false);
        float pulse = 0.5f + 0.5f * sinf(renderTime * 4);
        drawBox(mul(part({0.5f, 0.01f, 0.5f}, {0, 0, 0}), MatrixTranslate(d.pos.x, d.pos.y - 0.03f, d.pos.z)), Color{255, 255, 255, 255}, 0.3f * pulse);
    }
    // bomb
    if (w.bomb.state == BOMB_DROPPED || w.bomb.state == BOMB_PLANTED || w.bomb.state == BOMB_DEFUSED) {
        Vector3 b = w.bomb.pos;
        Matrix m = MatrixTranslate(b.x, b.y + 0.06f, b.z);
        drawWeaponModel(W_BOMB, mul(MatrixScale(1.6f, 1.6f, 1.6f), m), WHITE, 0, false);
        if (w.bomb.state == BOMB_PLANTED) {
            float left = w.bomb.explodeTime - w.time;
            float blink = fmodf(w.time, std::max(0.1f, left / 20.f)) < 0.06f ? 1.0f : 0.15f;
            drawBox(mul(part({0.07f, 0.07f, 0.07f}, {0, 0.14f, 0}), m), Color{255, 40, 30, 255}, blink);
        }
    }
    // decals
    for (auto& d : decals) {
        Vector3 n = d.normal;
        Vector3 t = fabsf(n.y) > 0.9f ? V3(1, 0, 0) : norm3(cross3(n, V3(0, 1, 0)));
        Vector3 b = cross3(n, t);
        float s = 0.05f;
        Vector3 p = d.pos;
        Color c = d.mat == MAT_WOOD ? Color{40, 28, 18, 230} : Color{30, 30, 32, 220};
        Vector3 a1 = p - t * s - b * s, a2 = p + t * s - b * s, a3 = p + t * s + b * s, a4 = p - t * s + b * s;
        DrawTriangle3D(a1, a2, a3, c); DrawTriangle3D(a1, a3, a4, c);
        DrawTriangle3D(a3, a2, a1, c); DrawTriangle3D(a4, a3, a1, c);
    }
    // player blob shadows
    for (auto& p : w.players) {
        if (!p.active || !p.alive || (p.id == v.localId && !v.thirdPerson)) continue;
        Vector3 pos = lerp3(p.prevPos, p.pos, v.alpha);
        float g = w.map.groundHeightAt(pos, 0.2f, pos.y + 0.05f);
        DrawCylinder(V3(pos.x, g + 0.012f, pos.z), 0.42f, 0.42f, 0.005f, 12, Color{0, 0, 0, 70});
    }
    // particles
    BeginBlendMode(BLEND_ALPHA);
    for (auto& p : particles) {
        if (p.additive) continue;
        float a = clampf(p.life / p.maxLife, 0, 1);
        DrawCube(p.pos, p.size, p.size, p.size, withAlpha(p.col, a));
    }
    EndBlendMode();
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& p : particles) {
        if (!p.additive) continue;
        float a = clampf(p.life / p.maxLife, 0, 1);
        DrawCube(p.pos, p.size, p.size, p.size, withAlpha(p.col, a));
    }
    // tracers as camera-facing quads
    for (auto& t : tracers) {
        float age = worldTime - t.time;
        Vector3 dir = t.b - t.a;
        float L = len3(dir);
        if (L < 0.5f) continue;
        dir = dir * (1.0f / L);
        float head = std::min(L, age * 450.f + 2.0f), tail = std::max(0.f, head - 6.0f);
        Vector3 a = t.a + dir * tail, b = t.a + dir * head;
        Vector3 side = norm3(cross3(dir, v.eye - a)) * (t.local ? 0.008f : 0.012f);
        Color c = Color{255, 230, 160, (unsigned char)(200 * clampf(1 - age / 0.12f, 0, 1))};
        DrawTriangle3D(a - side, b - side, b + side, c); DrawTriangle3D(a - side, b + side, a + side, c);
        DrawTriangle3D(b + side, b - side, a - side, c); DrawTriangle3D(a + side, b + side, a - side, c);
    }
    // muzzle flash of other players
    if (worldTime - muzzleFlashTime < 0.04f) {
        Vector3 p = muzzleFlashPos;
        for (int i = 0; i < 3; i++) DrawCube(p, 0.12f - i * 0.03f, 0.12f - i * 0.03f, 0.12f - i * 0.03f, Color{255, 200, 120, 160});
    }
    EndBlendMode();
}

// ---------------------------------------------------------------------------
void Renderer::drawViewmodel(const World& w, const ViewState& v) {
    if (v.localId < 0) return;
    const Player& p = w.players[v.localId];
    if (!p.alive || p.scope > 0) return;
    const WeaponDef& wd = weaponDef(p.activeW);
    float t = w.time;
    Vector3 f = forwardFromAngles(v.yaw, v.pitch);
    Vector3 r = norm3(cross3(f, V3(0, 1, 0)));
    Vector3 u = cross3(r, f);
    const float S = 0.1f;   // shrink toward the eye so it never clips through walls
    Matrix B = {r.x * S, u.x * S, -f.x * S, v.eye.x,
                r.y * S, u.y * S, -f.y * S, v.eye.y,
                r.z * S, u.z * S, -f.z * S, v.eye.z,
                0, 0, 0, 1};
    // raymath Matrix layout is row-major fields m0,m4,m8,m12 = first row
    // bob
    float speed = len2xz(p.vel);
    vmBobPhase += v.frameTime * (speed > 0.5f && p.onGround ? speed * 1.9f : 0);
    vmBobAmp = lerpf(vmBobAmp, p.onGround ? clampf(speed / 5.f, 0, 1) : 0, clampf(v.frameTime * 8, 0, 1));
    float bob = g_settings.viewBob;
    float hand = g_settings.viewmodelLeftHand ? -1.f : 1.f;
    bool pistolLike = wd.cls == WC_PISTOL || wd.cls == WC_KNIFE || wd.cls == WC_GRENADE;
    Vector3 off = {hand * (0.15f + 0.02f * g_settings.viewmodelX), (pistolLike ? -0.115f : -0.14f) + 0.02f * g_settings.viewmodelY, (pistolLike ? -0.36f : -0.35f) - 0.02f * g_settings.viewmodelZ};
    if (wd.cls == WC_GRENADE) { off.y -= 0.035f; off.z -= 0.06f; }
    off.x += sinf(vmBobPhase) * 0.009f * vmBobAmp * bob;
    off.y += -fabsf(cosf(vmBobPhase)) * 0.008f * vmBobAmp * bob - vmLandDip * 0.03f;
    off.x -= vmSwayX * 0.02f;
    off.y += vmSwayY * 0.02f;
    float pitchRot = 0, yawRot = 0, rollRot = 0;
    // recoil kick
    off.z += vmKick * 0.035f;
    pitchRot += vmKick * 3.5f;
    // draw anim
    float drawFrac = wd.drawTime > 0 ? clampf(1.0f - (p.drawEnd - t) / std::max(0.2f, wd.drawTime), 0, 1) : 1;
    off.y -= (1 - drawFrac) * (1 - drawFrac) * 0.22f;
    pitchRot -= (1 - drawFrac) * 40.f;
    // reload anim
    float magDrop = 0;
    if (p.reloading) {
        float rp = clampf(1.0f - (p.reloadEnd - t) / wd.reloadTime, 0, 1);
        float k = sinf(rp * 3.14159f);
        off.y -= k * 0.06f;
        rollRot += k * 28.f * hand;
        pitchRot += k * 12.f;
        if (rp > 0.1f && rp < 0.55f) magDrop = (rp - 0.1f) * 0.6f;
    }
    // inspect
    if (t < p.inspectUntil) {
        float ip = 1.0f - (p.inspectUntil - t) / 2.2f;
        float k = sinf(ip * 3.14159f);
        yawRot += k * 50.f * hand;
        rollRot += k * 35.f * hand;
        off.x -= k * 0.05f * hand;
    }
    // knife
    if (p.activeW == W_KNIFE && worldTime - knifeSwingTime < (knifeHeavy ? 0.6f : 0.35f)) {
        float kp = (worldTime - knifeSwingTime) / (knifeHeavy ? 0.6f : 0.35f);
        float k = sinf(kp * 3.14159f);
        yawRot += k * (knifeHeavy ? 10.f : 60.f) * hand;
        pitchRot += k * (knifeHeavy ? -60.f : 10.f);
        off.x -= k * 0.08f * hand;
        off.z -= k * 0.08f;
    }
    // grenade
    if (isGrenade(p.activeW)) {
        if (p.pinPulled) { off.y += 0.04f; off.z += 0.06f; pitchRot += 20; }
        if (worldTime - throwAnimTime < 0.4f) { float k = (worldTime - throwAnimTime) / 0.4f; off.z -= sinf(k * 3.14159f) * 0.2f; off.y += sinf(k * 3.14159f) * 0.08f; }
    }
    if (p.activeW == W_BOMB) { off.y -= 0.05f; pitchRot -= 25; off.z -= 0.02f; }
    if (p.planting || p.defusing) { off.y -= 0.08f; pitchRot -= 35; }

    Matrix local = mul(mul(mul(MatrixRotateZ(rollRot * DEG), MatrixRotateY(yawRot * DEG)), MatrixRotateX(pitchRot * DEG)), MatrixTranslate(off.x, off.y, off.z));
    Matrix base = mul(mul(MatrixScale(0.85f, 0.85f, 0.85f), local), B);
    baseEmissive = 0.18f;
    Color tc = teamColor(p.team);
    Color glove = {44, 46, 50, 255}, sleeve = mixColor(tc, Color{40, 42, 48, 255}, 0.5f);
    // arms: right hand at grip, left hand under the handguard
    bool twoHanded = wd.cls == WC_RIFLE || wd.cls == WC_SMG || wd.cls == WC_SNIPER || wd.cls == WC_SHOTGUN || wd.cls == WC_LMG;
    drawBox(mul(part({0.055f, 0.06f, 0.08f}, {0, -0.06f, 0.02f}), base), glove);
    drawBox(mul(mul(part({0.075f, 0.075f, 0.35f}, {0, 0, 0.18f}), MatrixRotateX(-0.35f)), mul(MatrixTranslate(0.01f, -0.08f, 0.04f), base)), sleeve);
    if (twoHanded) {
        drawBox(mul(part({0.06f, 0.055f, 0.08f}, {0, -0.045f, -0.3f}), base), glove);
        drawBox(mul(mul(part({0.075f, 0.075f, 0.4f}, {0, 0, 0.2f}), MatrixRotateY(-0.5f * hand)), mul(MatrixTranslate(-0.02f * hand, -0.07f, -0.28f), base)), sleeve);
    } else if (wd.cls == WC_PISTOL) {
        drawBox(mul(part({0.05f, 0.055f, 0.07f}, {-0.025f * hand, -0.065f, 0.0f}), base), glove);
    }
    drawWeaponModel(p.activeW, base, tc, t, true);
    if (magDrop > 0 && twoHanded) {
        drawBox(mul(part({0.045f, 0.14f, 0.07f}, {0, -0.12f - magDrop, -0.15f}), base), Color{40, 40, 44, 255});
    }
    // muzzle flash (additive)
    if (worldTime - lastShotAnim < 0.035f && isGun(p.activeW)) {
        float len = wd.cls == WC_PISTOL ? 0.2f : (wd.cls == WC_SMG ? 0.48f : 0.75f);
        if (wd.cls == WC_SNIPER) len = 0.95f;
        Matrix mz = mul(MatrixTranslate(0, 0.02f, -len), base);
        BeginBlendMode(BLEND_ADDITIVE);
        setEmissive(1.0f);
        float rot = fmodf(worldTime * 977.f, 6.283f);
        for (int i = 0; i < 3; i++) {
            Matrix m = mul(mul(part({0.02f, 0.11f - i * 0.02f, 0.02f}, {0, 0, 0}), MatrixRotateZ(rot + i * 1.05f)), mz);
            drawBox(m, Color{255, 190, 90, 255});
        }
        drawBox(mul(part({0.05f, 0.05f, 0.09f}, {0, 0, -0.03f}), mz), Color{255, 240, 200, 255});
        setEmissive(0);
        EndBlendMode();
    }
    baseEmissive = 0;
}

// ---------------------------------------------------------------------------
void Renderer::render(const World& w, const ViewState& v) {
    // sky gradient (2D) then the world
    Camera3D cam{};
    cam.position = v.eye;
    cam.target = v.eye + forwardFromAngles(v.yaw, v.pitch);
    cam.up = {0, 1, 0};
    cam.fovy = v.fovY;
    cam.projection = CAMERA_PERSPECTIVE;
    lastCamera = cam;

    float fogDensity = 0.0062f * g_settings.renderDistanceFog;
    Vector3 fogColor = {0.74f, 0.80f, 0.88f};
    float bright = g_settings.brightness;
    SetShaderValue(shader, locViewPos, &v.eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locFogColor, &fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locBrightness, &bright, SHADER_UNIFORM_FLOAT);
    float shadowsOn = 1.0f;
    SetShaderValue(shader, locShadows, &shadowsOn, SHADER_UNIFORM_FLOAT);
    float fl[4] = {muzzleFlashPos.x, muzzleFlashPos.y, muzzleFlashPos.z, 0};
    if (g_settings.muzzleFlashLight && worldTime - muzzleFlashTime < 0.05f) fl[3] = 2.2f;
    SetShaderValue(shader, locFlash, fl, SHADER_UNIFORM_VEC4);
    setEmissive(0);

    BeginMode3D(cam);
    if (mapLoaded) {
        mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(mapModel.meshes[0], mat, MatrixIdentity());
    }
    drawPlayers(w, v);
    drawEffects(w, v);
    drawSmokes(w, v);
    if (extra3D) extra3D();
    EndMode3D();

    // viewmodel pass with its own FOV (depth buffer kept; model is tiny and close)
    if (!v.thirdPerson && v.localId >= 0) {
        Camera3D vc = cam;
        float vfov = 2.f * atanf(tanf(g_settings.viewmodelFov * 0.5f * DEG) * 0.75f) / DEG;
        vc.fovy = vfov;
        BeginMode3D(vc);
        drawViewmodel(w, v);
        EndMode3D();
    }
}

} // namespace ax
