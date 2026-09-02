// mod_companion_caddy.cpp
//
// AzerothCore module: Companion Caddy — server-side point tracking and
// promo shop integration with mod-tcg-vendors.
//
// Graceful degradation: if account_tcg_codes (mod-tcg-vendors) is absent,
// the promo shop tab is disabled but point earning still functions.

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "EventProcessor.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"

#include <algorithm>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
//  Forward declarations
// ============================================================
static void SendAddonMsg(Player* player, const std::string& payload);
static uint32 GetDailyEarned(uint32 guid);

// ============================================================
//  Constants
// ============================================================
static const std::string CADDY_PREFIX        = "CCADDY";
static const int32       DEFAULT_DAILY_CAP   = 200;
static const uint32      DEFAULT_COOLDOWN_S  = 600;   // 10 min per challenge type

// ============================================================
//  TCG module availability flag (set on startup)
// ============================================================
static bool s_tcgAvailable = false;

// ============================================================
//  Server-authoritative point awards per challenge type.
// The client sends the challenge type; any points value it
// also sends is IGNORED. This prevents a modified addon from
// granting itself extra points.
// ============================================================
static const std::map<std::string, uint32> CHALLENGE_AWARDS =
{
    { "feed",               5  },
    { "play",               5  },
    { "rest",               5  },
    { "clean",              5  },
    { "soothe",             5  },
    { "warm",               5  },
    { "work_ticks",         3  },
    { "zone_activity",      8  },
    { "quest_turnin",      15  },
    { "dungeon_complete",  30  },
    { "emote_dance",        8  },
    { "emote_salute",       6  },
    { "emote_wave",         6  },
    { "emote_laugh",        6  },
    { "emote_bow",          6  },
    { "emote_roar",         6  },
    { "emote_cheer",        6  },
    { "emote_sleep",        8  },
    { "emote",              5  },
    { "visit_vendor",      10  },
    { "visit_bank",        10  },
    { "visit_trainer",     10  },
    { "check_mail",         8  },
    { "visit_major_city",  10  },
    { "visit_inn",          8  },
    { "use_hearthstone",   12  },
    { "leave_combat",       8  },
    { "craft_anything",    12  },
    { "cook_food",         12  },
    { "fish_zone",         15  },
    { "gather_herb",       15  },
    { "gather_ore",        15  },
    { "gather_skin",       12  },
    { "use_mount",          8  },
    { "use_bandage",       10  },
    { "use_potion",         8  },
    { "level_up",          50  },
    { "swim_short",        10  },
    { "swim_long",         20  },
    { "drink_any",          5  },
    { "create_fire",        8  },
    { "summon_companion",  10  },
    { "dk_ability",         8  },
    { "druid_shapeshift",  10  },
    { "duel_request",      10  },
    { "explore_zone",      10  },
    { "gain_reputation",   12  },
    { "group_up",          10  },
    { "honorable_kill",    15  },
    { "hunter_feign_death", 8  },
    { "learn_companion",   15  },
    { "loot_gold",          6  },
    { "loot_rare_item",    18  },
    { "mage_conjure",       8  },
    { "paladin_blessing",   8  },
    { "priest_heal",       10  },
    { "return_from_death", 10  },
    { "rogue_stealth",      8  },
    { "roll_dice",          6  },
    { "scavenge",          15  },
    { "shaman_totem",       8  },
    { "trade_player",       8  },
    { "visit_auction_house",8  },
    { "warlock_summon_pet", 8  },
    { "warrior_charge",     8  },
    { "emote_thank",        6  },
    { "emote_cry",          6  },
    { "emote_yawn",         6  },
    { "emote_clap",         6  },
    { "earn_achievement",  20  },
    { "guild_chat",         6  },
    { "use_flight_path",    8  },
    { "scavenge_squad",    30  },
};

// ============================================================
//  Shop catalog — one entry per purchasable reward group.
//  rewardGroup keys must exactly match REWARD_GROUPS in
//  mod-tcg-vendors (account_tcg_codes.reward_group column).
// ============================================================
struct CaddyItem
{
    std::string rewardGroup;
    std::string displayName;
    uint32      pointCost;
    std::string category;    
    uint32      itemEntry;   
    uint32      itemEntryAlt;
    bool        factionSplit;
};

