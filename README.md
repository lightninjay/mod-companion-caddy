<div align="center">
  <img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/banner.png" alt="Companion Caddy Banner" width="800px">
  <H1><b>Companion Caddy</b></H1><H3>Author: lightninjay<br>with the help of Claude.ai</H3><br>

Inspired by the [Murloc Minder](https://www.curseforge.com/wow/addons/murloc-minder) addon developed by Kagrok

An [AzerothCore](https://www.azerothcore.org/) module and companion World of Warcraft addon
for the 3.3.5a client that turns summoned companion pets into something worth looking after:
feed, play with, rest, and clean your companion to keep it out of neglect, send it out
scavenging for bonus loot, and complete daily challenges to earn treats, or if the server has
[mod-tcg-vendors](https://github.com/lightninjay/mod-tcg-vendors) enabled, server-tracked **Promo
Points** redeemable in an optional in-game shop for TCG and Promo items that are otherwise unobtainable in-game.

| Component | Path | What it is |
|-----------|------|------------|
| **mod-companion-caddy** | `mod-companion-caddy/Root of the Repository` | AzerothCore C++ module — server-authoritative point tracking, scavenge resolution, and promo shop fulfillment |
| **CompanionCaddy** | `mod-companion-caddy/CompanionCaddy-Addon.zip/CompanionCaddy` | Client addon — companion care UI, challenge tracking, minimap button, and the promo shop panel |

The addon is presentation and challenge-tracking only. It can be used on any 3.3.5a private server, and it will failback to whatever it is capable of interacting with. At a minimum,
the addon will let you earn treats to be able to buy in-addon items to care for your companions more effectively.

> **Note:** Developed against the
> [mod-playerbots fork](https://github.com/liyunfan1223/azerothcore-wotlk) of AzerothCore.
> Should be compatible with standard AzerothCore mainline. See
> [Compatibility](#compatibility) for details.

---

## Screenshots

<div align="center">

<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-Habitat.jpg" alt="Habitat panel" width="1080px"><br><br>
<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-Store.jpg" alt="Store panel" width="1080px"><br><br>
<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-Info.jpg" alt="Info panel" width="1080px"><br><br>
<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-customization.jpg" alt="Customization panel" width="1080px"><br><br>
<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-promo.jpg" alt="Promo panel" width="1080px"><br><br>
<img src="https://raw.githubusercontent.com/lightninjay/mod-companion-caddy/refs/heads/main/screenshots/screenshot-config.jpg" alt="Config options" width="1080px">

</div>

---

## Features

### Companion care

- Four tracked Need bars — **Hunger, Joy, Energy, Cleanliness** — that decay over time and are
  restored by feeding, playing, resting, and cleaning your active companion.
- A four-tier care rating (**Struggling → Okay → Good → Thriving**) computed from those Needs,
  which feeds directly into scavenge loot odds (see below).
- Neglect consequences — if every Need bar bottoms out, the companion needs to fully re-bond
  with you rather than simply picking back up where it left off.
- A sustained-care streak system that rewards keeping a companion in the Thriving tier over
  time, decaying gradually (not resetting outright) on a single drop out of it.
- Occasional spontaneous care events while a companion is Good/Thriving.

### Scavenging

- Send your companion out on a timed scavenge in one of three tiers — green (30 min), blue
  (1 hr), purple (2 hr) — for a client-side Treats reward plus a server-rolled chance at bonus
  loot from a per-tier, per-server item pool.
- Bonus loot odds are multiplied by the companion's care tier at the moment it was sent out
  (Struggling 0.5× up to Thriving 1.4× by default), so a well-kept companion scavenges better.
- A "scavenge squad" variant challenge type for coordinated/group scavenging.

### Daily challenges → Promo Points

- Around 50 tracked challenge types spanning companion care, world activity, gathering and
  professions, class abilities, and social actions — see [Challenge Types](#challenge-types)
  for the full list.
- Per-challenge-type cooldown (10 minutes by default) prevents spamming the same action for
  repeat rewards.
- A configurable daily point cap that can run **capped**, **uncapped**, or **fully disabled**
  — see [Configuration](#configuration).

### Promo shop

- An optional in-game shop, populated from a server-side catalog, that mails redemption codes
  to the player rather than handling raw codes client-side.
- Automatically enabled/disabled based on whether [mod-tcg-vendors](https://github.com/lightninjay/mod-tcg-vendors) is
  installed (detected via the `account_tcg_codes` table) — if it's absent, or if
  `CompanionCaddy.DailyCap` is set to disable Promo Points outright, the shop tab loads itself
  into a greyed out state, and stops reporting promo points entirely instead of erroring.

### Addon UI

- A tabbed main window: **Habitat** (care actions and Need bars), **Store** (Treats economy),
  **Customization**, **Info**, and **Promo** (the shop, when active).
- A draggable minimap button, togglable independently via `/cc minimap`.
- Slash commands for everything below — no dependency on a separate config addon.

---

## Requirements

- [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) (3.3.5a branch)
- A `characters` database Companion Caddy can create its own tables in (done automatically on
  first boot)
- Optional: [mod-tcg-vendors](https://github.com/) — if its `account_tcg_codes` table is
  present, the promo shop is enabled automatically; otherwise Companion Caddy still tracks
  care and challenges with the shop items simply hidden

---

## Installation

### Server module

1. Clone this repo's `mod-companion-caddy/` folder into your AzerothCore `modules/` directory.
2. Re-run CMake and rebuild `worldserver`.
3. Copy `mod-companion-caddy/conf/mod-companion-caddy.conf.dist` to your server's `configs/`
   (or `configs/modules/`) directory as `mod-companion-caddy.conf`, and adjust values as needed.
4. Start `worldserver` — the module creates its own `characters` DB tables on first launch.

### Client addon

1. Copy the `CompanionCaddy-Addon.zip` file into your WoW 3.3.5a client's `Interface/AddOns/`
   directory and extract the `CompanionCaddy` folder into the same Interface/AddOns directory.
2. Enable **Companion Caddy** at the character select AddOns list.

---

## In-game usage

| Command | Effect |
|---------|--------|
| `/companioncaddy`, `/caddy`, `/cc` | Open the main panel |
| `/cc hide` / `/cc close` | Close the main panel |
| `/cc toggle` | Toggle the main panel |
| `/cc config` / `/cc options` | Open settings |
| `/cc minimap` | Toggle the minimap button |
| `/cc tutorial` | Show the in-addon tutorial |
| `/cc debug` | Print current companion state and Promo Point balance to chat |

---

## Configuration

Set in `mod-companion-caddy.conf`:

| Option | Default | Notes |
|--------|---------|-------|
| `CompanionCaddy.DailyCap` | `200` | Max Promo Points earnable per calendar day. `> 0` = normal cap · `0` = unlimited earning, feature stays fully on · `-1` = Promo Points disabled entirely — earning, the daily-cap log message, and the shop all turn off, and the addon stops reporting challenges rather than spamming errors. Useful on a server where a TCG mode of Free makes Promo Points pointless. |
| `CompanionCaddy.ChallengeCooldown` | `600` | Seconds between repeat point awards for the same challenge type. Tracked in-memory; resets on relog. |
| `CompanionCaddy.ScavengeLootEnable` | `1` | Master switch for bonus scavenge loot. Separate from the Treats reward, which is client-side and always happens. |
| `CompanionCaddy.ScavengeLootChanceGreen` / `*Blue` / `*Purple` | `20` / `12` / `6` | Percent chance (0–100) of bonus loot per scavenge tier. |
| `CompanionCaddy.ScavengeLootPoolGreen` / `*Blue` / `*Purple` | *(empty)* | Comma-separated `item_template` IDs each tier can drop. Left blank by default — populate with IDs that exist, and make sense for the quality tier, on your own server's DB. Unparseable entries are logged and skipped. |
| `CompanionCaddy.ScavengeCareMultStruggling` / `*Okay` / `*Good` / `*Thriving` | `0.5` / `0.8` / `1.0` / `1.4` | Multiplier on `ScavengeLootChance*` based on the companion's care tier at send-off time. Final chance is clamped 0–100. |

See the comments in `mod-companion-caddy.conf.dist` for the authoritative, up-to-date list.

---

## Challenge Types

<details>
<summary><strong>Full list of tracked challenges and their point value</strong></summary>

| Category | Challenge types |
|----------|------------------|
| **Companion care** | `feed`, `play`, `rest`, `clean`, `soothe`, `warm` (5 pts each) · `scavenge` (15) · `scavenge_squad` (30) · `summon_companion` (10) · `learn_companion` (15) |
| **World & travel** | `zone_activity` (8) · `explore_zone` (10) · `visit_major_city` (10) · `visit_inn` (8) · `use_hearthstone` (12) · `use_flight_path` (8) · `use_mount` (8) · `swim_short` (10) · `swim_long` (20) · `group_up` (10) |
| **Quests & combat** | `quest_turnin` (15) · `dungeon_complete` (30) · `leave_combat` (8) · `return_from_death` (10) · `honorable_kill` (15) · `duel_request` (10) · `earn_achievement` (20) · `level_up` (50) |
| **Gathering & professions** | `craft_anything` (12) · `cook_food` (12) · `fish_zone` (15) · `gather_herb` (15) · `gather_ore` (15) · `gather_skin` (12) |
| **Vendors & economy** | `visit_vendor` (10) · `visit_bank` (10) · `visit_trainer` (10) · `visit_auction_house` (8) · `check_mail` (8) · `trade_player` (8) · `loot_gold` (6) · `loot_rare_item` (18) |
| **Consumables & utility** | `use_bandage` (10) · `use_potion` (8) · `drink_any` (5) · `create_fire` (8) · `roll_dice` (6) |
| **Class abilities** | `dk_ability` (8) · `druid_shapeshift` (10) · `hunter_feign_death` (8) · `mage_conjure` (8) · `paladin_blessing` (8) · `priest_heal` (10) · `rogue_stealth` (8) · `shaman_totem` (8) · `warlock_summon_pet` (8) · `warrior_charge` (8) |
| **Social** | `emote` (5) · `emote_dance`, `emote_salute`, `emote_wave`, `emote_laugh`, `emote_bow`, `emote_roar`, `emote_cheer`, `emote_thank`, `emote_cry`, `emote_yawn`, `emote_clap` (6–8) · `emote_sleep` (8) · `guild_chat` (6) |
| **Reputation** | `gain_reputation` (12) |

Awards live in `CHALLENGE_AWARDS` in `src/mod_companion_caddy.cpp` — the table above is a
categorized view of that source for readability.

</details>

---

## Protocol

Client and server exchange pipe-delimited messages over the `CCADDY` addon prefix. Key
message shapes:

| Message | Direction | Meaning |
|---------|-----------|---------|
| `CHALLENGE\|<type>` | Addon → Server | Report a completed challenge for validation and award |
| `BALANCE\|<bal>\|<left>\|<tcgReady>\|<promoEnabled>` | Server → Addon | Current point balance, points left today (`-1` = unlimited), whether TCG delivery is available, and whether Promo Points are enabled at all |
| `PURCHASE\|<rewardGroup>` | Addon → Server | Request a shop redemption; server validates balance and mails a code |
| `ERR\|<code>` | Server → Addon | Error/state code (e.g. `daily_cap`) — only sent for a genuine cap hit, never when Promo Points are simply disabled |

The server is the source of truth for balances, catalog contents, and scavenge timers; the
addon renders what it's told and reports challenge completions and scavenge care tier for the
server to validate and roll against.

---

## Compatibility

| Core | Status |
|------|--------|
| [mod-playerbots fork](https://github.com/liyunfan1223/azerothcore-wotlk) | ✅ Developed and tested here |
| [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) mainline | ✅ Should be compatible |

---

## File Index

| File | Description |
|------|--------------|
| `mod-companion-caddy/src/mod_companion_caddy.cpp` | All server-side C++ logic — point tracking, scavenge resolution, promo shop fulfillment |
| `mod-companion-caddy/conf/mod-companion-caddy.conf.dist` | Configuration template — copy and remove `.dist` to activate |
| `mod-companion-caddy/data/sql/characters/base/create_tables.sql` | Creates Companion Caddy's `characters` DB tables |
| `mod-companion-caddy/CompanionCaddy-Addon.zip/CompanionCaddy/CompanionCaddy.toc` | Addon manifest |
| `mod-companion-caddy/CompanionCaddy-Addon.zip/CompanionCaddy/Core.lua` | Care system, pet state, challenge tracking, minimap, slash commands, server comms |
| `mod-companion-caddy/CompanionCaddy-Addon.zip/CompanionCaddy/UI.lua` | Main window, tabs, and promo shop panel |

---

## Credits

- [AzerothCore](https://www.azerothcore.org/) — the core emulator and module framework.
- The AzerothCore community for module conventions and reference implementations.

---

## License

This module and addon are released under the [MIT License](LICENSE).

---

*AzerothCore: [repository](https://github.com/azerothcore/azerothcore-wotlk) •
[website](https://www.azerothcore.org/) •
[Discord](https://discord.gg/gkt4y2x)*

</div>
