// data.h : every unit, building, hero, ability and item definition
#pragma once
#include "common.h"

namespace ax {

enum EntKind : uint8_t { K_UNIT, K_HERO, K_BUILDING, K_MINION, K_NEUTRAL, K_TOWER, K_RESOURCE };
enum ArmorClass : uint8_t { AC_LIGHT, AC_HEAVY, AC_STRUCTURE, AC_HERO };

enum UnitType : uint8_t {
    U_NONE = 0,
    U_DRONE, U_WARDEN, U_STRIKER, U_LANCER, U_MENDER, U_SIEGE,
    U_MINION_MELEE, U_MINION_RANGED, U_MINION_SIEGE,
    U_GOLEM, U_TITAN,
    B_CORE, B_BARRACKS, B_FACTORY, B_FORGE, B_RELAY, B_WATCHTOWER, B_EXTRACTOR, B_HUB,
    T_TOWER,
    R_CRYSTAL, R_GEYSER,
    H_VANGUARD, H_PYRA, H_HAWKE, H_SERAPH, H_WARLORD, H_SHADE,
    UT_COUNT
};
constexpr int HERO_FIRST = H_VANGUARD, HERO_COUNT = 6;
inline bool isHeroType(int t) { return t >= H_VANGUARD && t <= H_SHADE; }
inline bool isBuildingType(int t) { return t >= B_CORE && t <= T_TOWER; }

struct UnitDef {
    const char* name;
    const char* desc;
    EntKind kind;
    ArmorClass ac;
    float hp, armor, dmg, range, cooldown, speed, sight, radius;
    int cost, supply;
    int flux = 0;              // second resource cost
    float buildTime;
    UnitType producedAt;      // building that trains it (or worker-built for buildings)
    UnitType reqTech;        // tech requirement
    float vsLight, vsHeavy, vsStructure;   // damage multipliers
    float splash;
    bool ranged;
    float projSpeed;
    int bounty, xp;
    float hpPerLevel, dmgPerLevel, manaMax, manaRegen;
    Color color;
    char hotkey;
    int footprint;            // buildings: size in nav cells
};

enum TargetMode : uint8_t { TM_NONE, TM_POINT, TM_ENEMY, TM_ALLY, TM_DIRECTION };
struct AbilityDef {
    const char* name;
    const char* desc;
    TargetMode tm;
    float range, radius, cooldown, mana;
    int unlockLevel;
};

enum ItemId : uint8_t { I_NONE, I_BLADE, I_BOW, I_BOOTS, I_PLATE, I_HEART, I_FOCUS, I_VAMP, I_SCEPTER, I_AEGIS, I_POTION, I_COUNT };
struct ItemDef {
    const char* name;
    const char* desc;
    int cost;
    float dmg, attackSpeed, moveSpeed, armor, hp, cdr, mana, lifesteal, abilityPower, armyAura;
};

enum UpgradeId : uint8_t { UP_WEAPONS, UP_ARMOR, UP_COUNT };
constexpr int UPGRADE_MAX = 3;
int upgradeCost(int up, int level);
int upgradeFlux(int up, int level);
float upgradeTime(int level);

const UnitDef& unitDef(int t);
const AbilityDef& abilityDef(int heroType, int slot);
const ItemDef& itemDef(int id);
void initData();

// production lists
const std::vector<UnitType>& trainList(int buildingType);
const std::vector<UnitType>& buildList();
const char* heroRole(int heroType);

constexpr int XP_PER_LEVEL[12] = {0, 120, 280, 480, 720, 1000, 1320, 1680, 2080, 2520, 3000, 3520};
constexpr int MAX_LEVEL = 12;
constexpr int MAX_SUPPLY = 120;
constexpr int CRYSTAL_AMOUNT = 900, CRYSTAL_RICH_AMOUNT = 1100, GEYSER_AMOUNT = 1800;
constexpr int CRYSTAL_TRIP = 5, CRYSTAL_RICH_TRIP = 7, FLUX_TRIP = 4;

} // namespace ax