static const std::vector<CaddyItem> SHOP_CATALOG =
{
    // ── Tabards ────────────────────────────────────────────────────────────
    { "TCG_TABARD_OF_FROST",           "Tabard of Frost",              250, "Tabards",     23709, 0,     false },
    { "TCG_TABARD_OF_FLAME",           "Tabard of Flame",              250, "Tabards",     23705, 0,     false },
    { "TCG_TABARD_OF_THE_ARCANE",      "Tabard of the Arcane",         300, "Tabards",     38310, 0,     false },
    { "TCG_TABARD_OF_BRILLIANCE",      "Tabard of Brilliance",         300, "Tabards",     38312, 0,     false },
    { "TCG_TABARD_OF_THE_DEFENDER",    "Tabard of the Defender",       300, "Tabards",     38314, 0,     false },
    { "TCG_TABARD_OF_FURY",            "Tabard of Fury",               300, "Tabards",     38313, 0,     false },
    { "TCG_TABARD_OF_NATURE",          "Tabard of Nature",             300, "Tabards",     38309, 0,     false },
    { "TCG_TABARD_OF_THE_VOID",        "Tabard of the Void",           300, "Tabards",     38311, 0,     false },
    { "TCG_EPIC_PURPLE_SHIRT",         "Epic Purple Shirt",            250, "Tabards",     45037, 0,     false },

    // ── Toys ───────────────────────────────────────────────────────────────
    { "TCG_FISHING_CHAIR",             "Fishing Chair",                420, "Toys",        33223, 0,     false },
    { "TCG_PERPETUAL_PURPLE_FIREWORK", "Perpetual Purple Firework",    200, "Toys",        23714, 0,     false },
    { "TCG_CARVED_OGRE_IDOL",          "Carved Ogre Idol",             250, "Toys",        23716, 0,     false },
    { "TCG_SANDBOX_TIGER",             "Sandbox Tiger",                350, "Toys",        45047, 0,     false },
    { "TCG_PET_BISCUIT",               "Papa Hummel's Pet Biscuit",    200, "Toys",        35223, 0,     false },
    { "TCG_FOAM_SWORD_RACK",           "Foam Sword Rack",              400, "Toys",        45063, 0,     false },
    { "TCG_PARTY_GRENADE",             "Party G.R.E.N.A.D.E.",         350, "Toys",        38577, 0,     false },
    { "TCG_PAINT_BOMB",                "Paint Bomb",                   300, "Toys",        54455, 0,     false },
    { "TCG_PATH_OF_ILLIDAN",           "Path of Illidan",              400, "Toys",        38233, 0,     false },
    { "TCG_PATH_OF_CENARIUS",          "Path of Cenarius",             400, "Toys",        46779, 0,     false },
    { "TCG_IMP_IN_A_BALL",             "Imp in a Ball",                500, "Toys",        32542, 0,     false },
    { "TCG_PAPER_FLYING_MACHINE",      "Paper Flying Machine Kit",     500, "Toys",        34499, 0,     false },
    { "TCG_FLAG_OF_OWNERSHIP",         "The Flag of Ownership",        700, "Toys",        38578, 0,     false },
    { "TCG_PICNIC_BASKET",             "Picnic Basket",                600, "Toys",        32566, 0,     false },
    { "TCG_INSTANT_STATUE_PEDESTAL",   "Instant Statue Pedestal",      700, "Toys",        54212, 0,     false },
    { "TCG_GOBLIN_GUMBO_KETTLE",       "Goblin Gumbo Kettle",          600, "Toys",        33219, 0,     false },
    { "BLIZZCON_MURLOC_COSTUME",       "Murloc Costume",               800, "Toys",        33079, 0,     false },
    { "TCG_GOBLIN_WEATHER_MACHINE",    "Goblin Weather Machine",       800, "Toys",        35227, 0,     false },
    { "TCG_ETHEREAL_PORTAL",           "Ethereal Portal",              900, "Toys",        54452, 0,     false },
    { "TCG_DISCO",                     "D.I.S.C.O.",                  1100, "Toys",        38301, 0,     false },

    // ── Companions ─────────────────────────────────────────────────────────
    { "PROMO_ORANGE_MURLOC_EGG",       "Orange Murloc Egg",            600, "Companions",  20651, 0,     false },
    { "PROMO_WHITE_MURLOC_EGG",        "White Murloc Egg",             600, "Companions",  22780, 0,     false },
    { "PROMO_GURKY",                   "Gurky (Pink Murloc Egg)",      700, "Companions",  22114, 0,     false },
    { "PROMO_HEAVY_MURLOC_EGG",        "Heavy Murloc Egg",             800, "Companions",  46802, 0,     false },
    { "PROMO_MURKIMUS_SPEAR",          "Murkimus' Little Spear",       900, "Companions",  45180, 0,     false },
    { "BLIZZCON_MURKY",                "Murky (Blue Murloc Egg)",     1000, "Companions",  20371, 0,     false },
    { "PROMO_ZERGLING_LEASH",          "Zergling Leash",               800, "Companions",  13582, 0,     false },
    { "PROMO_PANDA_COLLAR",            "Panda Collar",                 800, "Companions",  13583, 0,     false },
    { "PROMO_DIABLO_STONE",            "Diablo Stone",                 800, "Companions",  13584, 0,     false },
    { "PROMO_NETHERWHELP",             "Netherwhelp's Collar",        1000, "Companions",  25535, 0,     false },
    { "PROMO_FROSTYS_COLLAR",          "Frosty's Collar",             1000, "Companions",  39286, 0,     false },
    { "PROMO_GRYPHON_HATCHLING",       "Gryphon Hatchling",            900, "Companions",  49662, 0,     false },
    { "PROMO_WIND_RIDER_CUB",          "Wind Rider Cub",               900, "Companions",  49663, 0,     false },
    { "TCG_BANANA_CHARM",              "Banana Charm",                1000, "Companions",  32588, 0,     false },
    { "PROMO_PANDAREN_MONK",           "Pandaren Monk",               1000, "Companions",  49665, 0,     false },
    { "PROMO_ENCHANTED_ONYX",          "Enchanted Onyx",              1000, "Companions",  48527, 0,     false },
    { "PROMO_CORE_HOUND_PUP",          "Core Hound Pup",              1000, "Companions",  49646, 0,     false },
    { "PROMO_ONYXIAN_WHELPLING",       "Onyxian Whelpling",           1200, "Companions",  49362, 0,     false },
    { "PROMO_LIL_PHYLACTERY",          "Lil' Phylactery",             1200, "Companions",  49693, 0,     false },
    { "PROMO_LIL_XT",                  "Lil' XT",                     1200, "Companions",  54847, 0,     false },
    { "PROMO_WARBOT_KEY",              "Warbot Ignition Key",         1500, "Companions",  46767, 0,     false },
    { "TCG_HIPPOGRYPH_HATCHLING",      "Hippogryph Hatchling",         800, "Companions",  23713, 0,     false },
    { "TCG_DRAGON_KITE",               "Dragon Kite",                  900, "Companions",  34493, 0,     false },
    { "TCG_TUSKARR_KITE",              "Tuskarr Kite",                 900, "Companions",  49287, 0,     false },
    { "TCG_ROCKET_CHICKEN",            "Rocket Chicken",               900, "Companions",  34492, 0,     false },
    { "TCG_SPECTRAL_TIGER_CUB",        "Spectral Tiger Cub",          1200, "Companions",  49343, 0,     false },
    { "TCG_SOUL_TRADER_BEACON",        "Soul-Trader Beacon",          1400, "Companions",  38050, 0,     false },
    { "PROMO_MINI_THOR",               "Mini Thor",                   1500, "Companions",  56806, 0,     false },
    { "WWI_TYRAELS_HILT",              "Tyrael's Hilt",               1800, "Companions",  39656, 0,     false },

    // ── Mounts ─────────────────────────────────────────────────────────────
    { "TCG_SCOURGEWAR_MINIMOUNT",      "Scourgewar Mini-Mount",       2000, "Mounts",      49289, 49288, true  },
    { "TCG_RIDING_TURTLE",             "Riding Turtle",               2000, "Mounts",      23720, 0,     false },
    { "BLIZZCON_BIG_BLIZZARD_BEAR",    "Big Blizzard Bear",           2500, "Mounts",      43599, 0,     false },
    { "TCG_BIG_BATTLE_BEAR",           "Big Battle Bear",             2500, "Mounts",      38576, 0,     false },
    { "TCG_MAGIC_ROOSTER_EGG",         "Magic Rooster Egg",           2500, "Mounts",      46778, 0,     false },
    { "TCG_WOOLY_WHITE_RHINO",         "Wooly White Rhino",           2500, "Mounts",      54068, 0,     false },
    { "TCG_BLAZING_HIPPOGRYPH",        "Blazing Hippogryph",          2500, "Mounts",      54069, 0,     false },
    { "TCG_X51_NETHER_ROCKET",         "X-51 Nether-Rocket",          2500, "Mounts",      35225, 35226, false },
    { "TCG_SPECTRAL_TIGER",            "Reins of the Spectral Tiger", 5000, "Mounts",      33224, 33225, false },

    // ── Special ────────────────────────────────────────────────────────────
    { "TCG_LANDROS_PET_BOX",           "Landro's Pet Box",             600, "Special",     50301, 0,     false },
    { "TCG_LANDROS_GIFT_BOX",          "Landro's Gift Box",            800, "Special",     54218, 0,     false },
};

// ============================================================
//  TCG/Promo vendor NPCs — used to make redemption mail appear to come
//  from the vendor the stationery should be redeemed at, with flavor
//  text inviting the player to that vendor's location.
// ============================================================
struct CaddyVendor
{
    uint32      entry;      // creature_template ID — MailSender resolves the
                             // "From:" name from this via MAIL_CREATURE
    std::string name;
    std::string location;   // used in the invite line of the mail body
};

static const CaddyVendor VENDOR_LANDRO  = { 17249, "Landro Longshot",     "Booty Bay" };
static const CaddyVendor VENDOR_RANSIN  = { 2943,  "Ransin Donner",       "the Forlorn Cavern in Ironforge" };
static const CaddyVendor VENDOR_ZASTYSH = { 7951,  "Zas'Tysh",            "the Valley of Honor in Orgrimmar" };
static const CaddyVendor VENDOR_GAREL   = { 16070, "Garel Redrock",       "the Forlorn Cavern in Ironforge" };
static const CaddyVendor VENDOR_THARL   = { 16076, "Tharl Stonebleeder",  "the Valley of Honor in Orgrimmar" };
static const CaddyVendor VENDOR_EDWARD  = { 29095, "Edward Cairn",        "Undercity" };
static const CaddyVendor VENDOR_IAN     = { 29093, "Ian Drake",           "Stormwind" };

enum class VendorGroup { LANDRO, OLD_GUARD, STONE, CAPITAL };

