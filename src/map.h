// map.h : tile based world geometry, collision, ballistics raycast and navigation
#pragma once
#include "common.h"

namespace ax {

enum Material : uint8_t { MAT_NONE = 0, MAT_CONCRETE, MAT_WOOD, MAT_METAL, MAT_FLOOR };

enum ZoneFlags : uint8_t {
    ZONE_SITE_A = 1, ZONE_SITE_B = 2, ZONE_SPAWN_ATK = 4, ZONE_SPAWN_DEF = 8, ZONE_BUY_ATK = 16, ZONE_BUY_DEF = 32
};

struct Tile {
    float height = 0;      // solid from 0 to height (0 = plain floor)
    Material mat = MAT_FLOOR;
    uint8_t zone = 0;
    bool walkableNav = true;  // used by bots
    char ch = '.';
};

struct RayHit {
    bool hit = false;
    float t = 0;            // distance along ray
    Vector3 pos{}, normal{};
    Material mat = MAT_NONE;
    int tx = -1, tz = -1;
};

// A solid segment crossed by a ray (for wall penetration)
struct SolidSpan { float tIn, tOut; Material mat; Vector3 nIn; };

struct HoldSpot { Vector3 pos; float yaw; uint8_t zone; };  // bot defensive spots

class Map {
public:
    static constexpr float TILE = 2.0f;
    int w = 0, h = 0;
    std::vector<Tile> tiles;
    std::string name;
    std::vector<Vector3> spawnAtk, spawnDef;
    std::vector<HoldSpot> holdA, holdB, holdMid;
    Vector3 siteA{}, siteB{};
    std::vector<Vector3> routeA, routeB;   // intermediate waypoints for attack routes

    void loadCitadel();
    void loadAimArena();

    bool inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < w && z < h; }
    const Tile& at(int x, int z) const { static Tile solid{6, MAT_CONCRETE, 0, false, '#'}; return inBounds(x, z) ? tiles[z * w + x] : solid; }
    Tile& atMut(int x, int z) { return tiles[z * w + x]; }
    void worldToTile(Vector3 p, int& tx, int& tz) const { tx = (int)floorf(p.x / TILE + w * 0.5f); tz = (int)floorf(p.z / TILE + h * 0.5f); }
    Vector3 tileCenter(int tx, int tz) const { return {(tx - w * 0.5f + 0.5f) * TILE, 0, (tz - h * 0.5f + 0.5f) * TILE}; }
    float tileMinX(int tx) const { return (tx - w * 0.5f) * TILE; }
    float tileMinZ(int tz) const { return (tz - h * 0.5f) * TILE; }
    float worldSizeX() const { return w * TILE; }
    float worldSizeZ() const { return h * TILE; }

    // Highest floor/solid top under an AABB footprint (ignoring tiles taller than maxTop)
    float groundHeightAt(Vector3 pos, float radius, float maxTop) const;
    // Does an AABB (feet pos, radius, height) overlap solid geometry?
    bool boxBlocked(Vector3 feet, float radius, float height) const;
    uint8_t zoneAt(Vector3 p) const { int x, z; worldToTile(p, x, z); return at(x, z).zone; }

    // Ballistic raycast against geometry. Returns first hit within maxDist.
    RayHit raycast(Vector3 o, Vector3 dir, float maxDist) const;
    // Collect all solid spans along a ray (sorted) - used by bullet penetration
    void raySpans(Vector3 o, Vector3 dir, float maxDist, std::vector<SolidSpan>& out) const;
    bool lineOfSight(Vector3 a, Vector3 b) const;

    // Navigation (A* on tiles, 8-connected)
    bool findPath(Vector3 from, Vector3 to, std::vector<Vector3>& out) const;
    bool tileWalkable(int x, int z) const;
    Vector3 randomWalkableNear(Vector3 p, float radius, Rng& rng) const;

    // Render mesh (built once)
    Mesh buildMesh() const;
};

} // namespace ax
