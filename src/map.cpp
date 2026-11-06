#include "map.h"
#include <queue>

namespace ax {

// 48x48 tiles of 2m. Generated with tools/mapgen.py
// # wall  x wood wall  c crate(0.85) k crate(1.35 crouch-jump) C big crate(2.1) w low wall(1.05)
// 1..4 platforms (0.4m per level)  A/B plant zones  T/D spawns
static const char* kCitadel[48] = {
    "################################################",
    "################################################",
    "#################......k.......#################",
    "########...........DDDDDDDDDD...........########",
    "########................................########",
    "########..........c..........c..........########",
    "###.www.....................................3###",
    "###............44####ww....####.............3###",
    "###..AAAAAAAAc.44####......####...BBBBBBBBc.3###",
    "###2.AAAAAAAAk.3.####....c.####...kBBBBBBBc.2###",
    "###2.AAACCcAAA.2.####......####...BBBkCCBBB.1###",
    "###1.AAkCCAAAA.1.####..##..####...BBBBCCcBB..###",
    "###..AAAAAAAAA...####..##..####x..BBBBBBBBB..###",
    "###..AAAAAAAAA...###........###x..BBBBBBBBB..###",
    "###..ccAAAAAAA...###.......k###x..BB#BBBBBB..###",
    "###..AAAAAAAAA...###........###...BBBBBBBwww.###",
    "###...........www###..c.....###..............###",
    "###.....##.....#####........#####.....##.....###",
    "##......##.............ww...#####.....###....###",
    "##......##.......k.............##.....###....###",
    "##......##..c..##..............##..k..##......##",
    "##.c....##.....##.............c.......##......##",
    "##c.....##..........xxx..xxx..........##......##",
    "##......############........############......##",
    "##......############........############.c....##",
    "##......############........############......##",
    "####...#############.....ck.############......##",
    "####...#############........############......##",
    "##......############c.......############......##",
    "##......############........############..######",
    "##......############........############..######",
    "##......############........############...c..##",
    "##....CC############........############......##",
    "##...kCC############.CC.....###############...##",
    "##......############.CCc....###############...##",
    "##......############........############......##",
    "##......############........############....k.##",
    "##....c.############........############......##",
    "##.......................................c....##",
    "##............................................##",
    "##............................................##",
    "#####.......ck....................c........#####",
    "#####......................................#####",
    "#####.....TTTTTTTTTTTTTTTTTTTTTTTTTTTT.....#####",
    "#####...................C..................#####",
    "#####......................................#####",
    "################################################",
    "################################################",
};

static Tile tileFromChar(char c) {
    Tile t; t.ch = c;
    switch (c) {
    case '#': t.height = 6.0f; t.mat = MAT_CONCRETE; t.walkableNav = false; break;
    case 'x': t.height = 3.6f; t.mat = MAT_WOOD; t.walkableNav = false; break;
    case 'c': t.height = 0.85f; t.mat = MAT_WOOD; t.walkableNav = false; break;
    case 'k': t.height = 1.35f; t.mat = MAT_WOOD; t.walkableNav = false; break;
    case 'C': t.height = 2.1f; t.mat = MAT_WOOD; t.walkableNav = false; break;
    case 'w': t.height = 1.05f; t.mat = MAT_CONCRETE; t.walkableNav = false; break;
    case 'm': t.height = 1.2f; t.mat = MAT_METAL; t.walkableNav = false; break;
    case '1': case '2': case '3': case '4':
        t.height = 0.4f * (c - '0'); t.mat = MAT_CONCRETE; t.walkableNav = true; break;
    case 'A': t.zone = ZONE_SITE_A; break;
    case 'B': t.zone = ZONE_SITE_B; break;
    case 'T': t.zone = ZONE_SPAWN_ATK; break;
    case 'D': t.zone = ZONE_SPAWN_DEF; break;
    default: break;
    }
    return t;
}

static float yawTo(Vector3 a, Vector3 b) { float y, p; anglesFromDir(b - a, y, p); return y; }

void Map::loadCitadel() {
    name = "CITADEL";
    w = 48; h = 48;
    tiles.assign(w * h, Tile{});
    spawnAtk.clear(); spawnDef.clear();
    for (int z = 0; z < h; z++)
        for (int x = 0; x < w; x++) tiles[z * w + x] = tileFromChar(kCitadel[z][x]);

    // buy zones: BFS distance <= 8 tiles from spawn tiles
    auto bfsZone = [&](uint8_t spawnFlag, uint8_t buyFlag) {
        std::vector<int> dist(w * h, -1);
        std::queue<int> q;
        for (int i = 0; i < w * h; i++) if (tiles[i].zone & spawnFlag) { dist[i] = 0; q.push(i); }
        while (!q.empty()) {
            int i = q.front(); q.pop();
            tiles[i].zone |= buyFlag;
            if (dist[i] >= 8) continue;
            int x = i % w, z = i / w;
            const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], nz = z + dz[k];
                if (!inBounds(nx, nz)) continue;
                int j = nz * w + nx;
                if (dist[j] >= 0 || tiles[j].height > 0.5f) continue;
                dist[j] = dist[i] + 1; q.push(j);
            }
        }
    };
    bfsZone(ZONE_SPAWN_ATK, ZONE_BUY_ATK);
    bfsZone(ZONE_SPAWN_DEF, ZONE_BUY_DEF);

    for (int x = 11; x <= 36; x += 5) spawnAtk.push_back(tileCenter(x, 43));
    for (int x = 13; x <= 34; x += 5) spawnAtk.push_back(tileCenter(x, 44));
    for (int x = 19; x <= 28; x += 2) spawnDef.push_back(tileCenter(x, 3));
    for (int x = 20; x <= 28; x += 2) spawnDef.push_back(tileCenter(x, 4));

    siteA = tileCenter(9, 13);
    siteB = tileCenter(38, 13);
    auto T = [&](int x, int z) { return tileCenter(x, z); };
    auto hs = [&](int x, int z, int lx, int lz, uint8_t zone) {
        Vector3 p = T(x, z); p.y = tiles[z * w + x].height;
        return HoldSpot{p, yawTo(T(x, z), T(lx, lz)), zone};
    };
    holdA = {hs(12, 12, 12, 19, ZONE_SITE_A), hs(6, 9, 5, 22, ZONE_SITE_A), hs(16, 8, 5, 24, ZONE_SITE_A), hs(11, 15, 12, 21, ZONE_SITE_A)};
    holdB = {hs(36, 12, 35, 19, ZONE_SITE_B), hs(41, 9, 42, 21, ZONE_SITE_B), hs(44, 7, 42, 22, ZONE_SITE_B), hs(39, 14, 43, 20, ZONE_SITE_B)};
    holdMid = {hs(21, 12, 23, 26, 0), hs(25, 15, 24, 30, 0)};
    routeA = {T(5, 24), T(12, 20), T(24, 19)};
    routeB = {T(43, 23), T(35, 20), T(24, 19)};
}