static const std::map<std::string, VendorGroup> REWARD_GROUP_VENDOR =
{
    // ── Tabards -- all Landro (Points Redemption / TCG expansion loot) ──────
    { "TCG_TABARD_OF_FROST",           VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_FLAME",           VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_THE_ARCANE",      VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_BRILLIANCE",      VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_THE_DEFENDER",    VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_FURY",            VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_NATURE",          VendorGroup::LANDRO    },
    { "TCG_TABARD_OF_THE_VOID",        VendorGroup::LANDRO    },
    { "TCG_EPIC_PURPLE_SHIRT",         VendorGroup::LANDRO    },

    // ── Toys -- all Landro (TCG expansion loot) ─────────────────────────────
    { "TCG_PERPETUAL_PURPLE_FIREWORK", VendorGroup::LANDRO    },
    { "TCG_CARVED_OGRE_IDOL",          VendorGroup::LANDRO    },
    { "TCG_SANDBOX_TIGER",             VendorGroup::LANDRO    },
    { "TCG_PET_BISCUIT",               VendorGroup::LANDRO    },
    { "TCG_FOAM_SWORD_RACK",           VendorGroup::LANDRO    },
    { "TCG_PARTY_GRENADE",             VendorGroup::LANDRO    },
    { "TCG_PAINT_BOMB",                VendorGroup::LANDRO    },
    { "TCG_PATH_OF_ILLIDAN",           VendorGroup::LANDRO    },
    { "TCG_PATH_OF_CENARIUS",          VendorGroup::LANDRO    },
    { "TCG_IMP_IN_A_BALL",             VendorGroup::LANDRO    },
    { "TCG_PAPER_FLYING_MACHINE",      VendorGroup::LANDRO    },
    { "TCG_FLAG_OF_OWNERSHIP",         VendorGroup::LANDRO    },
    { "TCG_PICNIC_BASKET",             VendorGroup::LANDRO    },
    { "TCG_FISHING_CHAIR",             VendorGroup::LANDRO    },
    { "TCG_INSTANT_STATUE_PEDESTAL",   VendorGroup::LANDRO    },
    { "TCG_GOBLIN_GUMBO_KETTLE",       VendorGroup::LANDRO    },
    { "BLIZZCON_MURLOC_COSTUME",       VendorGroup::OLD_GUARD },
    { "TCG_GOBLIN_WEATHER_MACHINE",    VendorGroup::LANDRO    },
    { "TCG_ETHEREAL_PORTAL",           VendorGroup::LANDRO    },
    { "TCG_DISCO",                     VendorGroup::LANDRO    },

    // ── Companions ───────────────────────────────────────────────────────
    { "PROMO_ORANGE_MURLOC_EGG",       VendorGroup::STONE     },
    { "PROMO_WHITE_MURLOC_EGG",        VendorGroup::STONE     },
    { "PROMO_GURKY",                   VendorGroup::STONE     },
    { "PROMO_HEAVY_MURLOC_EGG",        VendorGroup::STONE     },
    { "PROMO_MURKIMUS_SPEAR",          VendorGroup::STONE     },
    { "BLIZZCON_MURKY",                VendorGroup::OLD_GUARD },
    { "TCG_BANANA_CHARM",              VendorGroup::LANDRO    },
    { "PROMO_ZERGLING_LEASH",          VendorGroup::STONE     },
    { "PROMO_PANDA_COLLAR",            VendorGroup::STONE     },
    { "PROMO_DIABLO_STONE",            VendorGroup::STONE     },
    { "PROMO_NETHERWHELP",             VendorGroup::STONE     },
    { "PROMO_FROSTYS_COLLAR",          VendorGroup::STONE     },
    { "PROMO_GRYPHON_HATCHLING",       VendorGroup::STONE     },
    { "PROMO_WIND_RIDER_CUB",          VendorGroup::STONE     },
    { "PROMO_PANDAREN_MONK",           VendorGroup::STONE     },
    { "PROMO_ENCHANTED_ONYX",          VendorGroup::STONE     },
    { "PROMO_CORE_HOUND_PUP",          VendorGroup::STONE     },
    { "PROMO_ONYXIAN_WHELPLING",       VendorGroup::STONE     },
    { "PROMO_LIL_PHYLACTERY",          VendorGroup::STONE     },
    { "PROMO_LIL_XT",                  VendorGroup::STONE     },
    { "PROMO_WARBOT_KEY",              VendorGroup::STONE     },
    { "TCG_HIPPOGRYPH_HATCHLING",      VendorGroup::LANDRO    },
    { "TCG_DRAGON_KITE",               VendorGroup::LANDRO    },
    { "TCG_TUSKARR_KITE",              VendorGroup::LANDRO    },
    { "TCG_ROCKET_CHICKEN",            VendorGroup::LANDRO    },
    { "TCG_SPECTRAL_TIGER_CUB",        VendorGroup::LANDRO    },
    { "TCG_SOUL_TRADER_BEACON",        VendorGroup::LANDRO    },
    { "PROMO_MINI_THOR",               VendorGroup::STONE     },
    { "WWI_TYRAELS_HILT",              VendorGroup::CAPITAL   },

    // ── Mounts ───────────────────────────────────────────────────────────
    { "TCG_SCOURGEWAR_MINIMOUNT",      VendorGroup::LANDRO    },
    { "TCG_RIDING_TURTLE",             VendorGroup::LANDRO    },
    { "BLIZZCON_BIG_BLIZZARD_BEAR",    VendorGroup::OLD_GUARD },
    { "TCG_BIG_BATTLE_BEAR",           VendorGroup::LANDRO    },
    { "TCG_MAGIC_ROOSTER_EGG",         VendorGroup::LANDRO    },
    { "TCG_WOOLY_WHITE_RHINO",         VendorGroup::LANDRO    },
    { "TCG_BLAZING_HIPPOGRYPH",        VendorGroup::LANDRO    },
    { "TCG_X51_NETHER_ROCKET",         VendorGroup::LANDRO    },
    { "TCG_SPECTRAL_TIGER",            VendorGroup::LANDRO    },

    // ── Special ──────────────────────────────────────────────────────────
    { "TCG_LANDROS_PET_BOX",           VendorGroup::LANDRO    },
    { "TCG_LANDROS_GIFT_BOX",          VendorGroup::LANDRO    },
};

static const CaddyVendor& PickVendor(const std::string& rewardGroup, TeamId team)
{
    auto it = REWARD_GROUP_VENDOR.find(rewardGroup);
    VendorGroup group = (it != REWARD_GROUP_VENDOR.end()) ? it->second : VendorGroup::LANDRO;

    switch (group)
    {
        case VendorGroup::OLD_GUARD: return (team == TEAM_ALLIANCE) ? VENDOR_RANSIN : VENDOR_ZASTYSH;
        case VendorGroup::STONE:     return (team == TEAM_ALLIANCE) ? VENDOR_GAREL  : VENDOR_THARL;
        case VendorGroup::CAPITAL:   return (team == TEAM_ALLIANCE) ? VENDOR_IAN    : VENDOR_EDWARD;
        case VendorGroup::LANDRO:
        default:                     return VENDOR_LANDRO;
    }
}

// ============================================================
//  In-memory per-session cooldown tracker
// ============================================================
static std::map<uint32, std::map<std::string, time_t>> s_cooldowns;

// ============================================================
//  In-memory mirror of "which companions (by spell ID) does this
//  character currently have a still-running scavenge for"
// ============================================================
static std::map<uint32, std::set<uint32>> s_activeScavenges;

// ============================================================
//  In-flight state-push reassembly buffers.
// ============================================================
struct CaddyStatePushBuffer
{
    uint32 revision = 0;
    uint32 checksum = 0;
    std::vector<std::string> chunks;
};
static std::map<uint32, CaddyStatePushBuffer> s_statePushBuffers;

