#include "weapons.h"

namespace ax {

static WeaponDef g_defs[W_COUNT];

// Deterministic, learnable spray patterns.
//   upShots : number of shots climbing mostly vertically
//   climb   : pitch added per early shot (deg)
//   sway    : horizontal amplitude after the climb
//   period  : shots per horizontal half swing
//   firstDir: initial horizontal direction (-1 left, +1 right)
static std::vector<RecoilStep> makePattern(int shots, int upShots, float climb, float sway, float period, float firstDir, float lateClimb, float jitterSeed) {
    std::vector<RecoilStep> p;
    p.reserve(shots);
    for (int i = 0; i < shots; i++) {
        RecoilStep s{0, 0};
        if (i == 0) { s.pitch = climb * 0.35f; }
        else if (i < upShots) {
            float k = 1.0f - 0.35f * (float)i / upShots;
            s.pitch = climb * k;
            s.yaw = firstDir * climb * 0.12f * sinf(i * 0.9f + jitterSeed);
        } else {
            int j = i - upShots;
            float phase = (float)j / period;
            s.pitch = lateClimb * (0.6f + 0.4f * cosf(j * 0.7f + jitterSeed));
            s.yaw = firstDir * sway * cosf(phase * 3.14159f) * (0.8f + 0.2f * sinf(j * 1.3f + jitterSeed));
            if (((int)floorf(phase)) % 2 == 1) s.pitch *= 0.4f;
        }
        p.push_back(s);
    }
    return p;
}

static void def(WeaponId id, const char* name, const char* desc, WeaponClass cls, WeaponSlot slot, int price, int reward,
                float dmg, float armorPen, float rangeMod, float pen, int pellets, float rpm, bool autoFire, int mag, int reserve,
                float reload, float draw, float sStand, float sCrouch, float sMove, float sAir, float sShot, float sMax, float sRec,
                float scoped, float rScale, float rRec, float rRand, float speed, int scopes, float sf1, float sf2, float tag,
                SoundKind snd, Color col) {
    WeaponDef& d = g_defs[id];
    d.id = id; d.name = name; d.desc = desc; d.cls = cls; d.slot = slot; d.price = price; d.killReward = reward;
    d.damage = dmg; d.armorPen = armorPen; d.rangeMod = rangeMod; d.penetration = pen; d.pellets = pellets;
    d.rpm = rpm; d.automatic = autoFire; d.magSize = mag; d.reserve = reserve; d.reloadTime = reload; d.drawTime = draw;
    d.spreadStand = sStand; d.spreadCrouch = sCrouch; d.spreadMove = sMove; d.spreadAir = sAir; d.spreadPerShot = sShot;
    d.spreadMax = sMax; d.spreadRecovery = sRec; d.scopedSpread = scoped; d.recoilScale = rScale; d.recoilRecovery = rRec;
    d.recoilRandom = rRand; d.maxSpeed = speed; d.scopeLevels = scopes; d.scopeFov[0] = sf1; d.scopeFov[1] = sf2;
    d.tagging = tag; d.sound = snd; d.color = col;
}

void initWeapons() {
    //    id          name        desc                                                  class      slot            price rew   dmg  apen  range pen  pel rpm  auto  mag res  rel  draw  sSt   sCr   sMv  sAir sShot sMax sRec  scoped rS   rRec rRnd speed sc  f1   f2  tag  sound        color
    def(W_KNIFE,   "LAME",     "Arme blanche. Clic gauche rapide, clic droit lourd.", WC_KNIFE, SLOT_KNIFE,     0,   1500, 40,  0.85f, 1.0f, 0,   1,  120, true,  0,  0,   0,   0.5f, 0,    0,    0,   0,   0,    0,   0,    0,     0,   0,   0,   5.40f, 0, 0,   0,  0.0f, SK_NONE,   {200, 205, 215, 255});
    def(W_VIPER,   "VIPER",    "Pistolet de base. Précis à l'arrêt, faible contre les armures.", WC_PISTOL, SLOT_SECONDARY, 200, 300, 30,  0.47f, 0.82f, 1.0f, 1,  400, false, 12, 36,  2.2f, 0.9f, 0.18f, 0.12f, 2.6f, 5.0f, 0.55f, 4.0f, 7.0f, 0,  1.0f, 9.0f, 0.15f, 5.0f, 0, 0, 0, 0.45f, SK_PISTOL, {70, 75, 85, 255});
    def(W_MAMBA,   "MAMBA",    "Pistolet perforant. Excellent rapport qualité/prix.",  WC_PISTOL, SLOT_SECONDARY, 300, 300, 38,  0.64f, 0.80f, 1.4f, 1,  400, false, 13, 26,  2.2f, 0.9f, 0.22f, 0.15f, 2.8f, 5.0f, 0.70f, 4.5f, 6.5f, 0,  1.2f, 8.0f, 0.15f, 5.0f, 0, 0, 0, 0.5f, SK_PISTOL, {60, 66, 58, 255});
    def(W_HORNET,  "HORNET",   "Pistolet lourd. Un tir à la tête = un kill.",          WC_PISTOL, SLOT_SECONDARY, 700, 300, 63,  0.93f, 0.81f, 2.0f, 1,  267, false, 7,  35,  2.2f, 1.0f, 0.30f, 0.22f, 4.5f, 6.0f, 1.80f, 6.0f, 4.0f, 0,  2.6f, 6.0f, 0.25f, 4.9f, 0, 0, 0, 0.6f, SK_HEAVYPISTOL, {140, 140, 150, 255});
    def(W_WASP,    "WASP",     "Pistolet-mitrailleur. Mobile, prime d'élimination élevée.",  WC_SMG,   SLOT_PRIMARY,   1200, 600, 26, 0.60f, 0.75f, 1.2f, 1,  857, true,  30, 120, 2.6f, 1.0f, 0.40f, 0.30f, 1.6f, 4.0f, 0.36f, 3.0f, 9.0f, 0,  0.75f, 11.f, 0.2f, 5.2f, 0, 0, 0, 0.5f, SK_SMG, {50, 52, 60, 255});
    def(W_BREACH,  "BREACH",   "Fusil a pompe. Dévastateur à courte portée.",          WC_SHOTGUN, SLOT_PRIMARY, 1100, 900, 22, 0.75f, 0.45f, 0.5f, 9,  70,  false, 7,  32,  0.55f, 1.0f, 3.2f, 3.0f, 3.6f, 6.0f, 0.8f, 5.0f, 5.0f, 0,  3.5f, 5.0f, 0.3f, 4.8f, 0, 0, 0, 0.7f, SK_SHOTGUN, {90, 60, 40, 255});
    def(W_RAPTOR,  "RAPTOR",   "Fusil d'assaut économique. Gros chargeur.",             WC_RIFLE, SLOT_PRIMARY,   2000, 300, 30, 0.77f, 0.96f, 2.0f, 1,  666, true,  35, 90,  2.9f, 1.0f, 0.24f, 0.16f, 3.4f, 6.5f, 0.42f, 3.2f, 7.0f, 0,  0.95f, 8.f, 0.18f, 4.7f, 0, 0, 0, 0.6f, SK_RIFLE2, {110, 92, 60, 255});
    def(W_VANGUARD,"VANGUARD", "Fusil d'assaut lourd. Tir à la tête mortel, recul fort.", WC_RIFLE, SLOT_PRIMARY, 2700, 300, 36, 0.775f, 0.98f, 2.5f, 1, 600, true,  30, 90,  2.5f, 1.0f, 0.20f, 0.14f, 4.2f, 7.0f, 0.50f, 3.5f, 6.5f, 0,  1.20f, 7.f, 0.2f, 4.6f, 0, 0, 0, 0.65f, SK_RIFLE, {130, 90, 55, 255});
    def(W_SPECTER, "SPECTER",  "Fusil de précision automatique. Recul maîtrisable.",  WC_RIFLE, SLOT_PRIMARY,   2900, 300, 33, 0.70f, 0.97f, 2.2f, 1,  666, true,  25, 75,  3.1f, 1.0f, 0.16f, 0.11f, 4.0f, 7.0f, 0.40f, 3.0f, 7.5f, 0,  0.92f, 8.f, 0.15f, 4.75f, 0, 0, 0, 0.6f, SK_RIFLE2, {58, 64, 72, 255});
    def(W_LANCER,  "LANCER",   "Fusil léger à lunette. Précis à l'apogée du saut.", WC_SNIPER, SLOT_PRIMARY, 1700, 300, 75, 0.85f, 0.98f, 2.5f, 1, 48, false, 10, 90,  2.0f, 1.0f, 2.5f, 2.0f, 4.0f, 3.5f, 0.0f, 3.0f, 10.f, 0.02f, 2.0f, 8.f, 0.1f, 5.2f, 1, 40, 0, 0.6f, SK_MARKSMAN, {70, 90, 70, 255});
    def(W_ORACLE,  "ORACLE",   "Sniper lourd. Un tir dans le torse élimine.",          WC_SNIPER, SLOT_PRIMARY,   4700, 100, 115, 0.97f, 0.99f, 3.5f, 1, 41, false, 5,  30,  3.6f, 1.25f, 4.0f, 3.4f, 8.0f, 10.f, 0.0f, 3.0f, 6.f, 0.005f, 3.0f, 6.f, 0.1f, 4.0f, 2, 40, 15, 0.9f, SK_SNIPER, {40, 70, 50, 255});
    def(W_TITAN,   "TITAN",    "Mitrailleuse lourde. 150 balles de suppression.",      WC_LMG,   SLOT_PRIMARY,   1700, 300, 35, 0.71f, 0.97f, 2.4f, 1,  800, true,  150, 300, 5.5f, 1.0f, 0.80f, 0.50f, 4.5f, 7.0f, 0.30f, 3.5f, 5.0f, 0,  0.85f, 9.f, 0.2f, 3.9f, 0, 0, 0, 0.6f, SK_LMG, {60, 60, 60, 255});
    def(W_FLASH,   "FLASH",    "Aveugle les joueurs qui la regardent.",                WC_GRENADE, SLOT_GRENADE, 200, 300, 0, 0, 1, 0, 1, 60, false, 1, 0, 0, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.1f, 0, 0, 0, 0, SK_NONE, {220, 220, 230, 255});
    def(W_SMOKE,   "FUMIGÈNE", "Bloque la vision pendant 18 secondes.",                WC_GRENADE, SLOT_GRENADE, 300, 300, 0, 0, 1, 0, 1, 60, false, 1, 0, 0, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.1f, 0, 0, 0, 0, SK_NONE, {120, 140, 120, 255});
    def(W_FRAG,    "FRAG",     "Grenade explosive. Jusqu'a 98 degats.",                WC_GRENADE, SLOT_GRENADE, 300, 300, 98, 0.5f, 1, 0, 1, 60, false, 1, 0, 0, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.1f, 0, 0, 0, 0, SK_NONE, {80, 100, 60, 255});
    def(W_MOLLY,   "INCENDIAIRE", "Zone de feu de 7 s. Éteinte par la fumée.",          WC_GRENADE, SLOT_GRENADE, 500, 300, 40, 1, 1, 0, 1, 60, false, 1, 0, 0, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.1f, 0, 0, 0, 0, SK_NONE, {200, 90, 40, 255});
    def(W_BOMB,    "NOYAU",    "Charge explosive. À poser sur un site.",               WC_BOMB, SLOT_BOMB,       0,   0,   0, 0, 1, 0, 1, 60, false, 1, 0, 0, 0.6f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5.1f, 0, 0, 0, 0, SK_NONE, {220, 60, 50, 255});

    g_defs[W_VIPER].pattern    = makePattern(12, 4, 1.4f, 0.6f, 3, 1, 0.8f, 0.3f);
    g_defs[W_MAMBA].pattern    = makePattern(13, 4, 1.7f, 0.7f, 3, -1, 0.9f, 1.1f);
    g_defs[W_HORNET].pattern   = makePattern(7, 3, 3.6f, 1.2f, 2, 1, 2.0f, 2.2f);
    g_defs[W_WASP].pattern     = makePattern(30, 7, 0.75f, 0.85f, 5, -1, 0.25f, 0.7f);
    g_defs[W_BREACH].pattern   = makePattern(7, 7, 3.2f, 0.5f, 2, 1, 1.0f, 0.0f);
    g_defs[W_RAPTOR].pattern   = makePattern(35, 9, 0.95f, 1.05f, 6, 1, 0.3f, 1.7f);
    g_defs[W_VANGUARD].pattern = makePattern(30, 9, 1.15f, 1.35f, 6, 1, 0.35f, 0.4f);
    g_defs[W_SPECTER].pattern  = makePattern(25, 8, 0.85f, 0.85f, 6, -1, 0.25f, 2.6f);
    g_defs[W_LANCER].pattern   = makePattern(10, 10, 2.8f, 0.3f, 2, 1, 1, 0.0f);
    g_defs[W_ORACLE].pattern   = makePattern(5, 5, 4.5f, 0.3f, 2, -1, 1, 0.0f);
    g_defs[W_TITAN].pattern    = makePattern(150, 12, 0.7f, 1.1f, 9, -1, 0.12f, 0.9f);
    // the LMG settles after a while: very small vertical climb late in the burst
    for (size_t i = 25; i < g_defs[W_TITAN].pattern.size(); i++) g_defs[W_TITAN].pattern[i].pitch *= 0.25f;
}

const WeaponDef& weaponDef(WeaponId id) { return g_defs[id < W_COUNT ? id : W_NONE]; }

} // namespace ax