void Map::loadAimArena() {
    name = "ARENA";
    w = 24; h = 24;
    tiles.assign(w * h, Tile{});
    for (int z = 0; z < h; z++)
        for (int x = 0; x < w; x++)
            if (x == 0 || z == 0 || x == w - 1 || z == h - 1) tiles[z * w + x] = tileFromChar('#');
    spawnAtk = {tileCenter(12, 20)};
    spawnDef = {tileCenter(12, 3)};
    siteA = tileCenter(12, 4); siteB = siteA;
    holdA.clear(); holdB.clear(); holdMid.clear();
}

// ---------------------------------------------------------------------------
float Map::groundHeightAt(Vector3 pos, float r, float maxTop) const {
    int x0, z0, x1, z1;
    worldToTile({pos.x - r, 0, pos.z - r}, x0, z0);
    worldToTile({pos.x + r, 0, pos.z + r}, x1, z1);
    float g = 0;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            float hh = at(x, z).height;
            if (hh <= maxTop && hh > g) g = hh;
        }
    return g;
}

bool Map::boxBlocked(Vector3 feet, float r, float height) const {
    (void)height;
    int x0, z0, x1, z1;
    const float e = 1e-4f;
    worldToTile({feet.x - r + e, 0, feet.z - r + e}, x0, z0);
    worldToTile({feet.x + r - e, 0, feet.z + r - e}, x1, z1);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            if (at(x, z).height > feet.y + 1e-3f) return true;
    return false;
}

bool Map::tileWalkable(int x, int z) const { return inBounds(x, z) && at(x, z).walkableNav; }