// ============================================================
//  Config helpers
// ============================================================
// Raw config value:
//   > 0  -- normal daily cap
//   == 0 -- earning is uncapped/unlimited for the day
//   < 0  -- promo points are disabled entirely (canonical value: -1)
static int32 GetDailyCap()
{
    return sConfigMgr->GetOption<int32>("CompanionCaddy.DailyCap", DEFAULT_DAILY_CAP);
}

// Whether the promo point system is active at all. False when DailyCap is
// negative -- earning, the daily-cap error, and the promo shop should all
// behave as if the feature doesn't exist (same posture as TCG being
// unavailable), instead of silently blocking every award.
static bool PromoPointsEnabled()
{
    return GetDailyCap() >= 0;
}

// Points still earnable today.
//   Returns a real remaining count when capped (cap > 0).
//   Returns -1 to signal "unlimited" when cap == 0.
//   Returns 0 when promo points are disabled (cap < 0); callers should be
//   checking PromoPointsEnabled() anyway in that case.
static int32 GetDailyLeft(uint32 guid)
{
    int32 cap = GetDailyCap();
    if (cap < 0)
        return 0;
    if (cap == 0)
        return -1;

    uint32 earned = GetDailyEarned(guid);
    return (earned < (uint32)cap) ? (int32)((uint32)cap - earned) : 0;
}

static uint32 GetCooldown()
{
    return sConfigMgr->GetOption<uint32>("CompanionCaddy.ChallengeCooldown", DEFAULT_COOLDOWN_S);
}

static bool GetScavengeLootEnabled()
{
    return sConfigMgr->GetOption<bool>("CompanionCaddy.ScavengeLootEnable", true);
}

static uint32 GetScavengeLootChance(const std::string& tier)
{
    if (tier == "green")  return sConfigMgr->GetOption<uint32>("CompanionCaddy.ScavengeLootChanceGreen", 20);
    if (tier == "blue")   return sConfigMgr->GetOption<uint32>("CompanionCaddy.ScavengeLootChanceBlue", 12);
    if (tier == "purple") return sConfigMgr->GetOption<uint32>("CompanionCaddy.ScavengeLootChancePurple", 6);
    return 0;
}

// ============================================================
//  Care-quality multiplier on the bonus-loot chance above.
// ============================================================
static double GetScavengeCareMult(const std::string& careTier)
{
    if (careTier == "struggling") return sConfigMgr->GetOption<float>("CompanionCaddy.ScavengeCareMultStruggling", 0.5f);
    if (careTier == "okay")       return sConfigMgr->GetOption<float>("CompanionCaddy.ScavengeCareMultOkay", 0.8f);
    if (careTier == "good")       return sConfigMgr->GetOption<float>("CompanionCaddy.ScavengeCareMultGood", 1.0f);
    if (careTier == "thriving")   return sConfigMgr->GetOption<float>("CompanionCaddy.ScavengeCareMultThriving", 1.4f);
    return 1.0;
}

// ============================================================
//  Scavenging bonus loot
// ============================================================
static std::vector<uint32> ParseItemIdList(const std::string& csv)
{
    std::vector<uint32> ids;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end   = token.find_last_not_of(" \t\r\n");
        if (start == std::string::npos)
            continue; // blank entry (e.g. a trailing comma) -- ignore quietly
        token = token.substr(start, end - start + 1);

        try
        {
            size_t consumed = 0;
            unsigned long id = std::stoul(token, &consumed);
            if (consumed != token.size() || id == 0 || id > std::numeric_limits<uint32>::max())
                throw std::invalid_argument(token);
            ids.push_back(static_cast<uint32>(id));
        }
        catch (const std::exception&)
        {
            LOG_ERROR("module",
                "mod-companion-caddy: '{}' in a ScavengeLootPool config "
                "entry is not a valid item ID -- skipping it.", token);
        }
    }
    return ids;
}

static std::vector<uint32> GetScavengeLootPool(const std::string& tier)
{
    if (tier == "green")
        return ParseItemIdList(sConfigMgr->GetOption<std::string>("CompanionCaddy.ScavengeLootPoolGreen", ""));
    if (tier == "blue")
        return ParseItemIdList(sConfigMgr->GetOption<std::string>("CompanionCaddy.ScavengeLootPoolBlue", ""));
    if (tier == "purple")
        return ParseItemIdList(sConfigMgr->GetOption<std::string>("CompanionCaddy.ScavengeLootPoolPurple", ""));
    return {};
}

static void RollScavengeLoot(Player* player, const std::string& tier, const std::string& careTier)
{
    if (!player || !GetScavengeLootEnabled())
        return;

    std::vector<uint32> pool = GetScavengeLootPool(tier);
    if (pool.empty())
        return;

    double chance = static_cast<double>(GetScavengeLootChance(tier)) * GetScavengeCareMult(careTier);
    chance = std::max(0.0, std::min(100.0, chance));
    if (chance <= 0.0 || urand(1, 100) > static_cast<uint32>(chance))
        return;

    uint32 itemEntry = pool[urand(0, (uint32)(pool.size() - 1))];
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
    {
        LOG_ERROR("module",
            "mod-companion-caddy: scavenge bonus loot roll picked item {} "
            "but it has no item_template row -- check the {} pool for a "
            "stale/incorrect entry.", itemEntry, tier);
        return;
    }

    ItemPosCountVec dest;
    InventoryResult canStore = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1);
    bool mailed = false;

    if (canStore == EQUIP_ERR_OK)
    {
        Item* item = player->StoreNewItem(dest, itemEntry, true);
        if (!item)
        {
            LOG_ERROR("module",
                "mod-companion-caddy: StoreNewItem unexpectedly failed for "
                "item {} (guid {}) after CanStoreNewItem reported room -- "
                "no item granted.", itemEntry, player->GetGUID().GetCounter());
            return;
        }
        player->SendNewItem(item, 1, true, false);
    }
    else
    {
        mailed = true;

        std::string subject = "A find while scavenging!";
        std::string body    =
            "While your companion was out scavenging, it dug up something "
            "worth more than treats -- an item, still in good condition. "
            "Your bags were full at the time, so it's being mailed to you "
            "instead of handed straight over. Honestly, I've seen "
            "stranger things turn up in a scavenged parcel. Hope you can "
            "put it to good use!";

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        Item* item = Item::CreateItem(itemEntry, 1, player);
        if (!item)
        {
            CharacterDatabase.CommitTransaction(trans);
            return;
        }
        item->SaveToDB(trans);
        MailDraft(subject, body)
            .AddItem(item)
            .SendMailTo(
                trans,
                MailReceiver(player, player->GetGUID().GetCounter()),
                MailSender(MAIL_CREATURE, VENDOR_LANDRO.entry, MAIL_STATIONERY_DEFAULT));
        CharacterDatabase.CommitTransaction(trans);
    }

    // Tells the addon to show its "your companion brought back something"
    // popup (with a real, hoverable item tooltip)
    SendAddonMsg(player, "SCAVENGE_ITEM|" + std::to_string(itemEntry) + "|" + (mailed ? "1" : "0"));

    LOG_INFO("module",
        "mod-companion-caddy: player {} (guid {}) received bonus scavenge "
        "loot: item {} ({} tier, {} care){}",
        player->GetName(), player->GetGUID().GetCounter(), itemEntry, tier, careTier,
        mailed ? " -- mailed (bags full)" : " -- placed directly in bags");
}

// ============================================================
//  Server-authoritative scavenge timers
// ============================================================
static uint32 GetScavengeDurationSeconds(const std::string& tier)
{
    if (tier == "green")  return 1800;
    if (tier == "blue")   return 3600;
    if (tier == "purple") return 7200;
    return 0;
}

