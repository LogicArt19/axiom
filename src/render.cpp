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
uniform sampler2D texture0;     // height map (r = height / 8)
uniform sampler2D fogTex;       // fog of war (r = visibility)
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform float brightness;
uniform float emissive;
uniform vec4 mapInfo;           // cells, cells, cell size, world size
uniform float shadows;
uniform float time;
uniform float useFog;
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
float sunVisibility(vec3 p, vec3 d) {
    vec2 g = p.xz / mapInfo.z;
    vec2 dd = d.xz / mapInfo.z;
    ivec2 cell = ivec2(floor(g));
    ivec2 st = ivec2(dd.x > 0.0 ? 1 : -1, dd.y > 0.0 ? 1 : -1);
    vec2 tDelta = vec2(abs(dd.x) > 1e-6 ? abs(1.0 / dd.x) : 1e9, abs(dd.y) > 1e-6 ? abs(1.0 / dd.y) : 1e9);
    vec2 fr = g - floor(g);
    vec2 tMax = vec2(dd.x > 0.0 ? (1.0 - fr.x) * tDelta.x : fr.x * tDelta.x, dd.y > 0.0 ? (1.0 - fr.y) * tDelta.y : fr.y * tDelta.y);
    float t = 0.0;
    for (int i = 0; i < 40; i++) {
        float y = p.y + d.y * t;
        if (y > 7.5) return 1.0;
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
    vec2 uv = fragPos.xz;
    vec3 an = abs(n);
    if (pid < 0.99) {
        if (pid > 0.85) {              // rock
            vec2 ruv = an.y > 0.5 ? uv : (an.x > 0.5 ? fragPos.zy : fragPos.xy);
            float strata = vnoise(vec2(ruv.x * 0.6, ruv.y * 3.0)) * 0.25 + vnoise(ruv * 4.0) * 0.12;
            col *= 0.78 + strata;
            if (an.y > 0.5) col = mix(col, vec3(0.36, 0.42, 0.28), 0.35 * vnoise(uv * 0.7));
        } else if (pid > 0.5) {        // base platform tiles
            vec2 f = fract(uv / 2.0);
            float e = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
            col *= 0.86 + 0.14 * smoothstep(0.0, 0.03, e);
            col *= 0.93 + hash(floor(uv / 2.0)) * 0.08;
        } else if (pid > 0.28) {       // lane cobblestones
            vec2 g = uv * 0.9;
            g.x += floor(g.y) * 0.5;
            vec2 f = fract(g);
            float e = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
            col *= (0.82 + 0.18 * smoothstep(0.0, 0.08, e)) * (0.9 + hash(floor(g)) * 0.15);
        } else if (pid > 0.12) {       // grass / jungle
            float g1 = vnoise(uv * 0.35), g2 = vnoise(uv * 3.1), g3 = vnoise(uv * 11.0);
            col *= 0.78 + g1 * 0.25 + g2 * 0.1 + g3 * 0.08;
        } else {                       // water
            float w1 = sin(uv.x * 0.9 + time * 1.3 + vnoise(uv * 0.5) * 4.0) * 0.5 + 0.5;
            float w2 = vnoise(uv * 1.7 + vec2(time * 0.4, time * 0.25));
            col *= 0.85 + w1 * 0.1 + w2 * 0.15;
        }
    }
    float diff = max(dot(n, -sunDir), 0.0);
    float sh = 1.0;
    if (shadows > 0.5 && diff > 0.0) sh = sunVisibility(fragPos + n * 0.03, -sunDir);
    float hemi = 0.55 + 0.45 * n.y;
    vec3 light = vec3(1.0, 0.95, 0.86) * diff * 0.75 * (0.25 + 0.75 * sh) + vec3(0.55, 0.62, 0.75) * hemi * 0.6;
    col *= light * brightness;
    col = mix(col, base.rgb * 1.3, emissive);
    if (useFog > 0.5) {
        float v = texture(fogTex, fragPos.xz / mapInfo.w).r;
        float g = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(g) * 0.55, col, smoothstep(0.35, 0.95, v)) * mix(0.18, 1.0, smoothstep(0.08, 0.9, v));
    }
    finalColor = vec4(col, colDiffuse.a);
}
)";

static Matrix mul(const Matrix& a, const Matrix& b) { return MatrixMultiply(a, b); }
static Matrix part(Vector3 s, Vector3 c) { return mul(MatrixScale(s.x, s.y, s.z), MatrixTranslate(c.x, c.y, c.z)); }

void Renderer::init() {
    shader = LoadShaderFromMemory(kVS, kFS);
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader, "matNormal");
    shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(shader, "colDiffuse");
    shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(shader, "texture0");
    shader.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(shader, "fogTex");
    locSun = GetShaderLocation(shader, "sunDir");
    locBrightness = GetShaderLocation(shader, "brightness");
    locEmissive = GetShaderLocation(shader, "emissive");
    locMapInfo = GetShaderLocation(shader, "mapInfo");
    locShadows = GetShaderLocation(shader, "shadows");
    locTime = GetShaderLocation(shader, "time");
    locUseFog = GetShaderLocation(shader, "useFog");
    cube = GenMeshCube(1, 1, 1);
    sphere = GenMeshSphere(1, 10, 14);
    mat = LoadMaterialDefault();
    mat.shader = shader;
    Vector3 sd = norm3(V3(-0.5f, -0.78f, 0.38f));
    SetShaderValue(shader, locSun, &sd, SHADER_UNIFORM_VEC3);
    float info[4] = {(float)Map::N, (float)Map::N, Map::CELL, Map::SIZE};
    SetShaderValue(shader, locMapInfo, info, SHADER_UNIFORM_VEC4);
    fogImg = GenImageColor(Map::N, Map::N, WHITE);
    ImageFormat(&fogImg, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    fogTex = LoadTextureFromImage(fogImg);
    SetTextureFilter(fogTex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(fogTex, TEXTURE_WRAP_CLAMP);
    mat.maps[MATERIAL_MAP_SPECULAR].texture = fogTex;
}

void Renderer::shutdown() {
    if (terrainLoaded) { UnloadMesh(terrain); UnloadTexture(heightTex); }
    UnloadTexture(fogTex); UnloadImage(fogImg);
    UnloadMesh(cube); UnloadMesh(sphere);
    UnloadShader(shader);
}

// --- terrain mesh -------------------------------------------------------------
struct MB {
    std::vector<float> v, n;
    std::vector<unsigned char> c;
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 nn, Color col) {
        Vector3 p[6] = {a, b, cc, a, cc, d};
        for (auto& q : p) { v.insert(v.end(), {q.x, q.y, q.z}); n.insert(n.end(), {nn.x, nn.y, nn.z}); c.insert(c.end(), {col.r, col.g, col.b, col.a}); }
    }
};