// DDA over tiles collecting solid spans
void Map::raySpans(Vector3 o, Vector3 d, float maxDist, std::vector<SolidSpan>& out) const {
    out.clear();
    float gx = o.x / TILE + w * 0.5f, gz = o.z / TILE + h * 0.5f;
    int tx = (int)floorf(gx), tz = (int)floorf(gz);
    float ddx = d.x / TILE, ddz = d.z / TILE;   // tile units per world distance
    int stepX = ddx > 0 ? 1 : -1, stepZ = ddz > 0 ? 1 : -1;
    float tDeltaX = fabsf(ddx) > 1e-9f ? fabsf(1.0f / ddx) : 1e30f;
    float tDeltaZ = fabsf(ddz) > 1e-9f ? fabsf(1.0f / ddz) : 1e30f;
    float tMaxX = fabsf(ddx) > 1e-9f ? ((stepX > 0 ? (tx + 1 - gx) : (gx - tx)) * tDeltaX) : 1e30f;
    float tMaxZ = fabsf(ddz) > 1e-9f ? ((stepZ > 0 ? (tz + 1 - gz) : (gz - tz)) * tDeltaZ) : 1e30f;
    float t0 = 0;
    Vector3 entryNormal = {0, 0, 0};
    float floorT = d.y < -1e-6f ? -o.y / d.y : 1e30f;
    float limit = std::min(maxDist, floorT);
    int guard = 0;
    while (t0 < limit && guard++ < 512) {
        float t1;
        Vector3 nextNormal;
        int ntx = tx, ntz = tz;
        if (tMaxX < tMaxZ) { t1 = tMaxX; tMaxX += tDeltaX; ntx += stepX; nextNormal = {(float)-stepX, 0, 0}; }
        else { t1 = tMaxZ; tMaxZ += tDeltaZ; ntz += stepZ; nextNormal = {0, 0, (float)-stepZ}; }
        t1 = std::min(t1, limit);
        const Tile& tl = at(tx, tz);
        if (tl.height > 0) {
            float a = t0, b = t1;
            Vector3 n = entryNormal;
            float hh = tl.height;
            if (fabsf(d.y) < 1e-7f) { if (o.y > hh) a = b + 1; }
            else {
                float th = (hh - o.y) / d.y;
                if (d.y > 0) b = std::min(b, th);
                else { if (th > a) { a = th; n = {0, 1, 0}; } }
            }
            if (a < b) {
                if (a == 0 && t0 == 0) n = d * -1.0f;
                if (!out.empty() && fabsf(out.back().tOut - a) < 1e-4f && out.back().mat == tl.mat) out.back().tOut = b;
                else out.push_back({a, b, tl.mat, n});
            }
        }
        if (!inBounds(tx, tz) && t1 > 4.0f) break;
        tx = ntx; tz = ntz; t0 = t1; entryNormal = nextNormal;
        if (t1 >= limit) break;
    }
    if (floorT <= maxDist) out.push_back({floorT, 1e30f, MAT_FLOOR, {0, 1, 0}});
}

RayHit Map::raycast(Vector3 o, Vector3 d, float maxDist) const {
    static thread_local std::vector<SolidSpan> spans;
    raySpans(o, d, maxDist, spans);
    RayHit r;
    if (!spans.empty()) {
        r.hit = true; r.t = spans[0].tIn; r.pos = o + d * r.t; r.normal = spans[0].nIn; r.mat = spans[0].mat;
        worldToTile(r.pos - r.normal * 0.01f, r.tx, r.tz);
    }
    return r;
}

bool Map::lineOfSight(Vector3 a, Vector3 b) const {
    Vector3 d = b - a;
    float L = len3(d);
    if (L < 1e-4f) return true;
    RayHit h = raycast(a, d * (1.0f / L), L);
    return !h.hit || h.t >= L - 0.05f;
}

