#include "map.h"
#include <queue>

namespace ax {

float (*g_groundFn)(Vector2 p) = nullptr;

static const MapInfo kMaps[MAP_COUNT] = {
    {"FAILLE D'OBSIDIENNE", "Ouest contre est. Naturelles exposées, base riche au sud et au nord."},
    {"PLATEAUX JUMEAUX", "Nord contre sud. Deux longues routes et des expansions sur les flancs."},
    {"CRATÈRE DE VESTA", "Bases décalées autour d'un cratère. Bases riches au centre."},
    {"HAUTE TENSION", "Départs collés au bord. Rush rapide par le centre ou expansion risquée."},
};
const MapInfo& mapInfo(int id) { return kMaps[std::clamp(id, 0, MAP_COUNT - 1)]; }

namespace {
struct BaseDef { Vector2 hub; float dirDeg; bool rich; };
struct Blob { Vector2 c; float r; };
struct MapDef {
    Vector2 main; float dirDeg; float rampDeg;
    std::vector<BaseDef> bases;                        // team 0 side; mirrored for team 1
    std::vector<Blob> rocks, lakes;                    // mirrored
    std::vector<Vector2> camps, beacons;               // mirrored
    std::vector<std::pair<Vector2, Vector2>> paths;    // mirrored (decoration)
    std::vector<std::pair<Vector2, Vector2>> ridges;   // mirrored rock walls (chokes)
    uint32_t seed;
};

MapDef mapDef(int id) {
    MapDef d;
    switch (id) {
    default:
    case 0:   // west vs east
        d.main = {30, 100}; d.dirDeg = 180; d.rampDeg = 300;
        d.bases = {{{62, 64}, 225, false}, {{32, 30}, 225, false}, {{70, 170}, 135, false}, {{100, 148}, 90, true}};
        d.rocks = {{{84, 122}, 5}, {{48, 152}, 5}, {{20, 62}, 5}, {{86, 74}, 4}, {{118, 160}, 4}};
        d.lakes = {{{74, 96}, 5}};
        d.camps = {{50, 132}, {82, 40}};
        d.beacons = {{82, 112}};
        d.paths = {{{43, 83}, {62, 64}}, {{62, 64}, {100, 100}}, {{100, 100}, {100, 148}}, {{62, 64}, {32, 30}}, {{100, 148}, {70, 170}}};
        d.ridges = {{{84, 42}, {92, 66}}, {{46, 126}, {60, 150}}, {{118, 96}, {128, 118}}};
        d.seed = 101;
        break;
    case 1:   // north vs south
        d.main = {100, 30}; d.dirDeg = 270; d.rampDeg = 135;
        d.bases = {{{66, 62}, 200, false}, {{150, 48}, 315, false}, {{28, 100}, 180, false}};
        d.rocks = {{{100, 72}, 5}, {{70, 100}, 5}, {{120, 64}, 4}, {{46, 128}, 4}};
        d.lakes = {{{168, 18}, 7}, {{88, 120}, 4}};
        d.camps = {{122, 76}, {40, 70}};
        d.beacons = {{60, 100}};
        d.paths = {{{86, 47}, {66, 62}}, {{66, 62}, {100, 100}}, {{66, 62}, {28, 100}}, {{100, 100}, {150, 48}}};
        d.ridges = {{{122, 26}, {128, 52}}, {{38, 82}, {54, 78}}, {{84, 92}, {90, 112}}};
        d.seed = 202;
        break;
    case 2:   // offset diagonal around a crater
        d.main = {42, 150}; d.dirDeg = 135; d.rampDeg = 0;
        d.bases = {{{86, 160}, 90, false}, {{36, 98}, 180, false}, {{70, 70}, 225, true}};
        d.rocks = {{{76, 134}, 5}, {{50, 76}, 5}, {{110, 176}, 4}};
        d.lakes = {{{88, 116}, 5}};
        d.camps = {{60, 118}, {132, 172}};
        d.beacons = {{100, 132}};
        d.paths = {{{64, 148}, {86, 160}}, {{64, 148}, {100, 100}}, {{36, 98}, {64, 148}}, {{100, 100}, {70, 70}}};
        d.ridges = {{{98, 148}, {104, 174}}, {{52, 98}, {60, 84}}, {{112, 124}, {124, 114}}};
        d.seed = 303;
        break;
    case 3:   // starts against the top / bottom edges
        d.main = {64, 30}; d.dirDeg = 270; d.rampDeg = 60;
        d.bases = {{{100, 60}, 300, false}, {{28, 80}, 180, false}, {{150, 30}, 315, false}, {{60, 122}, 135, true}};
        d.rocks = {{{126, 56}, 5}, {{42, 104}, 4}, {{80, 82}, 4}};
        d.lakes = {{{74, 98}, 4}};
        d.camps = {{112, 30}, {90, 118}};
        d.beacons = {{30, 130}};
        d.paths = {{{74, 50}, {100, 60}}, {{100, 60}, {100, 100}}, {{74, 50}, {28, 80}}, {{100, 60}, {150, 30}}, {{100, 100}, {60, 122}}};
        d.ridges = {{{122, 40}, {126, 62}}, {{40, 94}, {54, 98}}, {{80, 128}, {92, 138}}};
        d.seed = 404;
        break;
    }
    return d;
}

Vector2 mirror(Vector2 p) { return {Map::SIZE - p.x, Map::SIZE - p.y}; }
Vector2 dirOf(float deg) { float a = deg * 3.14159265f / 180.f; return {cosf(a), sinf(a)}; }
}

void Map::generate(int mapId) {
    id = std::clamp(mapId, 0, MAP_COUNT - 1);
    MapDef D = mapDef(id);
    const int C = N * N;
    type.assign(C, CELL_GROUND);
    blocked.assign(C, 0);
    level.assign(C, 0);
    height.assign(C, 0);
    ground.assign(C, 0);
    expansions.clear(); camps.clear(); beacons.clear();
    Rng r(D.seed);
    std::vector<uint8_t> protect(C, 0);     // cells that must stay open

    auto forCells = [&](Vector2 c, float rad, const std::function<void(int, float)>& f) {
        int x0 = std::max(0, (int)((c.x - rad) / CELL) - 1), x1 = std::min(N - 1, (int)((c.x + rad) / CELL) + 1);
        int z0 = std::max(0, (int)((c.y - rad) / CELL) - 1), z1 = std::min(N - 1, (int)((c.y + rad) / CELL) + 1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                int i = z * N + x;
                float d = dist2(cellCenter(i), c);
                if (d < rad) f(i, d);
            }
    };
    auto rock = [&](int i) { if (protect[i]) return; type[i] = CELL_ROCK; blocked[i] = 1; level[i] = 0; };
    auto open = [&](int i) { if (type[i] == CELL_ROCK || type[i] == CELL_WATER) { type[i] = CELL_GROUND; blocked[i] = 0; } };

    // everything is symmetric by a half turn around the map centre
    struct Main { Vector2 hub, dir, center, rampDir; };
    Main mains[2];
    mains[0].hub = D.main; mains[0].dir = dirOf(D.dirDeg); mains[0].rampDir = dirOf(D.rampDeg);
    mains[1].hub = mirror(D.main); mains[1].dir = mains[0].dir * -1; mains[1].rampDir = mains[0].rampDir * -1;
    for (auto& m : mains) m.center = m.hub - m.dir * 3;
    std::vector<BaseDef> bases;
    for (auto& b : D.bases) { bases.push_back(b); bases.push_back({mirror(b.hub), b.dirDeg + 180, b.rich}); }
    for (auto& c : D.camps) { camps.push_back(c); camps.push_back(mirror(c)); }
    for (auto& b : D.beacons) { beacons.push_back(b); beacons.push_back(mirror(b)); }
    titanPos = {100, 100};

    // 1. rock formations and lakes (+ small procedural outcrops)
    std::vector<Blob> rocks, lakes;
    for (auto& b : D.rocks) { rocks.push_back(b); rocks.push_back({mirror(b.c), b.r}); }
    for (auto& b : D.lakes) { lakes.push_back(b); lakes.push_back({mirror(b.c), b.r}); }
    std::vector<Vector2> keyPoints;
    for (auto& m : mains) keyPoints.push_back(m.hub);
    for (auto& b : bases) keyPoints.push_back(b.hub);
    for (auto& c : camps) keyPoints.push_back(c);
    for (auto& b : beacons) keyPoints.push_back(b);
    keyPoints.push_back(titanPos);
    for (int k = 0; k < 9; k++) {
        Vector2 p = {r.range(14, SIZE - 14), r.range(14, SIZE / 2)};
        bool ok = true;
        for (auto& q : keyPoints) if (dist2(p, q) < 20 || dist2(mirror(p), q) < 20) ok = false;
        for (auto& m : mains) if (dist2(p, m.center) < 34 || dist2(mirror(p), m.center) < 34) ok = false;
        if (!ok) continue;
        float rr = r.range(3.0f, 5.0f);
        rocks.push_back({p, rr}); rocks.push_back({mirror(p), rr});
    }
    for (auto& b : rocks)
        forCells(b.c, b.r * 1.6f, [&](int i, float d) {
            Vector2 q = cellCenter(i);
            float wob = b.r * (0.75f + 0.35f * sinf(q.x * 0.9f + b.c.y) * cosf(q.y * 0.7f + b.c.x));
            if (d < wob) rock(i);
        });
    for (auto& rg : D.ridges)
        for (int m = 0; m < 2; m++) {
            Vector2 a = m ? mirror(rg.first) : rg.first, b = m ? mirror(rg.second) : rg.second;
            for (int i = 0; i < C; i++) {
                Vector2 q = cellCenter(i);
                float w = 4.4f * (0.88f + 0.14f * sinf(q.x * 0.35f + q.y * 0.5f));
                if (distPointSeg(q, a, b) < w) rock(i);
            }
        }
    for (auto& b : lakes)
        forCells(b.c, b.r * 1.5f, [&](int i, float d) {
            Vector2 q = cellCenter(i);
            float wob = b.r * (0.85f + 0.25f * sinf(q.x * 0.6f + q.y * 0.4f));
            if (d < wob) { type[i] = CELL_WATER; blocked[i] = 1; ground[i] = -0.35f; }
        });

    // 2. clear the bases, camps, beacons and the titan arena
    auto clearAround = [&](Vector2 c, float rad) { forCells(c, rad, [&](int i, float) { open(i); ground[i] = 0; protect[i] = 1; }); };
    for (auto& b : bases) clearAround(b.hub, 13);
    for (auto& c : camps) clearAround(c, 7);
    for (auto& b : beacons) clearAround(b, 5);
    clearAround(titanPos, 9);

    // 3. main plateaus: high ground ringed by cliffs, one ramp
    for (int t = 0; t < 2; t++) {
        Main& m = mains[t];
        float ph = r.range(0, 6.28f);
        auto radiusAt = [&](float a) { return 20.0f + 1.8f * sinf(3 * a + ph) + 1.0f * sinf(5 * a + ph * 2); };
        Vector2 rampOuter = m.center + m.rampDir * (radiusAt(angleOf(m.rampDir)) + 7);
        forCells(m.center, 28, [&](int i, float d) {
            Vector2 q = cellCenter(i);
            float R = radiusAt(angleOf(q - m.center));
            Vector2 rel = q - m.center;
            float along = dot2(rel, m.rampDir), lateral = fabsf(rel.x * m.rampDir.y - rel.y * m.rampDir.x);
            bool inRamp = along > 0 && lateral < 3.6f && d > R - 3 && d < R + 7;
            if (d < R - 0.01f && !inRamp) { type[i] = CELL_HIGH; blocked[i] = 0; level[i] = 1; ground[i] = HIGH; protect[i] = 1; }
            else if (inRamp) {
                float k = clampf((R + 7 - d) / 10.0f, 0, 1);
                k = k * k * (3 - 2 * k);
                type[i] = CELL_RAMP; blocked[i] = 0; level[i] = 0; ground[i] = HIGH * k; protect[i] = 1;
            } else if (d < R + 3.4f) {
                type[i] = CELL_ROCK; blocked[i] = 1; level[i] = 0;
                height[i] = HIGH + 0.25f + 0.35f * fabsf(sinf(q.x * 1.3f + q.y * 0.7f));
                protect[i] = 2;
            }
        });
        clearAround(rampOuter + m.rampDir * 1.5f, 6);
        ramp[t] = rampOuter + m.rampDir * 2.0f;
        rampTop[t] = m.center + m.rampDir * (radiusAt(angleOf(m.rampDir)) - 3);
        base[t] = m.hub;
        // tiles around the core
        forCells(m.hub, 9, [&](int i, float) { if (type[i] == CELL_HIGH) type[i] = (uint8_t)(t == 0 ? CELL_BASE0 : CELL_BASE1); });
    }

    // 4. expansions: crystals in an arc behind the hub, two flux vents on the sides
    auto makeExp = [&](Vector2 hub, Vector2 dir, bool rich, int mainOf) {
        Expansion e;
        e.hub = snapBuilding(hub, 5); e.dir = dir; e.rich = rich; e.mainOf = mainOf;
        float a0 = angleOf(dir);
        for (int k = 0; k < 8; k++) {
            float a = a0 + (-60.f + k * 17.f) * 3.14159f / 180.f;
            float rad = (k % 2) ? 9.4f : 8.4f;
            Vector2 p = e.hub + V2(cosf(a), sinf(a)) * rad;
            int ci = cellIndex(p);
            if (ci < 0) continue;
            p = cellCenter(ci);
            bool dup = false;
            for (auto& q : e.crystals) if (dist2(q, p) < 1.0f) dup = true;
            if (!dup) e.crystals.push_back(p);
        }
        for (int s = -1; s <= 1; s += 2) {
            float a = a0 + s * 102.f * 3.14159f / 180.f;
            e.geysers.push_back(snapBuilding(e.hub + V2(cosf(a), sinf(a)) * 10.5f, 3));
        }
        expansions.push_back(e);
    };
    for (int t = 0; t < 2; t++) makeExp(mains[t].hub, mains[t].dir, false, t);
    for (auto& b : bases) makeExp(b.hub, dirOf(b.dirDeg), b.rich, -1);
    for (auto& e : expansions) {
        for (auto& c : e.crystals) { int ci = cellIndex(c); if (ci >= 0) { open(ci); blocked[ci] = 3; } }
        for (auto& g : e.geysers) forCells(g, 3.2f, [&](int i, float) { open(i); });
    }

    // 5. decoration: worn paths and grass
    for (auto& pr : D.paths)
        for (int m = 0; m < 2; m++) {
            Vector2 a = m ? mirror(pr.first) : pr.first, b = m ? mirror(pr.second) : pr.second;
            for (int i = 0; i < C; i++)
                if (type[i] == CELL_GROUND && distPointSeg(cellCenter(i), a, b) < 2.6f + 0.8f * sinf(cellCenter(i).x * 0.3f)) type[i] = CELL_PATH;
        }
    for (int i = 0; i < C; i++) {
        if (type[i] != CELL_GROUND) continue;
        Vector2 q = cellCenter(i);
        float n = sinf(q.x * 0.11f + D.seed) * cosf(q.y * 0.09f) + 0.5f * sinf(q.x * 0.31f - q.y * 0.27f);
        if (n > 0.55f) type[i] = CELL_GRASS;
    }
    for (auto& c : camps) forCells(c, 6, [&](int i, float) { if (type[i] == CELL_GROUND || type[i] == CELL_PATH) type[i] = CELL_GRASS; });

    // 6. map border
    for (int i = 0; i < C; i++) {
        int x = i % N, z = i / N;
        if (x < 2 || z < 2 || x >= N - 2 || z >= N - 2) { type[i] = CELL_ROCK; blocked[i] = 1; level[i] = 0; ground[i] = 0; }
    }
    // 7. connectivity: every important point must be reachable from the first ramp
    auto flood = [&]() {
        std::vector<uint8_t> seen(C, 0);
        std::vector<int> st;
        int s0 = cellIndex(ramp[0]);
        if (s0 < 0) return seen;
        st.push_back(s0); seen[s0] = 1;
        while (!st.empty()) {
            int c = st.back(); st.pop_back();
            int cx = c % N, cz = c / N;
            const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = cx + dx[k], nz = cz + dz[k];
                if (!inside(nx, nz)) continue;
                int ni = nz * N + nx;
                if (seen[ni] || blocked[ni] == 1) continue;
                seen[ni] = 1; st.push_back(ni);
            }
        }
        return seen;
    };
    std::vector<Vector2> must = {ramp[1], titanPos, base[0], base[1]};
    for (auto& e : expansions) must.push_back(e.hub + e.dir * -7.0f);
    for (auto& c : camps) must.push_back(c);
    for (auto& b : beacons) must.push_back(b);
    for (int pass = 0; pass < 12; pass++) {
        auto seen = flood();
        bool all = true;
        for (auto& p : must) {
            int ci = cellIndex(p);
            if (ci < 0 || seen[ci]) continue;
            all = false;
            Vector2 d = norm2(titanPos - p);
            for (float s = 0; s < 140; s += 1.0f) {
                Vector2 q = p + d * s;
                int qi = cellIndex(q);
                if (qi >= 0 && seen[qi]) break;
                forCells(q, 3.2f, [&](int i, float) { if (protect[i] != 2 && blocked[i] == 1) { type[i] = CELL_GROUND; blocked[i] = 0; ground[i] = level[i] ? HIGH : 0; } });
            }
        }
        if (all) break;
    }
    // rock heights with gentle variation (cliffs keep theirs)
    for (int i = 0; i < C; i++) {
        if (type[i] != CELL_ROCK || height[i] > 0) continue;
        int x = i % N, z = i / N;
        float n = 0.5f + 0.5f * sinf(x * 0.37f + z * 0.13f) * cosf(z * 0.29f - x * 0.11f);
        height[i] = 2.8f + n * 2.6f + r.f01() * 0.4f;
    }
    // smooth surface for the renderer
    vheight.assign((N + 1) * (N + 1), 0);
    for (int vz = 0; vz <= N; vz++)
        for (int vx = 0; vx <= N; vx++) {
            float sum = 0; int cnt = 0;
            for (int dz = -1; dz <= 0; dz++)
                for (int dx = -1; dx <= 0; dx++) {
                    int x = vx + dx, z = vz + dz;
                    if (!inside(x, z)) continue;
                    int i = z * N + x;
                    if (type[i] == CELL_ROCK || type[i] == CELL_WATER) continue;
                    sum += ground[i]; cnt++;
                }
            vheight[vz * (N + 1) + vx] = cnt ? sum / cnt : 0;
        }
    baseRadius = 20;
    version++;
    flows.clear();
}