void Renderer::onMapLoaded(const World& w) {
    if (terrainLoaded) { UnloadMesh(terrain); UnloadTexture(heightTex); }
    const Map& m = w.map;
    MB mb;
    const float C = Map::CELL;
    auto groundColor = [&](int i) -> Color {
        Vector2 p = m.cellCenter(i);
        float n = 0.5f + 0.5f * sinf(p.x * 0.07f) * cosf(p.y * 0.05f);
        switch (m.type[i]) {
        case CELL_PATH: return Color{(unsigned char)(132 + n * 10), (unsigned char)(116 + n * 8), 88, 60};
        case CELL_WATER: return Color{58, 112, 148, 10};
        case CELL_BASE0: return Color{112, 128, 150, 150};
        case CELL_BASE1: return Color{150, 118, 112, 150};
        case CELL_GRASS: return Color{(unsigned char)(60 + n * 10), (unsigned char)(102 + n * 14), 56, 60};
        case CELL_HIGH: return Color{(unsigned char)(146 + n * 12), (unsigned char)(136 + n * 10), 104, 60};
        case CELL_RAMP: return Color{138, 128, 102, 90};
        default: return Color{(unsigned char)(84 + n * 14), (unsigned char)(114 + n * 16), 68, 60};
        }
    };
    // a dark floor under everything (seen through lake edges)
    mb.quad({-20, -0.6f, -20}, {-20, -0.6f, Map::SIZE + 20}, {Map::SIZE + 20, -0.6f, Map::SIZE + 20}, {Map::SIZE + 20, -0.6f, -20}, {0, 1, 0}, Color{22, 40, 52, 10});
    const int VW = Map::N + 1;
    for (int z = 0; z < Map::N; z++)
        for (int x = 0; x < Map::N; x++) {
            int i = z * Map::N + x;
            uint8_t t = m.type[i];
            if (t == CELL_ROCK) continue;
            Color col = groundColor(i);
            if (t == CELL_WATER) {
                float y = m.ground[i];
                mb.quad({x * C, y, z * C}, {x * C, y, (z + 1) * C}, {(x + 1) * C, y, (z + 1) * C}, {(x + 1) * C, y, z * C}, {0, 1, 0}, col);
                continue;
            }
            float h00 = m.vheight[z * VW + x], h01 = m.vheight[(z + 1) * VW + x], h11 = m.vheight[(z + 1) * VW + x + 1], h10 = m.vheight[z * VW + x + 1];
            Vector3 a = {x * C, h00, z * C}, b = {x * C, h01, (z + 1) * C}, c = {(x + 1) * C, h11, (z + 1) * C}, d = {(x + 1) * C, h10, z * C};
            Vector3 nn = norm3(cross3(b - a, d - a));
            if (nn.y < 0) nn = nn * -1;
            mb.quad(a, b, c, d, nn, col);
        }
    // rocks and cliffs: one box per cell column
    for (int i = 0; i < Map::N * Map::N; i++) {
        if (m.type[i] != CELL_ROCK) continue;
        int x = i % Map::N, z = i / Map::N;
        float h = m.height[i];
        bool cliff = h < Map::HIGH + 1.0f;
        Vector3 mn = {x * C, -0.5f, z * C}, mx = {(x + 1) * C, h, (z + 1) * C};
        float shade = 0.85f + 0.15f * sinf(x * 1.7f + z * 0.9f);
        Color side = cliff ? Color{(unsigned char)(128 * shade), (unsigned char)(116 * shade), (unsigned char)(96 * shade), 235}
                           : Color{(unsigned char)(112 * shade), (unsigned char)(106 * shade), (unsigned char)(96 * shade), 235};
        Color top = cliff ? Color{(unsigned char)(140 * shade), (unsigned char)(130 * shade), (unsigned char)(100 * shade), 235}
                          : Color{(unsigned char)(92 * shade), (unsigned char)(102 * shade), (unsigned char)(78 * shade), 235};
        mb.quad({mn.x, mx.y, mn.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {0, 1, 0}, top);
        auto solid = [&](int xx, int zz) { return !m.inside(xx, zz) || (m.type[zz * Map::N + xx] == CELL_ROCK && m.height[zz * Map::N + xx] >= h); };
        if (!solid(x, z - 1)) mb.quad({mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mn.y, mn.z}, {0, 0, -1}, side);
        if (!solid(x, z + 1)) mb.quad({mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mn.y, mx.z}, {0, 0, 1}, side);
        if (!solid(x - 1, z)) mb.quad({mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {-1, 0, 0}, side);
        if (!solid(x + 1, z)) mb.quad({mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, {1, 0, 0}, side);
    }
    terrain = Mesh{};
    terrain.vertexCount = (int)mb.v.size() / 3;
    terrain.triangleCount = terrain.vertexCount / 3;
    terrain.vertices = (float*)MemAlloc((unsigned)(mb.v.size() * sizeof(float)));
    terrain.normals = (float*)MemAlloc((unsigned)(mb.n.size() * sizeof(float)));
    terrain.colors = (unsigned char*)MemAlloc((unsigned)mb.c.size());
    memcpy(terrain.vertices, mb.v.data(), mb.v.size() * sizeof(float));
    memcpy(terrain.normals, mb.n.data(), mb.n.size() * sizeof(float));
    memcpy(terrain.colors, mb.c.data(), mb.c.size());
    UploadMesh(&terrain, false);
    Image img = GenImageColor(Map::N, Map::N, BLACK);
    heightTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(heightTex, TEXTURE_FILTER_POINT);
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = heightTex;
    terrainLoaded = true;
    mapVersion = -1;
    clearEffects();
}

void Renderer::updateHeightTex(const World& w) {
    if (w.map.version == mapVersion) return;
    mapVersion = w.map.version;
    std::vector<unsigned char> px(Map::N * Map::N * 4, 0);
    for (int i = 0; i < Map::N * Map::N; i++) {
        float g = std::max(0.f, w.map.ground[i]);
        float h = w.map.type[i] == CELL_ROCK ? w.map.height[i] : g + (w.map.blocked[i] == 2 ? 2.6f : (w.map.blocked[i] == 3 ? 1.0f : 0));
        unsigned char v = (unsigned char)clampf(h / 8.f * 255.f + 0.5f, 0, 255);
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v; px[i * 4 + 3] = 255;
    }
    UpdateTexture(heightTex, px.data());
}

void Renderer::updateFogTex(const World& w, int team) {
    unsigned char* d = (unsigned char*)fogImg.data;
    for (int i = 0; i < Map::N * Map::N; i++) {
        unsigned char t;
        if (team < 0) t = 255;
        else t = w.vis[team][i] ? 255 : (w.explored[team][i] ? 120 : 30);
        d[i] = t;
    }
    UpdateTexture(fogTex, d);
}

// --- primitives ---------------------------------------------------------------
void Renderer::setEmissive(float e) { SetShaderValue(shader, locEmissive, &e, SHADER_UNIFORM_FLOAT); }
void Renderer::box(const Matrix& m, Color c, float em) {
    float e = std::max(em, baseEmissive);
    if (e != 0) setEmissive(e);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(cube, mat, m);
    if (e != 0) setEmissive(0);
}
void Renderer::ball(Vector3 c, float r, Color col, float em) {
    if (em != 0) setEmissive(em);
    mat.maps[MATERIAL_MAP_DIFFUSE].color = col;
    DrawMesh(sphere, mat, mul(MatrixScale(r, r, r), MatrixTranslate(c.x, c.y, c.z)));
    if (em != 0) setEmissive(0);
}
void Renderer::ring(Vector2 c, float r, float th, Color col, float y) {
    const int S = 36;
    y += groundAt(c);
    for (int i = 0; i < S; i++) {
        float a0 = i * 6.2831853f / S, a1 = (i + 1) * 6.2831853f / S;
        Vector3 p0 = {c.x + cosf(a0) * r, y, c.y + sinf(a0) * r}, p1 = {c.x + cosf(a1) * r, y, c.y + sinf(a1) * r};
        Vector3 q0 = {c.x + cosf(a0) * (r - th), y, c.y + sinf(a0) * (r - th)}, q1 = {c.x + cosf(a1) * (r - th), y, c.y + sinf(a1) * (r - th)};
        DrawTriangle3D(p0, q0, q1, col); DrawTriangle3D(p0, q1, p1, col);
        DrawTriangle3D(q1, q0, p0, col); DrawTriangle3D(p1, q1, p0, col);
    }
}
void Renderer::disc(Vector2 c, float r, Color col, float y) {
    const int S = 32;
    y += groundAt(c);
    Vector3 ctr = {c.x, y, c.y};
    for (int i = 0; i < S; i++) {
        float a0 = i * 6.2831853f / S, a1 = (i + 1) * 6.2831853f / S;
        Vector3 p0 = {c.x + cosf(a0) * r, y, c.y + sinf(a0) * r}, p1 = {c.x + cosf(a1) * r, y, c.y + sinf(a1) * r};
        DrawTriangle3D(ctr, p1, p0, col); DrawTriangle3D(ctr, p0, p1, col);
    }
}

void Renderer::burst(Vector3 p, Color c, int n, float speed, float life, float size, float gravity, bool additive) {
    if (!g_settings.particles) return;
    static Rng r(777);
    for (int i = 0; i < n; i++) {
        Vector3 d = norm3(V3(r.range(-1, 1), r.range(0.1f, 1.2f), r.range(-1, 1)));
        particles.push_back({p, d * (speed * r.range(0.4f, 1.0f)), life * r.range(0.6f, 1.2f), life, size * r.range(0.6f, 1.3f), gravity, c, additive});
    }
    if (particles.size() > 2500) particles.erase(particles.begin(), particles.begin() + (particles.size() - 2500));
}

// --- models -------------------------------------------------------------------
// local space: +X forward, +Y up, +Z right
void Renderer::drawUnitModel(const Entity& e, Vector3 pos, float facing, Color tc, Color acc, float t, float moveAmt, float atk, float s) {
    Matrix W = mul(mul(MatrixScale(s, s, s), MatrixRotateY(-facing)), MatrixTranslate(pos.x, pos.y, pos.z));
    auto B = [&](Vector3 sz, Vector3 c, Color col, float em = 0) { box(mul(part(sz, c), W), col, em); };
    float swing = sinf(t * 9.0f) * 0.5f * moveAmt;
    float lunge = atk > 0 ? sinf(atk * 3.14159f) * 0.25f : 0;
    Color dark = {52, 56, 64, 255}, metal = {120, 124, 132, 255};
    Color body = mixColor(tc, Color{70, 74, 82, 255}, 0.35f);
    auto legs = [&](float h, float w) {
        for (int k = -1; k <= 1; k += 2) {
            Matrix L = mul(mul(part({0.16f, h, 0.16f}, {0, -h * 0.5f, 0}), MatrixRotateZ(swing * k)), mul(MatrixTranslate(0, h, w * k), W));
            box(L, dark);
        }
    };
    switch (e.type) {
    case U_DRONE: {
        float hover = 0.55f + sinf(t * 4 + e.id) * 0.06f;
        B({0.55f, 0.32f, 0.5f}, {0, hover, 0}, metal);
        B({0.3f, 0.12f, 0.52f}, {0.05f, hover + 0.2f, 0}, tc, 0.3f);
        B({0.1f, 0.1f, 0.7f}, {0.15f, hover - 0.1f, 0}, dark);
        if (e.carry > 0) B({0.22f, 0.22f, 0.22f}, {-0.1f, hover + 0.38f, 0}, Color{90, 200, 255, 255}, 0.8f);
        if (e.gatherTimer > 0) B({0.5f + 0.1f * sinf(t * 30), 0.05f, 0.05f}, {0.45f, hover - 0.1f, 0}, Color{120, 220, 255, 255}, 1.0f);
        break;
    }
    case U_WARDEN: case U_MINION_MELEE: {
        bool minion = e.type == U_MINION_MELEE;
        float sc = minion ? 0.85f : 1.0f;
        legs(0.55f * sc, 0.13f);
        B({0.4f * sc, 0.55f * sc, 0.55f * sc}, {0, 0.82f * sc, 0}, minion ? body : metal);
        B({0.42f * sc, 0.16f * sc, 0.6f * sc}, {0, 0.95f * sc, 0}, tc, 0.15f);
        B({0.3f * sc, 0.3f * sc, 0.3f * sc}, {0.02f, 1.27f * sc, 0}, minion ? dark : body);
        B({0.08f, 0.6f * sc, 0.45f * sc}, {0.32f + lunge * 0.5f, 0.8f * sc, -0.18f}, minion ? dark : metal);
        B({0.55f * sc, 0.06f, 0.06f}, {0.3f + lunge, 0.85f * sc, 0.32f * sc}, Color{200, 205, 215, 255});
        if (!minion) { B({0.2f, 0.15f, 0.2f}, {0, 1.1f, -0.33f}, tc); B({0.2f, 0.15f, 0.2f}, {0, 1.1f, 0.33f}, tc); }
        break;
    }
    case U_STRIKER: case U_MINION_RANGED: {
        bool minion = e.type == U_MINION_RANGED;
        float sc = minion ? 0.85f : 1.0f;
        legs(0.55f * sc, 0.12f);
        B({0.34f * sc, 0.5f * sc, 0.42f * sc}, {0, 0.8f * sc, 0}, body);
        B({0.36f * sc, 0.1f * sc, 0.44f * sc}, {0, 0.98f * sc, 0}, tc, 0.2f);
        B({0.26f * sc, 0.26f * sc, 0.26f * sc}, {0, 1.2f * sc, 0}, dark);
        B({0.06f, 0.06f, 0.06f}, {0.13f * sc, 1.22f * sc, 0}, acc, 0.9f);
        B({0.75f * sc, 0.08f, 0.08f}, {0.35f - lunge * 0.3f, 0.92f * sc, 0.2f}, dark);
        break;
    }
    case U_LANCER: {
        legs(0.6f, 0.15f);
        B({0.45f, 0.55f, 0.6f}, {0, 0.88f, 0}, metal);
        B({0.47f, 0.14f, 0.62f}, {0, 1.0f, 0}, tc, 0.2f);
        B({0.3f, 0.3f, 0.3f}, {0, 1.32f, 0}, body);
        B({1.1f, 0.16f, 0.16f}, {0.3f - lunge * 0.4f, 1.25f, 0.32f}, dark);
        B({0.2f, 0.22f, 0.22f}, {0.85f - lunge * 0.4f, 1.25f, 0.32f}, acc, 0.6f);
        break;
    }
    case U_MENDER: {
        float hover = 1.0f + sinf(t * 3 + e.id) * 0.1f;
        B({0.5f, 0.5f, 0.5f}, {0, hover, 0}, Color{230, 235, 240, 255});
        B({0.12f, 0.36f, 0.12f}, {0.26f, hover, 0}, Color{90, 230, 140, 255}, 0.9f);
        B({0.12f, 0.12f, 0.36f}, {0.26f, hover, 0}, Color{90, 230, 140, 255}, 0.9f);
        for (int k = 0; k < 3; k++) {
            float a = t * 2 + k * 2.094f;
            B({0.12f, 0.06f, 0.12f}, {cosf(a) * 0.45f, hover - 0.2f, sinf(a) * 0.45f}, tc, 0.6f);
        }
        break;
    }
    case U_SIEGE: case U_MINION_SIEGE: {
        bool minion = e.type == U_MINION_SIEGE;
        float sc = minion ? 0.8f : 1.0f;
        for (int k = 0; k < 4; k++) {
            float lx = (k < 2 ? 0.4f : -0.4f) * sc, lz = (k % 2 ? 0.4f : -0.4f) * sc;
            float sw = sinf(t * 7 + k * 1.6f) * 0.3f * moveAmt;
            B({0.14f, 0.7f * sc, 0.14f}, {lx + sw * 0.2f, 0.35f * sc, lz}, dark);
        }
        B({1.0f * sc, 0.4f * sc, 0.8f * sc}, {0, 0.85f * sc, 0}, minion ? body : metal);
        B({1.02f * sc, 0.1f, 0.82f * sc}, {0, 1.0f * sc, 0}, tc, 0.2f);
        B({1.5f * sc, 0.22f * sc, 0.22f * sc}, {0.75f * sc - lunge, 1.15f * sc, 0}, dark);
        break;
    }
    case U_GOLEM: {
        legs(0.8f, 0.3f);
        B({0.8f, 0.9f, 1.0f}, {0, 1.3f, 0}, Color{120, 110, 96, 255});
        B({0.45f, 0.4f, 0.45f}, {0.15f, 1.95f, 0}, Color{105, 98, 86, 255});
        B({0.08f, 0.08f, 0.3f}, {0.38f, 2.0f, 0}, Color{255, 170, 60, 255}, 1.0f);
        B({0.9f + lunge, 0.25f, 0.25f}, {0.3f + lunge * 0.5f, 1.2f, 0.6f}, Color{110, 100, 88, 255});
        B({0.9f, 0.25f, 0.25f}, {0.2f, 1.2f, -0.6f}, Color{110, 100, 88, 255});
        break;
    }
    case U_TITAN: {
        float b = sinf(t * 1.3f) * 0.1f;
        legs(1.4f, 0.7f);
        B({1.8f, 2.0f, 2.4f}, {0, 2.4f + b, 0}, Color{90, 70, 130, 255});
        B({0.9f, 0.9f, 1.0f}, {0.6f, 3.7f + b, 0}, Color{70, 55, 105, 255});
        for (int k = -1; k <= 1; k += 2) {
            B({0.4f, 1.2f, 0.4f}, {-0.2f, 4.4f + b, k * 0.9f}, Color{190, 120, 255, 255}, 0.8f);
            B({2.0f + lunge * 2, 0.5f, 0.5f}, {0.7f + lunge, 2.4f, k * 1.5f}, Color{80, 64, 118, 255});
        }
        B({0.1f, 0.12f, 0.6f}, {1.06f, 3.8f + b, 0}, Color{255, 80, 200, 255}, 1.0f);
        break;
    }
    default: {
        // heroes
        Color gold = {240, 200, 90, 255};
        legs(0.75f, 0.16f);
        float chestY = 1.15f;
        switch (e.type) {
        case H_VANGUARD:
            B({0.6f, 0.75f, 0.8f}, {0, chestY, 0}, metal);
            B({0.62f, 0.2f, 0.82f}, {0, chestY + 0.15f, 0}, tc, 0.35f);
            B({0.38f, 0.4f, 0.38f}, {0, chestY + 0.6f, 0}, body);
            B({0.12f, 0.95f, 0.75f}, {0.42f + lunge * 0.3f, chestY - 0.05f, -0.42f}, Color{170, 175, 190, 255});
            B({0.13f, 0.4f, 0.4f}, {0.49f + lunge * 0.3f, chestY, -0.42f}, tc, 0.5f);
            B({0.9f, 0.12f, 0.12f}, {0.3f + lunge, chestY + 0.1f, 0.48f}, dark);
            B({0.3f, 0.3f, 0.3f}, {0.75f + lunge, chestY + 0.1f, 0.48f}, metal);
            break;
        case H_PYRA:
            B({0.5f, 0.9f, 0.6f}, {0, chestY - 0.1f, 0}, Color{120, 40, 30, 255});
            B({0.52f, 0.14f, 0.62f}, {0, chestY + 0.2f, 0}, tc, 0.3f);
            B({0.32f, 0.36f, 0.32f}, {0, chestY + 0.55f, 0}, Color{70, 30, 25, 255});
            for (int k = 0; k < 3; k++) {
                float a = t * 2.5f + k * 2.094f;
                ball({pos.x + cosf(a) * 0.8f * s, pos.y + (chestY + 0.6f + sinf(t * 3 + k) * 0.15f) * s, pos.z + sinf(a) * 0.8f * s}, 0.13f * s, Color{255, 140, 40, 255}, 1.0f);
            }
            break;
        case H_HAWKE:
            B({0.48f, 0.7f, 0.6f}, {0, chestY, 0}, Color{70, 90, 60, 255});
            B({0.5f, 0.14f, 0.62f}, {0, chestY + 0.15f, 0}, tc, 0.3f);
            B({0.34f, 0.36f, 0.34f}, {0, chestY + 0.55f, 0}, Color{60, 70, 50, 255});
            B({0.1f, 0.85f, 0.6f}, {-0.3f, chestY - 0.1f, 0}, Color{50, 70, 45, 255});
            B({1.4f, 0.1f, 0.1f}, {0.5f - lunge * 0.3f, chestY + 0.15f, 0.3f}, dark);
            B({0.18f, 0.14f, 0.14f}, {0.1f, chestY + 0.3f, 0.3f}, acc, 0.6f);
            break;
        case H_SERAPH:
            B({0.48f, 0.8f, 0.55f}, {0, chestY, 0}, Color{235, 230, 215, 255});
            B({0.5f, 0.14f, 0.57f}, {0, chestY + 0.15f, 0}, gold, 0.4f);
            B({0.32f, 0.34f, 0.32f}, {0, chestY + 0.58f, 0}, Color{230, 220, 200, 255});
            for (int k = -1; k <= 1; k += 2) {
                float flap = sinf(t * 3) * 0.2f;
                Matrix wing = mul(mul(part({0.08f, 0.9f, 1.1f}, {0, 0.3f, 0.55f * k}), MatrixRotateX(k * (0.5f + flap))), mul(MatrixTranslate(-0.25f, chestY + 0.3f, 0), W));
                box(wing, Color{255, 240, 190, 255}, 0.5f);
            }
            ball({pos.x, pos.y + (chestY + 1.1f) * s, pos.z}, 0.12f * s, gold, 1.0f);
            break;
        case H_WARLORD:
            B({0.62f, 0.78f, 0.82f}, {0, chestY, 0}, Color{110, 40, 40, 255});
            B({0.64f, 0.18f, 0.84f}, {0, chestY + 0.15f, 0}, tc, 0.3f);
            B({0.38f, 0.38f, 0.38f}, {0, chestY + 0.6f, 0}, dark);
            B({0.08f, 2.0f, 0.08f}, {-0.35f, chestY + 0.6f, -0.3f}, dark);
            B({0.04f, 0.55f, 0.6f}, {-0.35f, chestY + 1.3f, -0.0f}, tc, 0.5f);
            B({0.7f, 0.1f, 0.1f}, {0.35f + lunge, chestY + 0.1f, 0.5f}, dark);
            B({0.3f, 0.4f, 0.06f}, {0.68f + lunge, chestY + 0.15f, 0.5f}, metal);
            break;
        case H_SHADE:
            B({0.42f, 0.72f, 0.5f}, {0, chestY, 0}, Color{40, 30, 55, 255});
            B({0.44f, 0.12f, 0.52f}, {0, chestY + 0.15f, 0}, Color{170, 90, 240, 255}, 0.6f);
            B({0.3f, 0.34f, 0.3f}, {0, chestY + 0.55f, 0}, Color{30, 24, 40, 255});
            B({0.06f, 0.06f, 0.2f}, {0.16f, chestY + 0.6f, 0}, Color{200, 120, 255, 255}, 1.0f);
            for (int k = -1; k <= 1; k += 2) B({0.6f, 0.05f, 0.08f}, {0.35f + lunge * (k > 0 ? 1 : 0.4f), chestY - 0.05f, 0.35f * k}, Color{210, 200, 255, 255}, 0.4f);
            break;
        default: break;
        }
        break;
    }
    }
}

void Renderer::drawBuildingModel(const World& w, const Entity& e, Color tc, Color acc) {
    const UnitDef& d = unitDef(e.type);
    float prog = e.constructing ? clampf(e.buildProgress, 0.05f, 1.0f) : 1.0f;
    Vector3 p = to3(e.pos);
    Matrix W = mul(MatrixScale(1, prog, 1), MatrixTranslate(p.x, p.y, p.z));
    auto B = [&](Vector3 sz, Vector3 c, Color col, float em = 0) { box(mul(part(sz, c), W), col, em); };
    float fs = d.footprint * Map::CELL;
    Color stone = {150, 148, 140, 255}, dark = {70, 74, 82, 255}, metal = {128, 132, 140, 255};
    float t = clock;
    switch (e.type) {
    case B_CORE: {
        B({fs, 0.8f, fs}, {0, 0.4f, 0}, stone);
        B({fs * 0.75f, 1.6f, fs * 0.75f}, {0, 1.6f, 0}, Color{120, 124, 132, 255});
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5708f + 0.785f;
            B({1.2f, 3.6f, 1.2f}, {cosf(a) * fs * 0.38f, 2.2f, sinf(a) * fs * 0.38f}, dark);
            B({1.25f, 0.4f, 1.25f}, {cosf(a) * fs * 0.38f, 3.9f, sinf(a) * fs * 0.38f}, tc, 0.6f);
        }
        Matrix cr = mul(mul(part({2.2f, 2.8f, 2.2f}, {0, 0, 0}), MatrixRotateY(t * 0.6f)), mul(MatrixRotateZ(0.785f), MatrixTranslate(p.x, p.y + (5.6f + sinf(t * 1.4f) * 0.3f) * prog, p.z)));
        box(cr, tc, 0.85f);
        break;
    }
    case B_BARRACKS:
        B({fs, 2.2f, fs * 0.8f}, {0, 1.1f, 0}, stone);
        B({fs * 1.04f, 0.5f, fs * 0.84f}, {0, 2.4f, 0}, dark);
        B({0.2f, 1.4f, fs * 0.3f}, {fs * 0.5f, 0.7f, 0}, Color{60, 50, 40, 255});
        B({0.05f, 1.6f, 0.05f}, {-fs * 0.3f, 3.4f, fs * 0.3f}, dark);
        B({0.04f, 0.6f, 0.9f}, {-fs * 0.3f, 3.9f, fs * 0.3f + 0.45f}, tc, 0.4f);
        break;
    case B_FACTORY:
        B({fs, 2.6f, fs}, {0, 1.3f, 0}, metal);
        B({fs * 1.02f, 0.3f, fs * 1.02f}, {0, 1.6f, 0}, tc, 0.4f);
        B({0.9f, 2.4f, 0.9f}, {-fs * 0.25f, 3.6f, -fs * 0.25f}, dark);
        {
            Matrix gear = mul(mul(part({1.6f, 1.6f, 0.2f}, {0, 0, 0}), MatrixRotateZ(t * 1.2f)), mul(MatrixTranslate(fs * 0.15f, 2.0f, fs * 0.52f), W));
            box(gear, Color{200, 170, 90, 255});
        }
        break;
    case B_FORGE:
        B({fs, 1.8f, fs}, {0, 0.9f, 0}, stone);
        B({fs * 0.6f, 0.8f, fs * 0.6f}, {0, 2.2f, 0}, dark);
        B({fs * 0.3f, 0.5f, 0.1f}, {0, 0.7f, fs * 0.5f}, Color{255, 140, 40, 255}, 0.4f + 0.4f * fabsf(sinf(t * 3)));
        break;
    case B_RELAY:
        B({fs * 0.8f, 0.5f, fs * 0.8f}, {0, 0.25f, 0}, dark);
        B({0.4f, 2.8f, 0.4f}, {0, 1.9f, 0}, metal);
        {
            Matrix c = mul(mul(part({0.8f, 0.8f, 0.8f}, {0, 0, 0}), MatrixRotateY(t * 2)), mul(MatrixRotateX(0.6f), MatrixTranslate(p.x, p.y + (3.6f + sinf(t * 2) * 0.15f) * prog, p.z)));
            box(c, tc, 0.9f);
        }
        break;
    case B_WATCHTOWER: case T_TOWER: {
        bool lane = e.type == T_TOWER;
        float h = lane ? 6.5f : 4.0f, wdt = lane ? fs * 0.7f : 1.2f;
        B({wdt * 1.3f, 0.6f, wdt * 1.3f}, {0, 0.3f, 0}, stone);
        B({wdt, h, wdt}, {0, h * 0.5f + 0.3f, 0}, lane ? Color{140, 138, 132, 255} : metal);
        B({wdt * 1.25f, 0.5f, wdt * 1.25f}, {0, h + 0.3f, 0}, dark);
        float pulse = 0.6f + 0.3f * sinf(t * 2 + e.id);
        if (lane) ball({p.x, p.y + (h + 1.6f) * prog, p.z}, 0.9f, tc, pulse);
        else B({0.5f, 0.5f, 0.5f}, {0, h + 0.8f, 0}, tc, pulse);
        break;
    }
    case R_CRYSTAL: {
        Rng r(e.id * 77 + 3);
        float left = clampf(e.amount / (float)(e.rich ? CRYSTAL_RICH_AMOUNT : CRYSTAL_AMOUNT), 0.25f, 1.0f);
        Color cc = e.rich ? Color{255, 210, 90, 255} : Color{90, 200, 255, 255};
        for (int k = 0; k < 5; k++) {
            float a = r.range(0, 6.28f), dd = r.range(0, 0.6f), hh = r.range(0.8f, 1.9f) * (k < 3 ? left : left * 0.8f);
            Matrix cm = mul(mul(part({0.45f, hh, 0.45f}, {0, hh * 0.5f, 0}), MatrixRotateZ(r.range(-0.35f, 0.35f))), MatrixTranslate(p.x + cosf(a) * dd, p.y, p.z + sinf(a) * dd));
            box(cm, cc, 0.55f + 0.15f * sinf(t * 2 + k));
        }
        break;
    }
    case R_GEYSER: {
        B({fs * 0.62f, 0.3f, fs * 0.62f}, {0, 0.15f, 0}, Color{104, 108, 96, 255});
        B({fs * 0.38f, 0.5f, fs * 0.38f}, {0, 0.4f, 0}, Color{70, 80, 72, 255});
        B({fs * 0.28f, 0.05f, fs * 0.28f}, {0, 0.67f, 0}, Color{110, 255, 150, 255}, 0.9f);
        if (g_settings.particles && ((int)(t * 20) + e.id) % 3 == 0)
            burst(V3(p.x + GetRandomValue(-6, 6) / 10.f, p.y + 0.8f, p.z + GetRandomValue(-6, 6) / 10.f), Color{140, 255, 180, 255}, 1, 1.2f, 1.4f, 0.25f, -1.6f, true);
        break;
    }
    case B_EXTRACTOR: {
        B({fs * 0.95f, 0.6f, fs * 0.95f}, {0, 0.3f, 0}, metal);
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5708f + 0.785f;
            B({0.7f, 2.2f, 0.7f}, {cosf(a) * fs * 0.32f, 1.4f, sinf(a) * fs * 0.32f}, dark);
        }
        B({fs * 0.45f, 1.6f, fs * 0.45f}, {0, 1.3f, 0}, Color{110, 116, 124, 255});
        bool live = !e.constructing && e.amount > 0;
        B({fs * 0.47f, 0.3f, fs * 0.47f}, {0, 1.8f, 0}, live ? Color{110, 255, 150, 255} : Color{60, 70, 64, 255}, live ? 0.6f + 0.3f * sinf(t * 3) : 0);
        B({fs * 0.5f, 0.25f, fs * 0.5f}, {0, 2.25f, 0}, tc, 0.3f);
        break;
    }
    case B_HUB: {
        B({fs, 0.7f, fs}, {0, 0.35f, 0}, stone);
        B({fs * 0.7f, 1.6f, fs * 0.7f}, {0, 1.5f, 0}, metal);
        B({fs * 0.74f, 0.35f, fs * 0.74f}, {0, 2.45f, 0}, tc, 0.5f);
        B({fs * 0.35f, 1.4f, fs * 0.35f}, {0, 3.2f, 0}, dark);
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5708f;
            B({0.6f, 0.6f, fs * 0.25f}, {cosf(a) * fs * 0.42f, 0.9f, sinf(a) * fs * 0.42f}, dark);
        }
        Matrix cr = mul(mul(part({1.2f, 1.2f, 1.2f}, {0, 0, 0}), MatrixRotateY(t * 0.9f)), mul(MatrixRotateZ(0.785f), MatrixTranslate(p.x, p.y + (4.6f + sinf(t * 1.6f) * 0.2f) * prog, p.z)));
        box(cr, tc, 0.85f);
        break;
    }
    default: break;
    }
    (void)acc; (void)w;
    if (e.constructing) {
        // scaffolding frame
        for (int k = 0; k < 4; k++) {
            float sx = (k % 2 ? 0.5f : -0.5f) * fs, sz = (k < 2 ? 0.5f : -0.5f) * fs;
            box(part({0.12f, 3.0f, 0.12f}, {p.x + sx, p.y + 1.5f, p.z + sz}), Color{200, 170, 90, 255});
        }
    }
}