static std::string SanitizeCareTier(const std::string& careTier)
{
    if (careTier == "struggling" || careTier == "okay" || careTier == "good" || careTier == "thriving")
        return careTier;
    return "good";
}

static void StartScavengeTimer(Player* player, uint32 companionSpellId, const std::string& tier, const std::string& careTierIn)
{
    uint32 duration = GetScavengeDurationSeconds(tier);
    if (!player || duration == 0)
        return;

    std::string careTier = SanitizeCareTier(careTierIn);

    uint32 guid = player->GetGUID().GetCounter();
    uint32 now  = (uint32)time(nullptr);

    QueryResult r = CharacterDatabase.Query(
        "SELECT end_time FROM companion_caddy_scavenges "
        "WHERE guid = {} AND companion_spell_id = {}",
        guid, companionSpellId);

    if (r && r->Fetch()[0].Get<uint32>() > now)
    {
        LOG_WARN("module",
            "mod-companion-caddy: player {} (guid {}) tried to start a "
            "scavenge for companion {} while one is already running "
            "server-side -- ignored.",
            player->GetName(), guid, companionSpellId);
        return;
    }

    CharacterDatabase.DirectExecute(
        "REPLACE INTO companion_caddy_scavenges "
        "(guid, companion_spell_id, tier, care_tier, start_time, end_time) "
        "VALUES ({}, {}, '{}', '{}', {}, {})",
        guid, companionSpellId, tier, careTier, now, now + duration);

    s_activeScavenges[guid].insert(companionSpellId);

    if (ObjectGuid critterGuid = player->GetCritterGUID())
    {
        if (Creature* critter = ObjectAccessor::GetCreature(*player, critterGuid))
            critter->DespawnOrUnsummon();
    }
}

static void ClaimScavenge(Player* player, uint32 companionSpellId)
{
    if (!player)
        return;

    uint32 guid = player->GetGUID().GetCounter();
    uint32 now  = (uint32)time(nullptr);

    QueryResult r = CharacterDatabase.Query(
        "SELECT tier, care_tier, end_time FROM companion_caddy_scavenges "
        "WHERE guid = {} AND companion_spell_id = {}",
        guid, companionSpellId);

    if (!r)
        return; // no server-side record at all -- nothing to claim

    Field* f              = r->Fetch();
    std::string tier      = f[0].Get<std::string>();
    std::string careTier  = f[1].Get<std::string>();
    uint32 endTime        = f[2].Get<uint32>();

    if (endTime > now)
    {
        LOG_WARN("module",
            "mod-companion-caddy: player {} (guid {}) claimed a scavenge "
            "for companion {} with {} second(s) still remaining -- "
            "rejected.",
            player->GetName(), guid, companionSpellId, endTime - now);
        return;
    }

    CharacterDatabase.DirectExecute(
        "DELETE FROM companion_caddy_scavenges "
        "WHERE guid = {} AND companion_spell_id = {}",
        guid, companionSpellId);

    auto it = s_activeScavenges.find(guid);
    if (it != s_activeScavenges.end())
    {
        it->second.erase(companionSpellId);
        if (it->second.empty())
            s_activeScavenges.erase(it);
    }

    RollScavengeLoot(player, tier, careTier);
}

// ============================================================
//  Date string YYYY-MM-DD (server local time)
// ============================================================
static std::string TodayStr()
{
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    return std::string(buf);
}

// ============================================================
//  String split helper
// ============================================================
static std::vector<std::string> SplitStr(const std::string& s, char delim)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s)
    {
        if (c == delim) { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

// ============================================================
//  Send addon message from server to client.
// ============================================================
static void SendAddonMsg(Player* player, const std::string& payload)
{
    if (!player || !player->GetSession())
        return;

    std::string full;
    full.reserve(CADDY_PREFIX.size() + 1 + payload.size());
    full += CADDY_PREFIX;
    full.push_back('\t');
    full += payload;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON,
        player->GetGUID(), player->GetGUID(), full, 0);

    player->GetSession()->SendPacket(&data);
}

// ============================================================
//  DB helpers
// ============================================================
static uint32 GetBalance(uint32 guid)
{
    QueryResult r = CharacterDatabase.Query(
        "SELECT balance FROM companion_caddy_points WHERE guid = {}", guid);
    return r ? r->Fetch()[0].Get<uint32>() : 0u;
}

static uint32 GetDailyEarned(uint32 guid)
{
    QueryResult r = CharacterDatabase.Query(
        "SELECT points_earned FROM companion_caddy_daily "
        "WHERE guid = {} AND date_str = '{}'", guid, TodayStr());
    return r ? r->Fetch()[0].Get<uint32>() : 0u;
}

static uint32 AwardPoints(uint32 guid, uint32 amount, const std::string& ref)
{
    int32 cap = GetDailyCap();

    if (cap < 0)
        return 0;  // promo points disabled entirely -- never award

    if (cap > 0)  // capped mode; cap == 0 falls through as unlimited
    {
        uint32 earned = GetDailyEarned(guid);

        if (earned >= (uint32)cap)
            return 0;

        if (earned + amount > (uint32)cap)
            amount = (uint32)cap - earned;
    }

    if (amount == 0)
        return 0;

    std::string today = TodayStr();

    CharacterDatabase.DirectExecute(
        "INSERT INTO companion_caddy_points (guid, balance, total_earned) "
        "VALUES ({}, {}, {}) ON DUPLICATE KEY UPDATE "
        "balance = balance + {}, total_earned = total_earned + {}",
        guid, amount, amount, amount, amount);

    CharacterDatabase.DirectExecute(
        "INSERT INTO companion_caddy_daily (guid, date_str, points_earned) "
        "VALUES ({}, '{}', {}) ON DUPLICATE KEY UPDATE "
        "points_earned = points_earned + {}",
        guid, today, amount, amount);

    uint32 newBal = GetBalance(guid);

    std::string escRef = ref;
    CharacterDatabase.EscapeString(escRef);
    CharacterDatabase.Execute(
        "INSERT INTO companion_caddy_log "
        "(guid, type, delta, balance_after, ref) "
        "VALUES ({}, 'award', {}, {}, '{}')",
        guid, amount, newBal, escRef);

    return newBal;
}

static uint32 SpendPoints(uint32 guid, uint32 cost, const std::string& ref)
{
    uint32 bal = GetBalance(guid);
    if (bal < cost)
        return UINT32_MAX;

    CharacterDatabase.DirectExecute(
        "UPDATE companion_caddy_points "
        "SET balance = balance - {} "
        "WHERE guid = {} AND balance >= {}",
        cost, guid, cost);

    uint32 newBal = GetBalance(guid);
    if (newBal >= bal)
        return UINT32_MAX;

    std::string escRef = ref;
    CharacterDatabase.EscapeString(escRef);
    CharacterDatabase.Execute(
        "INSERT INTO companion_caddy_log "
        "(guid, type, delta, balance_after, ref) "
        "VALUES ({}, 'purchase', -{}, {}, '{}')",
        guid, cost, newBal, escRef);

    return newBal;
}

// ── Stationery helpers (mirrored verbatim from mod_tcg_vendors.cpp) ──────────

// Sets the item's readable text and the ITEM_FIELD_FLAG_READABLE flag so the
// client shows the right-click "Read" option and queries item_instance.text.
static void CaddyStampItemText(Item* item, Player* owner, const std::string& text)
{
    item->SetText(text);
    item->SetFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_READABLE);
    if (owner)
        item->SetState(ITEM_CHANGED, owner);
}