float Map::groundHeight(Vector2 p) const {
    if (vheight.empty()) return 0;
    float gx = clampf(p.x / CELL, 0, (float)N - 0.001f), gz = clampf(p.y / CELL, 0, (float)N - 0.001f);
    int x = (int)gx, z = (int)gz;
    float fx = gx - x, fz = gz - z;
    const int W = N + 1;
    float a = vheight[z * W + x], b = vheight[z * W + x + 1], c = vheight[(z + 1) * W + x], d = vheight[(z + 1) * W + x + 1];
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

int Map::teamOfBase(Vector2 p) const {
    for (int t = 0; t < 2; t++) if (dist2(p, base[t]) < baseRadius && levelAt(p) == 1) return t;
    return -1;
}

Vector2 Map::snapBuilding(Vector2 p, int fp) const {
    // align footprint to the cell grid
    float half = fp * CELL * 0.5f;
    float ox = (fp % 2) ? CELL * 0.5f : 0;
    float x = roundf((p.x - ox) / CELL) * CELL + ox;
    float z = roundf((p.y - ox) / CELL) * CELL + ox;
    (void)half;
    return {x, z};
}

bool Map::canPlace(Vector2 c, int fp) const {
    float half = fp * CELL * 0.5f;
    int x0 = (int)floorf((c.x - half) / CELL + 0.01f), z0 = (int)floorf((c.y - half) / CELL + 0.01f);
    for (int z = z0; z < z0 + fp; z++)
        for (int x = x0; x < x0 + fp; x++) {
            if (!inside(x, z) || blocked[z * N + x] || type[z * N + x] == CELL_RAMP) return false;
            if (x < 3 || z < 3 || x >= N - 3 || z >= N - 3) return false;
        }
    return true;
}

bool Map::nearResources(Vector2 c, int fp, float margin) const {
    float half = fp * CELL * 0.5f + margin;
    int x0 = (int)floorf((c.x - half) / CELL), z0 = (int)floorf((c.y - half) / CELL);
    int x1 = (int)floorf((c.x + half) / CELL), z1 = (int)floorf((c.y + half) / CELL);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (inside(x, z) && blocked[z * N + x] == 3) return true;
    return false;
}

void Map::setBlocked(Vector2 c, int fp, uint8_t v) {
    float half = fp * CELL * 0.5f;
    int x0 = (int)floorf((c.x - half) / CELL + 0.01f), z0 = (int)floorf((c.y - half) / CELL + 0.01f);
    for (int z = z0; z < z0 + fp; z++)
        for (int x = x0; x < x0 + fp; x++)
            if (inside(x, z) && blocked[z * N + x] != 1 && !(v == 0 && type[z * N + x] == CELL_WATER)) blocked[z * N + x] = v;
    version++;
}

bool Map::lineClear(Vector2 a, Vector2 b) const {
    Vector2 d = b - a;
    float L = len2(d);
    int steps = (int)(L / (CELL * 0.45f)) + 1;
    for (int i = 0; i <= steps; i++) {
        Vector2 p = a + d * ((float)i / steps);
        if (!walkable(p)) return false;
    }
    return true;
}

Vector2 Map::nearestWalkable(Vector2 p) const {
    if (walkable(p)) return p;
    int ci = cellIndex(p);
    int cx = ci >= 0 ? ci % N : (int)(p.x / CELL), cz = ci >= 0 ? ci / N : (int)(p.y / CELL);
    for (int r = 1; r < 20; r++)
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dz)) != r) continue;
                if (walkableCell(cx + dx, cz + dz)) return cellCenter((cz + dz) * N + cx + dx);
            }
    return p;
}

