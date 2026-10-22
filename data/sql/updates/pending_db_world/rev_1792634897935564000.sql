-- Firemaw, Ebonroc and Flamegor must be pulled directly, rather than recruited by nearby trash through assistance calls.
-- CREATURE_FLAG_EXTRA_IGNORE_ALL_ASSISTANCE_CALLS preserves proximity aggro and direct attacks.
UPDATE `creature_template` SET `flags_extra` = `flags_extra` | 0x04000000 WHERE `entry` IN (11983, 14601, 11981);