void Renderer::drawEntity(const World& w, const Entity& e, const RenderView& v) {
    Color tc = e.owner >= 0 ? playerColor(e.owner) : teamColor(e.team);
    if (g_settings.colorblind && e.team == 1) tc = e.owner >= 0 ? tc : Color{240, 200, 50, 255};
    Color acc = teamColor(e.team);
    if (e.kind == K_BUILDING || e.kind == K_TOWER || e.kind == K_RESOURCE) { drawBuildingModel(w, e, tc, acc); return; }
    Vector2 p2 = lerp2(e.prevPos, e.pos, v.alpha);
    float moveAmt = clampf(dist2(e.prevPos, e.pos) / (TICK_DT * 4.0f), 0, 1);
    float atk = clampf((w.time - e.attackAnim) / 0.35f, 0, 1);
    if (atk >= 1) atk = 0;
    float scale = e.kind == K_HERO ? 1.35f : (e.type == U_TITAN ? 1.3f : 1.0f);
    bool stealth = w.time < e.stealthUntil;
    if (stealth) { baseEmissive = 0.7f; }
    drawUnitModel(e, to3(p2), e.facing, tc, acc, clock + e.id * 0.37f, moveAmt, atk, scale);
    baseEmissive = 0;
    if (e.shield > 0 && w.time < e.shieldUntil) ring(p2, e.radius + 0.55f, 0.12f, Color{140, 200, 255, 200}, 0.08f);
    if (w.time < e.stunUntil) for (int k = 0; k < 3; k++) { float a = clock * 4 + k * 2.09f; ball({p2.x + cosf(a) * 0.5f, groundAt(p2) + 2.3f * scale, p2.y + sinf(a) * 0.5f}, 0.09f, Color{255, 230, 90, 255}, 1); }
    if (w.time < e.dmgUntil || (e.team < 2 && w.time < w.titanBuffUntil[e.team] && e.kind != K_MINION)) ring(p2, e.radius + 0.3f, 0.08f, Color{255, 120, 60, 160}, 0.07f);
    if (w.time < e.hasteUntil) ring(p2, e.radius + 0.4f, 0.06f, Color{120, 255, 200, 150}, 0.07f);
    if (w.time < e.slowUntil) ring(p2, e.radius + 0.2f, 0.08f, Color{120, 160, 255, 170}, 0.07f);
    (void)v;
}

