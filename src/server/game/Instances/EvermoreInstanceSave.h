/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVERMORE_INSTANCE_SAVE_H
#define EVERMORE_INSTANCE_SAVE_H

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace Evermore
{
// Evermore keeps its pre-integration on-disk format in both directions. This
// involution maps legacy saves to upstream state, and upstream state back to
// legacy saves, so existing lockouts and rollback retain their meaning.
inline std::string ConvertInstanceSave(char const* data, char firstHeader, char secondHeader,
    std::size_t bossCount, std::size_t persistentCount, bool eye)
{
    if (!data)
        return {};

    std::istringstream input(data);
    char first = 0;
    char second = 0;
    input >> first >> second;
    if (first != firstHeader || second != secondHeader)
        return {};

    std::vector<uint32_t> values(bossCount + persistentCount);
    for (uint32_t& value : values)
        if (!(input >> value))
            return {};

    // These scripts have no ReadSaveDataMore fields. Reject truncated, corrupt,
    // or unexpectedly extended input rather than partially loading boss states.
    input >> std::ws;
    if (!input.eof())
        return {};

    if (eye)
        std::swap(values[1], values[2]);
    else
        values[bossCount + 2] = values[bossCount + 2] ? 0 : 1;

    std::ostringstream output;
    output << first << ' ' << second << ' ';
    for (uint32_t value : values)
        output << value << ' ';
    return output.str();
}

inline std::string ConvertEyeSave(char const* data)
{
    return ConvertInstanceSave(data, 'T', 'E', 4, 0, true);
}

inline std::string ConvertUlduarSave(char const* data)
{
    return ConvertInstanceSave(data, 'U', 'U', 14, 8, false);
}
}
#endif