// Safety net: directly write item_instance.text after SaveToDB, because some
// AC forks omit m_text from the INSERT/UPDATE statement in Item::SaveToDB.
static void CaddyDirectWriteItemText(uint32 itemGuidLow, const std::string& text)
{
    std::string escaped = text;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE item_instance SET text = '{}' WHERE guid = {}",
        escaped, itemGuidLow);
}

// Creates a readable stationery scroll (item 9311) containing text.
static Item* CaddyCreateStationery(Player* owner, const std::string& text)
{
    Item* item = Item::CreateItem(9311, 1, owner);
    if (!item)
        return nullptr;
    CaddyStampItemText(item, nullptr, text);
    return item;
}

// Build the body text for the redemption-code mail, in the voice of the
// vendor the player will redeem it with, inviting them to that vendor's
// location. Landro Longshot is neutral and greets both factions; the
// faction-specific vendors only ever get picked for their own faction (see
// PickVendor), so their invite line doesn't need a faction branch.
static std::string BuildCodeMailBody(const CaddyVendor&  vendor,
                                     const std::string& playerName,
                                     const std::string& displayName,
                                     const std::string& code)
{
    std::string invite = (vendor.entry == VENDOR_LANDRO.entry)
        ? "Come find me in " + vendor.location + " -- Horde and Alliance "
          "adventurers are both welcome at my stall."
        : "Come find me in " + vendor.location + " to claim it.";

    return "Greetings, " + playerName + "!\n\n"
           "This is " + vendor.name + ". Word reached me that you've earned "
           "yourself a reward through the Companion Caddy promo shop.\n\n"
           "Your reward:\n"
           + displayName + "\n\n"
           "Redemption code:\n"
           + code + "\n\n"
           + invite + " Just show me this code and I'll get you all sorted out.\n\n"
           "This code is single-use and will be bound to your account upon "
           "redemption. Keep it safe!\n\n"
           "Good luck on your adventures in Azeroth!";
}

static std::string BuildMailEnvelopeBody(const std::string& playerName,
                                         const std::string& displayName)
{
    return "Greetings, " + playerName + "!\n\n"
           "See the attached scroll for your reward!\n\n"
           "If I'm right-- and I usually am about these sorts of things\n\n"
           "You should have a code to redeem a " + displayName + " waiting for you.";
}

// Code generation and mail delivery.
// Mirrors the GM send-code path in mod_tcg_vendors.cpp:
//   GenerateRandomCode() + InsertCodeToDatabase() + CreateStationeryWithText()
//   + MailDraft.AddItem.SendMailTo() + DirectWriteItemText()
//
// Returns true on success, false if TCG tables are absent or item creation fails.
static bool GenerateAndMailCode(Player*            player,
                                const std::string& rewardGroup,
                                const std::string& displayName)
{
    if (!s_tcgAvailable || !player)
        return false;

    const CaddyVendor& vendor = PickVendor(rewardGroup, player->GetTeamId());

    // ── 1. Generate code (same charset as GenerateRandomCode in TCG module) ──
    static const char alphanum[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    std::string raw;
    for (int i = 0; i < 16; ++i)
        raw += alphanum[urand(0, (uint32)(sizeof(alphanum) - 2))];
    std::string code = raw.substr(0,4) + "-" + raw.substr(4,4)
                     + "-" + raw.substr(8,4) + "-" + raw.substr(12,4);

    // ── 2. Insert into account_tcg_codes (unredeemed) ────────────────────────
    std::string escCode  = code;
    std::string escGroup = rewardGroup;
    CharacterDatabase.EscapeString(escCode);
    CharacterDatabase.EscapeString(escGroup);
    CharacterDatabase.Execute(
        "INSERT INTO account_tcg_codes "
        "(code, reward_group, redeemed, account_id, character_guid, redeemed_date) "
        "VALUES ('{}', '{}', 0, NULL, NULL, NULL)",
        escCode, escGroup);

    // ── 3. Create readable stationery scroll (item 9311) with code text ───────
    std::string body   = BuildCodeMailBody(vendor, player->GetName(), displayName, code);
    Item*       scroll = CaddyCreateStationery(player, body);
    if (!scroll)
    {
        LOG_ERROR("module",
            "mod-companion-caddy: Failed to create stationery for player {} "
            "(guid {}). Code {} was inserted but not mailed.",
            player->GetName(), player->GetGUID().GetCounter(), code);
        return false;
    }

    uint32 scrollGuidLow = scroll->GetGUID().GetCounter();

    // ── 4. Mail the scroll to the player ─────────────────────────────────────
    std::string envelope = BuildMailEnvelopeBody(player->GetName(), displayName);
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        scroll->SaveToDB(trans);
        MailDraft("Your Companion Caddy Promo Code: " + displayName, envelope)
            .AddItem(scroll)
            .SendMailTo(
                trans,
                MailReceiver(player, player->GetGUID().GetCounter()),
                MailSender(MAIL_CREATURE, vendor.entry, MAIL_STATIONERY_DEFAULT));
        CharacterDatabase.CommitTransaction(trans);
    }

    // ── 5. Safety-net direct write (for forks where SaveToDB omits m_text) ────
    CaddyDirectWriteItemText(scrollGuidLow, body);

    LOG_INFO("module",
        "mod-companion-caddy: Mailed promo code scroll to {} (guid {}) "
        "for item '{}' (group '{}', code {}).",
        player->GetName(), player->GetGUID().GetCounter(),
        displayName, rewardGroup, code);

    return true;
}

// ============================================================
//  State-sync helpers (see CaddyStatePushBuffer above and the
//  STATE_QUERY/STATE_PUSH_* branches of HandleCaddyMessage below).
// ============================================================

static uint32 ComputeStateChecksum(const std::string& blob)
{
    uint64 sum = 0;
    for (size_t i = 0; i < blob.size(); ++i)
        sum = (sum + static_cast<uint64>(static_cast<unsigned char>(blob[i])) * (i + 1)) % 1000000007ULL;
    return static_cast<uint32>(sum);
}

static std::vector<std::string> SplitIntoChunks(const std::string& data, size_t maxLen)
{
    std::vector<std::string> chunks;
    for (size_t i = 0; i < data.size(); i += maxLen)
        chunks.push_back(data.substr(i, maxLen));
    if (chunks.empty())
        chunks.push_back(std::string());
    return chunks;
}

static void SendStateDown(Player* player, uint32 revision, uint32 checksum, const std::string& blob)
{
    SendAddonMsg(player, "STATE_BEGIN|" + std::to_string(revision) + "|" + std::to_string(checksum));
    for (const auto& chunk : SplitIntoChunks(blob, 190))
        SendAddonMsg(player, "STATE_CHUNK:" + chunk);
    SendAddonMsg(player, "STATE_END");
}

// ============================================================
//  Build catalog message chunks.
// ============================================================
static std::vector<std::string> BuildCatalogChunks()
{
    std::vector<std::string> chunks;
    std::string cur;

    for (const auto& item : SHOP_CATALOG)
    {
        std::string entry = item.rewardGroup
            + "|" + item.displayName
            + "|" + std::to_string(item.pointCost)
            + "|" + item.category
            + "|" + std::to_string(item.itemEntry)
            + "|" + std::to_string(item.itemEntryAlt)
            + "|" + (item.factionSplit ? "1" : "0");

        if (!cur.empty() && (cur.size() + 1 + entry.size()) > 195)
        {
            chunks.push_back("CATALOG:" + cur);
            cur = entry;
        }
        else
        {
            if (!cur.empty()) cur += ";";
            cur += entry;
        }
    }
    if (!cur.empty())
        chunks.push_back("CATALOG:" + cur);

    chunks.push_back("CATALOG_END");
    return chunks;
}