// --- main render ----------------------------------------------------------------
static const Map* s_groundMap = nullptr;
static float groundFromMap(Vector2 p) { return s_groundMap ? s_groundMap->groundHeight(p) : 0.0f; }

void Renderer::render(const World& w, const RenderView& v) {
    s_groundMap = &w.map;
    g_groundFn = groundFromMap;
    updateHeightTex(w);
    updateFogTex(w, v.team);
    float bright = g_settings.brightness;
    float sh = g_settings.shadows ? 1.0f : 0.0f;
    float fog = v.team >= 0 ? 1.0f : 0.0f;
    SetShaderValue(shader, locBrightness, &bright, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locShadows, &sh, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locTime, &clock, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locUseFog, &fog, SHADER_UNIFORM_FLOAT);
    setEmissive(0);
    BeginMode3D(v.cam);
    if (terrainLoaded) { mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE; DrawMesh(terrain, mat, MatrixIdentity()); }
    // ground decals: selection, ranges, markers
    if (v.selection)
        for (uint16_t id : *v.selection) {
            const Entity* e = w.get(id);
            if (!e) continue;
            Vector2 p = lerp2(e->prevPos, e->pos, v.alpha);
            bool mine = e->owner == v.player;
            ring(p, e->radius + 0.25f, 0.12f, mine ? Color{90, 255, 130, 230} : Color{255, 90, 80, 230});
            if (g_settings.showRanges && unitDef(e->type).range > 2 && v.selection->size() == 1) ring(p, unitDef(e->type).range + e->radius, 0.06f, Color{255, 255, 255, 60});
            if (mine && e->hasRally && (e->kind == K_BUILDING)) {
                DrawLine3D(to3(e->pos, 0.2f), to3(e->rally, 0.2f), Color{90, 255, 130, 200});
                ring(e->rally, 0.6f, 0.15f, Color{90, 255, 130, 220});
            }
            if (mine && v.selection->size() <= 24 && e->kind != K_BUILDING) {
                Vector2 goal = e->order.pt;
                if (e->order.type == O_MOVE || e->order.type == O_ATTACK_MOVE)
                    DrawLine3D(to3(p, 0.15f), to3(goal, 0.15f), e->order.type == O_MOVE ? Color{90, 230, 120, 90} : Color{255, 110, 90, 90});
            }
        }
    if (v.hovered) {
        const Entity* e = w.get(v.hovered);
        if (e) ring(lerp2(e->prevPos, e->pos, v.alpha), e->radius + 0.35f, 0.07f, e->team == w.players[v.player].team ? Color{120, 255, 160, 160} : Color{255, 140, 120, 180});
    }
    for (auto& m : marks) {
        float k = clampf((clock - m.time) / m.dur, 0, 1);
        ring(m.pos, m.radius * (1.0f - k * 0.6f), 0.14f, withAlpha(m.col, 1 - k));
    }
    // zones (telegraphed spells)
    for (auto& z : w.zones) {
        if (z.kind == Z_FIRE) {
            disc(z.pos, z.radius, Color{255, 110, 30, 70}, 0.06f);
            ring(z.pos, z.radius, 0.2f, Color{255, 160, 60, 200});
            if (((int)(clock * 30) + (int)z.pos.x) % 2 == 0) burst(V3(z.pos.x + GetRandomValue(-30, 30) / 10.f, 0.2f, z.pos.y + GetRandomValue(-30, 30) / 10.f), Color{255, 140, 40, 255}, 1, 2.5f, 0.6f, 0.15f, -2.0f, true);
        } else if (!z.fired) {
            float k = clampf((w.time - z.start) / std::max(0.1f, z.delay), 0, 1);
            Color c = z.kind == Z_METEOR ? Color{255, 80, 40, 255} : Color{120, 255, 160, 255};
            ring(z.pos, z.radius, 0.18f, withAlpha(c, 0.9f));
            disc(z.pos, z.radius * k, withAlpha(c, 0.25f), 0.05f);
            if (z.kind == Z_METEOR) ball({z.pos.x - 8 * (1 - k), groundAt(z.pos) + 1 + 25 * (1 - k), z.pos.y - 8 * (1 - k)}, 1.2f, Color{255, 120, 40, 255}, 1.0f);
            else DrawCylinder({z.pos.x, groundAt(z.pos), z.pos.y}, 0.3f * k, 0.3f * k, 30, 8, Color{150, 255, 180, (unsigned char)(150 * k)});
        }
    }
    // watch beacons
    for (size_t k = 0; k < w.map.beacons.size(); k++) {
        Vector2 b = w.map.beacons[k];
        Vector3 p = to3(b);
        uint8_t held = k < 8 ? w.beaconHeld[k] : 0;
        Color glow = held == 0 ? Color{230, 220, 170, 255} : (held == 3 ? Color{255, 255, 255, 255} : teamColor(held & 1 ? 0 : 1));
        box(part({2.4f, 0.5f, 2.4f}, {p.x, p.y + 0.25f, p.z}), Color{120, 116, 106, 255});
        box(part({1.0f, 4.2f, 1.0f}, {p.x, p.y + 2.5f, p.z}), Color{96, 92, 86, 255});
        box(mul(mul(part({0.8f, 0.8f, 0.8f}, {0, 0, 0}), MatrixRotateY(clock)), mul(MatrixRotateX(0.6f), MatrixTranslate(p.x, p.y + 5.2f + sinf(clock * 1.5f) * 0.15f, p.z))), glow, held ? 1.0f : 0.5f);
        ring(b, 3.6f, 0.08f, withAlpha(glow, 0.35f));
    }
    // entities
    for (auto& e : w.ents) {
        if (!e.alive) continue;
        if (!w.visibleTo(v.team, e)) {
            // remembered structures stay drawn in explored fog
            bool structure = e.kind == K_BUILDING || e.kind == K_TOWER;
            int ci = w.map.cellIndex(e.pos);
            if (!(structure && v.team >= 0 && ci >= 0 && w.explored[v.team][ci])) continue;
        }
        Vector2 p = e.pos;
        // cheap frustum-ish culling on the ground plane
        Vector3 ct = v.cam.target;
        if (fabsf(p.x - ct.x) > 70 || fabsf(p.y - ct.z) > 60) continue;
        drawEntity(w, e, v);
    }
    // projectiles
    for (auto& pr : w.projs) {
        if (v.team >= 0 && !w.cellVisible(v.team, pr.pos) && pr.team != v.team) continue;
        Vector3 p = to3(pr.pos, pr.height);
        switch (pr.visual) {
        case 1: ball(p, 0.45f, Color{255, 120, 40, 255}, 1); burst(p, Color{255, 160, 60, 255}, 1, 1.5f, 0.3f, 0.12f, -1, true); break;
        case 2: { Vector3 tail = to3(pr.pos - pr.dir * 2.5f, pr.height); DrawCylinderEx(tail, p, 0.06f, 0.14f, 6, Color{180, 255, 140, 255}); break; }
        case 3: ball(p, 0.18f, Color{120, 255, 160, 255}, 1); break;
        case 4: ball(p, 0.16f, teamColor(pr.team), 1); break;
        case 5: ball(p, 0.32f, teamColor(pr.team), 1); break;
        default: ball(p, 0.1f, Color{255, 240, 180, 255}, 1); break;
        }
    }
    // placement ghost / spell aim
    if (v.placeType) {
        const UnitDef& d = unitDef(v.placeType);
        float s = d.footprint * Map::CELL;
        Color c = v.placeOk ? Color{90, 255, 130, 120} : Color{255, 80, 70, 120};
        DrawCube(to3(v.placePos, 0.6f), s, 1.2f, s, c);
        DrawCubeWires(to3(v.placePos, 0.6f), s, 1.2f, s, withAlpha(c, 1));
    }
    if (v.aiming) {
        ring(v.aimFrom, v.aimRange, 0.12f, Color{140, 200, 255, 140});
        if (v.aimMode == TM_DIRECTION) {
            Vector2 d = norm2(v.aimAt - v.aimFrom);
            Vector2 end = v.aimFrom + d * v.aimRange;
            Vector2 side = {-d.y * 0.6f, d.x * 0.6f};
            Color c = {140, 200, 255, 90};
            Vector3 a = to3(v.aimFrom + side, 0.08f), b = to3(end + side, 0.08f), cc = to3(end - side, 0.08f), dd = to3(v.aimFrom - side, 0.08f);
            DrawTriangle3D(a, b, cc, c); DrawTriangle3D(a, cc, dd, c); DrawTriangle3D(cc, b, a, c); DrawTriangle3D(dd, cc, a, c);
        } else if (v.aimRadius > 0) {
            ring(v.aimAt, v.aimRadius, 0.15f, Color{255, 200, 90, 220});
            disc(v.aimAt, v.aimRadius, Color{255, 200, 90, 50});
        } else ring(v.aimAt, 0.8f, 0.15f, Color{255, 200, 90, 220});
    }
    // beams & particles
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto& b : beams) {
        float k = clampf((clock - b.time) / b.dur, 0, 1);
        DrawCylinderEx(b.a, b.b, b.width * (1 - k), b.width * (1 - k), 6, withAlpha(b.col, 1 - k));
    }
    for (auto& pt : particles) if (pt.additive) DrawCube(pt.pos, pt.size, pt.size, pt.size, withAlpha(pt.col, clampf(pt.life / pt.maxLife, 0, 1)));
    EndBlendMode();
    for (auto& pt : particles) if (!pt.additive) DrawCube(pt.pos, pt.size, pt.size, pt.size, withAlpha(pt.col, clampf(pt.life / pt.maxLife, 0, 1)));
    EndMode3D();
}