FlowField& Map::getFlow(int goal, float radius) {
    long long key = (long long)goal * 64 + (int)std::min(radius, 60.f);
    FlowField& f = flows[key];
    f.lastUse = frame;
    if (f.version == version && f.goal == goal) return f;
    f.goal = goal; f.version = version;
    f.dist.assign(N * N, 1e9f);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    // seed: walkable cells within radius of the goal (handles blocked goals such as buildings)
    Vector2 gp = cellCenter(goal);
    int r = (int)ceilf(radius / CELL) + 1;
    int gx = goal % N, gz = goal / N;
    bool seeded = false;
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            int x = gx + dx, z = gz + dz;
            if (!walkableCell(x, z)) continue;
            if (dist2(cellCenter(z * N + x), gp) > radius + CELL * 0.75f) continue;
            f.dist[z * N + x] = 0; q.push({0, z * N + x}); seeded = true;
        }
    if (!seeded) {
        Vector2 w = nearestWalkable(gp);
        int wi = cellIndex(w);
        if (wi >= 0) { f.dist[wi] = 0; q.push({0, wi}); }
    }
    while (!q.empty()) {
        auto [d, c] = q.top(); q.pop();
        if (d > f.dist[c]) continue;
        int cx = c % N, cz = c / N;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dz) continue;
                int nx = cx + dx, nz = cz + dz;
                if (!walkableCell(nx, nz)) continue;
                if (dx && dz && (!walkableCell(cx + dx, cz) || !walkableCell(cx, cz + dz))) continue;
                float nd = d + ((dx && dz) ? 1.4142f : 1.0f);
                int ni = nz * N + nx;
                if (nd < f.dist[ni]) { f.dist[ni] = nd; q.push({nd, ni}); }
            }
    }
    // keep the cache small
    if (flows.size() > 48) {
        uint32_t oldest = UINT32_MAX; long long ok = -1;
        for (auto& kv : flows) if (kv.second.lastUse < oldest && kv.first != key) { oldest = kv.second.lastUse; ok = kv.first; }
        if (ok >= 0) flows.erase(ok);
        return flows[key];
    }
    return f;
}