// ============================================================
//  SYNC response helper
// ============================================================
static void SendSyncResponse(Player* player, uint32 guid)
{
    uint32 bal  = GetBalance(guid);
    int32  left = GetDailyLeft(guid);

    SendAddonMsg(player, "BALANCE|"
        + std::to_string(bal) + "|"
        + std::to_string(left) + "|"
        + (s_tcgAvailable ? "1" : "0") + "|"
        + (PromoPointsEnabled() ? "1" : "0"));

    for (const auto& chunk : BuildCatalogChunks())
        SendAddonMsg(player, chunk);
}

// ============================================================
//  Dispatch a CCADDY addon message from the client.
// ============================================================
static void HandleCaddyMessage(Player* player, const std::string& payload)
{
    if (payload.empty() || !player)
        return;

    uint32 guid = player->GetGUID().GetCounter();
    auto parts  = SplitStr(payload, '|');
    if (parts.empty())
        return;

    const std::string& cmd = parts[0];

    // ── SYNC ─────────────────────────────────────────────────────────────
    if (cmd == "SYNC")
    {
        SendSyncResponse(player, guid);
        return;
    }

    // ── SCAVENGE_START ───────────────────────────────────────────────────
    if (cmd == "SCAVENGE_START")
    {
        if (parts.size() < 3)
            return;
        uint32 companionSpellId = strtoul(parts[1].c_str(), nullptr, 10);
        if (companionSpellId == 0)
            return;
        std::string careTier = (parts.size() >= 4) ? parts[3] : "good";
        StartScavengeTimer(player, companionSpellId, parts[2], careTier);
        return;
    }

    // ── SCAVENGE_DONE ────────────────────────────────────────────────────
    if (cmd == "SCAVENGE_DONE")
    {
        if (parts.size() < 2)
            return;
        uint32 companionSpellId = strtoul(parts[1].c_str(), nullptr, 10);
        if (companionSpellId == 0)
            return;
        ClaimScavenge(player, companionSpellId);
        return;
    }

    // ── STATE_QUERY ──────────────────────────────────────────────────────
    if (cmd == "STATE_QUERY")
    {
        if (parts.size() < 2)
            return;
        uint32 clientRevision = strtoul(parts[1].c_str(), nullptr, 10);

        QueryResult r = CharacterDatabase.Query(
            "SELECT revision, checksum, data FROM companion_caddy_player_state "
            "WHERE guid = {}", guid);

        if (!r)
        {
            SendAddonMsg(player, "STATE_NONE");
            return;
        }

        Field* f = r->Fetch();
        uint32 revision = f[0].Get<uint32>();
        uint32 checksum = f[1].Get<uint32>();

        if (revision <= clientRevision)
        {
            SendAddonMsg(player, "STATE_CURRENT");
            return;
        }

        std::string data = f[2].Get<std::string>();
        SendStateDown(player, revision, checksum, data);
        return;
    }

    // ── STATE_PUSH_BEGIN / STATE_CHUNK: / STATE_PUSH_END ────────────────────
    if (cmd == "STATE_PUSH_BEGIN")
    {
        if (parts.size() < 3)
            return;
        CaddyStatePushBuffer buf;
        buf.revision = strtoul(parts[1].c_str(), nullptr, 10);
        buf.checksum = strtoul(parts[2].c_str(), nullptr, 10);
        s_statePushBuffers[guid] = buf;
        return;
    }

    if (payload.compare(0, 12, "STATE_CHUNK:") == 0)
    {
        auto it = s_statePushBuffers.find(guid);
        if (it != s_statePushBuffers.end())
            it->second.chunks.push_back(payload.substr(12));
        return;
    }

    if (cmd == "STATE_PUSH_END")
    {
        auto it = s_statePushBuffers.find(guid);
        if (it == s_statePushBuffers.end())
            return;

        CaddyStatePushBuffer buf = it->second;
        s_statePushBuffers.erase(it);

        std::string blob;
        for (const auto& chunk : buf.chunks)
            blob += chunk;

        if (ComputeStateChecksum(blob) != buf.checksum)
        {
            LOG_WARN("module",
                "mod-companion-caddy: player {} (guid {}) pushed a state "
                "blob that failed its checksum -- discarded, previous "
                "server-side copy (if any) left untouched.",
                player->GetName(), guid);
            return;
        }

        QueryResult existing = CharacterDatabase.Query(
            "SELECT revision FROM companion_caddy_player_state WHERE guid = {}", guid);
        if (existing && existing->Fetch()[0].Get<uint32>() >= buf.revision)
            return;

        std::string escBlob = blob;
        CharacterDatabase.EscapeString(escBlob);
        CharacterDatabase.Execute(
            "REPLACE INTO companion_caddy_player_state (guid, revision, checksum, data, updated_at) "
            "VALUES ({}, {}, {}, '{}', {})",
            guid, buf.revision, buf.checksum, escBlob, (uint32)time(nullptr));
        return;
    }

    // ── CHALLENGE ─────────────────────────────────────────────────────────
    // Client payload: CHALLENGE|<challengeType>
    // Points value supplied by client is ignored; server looks up award.
    if (cmd == "CHALLENGE")
    {
        if (parts.size() < 2)
            return;

        const std::string& ctype = parts[1];

        // Promo points disabled entirely (DailyCap < 0) — a correctly
        // behaving client won't even report challenges in this state (see
        // ReportChallengeToServer's promoEnabled guard), but if one does
        // anyway (stale state, old client, etc.), ignore it quietly rather
        // than emitting the daily_cap error. That error is meant for "cap
        // reached", not "feature off", and would otherwise spam the log.
        if (!PromoPointsEnabled())
            return;

        auto awardIt = CHALLENGE_AWARDS.find(ctype);
        if (awardIt == CHALLENGE_AWARDS.end())
            return;  // unknown type — silently ignore

        uint32 award = awardIt->second;

        // Per-type cooldown check (in-memory, resets on relog)
        time_t  now     = time(nullptr);
        uint32  cdSecs  = GetCooldown();
        auto&   cdMap   = s_cooldowns[guid];
        auto    cdIt    = cdMap.find(ctype);
        if (cdIt != cdMap.end() && (now - cdIt->second) < (time_t)cdSecs)
            return;  // too soon — silently ignore (not an error)

        uint32 newBal = AwardPoints(guid, award, ctype);

        // Update cooldown entry
        cdMap[ctype] = now;

        int32 left = GetDailyLeft(guid);

        if (newBal == 0 && award > 0)
        {
            // Numeric cap (cap > 0) was already hit before this call.
            // PromoPointsEnabled() was already confirmed true above, so
            // this can only mean the cap itself was reached, never that
            // the feature is off.
            uint32 bal = GetBalance(guid);
            SendAddonMsg(player, "BALANCE|"
                + std::to_string(bal) + "|0");
            SendAddonMsg(player, "ERR|daily_cap");
            return;
        }

        SendAddonMsg(player, "BALANCE|"
            + std::to_string(newBal) + "|"
            + std::to_string(left));
        return;
    }

    // ── PURCHASE ──────────────────────────────────────────────────────────
    // Client payload: PURCHASE|<rewardGroup>
    if (cmd == "PURCHASE")
    {
        if (parts.size() < 2)
            return;

        const std::string& rg = parts[1];

        // Find item in catalog (server validates — client cannot forge a price)
        const CaddyItem* item = nullptr;
        for (const auto& ci : SHOP_CATALOG)
        {
            if (ci.rewardGroup == rg)
            {
                item = &ci;
                break;
            }
        }

        if (!item)
        {
            SendAddonMsg(player, "ERR|unknown_item");
            return;
        }

        if (!s_tcgAvailable)
        {
            SendAddonMsg(player, "ERR|tcg_unavailable");
            return;
        }

        // Atomic deduct
        uint32 newBal = SpendPoints(guid, item->pointCost,
                                    "purchase:" + rg);
        if (newBal == UINT32_MAX)
        {
            uint32 bal = GetBalance(guid);
            SendAddonMsg(player, "BALANCE|"
                + std::to_string(bal) + "|0");
            SendAddonMsg(player, "ERR|insufficient_points");
            return;
        }

        if (!GenerateAndMailCode(player, rg, item->displayName))
        {
            // Mail delivery failed — refund the points.
            AwardPoints(guid, item->pointCost, "refund:" + rg);
            SendAddonMsg(player, "ERR|code_gen_failed");
            return;
        }

        int32 left = GetDailyLeft(guid);

        // Notify the addon that a code was mailed (no raw code over the wire).
        SendAddonMsg(player, "CODE_MAILED|" + rg);
        SendAddonMsg(player, "BALANCE|"
            + std::to_string(newBal) + "|"
            + std::to_string(left));
        return;
    }
}