// ---------------------------------------------------------------------------
bool Map::findPath(Vector3 from, Vector3 to, std::vector<Vector3>& out) const {
    out.clear();
    int sx, sz, gx, gz;
    worldToTile(from, sx, sz);
    worldToTile(to, gx, gz);
    auto nearestWalk = [&](int& x, int& z) {
        if (tileWalkable(x, z)) return true;
        for (int r = 1; r < 4; r++)
            for (int dz = -r; dz <= r; dz++)
                for (int dx = -r; dx <= r; dx++)
                    if (tileWalkable(x + dx, z + dz)) { x += dx; z += dz; return true; }
        return false;
    };
    if (!nearestWalk(sx, sz) || !nearestWalk(gx, gz)) return false;
    int N = w * h;
    std::vector<float> g(N, 1e30f);
    std::vector<int> parent(N, -1);
    std::vector<uint8_t> closed(N, 0);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto H = [&](int x, int z) { float dx = fabsf((float)(x - gx)), dz = fabsf((float)(z - gz)); return (dx + dz) + (1.4142f - 2) * std::min(dx, dz); };
    int s = sz * w + sx, goal = gz * w + gx;
    g[s] = 0; open.push({H(sx, sz), s});
    while (!open.empty()) {
        int cur = open.top().second; open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        int cx = cur % w, cz = cur / w;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dz) continue;
                int nx = cx + dx, nz = cz + dz;
                if (!tileWalkable(nx, nz)) continue;
                if (dx && dz && (!tileWalkable(cx + dx, cz) || !tileWalkable(cx, cz + dz))) continue;
                if (fabsf(at(nx, nz).height - at(cx, cz).height) > STEP_HEIGHT + 0.01f) continue;
                int ni = nz * w + nx;
                // penalize hugging walls a bit so bots keep off corners
                float wallPen = 0;
                for (int k = 0; k < 4; k++) {
                    static const int ox[4] = {1, -1, 0, 0}, oz[4] = {0, 0, 1, -1};
                    if (!tileWalkable(nx + ox[k], nz + oz[k])) wallPen += 0.15f;
                }
                float ng = g[cur] + ((dx && dz) ? 1.4142f : 1.0f) + wallPen;
                if (ng < g[ni]) { g[ni] = ng; parent[ni] = cur; open.push({ng + H(nx, nz), ni}); }
            }
    }
    if (parent[goal] < 0 && goal != s) return false;
    std::vector<Vector3> raw;
    for (int c = goal; c >= 0; c = parent[c]) {
        Vector3 p = tileCenter(c % w, c / w);
        p.y = at(c % w, c / w).height;
        raw.push_back(p);
        if (c == s) break;
    }
    std::reverse(raw.begin(), raw.end());
    if (!raw.empty()) { Vector3 t = to; t.y = raw.back().y; raw.back() = t; }
    // string pulling: skip waypoints when the straight segment is clear
    auto clear = [&](Vector3 a, Vector3 b) {
        Vector3 d = b - a; d.y = 0;
        float L = len2xz(d);
        if (L < 0.01f) return true;
        Vector3 dir = d * (1.0f / L);
        Vector3 side = {-dir.z, 0, dir.x};
        float baseH = -1;
        for (float t = 0; t <= L; t += 0.3f) {
            for (float o : {-0.55f, 0.0f, 0.55f}) {
                Vector3 p = a + dir * t + side * o;
                int x, z; worldToTile(p, x, z);
                if (!tileWalkable(x, z)) return false;
                float hh = at(x, z).height;
                if (baseH < 0) baseH = hh; else if (fabsf(hh - baseH) > 0.01f) return false;
            }
        }
        return true;
    };
    size_t i = 0;
    out.push_back(raw[0]);
    while (i < raw.size() - 1) {
        size_t j = raw.size() - 1;
        while (j > i + 1 && !clear(raw[i], raw[j])) j--;
        out.push_back(raw[j]);
        i = j;
    }
    return true;
}

