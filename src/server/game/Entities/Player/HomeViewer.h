/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HomeViewer_h__
#define HomeViewer_h__

#include "CharacterCache.h"
#include "ObjectGuid.h"
#include "Player.h"

// Evermore: the home screen keeps one character row per account that nobody plays (Player::IsHomeViewer,
// characters.home_viewer). This is the one way for code outside the core to leave those out. A
// module that should still build on other cores includes it behind __has_include:
//
//     #if __has_include("HomeViewer.h")
//     #include "HomeViewer.h"
//     #else
//     #define SQL_NOT_HOME_VIEWER "1"
//     inline bool IsHomeViewer(Player const*) { return false; }
//     #endif

// For the WHERE of a query on characters: "... WHERE account = {} AND " SQL_NOT_HOME_VIEWER
#define SQL_NOT_HOME_VIEWER "home_viewer = 0"

inline bool IsHomeViewer(Player const* player)
{
    return player && player->IsHomeViewer();
}

// By row, for a character that may be offline.
inline bool IsHomeViewer(ObjectGuid guid)
{
    return sCharacterCache->IsHomeViewer(guid);
}

#endif // HomeViewer_h__
