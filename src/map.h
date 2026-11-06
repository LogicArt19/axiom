// map.h : RTS battlefields (several layouts), elevation, expansions, navigation grid and flow fields
#pragma once
#include "common.h"
#include <unordered_map>

namespace ax {

enum CellType : uint8_t { CELL_GROUND, CELL_ROCK, CELL_PATH, CELL_WATER, CELL_BASE0, CELL_BASE1, CELL_GRASS, CELL_HIGH, CELL_RAMP };

struct FlowField {
    std::vector<float> dist;
    int goal = -1;
    int version = -1;
    uint32_t lastUse = 0;
};

// a base location: where a Core / Centre goes, with its crystal fields and flux vents
struct Expansion {
    Vector2 hub{};
    Vector2 dir{};                       // from the hub towards its resources
    std::vector<Vector2> crystals;
    std::vector<Vector2> geysers;
    bool rich = false;
    int mainOf = -1;                     // team whose starting base this is, -1 otherwise
};

struct MapInfo { const char* name; const char* desc; };
constexpr int MAP_COUNT = 4;
const MapInfo& mapInfo(int id);

class Map {
public:
    static constexpr float CELL = 2.0f;
    static constexpr int N = 100;           // 100 x 100 cells = 200 x 200 m
    static constexpr float SIZE = N * CELL;
    static constexpr float HIGH = 2.4f;     // height of the high ground plateaus

    int id = 0;
    std::vector<uint8_t> type;              // CellType
    std::vector<uint8_t> blocked;           // 1 rock/water, 2 building, 3 resource
    std::vector<uint8_t> level;             // 0 low ground, 1 high ground (vision)
    std::vector<float> height;              // rock heights (render + shadows)
    std::vector<float> ground;              // walkable surface height per cell
    std::vector<float> vheight;             // (N+1)^2 vertex heights of the ground surface
    int version = 0;                        // bumps when blocking changes

    Vector2 base[2];                        // starting Core positions
    Vector2 ramp[2];                        // bottom of each main ramp (army rally)
    Vector2 rampTop[2];
    std::vector<Expansion> expansions;
    std::vector<Vector2> camps;             // neutral camp centres
    std::vector<Vector2> beacons;           // watch beacons (vision when a unit stands next to them)
    Vector2 titanPos{100, 100};
    float baseRadius = 20;

    void generate(int mapId);
    bool inside(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < N && cz < N; }
    int cellIndex(Vector2 p) const { int cx = (int)floorf(p.x / CELL), cz = (int)floorf(p.y / CELL); return inside(cx, cz) ? cz * N + cx : -1; }
    Vector2 cellCenter(int idx) const { return {(idx % N + 0.5f) * CELL, (idx / N + 0.5f) * CELL}; }
    bool walkable(Vector2 p) const { int i = cellIndex(p); return i >= 0 && !blocked[i]; }
    bool walkableCell(int cx, int cz) const { return inside(cx, cz) && !blocked[cz * N + cx]; }
    bool lineClear(Vector2 a, Vector2 b) const;
    Vector2 nearestWalkable(Vector2 p) const;
    int teamOfBase(Vector2 p) const;        // -1 if not inside a starting base
    int levelAt(Vector2 p) const { int i = cellIndex(p); return i >= 0 ? level[i] : 0; }
    float groundHeight(Vector2 p) const;    // smooth surface height (render)
    bool canPlace(Vector2 center, int footprint) const;   // footprint free of obstacles
    bool nearResources(Vector2 center, int footprint, float margin) const;
    void setBlocked(Vector2 center, int footprint, uint8_t v);
    Vector2 snapBuilding(Vector2 p, int footprint) const;

    // flow fields
    Vector2 flowDirection(Vector2 from, Vector2 goal, float goalRadius);
    float flowDistance(Vector2 from, Vector2 goal);
    uint32_t frame = 0;
private:
    std::unordered_map<long long, FlowField> flows;
    FlowField& getFlow(int goalCell, float radius);
};

} // namespace ax
