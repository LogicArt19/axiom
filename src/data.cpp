#include "data.h"

namespace ax {

static UnitDef g_units[UT_COUNT];
static AbilityDef g_abil[HERO_COUNT][4];
static ItemDef g_items[I_COUNT];

static void U(UnitType t, const char* name, const char* desc, EntKind k, ArmorClass ac, float hp, float armor, float dmg, float range,
              float cd, float speed, float sight, float radius, int cost, int supply, float bt, UnitType at, UnitType req,
              float vL, float vH, float vS, float splash, bool ranged, float proj, int bounty, int xp, Color col, char hk, int fp = 0) {
    UnitDef& d = g_units[t];
    d.name = name; d.desc = desc; d.kind = k; d.ac = ac; d.hp = hp; d.armor = armor; d.dmg = dmg; d.range = range; d.cooldown = cd;
    d.speed = speed; d.sight = sight; d.radius = radius; d.cost = cost; d.supply = supply; d.buildTime = bt; d.producedAt = at;
    d.reqTech = req; d.vsLight = vL; d.vsHeavy = vH; d.vsStructure = vS; d.splash = splash; d.ranged = ranged; d.projSpeed = proj;
    d.bounty = bounty; d.xp = xp; d.color = col; d.hotkey = hk; d.footprint = fp;
}

static void H(UnitType t, const char* name, const char* desc, float hp, float armor, float dmg, float range, float cd, float speed,
              float hpLvl, float dmgLvl, float mana, float manaRegen, Color col) {
    U(t, name, desc, K_HERO, AC_HERO, hp, armor, dmg, range, cd, speed, 13, 0.9f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 0.6f, 0,
      range > 3, 22, 300, 200, col, 0);
    g_units[t].hpPerLevel = hpLvl; g_units[t].dmgPerLevel = dmgLvl; g_units[t].manaMax = mana; g_units[t].manaRegen = manaRegen;
}

static void A(UnitType hero, int slot, const char* name, const char* desc, TargetMode tm, float range, float radius, float cd, float mana, int lvl) {
    g_abil[hero - HERO_FIRST][slot] = {name, desc, tm, range, radius, cd, mana, lvl};
}

static void I(ItemId id, const char* name, const char* desc, int cost, float dmg, float as, float ms, float armor, float hp, float cdr,
              float mana, float ls, float ap, float aura) {
    g_items[id] = {name, desc, cost, dmg, as, ms, armor, hp, cdr, mana, ls, ap, aura};
}

void initData() {
    const Color neutral = {150, 140, 120, 255};
    //  type          name            desc                                                      kind        armor        hp    arm  dmg  rng  cd    spd  sight rad   cost sup  bt   at          req          vL    vH    vS    spl  rngd proj  bty  xp   color             key
    U(U_DRONE,   "DRONE",    "Ouvrier : récolte cristaux et flux, construit les bâtiments.",               K_UNIT, AC_LIGHT,     60,   0,   5,  1.0f, 1.2f, 5.6f, 9,  0.45f, 50,  1, 12, B_CORE,     U_NONE,      1, 1, 1, 0, false, 0, 10, 15, {200, 200, 210, 255}, 'Z');
    U(U_WARDEN,  "GARDIEN",  "Fantassin lourd. Encaisse et protège la ligne.",               K_UNIT, AC_HEAVY,    240,  15,  14, 1.4f, 1.1f, 4.6f, 9,  0.6f, 100, 2, 18, B_BARRACKS, U_NONE,      1.3f, 1, 1, 0, false, 0, 20, 35, {180, 180, 190, 255}, 'Z');
    U(U_STRIKER, "TIREUR",   "Tireur à distance. Efficace contre les unités légères.",      K_UNIT, AC_LIGHT,    125,   0,  12, 7.0f, 0.9f, 5.0f, 10, 0.5f, 100, 2, 18, B_BARRACKS, U_NONE,      1.5f, 0.8f, 0.7f, 0, true, 24, 20, 35, {180, 180, 190, 255}, 'X');
    U(U_LANCER,  "LANCIER",  "Perce-blindage : détruit unités lourdes, héros et bâtiments.", K_UNIT, AC_HEAVY,    180,  10,  24, 8.0f, 1.6f, 4.3f, 10, 0.6f, 175, 3, 26, B_FACTORY,  U_NONE,      0.7f, 2.0f, 1.6f, 0, true, 26, 30, 50, {180, 180, 190, 255}, 'Z');
    U(U_MENDER,  "SOIGNEUR", "Soigne les unités et le héros alliés proches.",                K_UNIT, AC_LIGHT,    110,   0,  14, 6.0f, 1.0f, 5.2f, 10, 0.5f, 125, 2, 22, B_FACTORY,  U_NONE,      1, 1, 0, 0, true, 0, 25, 40, {180, 180, 190, 255}, 'X');
    U(U_SIEGE,   "ARTILLEUR", "Artillerie : dégâts de zone à très longue portée.",           K_UNIT, AC_HEAVY,    170,   5,  42, 13.0f, 2.8f, 3.6f, 12, 0.75f, 225, 4, 32, B_FACTORY, B_FORGE,     1.2f, 1, 1.8f, 2.6f, true, 16, 40, 60, {180, 180, 190, 255}, 'C');
    U(U_MINION_MELEE,  "SBIRE",          "Sbire de voie.",                                   K_MINION, AC_LIGHT, 200, 5, 11, 1.3f, 1.2f, 4.4f, 8, 0.5f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 1, 0, false, 0, 18, 30, {170, 170, 180, 255}, 0);
    U(U_MINION_RANGED, "SBIRE TIREUR",   "Sbire de voie à distance.",                        K_MINION, AC_LIGHT, 120, 0, 15, 6.0f, 1.4f, 4.4f, 8, 0.45f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 1, 0, true, 20, 16, 30, {170, 170, 180, 255}, 0);
    U(U_MINION_SIEGE,  "SBIRE DE SIÈGE", "Sbire lourd, efficace contre les tours.",          K_MINION, AC_HEAVY, 520, 15, 32, 7.5f, 2.0f, 4.0f, 9, 0.75f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 2.0f, 0, true, 18, 45, 70, {170, 170, 180, 255}, 0);
    U(U_GOLEM,   "GOLEM",    "Créature neutre : rapporte or et expérience.",                                K_NEUTRAL, AC_HEAVY, 520, 12, 22, 1.6f, 1.4f, 3.8f, 7, 0.8f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 1, 0, false, 0, 55, 90, neutral, 0);
    U(U_TITAN,   "TITAN",    "Boss central. Le vaincre renforce toute votre équipe.",      K_NEUTRAL, AC_HEAVY, 4200, 30, 70, 3.2f, 1.8f, 3.5f, 9, 2.2f, 0, 0, 0, B_CORE, U_NONE, 1, 1, 1, 2.5f, false, 0, 300, 400, {120, 90, 160, 255}, 0);
    U(B_CORE,    "NOYAU",    "Cœur de votre base : dépôt, drones. S'il tombe, la partie est perdue.",        K_BUILDING, AC_STRUCTURE, 4200, 22, 55, 9.5f, 1.2f, 0, 14, 4.8f, 0, 0, 0, U_DRONE, U_NONE, 1, 1, 1, 0, true, 28, 600, 600, {200, 200, 210, 255}, 0, 5);
    U(B_BARRACKS, "CASERNE", "Entraîne Gardiens et Tireurs.",                                 K_BUILDING, AC_STRUCTURE, 1100, 25, 0, 0, 0, 0, 9, 2.8f, 150, 0, 30, U_DRONE, U_NONE, 1, 1, 1, 0, false, 0, 100, 100, {200, 200, 210, 255}, 'Z', 3);
    U(B_FACTORY, "USINE",    "Lanciers, Soigneurs et Artilleurs.",                           K_BUILDING, AC_STRUCTURE, 1200, 25, 0, 0, 0, 0, 9, 2.8f, 200, 0, 40, U_DRONE, B_BARRACKS,  1, 1, 1, 0, false, 0, 120, 120, {200, 200, 210, 255}, 'X', 3);
    U(B_FORGE,   "FORGE",    "Améliorations d'armes et de blindage pour toute l'armée.",    K_BUILDING, AC_STRUCTURE, 900, 25, 0, 0, 0, 0, 9, 2.2f, 150, 0, 30, U_DRONE, B_BARRACKS,  1, 1, 1, 0, false, 0, 90, 90, {200, 200, 210, 255}, 'C', 2);
    U(B_RELAY,   "RELAIS",   "Augmente votre ravitaillement de 10.",                         K_BUILDING, AC_STRUCTURE, 450, 15, 0, 0, 0, 0, 8, 1.4f, 100, 0, 16, U_DRONE, U_NONE,      1, 1, 1, 0, false, 0, 40, 40, {200, 200, 210, 255}, 'V', 2);
    U(B_WATCHTOWER, "SENTINELLE", "Tourelle défensive. Révèle les unités invisibles.",       K_BUILDING, AC_STRUCTURE, 650, 20, 26, 8.5f, 1.0f, 0, 12, 1.2f, 125, 0, 25, U_DRONE, B_BARRACKS, 1, 1, 1, 0, true, 26, 60, 60, {200, 200, 210, 255}, 'B', 1);
    U(B_EXTRACTOR, "EXTRACTEUR", "Se construit sur un geyser de flux. 3 drones le saturent.", K_BUILDING, AC_STRUCTURE, 550, 15, 0, 0, 0, 0, 7, 2.4f, 75, 0, 20, U_DRONE, U_NONE, 1, 1, 1, 0, false, 0, 50, 50, {200, 200, 210, 255}, 'N', 3);
    U(B_HUB,     "CENTRE",   "Nouvelle base : dépôt de ressources, drones, +10 ravitaillement.", K_BUILDING, AC_STRUCTURE, 1800, 18, 0, 0, 0, 0, 12, 4.6f, 400, 0, 60, U_DRONE, U_NONE, 1, 1, 1, 0, false, 0, 300, 300, {200, 200, 210, 255}, 'G', 5);
    U(T_TOWER,   "TOUR",     "Tour de voie. Cible en priorité les héros qui attaquent.",     K_TOWER, AC_STRUCTURE, 2600, 40, 85, 10.0f, 1.25f, 0, 13, 1.5f, 0, 0, 0, U_NONE, U_NONE, 1, 1, 1, 0, true, 30, 250, 250, {200, 200, 210, 255}, 0, 2);
    U(R_CRYSTAL, "CRISTAL",  "Gisement de cristaux.",                                        K_RESOURCE, AC_STRUCTURE, 1, 0, 0, 0, 0, 0, 0, 0.9f, 0, 0, 0, U_NONE, U_NONE, 1, 1, 1, 0, false, 0, 0, 0, {90, 200, 255, 255}, 0, 1);
    U(R_GEYSER,  "GEYSER DE FLUX", "Construisez un Extracteur dessus pour récolter le flux.",   K_RESOURCE, AC_STRUCTURE, 1, 0, 0, 0, 0, 0, 0, 2.4f, 0, 0, 0, U_NONE, U_NONE, 1, 1, 1, 0, false, 0, 0, 0, {110, 255, 150, 255}, 0, 3);
    // flux costs (second resource)
    g_units[U_STRIKER].flux = 25; g_units[U_LANCER].flux = 75; g_units[U_MENDER].flux = 100; g_units[U_SIEGE].flux = 150;
    g_units[B_FACTORY].flux = 100; g_units[B_FORGE].flux = 0; g_units[B_WATCHTOWER].flux = 25;
    g_units[U_STRIKER].cost = 75; g_units[U_LANCER].cost = 150; g_units[U_MENDER].cost = 100; g_units[U_SIEGE].cost = 200;
    g_units[B_FACTORY].cost = 150; g_units[B_FORGE].cost = 125;
    // neutral camps: worth clearing with a hero from the first minutes
    g_units[U_GOLEM].hp = 340; g_units[U_GOLEM].dmg = 14;
    g_units[B_CORE].dmg = 32;

    //   hero        name        role/desc                                                       hp    arm  dmg  rng   cd    spd   hp/l  dmg/l mana  regen  color
    H(H_VANGUARD, "VANGUARD", "Colosse de première ligne. Charge, protège et étourdit.", 720, 28, 42, 1.6f, 1.10f, 5.4f, 92, 4.0f, 280, 1.6f, {70, 140, 230, 255});
    H(H_PYRA,     "PYRA",     "Mage de feu. Zones dévastatrices et météore.",            520, 14, 34, 7.0f, 1.20f, 5.3f, 70, 3.0f, 420, 2.6f, {255, 110, 50, 255});
    H(H_HAWKE,    "HAWKE",    "Tireur d'élite. Dégâts soutenus et tir orbital.",         540, 16, 48, 8.0f, 0.95f, 5.4f, 74, 4.5f, 300, 1.8f, {90, 210, 120, 255});
    H(H_SERAPH,   "SERAPH",   "Soutien. Soigne et accélère toute l'armée.",              560, 18, 30, 6.5f, 1.15f, 5.4f, 78, 2.5f, 460, 3.0f, {250, 220, 120, 255});
    H(H_WARLORD,  "WARLORD",  "Commandant. Galvanise et invoque des renforts.",          660, 24, 40, 1.7f, 1.15f, 5.3f, 86, 3.5f, 320, 2.0f, {200, 70, 70, 255});
    H(H_SHADE,    "SHADE",    "Assassin furtif. Plonge sur les héros isolés.",           560, 18, 50, 1.6f, 0.90f, 5.7f, 72, 5.0f, 280, 1.8f, {160, 90, 220, 255});

    A(H_VANGUARD, 0, "CHARGE", "Fonce vers un point, blesse et ralentit les ennemis traversés.", TM_POINT, 10, 2.0f, 9, 50, 1);
    A(H_VANGUARD, 1, "REMPART", "Bouclier absorbant 140 (+35/niv) dégâts pendant 4 s.", TM_NONE, 0, 0, 13, 60, 2);
    A(H_VANGUARD, 2, "SÉISME", "Étourdit 1,2 s et blesse les ennemis autour de vous.", TM_NONE, 0, 5.5f, 14, 70, 3);
    A(H_VANGUARD, 3, "BASTION", "Vos unités proches gagnent un bouclier de 160 (+20/niv), vous êtes soigné.", TM_NONE, 0, 12, 70, 120, 6);
    A(H_PYRA, 0, "BOULE DE FEU", "Projectile : 90 (+28/niv) dégâts au premier ennemi touché.", TM_DIRECTION, 16, 0.8f, 5, 45, 1);
    A(H_PYRA, 1, "BRASIER", "Zone de feu 4 s : 28 (+9/niv) dégâts par seconde.", TM_POINT, 12, 4.0f, 11, 70, 2);
    A(H_PYRA, 2, "TRANSFERT", "Téléportation instantanée sur 8 m.", TM_POINT, 8, 0, 14, 50, 3);
    A(H_PYRA, 3, "MÉTÉORE", "Après 1,5 s, 320 (+45/niv) dégâts dans une large zone.", TM_POINT, 26, 7.0f, 60, 150, 6);
    A(H_HAWKE, 0, "TIR PERFORANT", "Ligne traversante : 95 (+30/niv) dégâts.", TM_DIRECTION, 18, 1.0f, 7, 45, 1);
    A(H_HAWKE, 1, "CADENCE", "+70% de vitesse d'attaque pendant 5 s.", TM_NONE, 0, 0, 12, 50, 2);
    A(H_HAWKE, 2, "ROULADE", "Bond de 6 m, la prochaine attaque inflige +60%.", TM_POINT, 6, 0, 8, 35, 3);
    A(H_HAWKE, 3, "TIR ORBITAL", "Vise un ennemi visible : 380 (+50/niv) dégâts après 1 s.", TM_ENEMY, 60, 0, 55, 120, 6);
    A(H_SERAPH, 0, "LUMIÈRE", "Soigne un allié de 110 (+32/niv).", TM_ALLY, 11, 0, 6, 55, 1);
    A(H_SERAPH, 1, "VENT ARRIÈRE", "Alliés proches : +35% vitesse et +20% cadence pendant 5 s.", TM_NONE, 0, 13, 16, 70, 2);
    A(H_SERAPH, 2, "CHAÎNES", "Ralentit de 50% pendant 3 s et blesse dans une zone.", TM_POINT, 11, 5.0f, 12, 60, 3);
    A(H_SERAPH, 3, "RENAISSANCE", "Soigne toutes vos unités dans un grand rayon de 300 (+40/niv).", TM_NONE, 0, 16, 75, 150, 6);
    A(H_WARLORD, 0, "RALLIEMENT", "Unités alliées proches : +30% dégâts pendant 6 s.", TM_NONE, 0, 13, 12, 50, 1);
    A(H_WARLORD, 1, "ESCOUADE", "Invoque 2 Gardiens pendant 30 s.", TM_NONE, 0, 0, 22, 80, 2);
    A(H_WARLORD, 2, "CRI DE GUERRE", "Ennemis proches : ralentis de 40% et +20% de dégâts subis, 4 s.", TM_NONE, 0, 7, 14, 60, 3);
    A(H_WARLORD, 3, "RENFORTS", "Largue 3 Gardiens et 3 Tireurs pendant 45 s.", TM_POINT, 14, 0, 85, 150, 6);
    A(H_SHADE, 0, "LAME D'OMBRE", "Saute sur un ennemi : 80 (+26/niv) dégâts.", TM_ENEMY, 8, 0, 7, 45, 1);
    A(H_SHADE, 1, "VOILE", "Invisible 4 s, +30% vitesse, prochaine attaque doublée.", TM_NONE, 0, 0, 16, 60, 2);
    A(H_SHADE, 2, "VENIN", "Les 4 prochaines attaques empoisonnent (12/s pendant 4 s).", TM_NONE, 0, 0, 10, 40, 3);
    A(H_SHADE, 3, "EXÉCUTION", "Fond sur un héros : 220 (+30/niv) + 25% des PV manquants.", TM_ENEMY, 11, 0, 50, 100, 6);

    //  id         name             desc                                              cost   dmg  as    ms    arm  hp   cdr   mana ls    ap    aura
    I(I_BLADE,   "LAME",           "+22 dégâts d'attaque",                            450,  22, 0,    0,    0,   0,   0,    0,   0,    0,    0);
    I(I_BOW,     "ARC-NERF",       "+35% vitesse d'attaque",                          450,  0,  0.35f, 0,   0,   0,   0,    0,   0,    0,    0);
    I(I_BOOTS,   "BOTTES",         "+15% vitesse de déplacement",                     350,  0,  0,    0.15f, 0,  0,   0,    0,   0,    0,    0);
    I(I_PLATE,   "PLASTRON",       "+30 armure",                                      500,  0,  0,    0,    30,  0,   0,    0,   0,    0,    0);
    I(I_HEART,   "CŒUR DE GOLEM",  "+320 PV",                                         600,  0,  0,    0,    0,   320, 0,    0,   0,    0,    0);
    I(I_FOCUS,   "PRISME",         "-20% temps de recharge, +150 mana",               550,  0,  0,    0,    0,   0,   0.2f, 150, 0,    0,    0);
    I(I_VAMP,    "CROC",           "+12 dégâts, 15% vol de vie",                      700,  12, 0,    0,    0,   0,   0,    0,   0.15f, 0,   0);
    I(I_SCEPTER, "SCEPTRE",        "+40% dégâts des compétences",                     800,  0,  0,    0,    0,   0,   0,    0,   0,    0.4f, 0);
    I(I_AEGIS,   "ÉTENDARD",       "Vos unités à 12 m : +20% dégâts (aura)",          900,  0,  0,    0,    10,  150, 0,    0,   0,    0,    0.2f);
    I(I_POTION,  "POTION",         "Rend 220 PV en 8 s (consommable)",                60,   0,  0,    0,    0,   0,   0,    0,   0,    0,    0);
}

const UnitDef& unitDef(int t) { return g_units[(t >= 0 && t < UT_COUNT) ? t : 0]; }
const AbilityDef& abilityDef(int heroType, int slot) { return g_abil[std::clamp(heroType - HERO_FIRST, 0, HERO_COUNT - 1)][std::clamp(slot, 0, 3)]; }
const ItemDef& itemDef(int id) { return g_items[(id >= 0 && id < I_COUNT) ? id : 0]; }

int upgradeCost(int up, int level) { (void)up; static const int c[3] = {100, 175, 250}; return c[std::clamp(level, 0, 2)]; }
int upgradeFlux(int up, int level) { return upgradeCost(up, level); }
float upgradeTime(int level) { return 45.f + level * 15.f; }

const std::vector<UnitType>& trainList(int b) {
    static const std::vector<UnitType> core = {U_DRONE}, hub = {U_DRONE}, barracks = {U_WARDEN, U_STRIKER}, factory = {U_LANCER, U_MENDER, U_SIEGE}, none;
    switch (b) {
    case B_CORE: return core;
    case B_HUB: return hub;
    case B_BARRACKS: return barracks;
    case B_FACTORY: return factory;
    default: return none;
    }
}
const std::vector<UnitType>& buildList() {
    static const std::vector<UnitType> l = {B_BARRACKS, B_FACTORY, B_FORGE, B_RELAY, B_WATCHTOWER, B_EXTRACTOR, B_HUB};
    return l;
}
const char* heroRole(int h) {
    switch (h) {
    case H_VANGUARD: return "COLOSSE";
    case H_PYRA: return "MAGE";
    case H_HAWKE: return "TIREUR";
    case H_SERAPH: return "SOUTIEN";
    case H_WARLORD: return "COMMANDANT";
    case H_SHADE: return "ASSASSIN";
    default: return "";
    }
}

} // namespace ax
