-- mod-companion-caddy: point tracking tables
-- Apply to: characters database
--
-- NOTE: The worldserver also runs CREATE TABLE IF NOT EXISTS on startup,
-- so this file only needs to be applied manually if you prefer explicit
-- schema management over auto-creation.

-- Per-character promo point balances
CREATE TABLE IF NOT EXISTS `companion_caddy_points` (
    `guid`         INT UNSIGNED NOT NULL,
    `balance`      INT UNSIGNED NOT NULL DEFAULT 0,
    `total_earned` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='Companion Caddy: per-character promo point balances';

-- Daily earning cap tracker (one row per character per calendar day)
CREATE TABLE IF NOT EXISTS `companion_caddy_daily` (
    `guid`          INT UNSIGNED NOT NULL,
    `date_str`      VARCHAR(10)  NOT NULL DEFAULT '',
    `points_earned` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `date_str`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='Companion Caddy: daily point earning cap tracking';

-- Full transaction audit log
CREATE TABLE IF NOT EXISTS `companion_caddy_log` (
    `id`            INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    `guid`          INT UNSIGNED NOT NULL,
    `type`          ENUM('award','purchase','refund') NOT NULL,
    `delta`         INT          NOT NULL,
    `balance_after` INT UNSIGNED NOT NULL,
    `ref`           VARCHAR(64)  DEFAULT NULL,
    `created_at`    TIMESTAMP    DEFAULT CURRENT_TIMESTAMP,
    KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='Companion Caddy: point transaction audit log';

-- Server-authoritative scavenge timers. One row per (character,
-- companion) pair with a scavenge currently in progress; deleted on
-- claim. start_time/end_time are server clock readings (unix seconds),
-- not anything the client supplies -- this is what lets the server
-- verify a completed-scavenge claim independently of the addon's own
-- (client-editable) SavedVariables timer. care_tier is the qualitative
-- Need-bar tier the addon reports at scavenge start ("struggling"/
-- "okay"/"good"/"thriving") -- see GetScavengeCareMult -- accepted on
-- the same trust basis as `tier` itself; see the comment on
-- GetScavengeCareMult for why that's an acceptable trade-off here.
CREATE TABLE IF NOT EXISTS `companion_caddy_scavenges` (
    `guid`               INT UNSIGNED NOT NULL,
    `companion_spell_id` INT UNSIGNED NOT NULL,
    `tier`               VARCHAR(10)  NOT NULL,
    `care_tier`          VARCHAR(10)  NOT NULL DEFAULT 'good',
    `start_time`         INT UNSIGNED NOT NULL,
    `end_time`           INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`, `companion_spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='Companion Caddy: server-authoritative scavenge timers';

-- The server's durable, cross-device copy of everything the addon
-- tracks locally in SavedVariables (companions, Treats, Care Item
-- inventory, visited-zone history, in-progress scavenges) -- one row per
-- character. `data` is an opaque blob in the addon's own flat delimited
-- format (see CC:SerializePlayerData in Core.lua); the server never
-- parses it, only stores and forwards it verbatim. `revision` is a
-- monotonically increasing counter the client supplies with each push --
-- NOT a timestamp -- used to decide whose copy is newer (see
-- HandleCaddyMessage's STATE_QUERY/STATE_PUSH_END handling); `checksum`
-- guards only against transport corruption/truncation across a chunked
-- push or pull, not against a client simply lying about its own data --
-- see the header comment on CC.STATE_CHUNK_SIZE in Core.lua for why that
-- trust boundary is an acceptable, deliberate one for this feature.
CREATE TABLE IF NOT EXISTS `companion_caddy_player_state` (
    `guid`         INT UNSIGNED  NOT NULL,
    `revision`     INT UNSIGNED  NOT NULL DEFAULT 0,
    `checksum`     INT UNSIGNED  NOT NULL DEFAULT 0,
    `data`         MEDIUMTEXT    NOT NULL,
    `updated_at`   INT UNSIGNED  NOT NULL,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  COMMENT='Companion Caddy: cross-device authoritative state blob per character';
