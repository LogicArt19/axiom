// weapons.h : weapon & equipment definitions, recoil patterns
#pragma once
#include "common.h"

namespace ax {

enum WeaponId : uint8_t {
    W_NONE = 0,
    W_KNIFE,
    W_VIPER,     // default pistol
    W_MAMBA,     // armor-piercing pistol
    W_HORNET,    // heavy pistol
    W_WASP,      // SMG
    W_BREACH,    // pump shotgun
    W_RAPTOR,    // budget rifle
    W_VANGUARD,  // assault rifle (attackers & defenders) - 1-tap HS
    W_SPECTER,   // precise rifle
    W_LANCER,    // light marksman bolt rifle
    W_ORACLE,    // heavy sniper
    W_TITAN,     // LMG
    W_FLASH, W_SMOKE, W_FRAG, W_MOLLY,
    W_BOMB,
    W_COUNT
};

enum WeaponSlot : uint8_t { SLOT_PRIMARY = 0, SLOT_SECONDARY = 1, SLOT_KNIFE = 2, SLOT_GRENADE = 3, SLOT_BOMB = 4 };
enum WeaponClass : uint8_t { WC_KNIFE, WC_PISTOL, WC_SMG, WC_SHOTGUN, WC_RIFLE, WC_SNIPER, WC_LMG, WC_GRENADE, WC_BOMB };
enum SoundKind : uint8_t { SK_PISTOL, SK_HEAVYPISTOL, SK_SMG, SK_SHOTGUN, SK_RIFLE, SK_RIFLE2, SK_SNIPER, SK_MARKSMAN, SK_LMG, SK_NONE };

struct RecoilStep { float yaw, pitch; };

struct WeaponDef {
    WeaponId id;
    const char* name;
    const char* desc;
    WeaponClass cls;
    WeaponSlot slot;
    int price;
    int killReward;
    // damage model
    float damage;
    float armorPen;          // fraction of damage kept vs armor (0..1)
    float rangeMod;          // damage multiplier per 25m
    float penetration;       // wall penetration power (meters of wood-equivalent)
    int   pellets;
    // firing
    float rpm;
    bool  automatic;
    int   magSize, reserve;
    float reloadTime, drawTime;
    // accuracy (degrees)
    float spreadStand, spreadCrouch, spreadMove, spreadAir, spreadPerShot, spreadMax, spreadRecovery; // recovery deg/s
    float scopedSpread;      // spread when scoped (snipers)
    // recoil
    float recoilScale;       // pattern scale
    float recoilRecovery;    // deg/s returning to center
    float recoilRandom;
    // movement
    float maxSpeed;          // m/s
    int   scopeLevels;
    float scopeFov[2];
    float tagging;           // slowdown factor when hit by this
    SoundKind sound;
    Color color;
    std::vector<RecoilStep> pattern;
};

const WeaponDef& weaponDef(WeaponId id);
void initWeapons();

constexpr int EQ_KEVLAR_PRICE = 650;
constexpr int EQ_HELMET_PRICE = 1000;   // kevlar + helmet
constexpr int EQ_KIT_PRICE = 400;
constexpr int MAX_MONEY = 16000;

inline bool isGrenade(WeaponId w) { return w >= W_FLASH && w <= W_MOLLY; }
inline bool isGun(WeaponId w) { return w >= W_VIPER && w <= W_TITAN; }

} // namespace ax