// ============================================================
//  Try to extract and handle a CCADDY message from msg.
// ============================================================
static bool TryHandleCaddy(Player* player, const std::string& msg)
{
    if (!player)
        return true;

    const size_t prefixLen = CADDY_PREFIX.size();

    if (msg.size() > prefixLen
        && msg.compare(0, prefixLen, CADDY_PREFIX) == 0
        && msg[prefixLen] == '\t')
    {
        HandleCaddyMessage(player, msg.substr(prefixLen + 1));
        return false;
    }

    if (msg == "SYNC"
        || (msg.size() > 10 && msg.compare(0, 10, "CHALLENGE|") == 0)
        || (msg.size() > 9  && msg.compare(0, 9,  "PURCHASE|") == 0)
        || (msg.size() > 15 && msg.compare(0, 15, "SCAVENGE_START|") == 0)
        || (msg.size() > 14 && msg.compare(0, 14, "SCAVENGE_DONE|") == 0))
    {
        HandleCaddyMessage(player, msg);
        return false;
    }

    return true;
}

// ============================================================
//  Deferred critter despawn
// ============================================================
class CaddyCritterDespawnEvent : public BasicEvent
{
public:
    explicit CaddyCritterDespawnEvent(ObjectGuid playerGuid) : _playerGuid(playerGuid) {}

    bool Execute(uint64 /*e_time*/, uint32 /*p_time*/) override
    {
        if (Player* player = ObjectAccessor::FindPlayer(_playerGuid))
        {
            if (ObjectGuid critterGuid = player->GetCritterGUID())
            {
                if (Creature* critter = ObjectAccessor::GetCreature(*player, critterGuid))
                    critter->DespawnOrUnsummon();
            }
        }
        return true;
    }

private:
    ObjectGuid _playerGuid;
};

// ============================================================
//  PlayerScript
// ============================================================
class CaddyPlayerScript : public PlayerScript
{
public:
    CaddyPlayerScript() : PlayerScript("CaddyPlayerScript") {}

    void OnPlayerLogin(Player* player) override
    {
        if (!player) return;
        uint32 guid = player->GetGUID().GetCounter();
        s_cooldowns.erase(guid);   // clear stale in-memory cooldowns

        QueryResult r = CharacterDatabase.Query(
            "SELECT companion_spell_id FROM companion_caddy_scavenges "
            "WHERE guid = {} AND end_time > {}",
            guid, (uint32)time(nullptr));
        if (r)
        {
            auto& set = s_activeScavenges[guid];
            do
            {
                set.insert(r->Fetch()[0].Get<uint32>());
            } while (r->NextRow());
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!player) return;
        uint32 guid = player->GetGUID().GetCounter();
        s_cooldowns.erase(guid);
        s_activeScavenges.erase(guid);
        s_statePushBuffers.erase(guid);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/,
                             std::string& msg) override
    {
        return TryHandleCaddy(player, msg);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/,
                             std::string& msg, Player* /*receiver*/) override
    {
        return TryHandleCaddy(player, msg);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!player || !spell) return;

        SpellInfo const* info = spell->GetSpellInfo();
        if (!info) return;

        auto it = s_activeScavenges.find(player->GetGUID().GetCounter());
        if (it == s_activeScavenges.end() || it->second.count(info->Id) == 0)
            return;   // not a spell we care about

        player->m_Events.AddEvent(new CaddyCritterDespawnEvent(player->GetGUID()),
            player->m_Events.CalculateTime(1));

        ChatHandler(player->GetSession()).SendSysMessage(
            "That companion is out scavenging and can't be summoned yet.");
    }
};

// ============================================================
//  WorldScript  —  startup: create tables, detect TCG module
// ============================================================
class CaddyWorldScript : public WorldScript
{
public:
    CaddyWorldScript() : WorldScript("CaddyWorldScript") {}

    void OnStartup() override
    {
        LOG_INFO("module", "mod-companion-caddy: OnStartup fired.");
        // Point balance table
        CharacterDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `companion_caddy_points` ("
            "  `guid`         INT UNSIGNED NOT NULL,"
            "  `balance`      INT UNSIGNED NOT NULL DEFAULT 0,"
            "  `total_earned` INT UNSIGNED NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
            "  COMMENT='Companion Caddy: per-character promo point balances'");

        // Daily cap tracker (one row per character per calendar day)
        CharacterDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `companion_caddy_daily` ("
            "  `guid`          INT UNSIGNED NOT NULL,"
            "  `date_str`      VARCHAR(10)  NOT NULL DEFAULT '',"
            "  `points_earned` INT UNSIGNED NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (`guid`, `date_str`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
            "  COMMENT='Companion Caddy: daily point earning cap tracking'");

        // Full transaction log
        CharacterDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `companion_caddy_log` ("
            "  `id`            INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
            "  `guid`          INT UNSIGNED NOT NULL,"
            "  `type`          ENUM('award','purchase','refund') NOT NULL,"
            "  `delta`         INT          NOT NULL,"
            "  `balance_after` INT UNSIGNED NOT NULL,"
            "  `ref`           VARCHAR(64)  DEFAULT NULL,"
            "  `created_at`    TIMESTAMP    DEFAULT CURRENT_TIMESTAMP,"
            "  KEY `idx_guid` (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
            "  COMMENT='Companion Caddy: point transaction audit log'");

        // Server-authoritative scavenge timers
        CharacterDatabase.Execute(
            "CREATE TABLE IF NOT EXISTS `companion_caddy_scavenges` ("
            "  `guid`               INT UNSIGNED NOT NULL,"
            "  `companion_spell_id` INT UNSIGNED NOT NULL,"
            "  `tier`               VARCHAR(10)  NOT NULL,"
            "  `start_time`         INT UNSIGNED NOT NULL,"
            "  `end_time`           INT UNSIGNED NOT NULL,"
            "  PRIMARY KEY (`guid`, `companion_spell_id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
            "  COMMENT='Companion Caddy: server-authoritative scavenge timers'");

        // Detect mod-tcg-vendors tables
        QueryResult r = CharacterDatabase.Query(
            "SHOW TABLES LIKE 'account_tcg_codes'");
        s_tcgAvailable = (r != nullptr);

        if (s_tcgAvailable)
            LOG_INFO("module",
                "mod-companion-caddy: TCG module detected — promo shop enabled.");
        else
            LOG_WARN("module",
                "mod-companion-caddy: account_tcg_codes not found. "
                "Install mod-tcg-vendors to enable the promo shop. "
                "Point tracking is still active.");
    }
};

// ============================================================
//  Registration
// ============================================================
void Addmod_companion_caddyScripts()
{
    new CaddyWorldScript();
    new CaddyPlayerScript();
}
