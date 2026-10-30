-- Evermore: characters.challenges grows to 32 bits, so mod-challenges' challenges past the first
-- seven (Boaring Adventure, Exhaustion) reach the character select screen: the challenges running
-- (or finished) in the low 16 bits, the ones a death ended (or finished) in the high 16. The old
-- layout had them in a byte each; the values move along with the column. mod-challenges writes
-- the column again from its own table at every startup. Only runs while the column is still
-- SMALLINT, so the file can run again.
SET @old_layout := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'characters' AND COLUMN_NAME = 'challenges' AND DATA_TYPE = 'smallint');

SET @widen := IF(@old_layout = 1, 'ALTER TABLE `characters` MODIFY COLUMN `challenges` INT UNSIGNED NOT NULL DEFAULT 0', 'DO 0');
PREPARE widen FROM @widen;
EXECUTE widen;
DEALLOCATE PREPARE widen;

SET @move := IF(@old_layout = 1, 'UPDATE `characters` SET `challenges` = (`challenges` & 255) | ((`challenges` >> 8) << 16) WHERE `challenges` > 255', 'DO 0');
PREPARE move FROM @move;
EXECUTE move;
DEALLOCATE PREPARE move;
