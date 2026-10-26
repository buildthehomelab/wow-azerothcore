-- Evermore: each account's warband camp on the character select screen (0 not chosen, 1 the era
-- camp, 2 the forest camp), set from the screen and sent back in the character list.
CREATE TABLE IF NOT EXISTS `account_warband` (
  `account` INT UNSIGNED NOT NULL,
  `camp` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