Vector3 Map::randomWalkableNear(Vector3 p, float radius, Rng& rng) const {
    for (int k = 0; k < 30; k++) {
        Vector3 q = p + V3(rng.range(-radius, radius), 0, rng.range(-radius, radius));
        int x, z; worldToTile(q, x, z);
        if (tileWalkable(x, z) && at(x, z).height == 0) return tileCenter(x, z);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Mesh building : vertex color alpha encodes the procedural pattern id
//   250 concrete wall panels, 200 wood planks, 150 floor tiles, 100 metal, 60 site floor
// ---------------------------------------------------------------------------
struct MeshBuilder {
    std::vector<float> v, n;
    std::vector<unsigned char> c;
    void quad(Vector3 a, Vector3 b, Vector3 cc, Vector3 d, Vector3 nn, Color col) {
        Vector3 p[6] = {a, b, cc, a, cc, d};
        for (auto& q : p) {
            v.insert(v.end(), {q.x, q.y, q.z});
            n.insert(n.end(), {nn.x, nn.y, nn.z});
            c.insert(c.end(), {col.r, col.g, col.b, col.a});
        }
    }
    void box(Vector3 mn, Vector3 mx, Color top, Color side) {
        // top (+y)
        quad({mn.x, mx.y, mn.z}, {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {0, 1, 0}, top);
        // -z
        quad({mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mn.y, mn.z}, {0, 0, -1}, side);
        // +z
        quad({mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mn.y, mx.z}, {0, 0, 1}, side);
        // -x
        quad({mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, {mn.x, mn.y, mn.z}, {-1, 0, 0}, side);
        // +x
        quad({mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mx.x, mn.y, mx.z}, {1, 0, 0}, side);
    }
};

Mesh Map::buildMesh() const {
    MeshBuilder mb;
    // floor tiles (merged per row by type)
    for (int z = 0; z < h; z++) {
        int x = 0;
        while (x < w) {
            const Tile& t = at(x, z);
            uint8_t key = (t.zone & (ZONE_SITE_A | ZONE_SITE_B)) ? (t.zone & (ZONE_SITE_A | ZONE_SITE_B)) : 0;
            int x2 = x;
            while (x2 + 1 < w) {
                const Tile& t2 = at(x2 + 1, z);
                uint8_t k2 = (t2.zone & (ZONE_SITE_A | ZONE_SITE_B)) ? (t2.zone & (ZONE_SITE_A | ZONE_SITE_B)) : 0;
                if (k2 != key) break;
                x2++;
            }
            Color col = key == ZONE_SITE_A ? Color{150, 120, 108, 60} : key == ZONE_SITE_B ? Color{108, 128, 150, 60} : Color{120, 122, 128, 150};
            float x0 = tileMinX(x), x1 = tileMinX(x2) + TILE, z0 = tileMinZ(z), z1 = z0 + TILE;
            mb.quad({x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}, {0, 1, 0}, col);
            x = x2 + 1;
        }
    }
    // solids: greedy merge rectangles of identical chars
    std::vector<uint8_t> used(w * h, 0);
    for (int z = 0; z < h; z++)
        for (int x = 0; x < w; x++) {
            const Tile& t = at(x, z);
            if (t.height <= 0 || used[z * w + x]) continue;
            int x2 = x;
            while (x2 + 1 < w && !used[z * w + x2 + 1] && at(x2 + 1, z).ch == t.ch) x2++;
            int z2 = z;
            bool ok = true;
            while (ok && z2 + 1 < h) {
                for (int xx = x; xx <= x2; xx++)
                    if (used[(z2 + 1) * w + xx] || at(xx, z2 + 1).ch != t.ch) { ok = false; break; }
                if (ok) z2++;
            }
            for (int zz = z; zz <= z2; zz++)
                for (int xx = x; xx <= x2; xx++) used[zz * w + xx] = 1;
            Vector3 mn = {tileMinX(x), 0, tileMinZ(z)}, mx = {tileMinX(x2) + TILE, t.height, tileMinZ(z2) + TILE};
            // regional tint: warm on the A side (x<0), cool on the B side (x>0)
            float cx = (mn.x + mx.x) * 0.5f / (w * TILE * 0.5f);
            Color warm = {196, 178, 150, 250}, cool = {150, 170, 182, 250};
            Color wall = mixColor(warm, cool, clampf(cx * 0.5f + 0.5f, 0, 1));
            wall.a = 250;
            Color top = wall, side = wall;
            switch (t.ch) {
            case 'x': side = top = Color{150, 112, 72, 200}; break;
            case 'c': side = Color{176, 128, 70, 200}; top = Color{190, 140, 80, 200}; break;
            case 'k': side = Color{160, 118, 66, 200}; top = Color{180, 132, 76, 200}; break;
            case 'C': side = Color{142, 104, 60, 200}; top = Color{165, 122, 70, 200}; break;
            case 'w': side = Color{132, 136, 140, 250}; top = Color{160, 162, 165, 250}; break;
            case 'm': side = top = Color{110, 120, 130, 100}; break;
            case '1': case '2': case '3': case '4': side = Color{140, 140, 146, 250}; top = Color{132, 134, 140, 150}; break;
            default: top = Color{(unsigned char)(wall.r * 0.8f), (unsigned char)(wall.g * 0.8f), (unsigned char)(wall.b * 0.8f), 250}; break;
            }
            mb.box(mn, mx, top, side);
        }
    Mesh m = {0};
    m.vertexCount = (int)mb.v.size() / 3;
    m.triangleCount = m.vertexCount / 3;
    m.vertices = (float*)MemAlloc(mb.v.size() * sizeof(float));
    m.normals = (float*)MemAlloc(mb.n.size() * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(mb.c.size());
    memcpy(m.vertices, mb.v.data(), mb.v.size() * sizeof(float));
    memcpy(m.normals, mb.n.data(), mb.n.size() * sizeof(float));
    memcpy(m.colors, mb.c.data(), mb.c.size());
    UploadMesh(&m, false);
    return m;
}

} // namespace ax
