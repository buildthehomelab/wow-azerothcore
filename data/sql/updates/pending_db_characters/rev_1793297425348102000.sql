-- Evermore: what the character select screen shows next to each character, sent in the character
-- list's per-character marker (Player::BuildEnumData). `favourite` is set from the launcher (the
-- portal's character order); `challenges` is kept by mod-challenges: the running challenges in the
-- low byte, the ones a death ended in the high byte, a finished Hardcore as both 0x0001 and 0x0100.
-- Each column is added only when it isn't there yet, so the file can run again.
SET @has_column := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'characters' AND COLUMN_NAME = 'favourite');
SET @add_column := IF(@has_column = 0, 'ALTER TABLE `characters` ADD COLUMN `favourite` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `order`', 'DO 0');
PREPARE add_column FROM @add_column;
EXECUTE add_column;
DEALLOCATE PREPARE add_column;

SET @has_column := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'characters' AND COLUMN_NAME = 'challenges');
SET @add_column := IF(@has_column = 0, 'ALTER TABLE `characters` ADD COLUMN `challenges` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `favourite`', 'DO 0');
PREPARE add_column FROM @add_column;
EXECUTE add_column;
DEALLOCATE PREPARE add_column;