Vector2 Map::flowDirection(Vector2 from, Vector2 goal, float goalRadius) {
    if (lineClear(from, goal)) return norm2(goal - from);
    int gi = cellIndex(goal);
    if (gi < 0) return norm2(goal - from);
    FlowField& f = getFlow(gi, goalRadius);
    int ci = cellIndex(from);
    if (ci < 0) return norm2(goal - from);
    if (blocked[ci]) {
        Vector2 w = nearestWalkable(from);
        return norm2(w - from);
    }
    int cx = ci % N, cz = ci / N;
    float best = f.dist[ci];
    Vector2 dir{0, 0};
    // weighted gradient over the 8 neighbours for smooth motion
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dz) continue;
            int nx = cx + dx, nz = cz + dz;
            if (!walkableCell(nx, nz)) continue;
            if (dx && dz && (!walkableCell(cx + dx, cz) || !walkableCell(cx, cz + dz))) continue;
            float nd = f.dist[nz * N + nx];
            if (nd < best - 0.01f) {
                float w = (best - nd) / ((dx && dz) ? 1.4142f : 1.0f);
                dir += norm2(V2((float)dx, (float)dz)) * (w * w);
            }
        }
    if (len2(dir) < 1e-4f) {
        // at a local minimum: aim at the lowest neighbour centre
        return norm2(goal - from);
    }
    return norm2(dir);
}

float Map::flowDistance(Vector2 from, Vector2 goal) {
    int gi = cellIndex(goal), ci = cellIndex(from);
    if (gi < 0 || ci < 0) return 1e9f;
    return getFlow(gi, 1.5f).dist[ci] * CELL;
}

} // namespace ax
