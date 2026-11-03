-- Evermore: the home screen. characters.home_viewer marks an account's viewer, the character it
-- lands on after logging in (0 a normal character, 1 a viewer, 2 a viewer turned off). Only adds the
-- column if it isn't there, so the file can run again.
SET @has_column := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'characters' AND COLUMN_NAME = 'home_viewer');

SET @add_column := IF(@has_column = 0, 'ALTER TABLE `characters` ADD COLUMN `home_viewer` TINYINT UNSIGNED NOT NULL DEFAULT 0', 'SELECT 1');
PREPARE add_column FROM @add_column;
EXECUTE add_column;
DEALLOCATE PREPARE add_column;