void Renderer::handleEvent(const World& w, const GameEvent& e) {
    switch (e.type) {
    case EV_DEATH: {
        const UnitDef& d = unitDef(e.c);
        Vector3 p = to3(e.p, 1.0f);
        if (d.kind == K_BUILDING || d.kind == K_TOWER) {
            burst(p, Color{255, 150, 50, 255}, 80, 12, 1.0f, 0.4f, 2, true);
            burst(p, Color{110, 105, 100, 255}, 60, 8, 2.5f, 0.6f, 6, false);
        } else if (d.kind != K_RESOURCE) {
            burst(p, Color{255, 200, 120, 255}, 10, 5, 0.4f, 0.15f, 4, true);
            burst(p, Color{90, 90, 96, 255}, 8, 3, 0.9f, 0.2f, 9, false);
        }
        break;
    }
    case EV_HIT:
        if (e.c) burst(to3(e.p, 1.2f), Color{255, 220, 150, 255}, 4, 3, 0.25f, 0.1f, 2, true);
        break;
    case EV_ABILITY: {
        const Entity* h = w.get((uint16_t)e.a);
        Color c = h ? unitDef(h->type).color : WHITE;
        burst(to3(e.p, 1.2f), c, 18, 5, 0.5f, 0.15f, 1, true);
        if (h && h->type == H_VANGUARD && e.c == 2) { marks.push_back({e.p, 5.5f, clock, 0.5f, Color{255, 220, 120, 255}}); burst(to3(e.p, 0.3f), Color{160, 140, 110, 255}, 40, 8, 0.8f, 0.3f, 8, false); }
        if (h && h->type == H_SERAPH && (e.c == 1 || e.c == 3)) marks.push_back({e.p, e.c == 3 ? 16.f : 13.f, clock, 0.8f, Color{255, 230, 140, 255}});
        if (h && h->type == H_WARLORD && (e.c == 0 || e.c == 2)) marks.push_back({e.p, e.c == 0 ? 13.f : 7.f, clock, 0.7f, Color{255, 90, 70, 255}});
        if (h && h->type == H_SERAPH && e.c == 2) marks.push_back({e.p2, 5.f, clock, 0.6f, Color{140, 180, 255, 255}});
        break;
    }
    case EV_BLINK:
        beams.push_back({to3(e.p, 1.0f), to3(e.p2, 1.0f), clock, 0.35f, Color{180, 140, 255, 255}, 0.35f});
        burst(to3(e.p2, 1.0f), Color{200, 160, 255, 255}, 20, 6, 0.4f, 0.15f, 0, true);
        break;
    case EV_METEOR:
        burst(to3(e.p, 0.5f), e.c == Z_METEOR ? Color{255, 130, 40, 255} : Color{150, 255, 170, 255}, 90, 14, 0.8f, 0.35f, 4, true);
        if (e.c == Z_ORBITAL) beams.push_back({to3(e.p, 30), to3(e.p, 0), clock, 0.5f, Color{150, 255, 170, 255}, 0.8f});
        break;
    case EV_HEAL:
        burst(to3(e.p, 1.0f), Color{120, 255, 150, 255}, 6, 2, 0.6f, 0.12f, -2, true);
        break;
    case EV_LEVELUP:
        marks.push_back({e.p, 2.0f, clock, 0.9f, Color{255, 220, 90, 255}});
        burst(to3(e.p, 0.5f), Color{255, 220, 90, 255}, 25, 3, 1.0f, 0.12f, -3, true);
        break;
    case EV_SPAWN:
        burst(to3(e.p, 1.0f), Color{255, 120, 90, 255}, 14, 4, 0.5f, 0.15f, 0, true);
        break;
    case EV_BUILD_DONE: case EV_UNIT_DONE:
        if (e.type == EV_BUILD_DONE) burst(to3(e.p, 1.0f), Color{255, 230, 160, 255}, 30, 6, 0.6f, 0.15f, 3, true);
        break;
    case EV_TITAN_SLAIN:
        burst(to3(e.p, 2.0f), Color{200, 120, 255, 255}, 150, 16, 1.4f, 0.4f, 2, true);
        break;
    default: break;
    }
}

void Renderer::update(float dt) {
    clock += dt;
    for (auto& p : particles) {
        p.life -= dt;
        p.vel.y -= p.gravity * dt;
        p.vel = p.vel * (1.0f - 1.2f * dt);
        p.pos = p.pos + p.vel * dt;
        if (p.pos.y < 0.05f && p.gravity > 0) { p.pos.y = 0.05f; p.vel = p.vel * 0.3f; }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }), particles.end());
    beams.erase(std::remove_if(beams.begin(), beams.end(), [&](const Beam& b) { return clock - b.time > b.dur; }), beams.end());
    marks.erase(std::remove_if(marks.begin(), marks.end(), [&](const GroundMark& m) { return clock - m.time > m.dur; }), marks.end());
}

} // namespace ax
