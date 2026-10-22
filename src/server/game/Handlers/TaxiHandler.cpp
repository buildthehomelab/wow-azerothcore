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

#include "GameTime.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WaypointMovementGenerator.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace
{
    // 3D length of every TaxiPath, the way the client weighs a leg when it picks a route; -1 for a path
    // the flight can't use as a middle leg (FlightPathMovementGenerator::LoadPath drops a middle leg's
    // first two and last points, so it needs at least four, and a gap in the points breaks it)
    std::vector<float> const& GetTaxiPathLengths()
    {
        static std::vector<float> const lengths = []()
        {
            std::vector<float> result(sTaxiPathNodesByPath.size(), 0.0f);
            for (std::size_t pathId = 0; pathId < sTaxiPathNodesByPath.size(); ++pathId)
            {
                TaxiPathNodeList const& points = sTaxiPathNodesByPath[pathId];
                if (points.size() < 4 || std::find(points.begin(), points.end(), nullptr) != points.end())
                {
                    result[pathId] = -1.0f;
                    continue;
                }
                for (std::size_t i = 1; i < points.size(); ++i)
                    if (points[i - 1]->mapid == points[i]->mapid)
                        result[pathId] += std::sqrt(
                            (points[i]->x - points[i - 1]->x) * (points[i]->x - points[i - 1]->x) +
                            (points[i]->y - points[i - 1]->y) * (points[i]->y - points[i - 1]->y) +
                            (points[i]->z - points[i - 1]->z) * (points[i]->z - points[i - 1]->z));
            }
            return result;
        }();
        return lengths;
    }

    // The client's route counts only flown distance. Picks the route again over the flight masters the
    // player knows, with every stop worth stopPenalty extra yards. Returns an empty route if there is none.
    std::vector<uint32> FindTaxiRouteWithFewerStops(Player* player, uint32 from, uint32 to, float stopPenalty)
    {
        TaxiNodesEntry const* start = sTaxiNodesStore.LookupEntry(from);
        if (!start)
            return {};

        TaxiMask const& teamMask = player->GetTeamId(true) == TEAM_ALLIANCE ? sAllianceTaxiNodesMask : sHordeTaxiNodesMask;
        auto usable = [&](uint32 node)
        {
            if (node == to)
                return true;
            TaxiNodesEntry const* entry = sTaxiNodesStore.LookupEntry(node);
            if (!entry || entry->map_id != start->map_id)
                return false;
            uint8 field = uint8((node - 1) / 32);
            uint32 submask = 1 << ((node - 1) % 32);
            if (field >= TaxiMaskSize || !(teamMask[field] & submask))
                return false;
            return player->isTaxiCheater() || player->m_taxi.IsTaximaskNodeKnown(node);
        };

        std::vector<float> const& lengths = GetTaxiPathLengths();
        std::map<uint32, float> cost;
        std::map<uint32, uint32> previous;
        using Entry = std::pair<float, uint32>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        cost[from] = 0.0f;
        queue.emplace(0.0f, from);
        while (!queue.empty())
        {
            auto [nodeCost, node] = queue.top();
            queue.pop();
            if (node == to)
                break;
            if (nodeCost > cost[node])
                continue;
            auto source = sTaxiPathSetBySource.find(node);
            if (source == sTaxiPathSetBySource.end())
                continue;
            for (auto const& [next, path] : source->second)
            {
                if (!usable(next) || path->ID >= lengths.size() || lengths[path->ID] < 0.0f)
                    continue;
                float nextCost = nodeCost + lengths[path->ID] + stopPenalty;
                auto known = cost.find(next);
                if (known == cost.end() || nextCost < known->second)
                {
                    cost[next] = nextCost;
                    previous[next] = node;
                    queue.emplace(nextCost, next);
                }
            }
        }

        if (!previous.count(to))
            return {};
        std::vector<uint32> route = { to };
        while (route.back() != from)
            route.push_back(previous[route.back()]);
        return { route.rbegin(), route.rend() };
    }
}

void WorldSession::HandleTaxiNodeStatusQueryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;

    recvData >> guid;
    SendTaxiStatus(guid);
}

void WorldSession::SendTaxiStatus(ObjectGuid guid)
{
    Player* const player = GetPlayer();
    Creature* unit = ObjectAccessor::GetCreature(*player, guid);
    // reaction must be checked both ways: the Dark Portal flight masters are neutral toward opposite-faction players, while players are hostile toward them
    if (!unit || unit->GetReactionTo(player) <= REP_UNFRIENDLY || player->IsHostileTo(unit) || !unit->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
    {
        LOG_DEBUG("network", "WorldSession::SendTaxiStatus - Unit ({}) not found.", guid.ToString());
        return;
    }

    // find taxi node
    uint32 nearest = sObjectMgr->GetNearestTaxiNode(*unit, player->GetTeamId(true));
    if (!nearest)
    {
        return;
    }

    WorldPacket data(SMSG_TAXINODE_STATUS, 9);
    data << guid;
    data << uint8(player->m_taxi.IsTaximaskNodeKnown(nearest) ? 1 : 0);
    SendPacket(&data);
    LOG_DEBUG("network", "WORLD: Sent SMSG_TAXINODE_STATUS");
}

void WorldSession::HandleTaxiQueryAvailableNodes(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // cheating checks
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!unit)
    {
        LOG_DEBUG("network", "WORLD: HandleTaxiQueryAvailableNodes - Unit ({}) not found or you can't interact with him.", guid.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // unknown taxi node case
    if (SendLearnNewTaxiNode(unit))
        return;

    // known taxi node case
    SendTaxiMenu(unit);
}

void WorldSession::SendTaxiMenu(Creature* unit)
{
    // find current node
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(*unit, GetPlayer()->GetTeamId(true));

    if (curloc == 0)
        return;

    bool lastTaxiCheaterState = GetPlayer()->isTaxiCheater();
    if (unit->GetEntry() == 29480) GetPlayer()->SetTaxiCheater(true); // Grimwing in Ebon Hold, special case. NOTE: Not perfect, Zul'Aman should not be included according to WoWhead, and I think taxicheat includes it.

    LOG_DEBUG("network", "WORLD: CMSG_TAXINODE_STATUS_QUERY {} ", curloc);

    WorldPacket data(SMSG_SHOWTAXINODES, (4 + 8 + 4 + 8 * 4));
    data << uint32(1);
    data << unit->GetGUID();
    data << uint32(curloc);
    GetPlayer()->m_taxi.AppendTaximaskTo(data, GetPlayer()->isTaxiCheater());
    SendPacket(&data);

    LOG_DEBUG("network", "WORLD: Sent SMSG_SHOWTAXINODES");

    GetPlayer()->SetTaxiCheater(lastTaxiCheaterState);
}

void WorldSession::SendDoFlight(uint32 mountDisplayId, uint32 path, uint32 pathNode)
{
    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    while (GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
        GetPlayer()->GetMotionMaster()->MovementExpired(false);

    if (mountDisplayId)
        GetPlayer()->Mount(mountDisplayId);

    if (Creature* critter = ObjectAccessor::GetCreature(*GetPlayer(), GetPlayer()->GetCritterGUID()))
        critter->DespawnOrUnsummon();

    GetPlayer()->GetMotionMaster()->MoveTaxiFlight(path, pathNode);
}

bool WorldSession::SendLearnNewTaxiNode(Creature* unit)
{
    // find current node
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(*unit, GetPlayer()->GetTeamId(true));

    if (curloc == 0)
        return true;                                        // `true` send to avoid WorldSession::SendTaxiMenu call with one more curlock seartch with same false result.

    if (GetPlayer()->m_taxi.SetTaximaskNode(curloc))
    {
        WorldPacket msg(SMSG_NEW_TAXI_PATH, 0);
        SendPacket(&msg);

        WorldPacket update(SMSG_TAXINODE_STATUS, 9);
        update << unit->GetGUID();
        update << uint8(1);
        SendPacket(&update);

        sScriptMgr->OnPlayerLearnTaxiNode(GetPlayer(), curloc);

        return true;
    }
    else
        return false;
}

void WorldSession::SendDiscoverNewTaxiNode(uint32 nodeid)
{
    if (GetPlayer()->m_taxi.SetTaximaskNode(nodeid))
    {
        WorldPacket msg(SMSG_NEW_TAXI_PATH, 0);
        SendPacket(&msg);

        sScriptMgr->OnPlayerLearnTaxiNode(GetPlayer(), nodeid);
    }
}

void WorldSession::HandleActivateTaxiExpressOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 node_count;

    recvData >> guid >> node_count;

    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        LOG_DEBUG("network", "WORLD: HandleActivateTaxiExpressOpcode - Unit ({}) not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }
    std::vector<uint32> nodes;

    for (uint32 i = 0; i < node_count; ++i)
    {
        uint32 node;
        recvData >> node;

        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(node) && !GetPlayer()->isTaxiCheater())
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            recvData.rfinish();
            return;
        }

        nodes.push_back(node);
    }

    if (nodes.empty())
        return;

    LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXIEXPRESS from {} to {}", nodes.front(), nodes.back());

    if (uint32 stopPenalty = sWorld->getIntConfig(CONFIG_TAXI_STOP_PENALTY); stopPenalty && nodes.size() > 2)
    {
        std::vector<uint32> route = FindTaxiRouteWithFewerStops(GetPlayer(), nodes.front(), nodes.back(), float(stopPenalty));
        // the map showed the client's price: keep its route if the player can't pay for this one
        uint32 routeCost = 0;
        for (std::size_t i = 1; i < route.size(); ++i)
        {
            uint32 path, cost;
            sObjectMgr->GetTaxiPath(route[i - 1], route[i], path, cost);
            routeCost += cost;
        }
        routeCost = uint32(std::ceil(routeCost * GetPlayer()->GetReputationPriceDiscount(npc)));
        if (!route.empty() && route.size() < nodes.size() && GetPlayer()->HasEnoughMoney(routeCost))
        {
            LOG_DEBUG("network", "WORLD: CMSG_ACTIVATETAXIEXPRESS {} -> {}: {} stops instead of the client's {}",
                nodes.front(), nodes.back(), route.size() - 2, nodes.size() - 2);
            nodes = std::move(route);
        }
    }

    GetPlayer()->ActivateTaxiPathTo(nodes, npc, 0);
}

void WorldSession::HandleMoveSplineDoneOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;           // used only for proper packet read
    MovementInfo movementInfo; // used only for proper packet read
    uint32 movementCounter;    // spline counter

    Unit* mover = _player->m_mover;

    recvData >> guid.ReadAsPacked();
    ReadMovementInfo(recvData, &movementInfo);
    recvData >> movementCounter;

    if (_player->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_TAXI_FLIGHT)) // taxi spline case
    {
        // in taxi flight packet received in 2 case:
        // 1) end taxi path in far (multi-node) flight
        // 2) switch from one map to other in case multim-map taxi path
        // we need process only (1)

        uint32 curDest = GetPlayer()->m_taxi.GetTaxiDestination();
        if (curDest)
        {
            TaxiNodesEntry const* curDestNode = sTaxiNodesStore.LookupEntry(curDest);

            // far teleport case
            if (curDestNode && curDestNode->map_id != GetPlayer()->GetMapId() && GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
            {
                if (FlightPathMovementGenerator* flight = dynamic_cast<FlightPathMovementGenerator*>(GetPlayer()->GetMotionMaster()->top()))
                {
                    // short preparations to continue flight
                    flight->SetCurrentNodeAfterTeleport();
                    TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
                    flight->SkipCurrentNode();

                    GetPlayer()->TeleportTo(curDestNode->map_id, node->x, node->y, node->z, GetPlayer()->GetOrientation(), TELE_TO_NOT_LEAVE_TAXI);
                }
            }

            return;
        }

        // at this point only 1 node is expected (final destination)
        if (GetPlayer()->m_taxi.GetPath().size() != 1)
        {
            return;
        }

        GetPlayer()->CleanupAfterTaxiFlight();
        GetPlayer()->SetFallInformation(GameTime::GetGameTime().count(), GetPlayer()->GetPositionZ());
        if (GetPlayer()->pvpInfo.IsHostile)
        {
            GetPlayer()->CastSpell(GetPlayer(), 2479, true);
        }
        return;
    }

    if (mover->GetGUID() != guid)
        return;

    if (!_player->IsExpectingChangeTransport() || !mover->movespline || mover->movespline->GetId() != movementCounter)
        return;

    _player->SetExpectingChangeTransport(false);
    WorldPacket data(_player->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_ROOT) ? MSG_MOVE_ROOT : MSG_MOVE_UNROOT, recvData.size());
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, _player);
}

void WorldSession::HandleActivateTaxiOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    std::vector<uint32> nodes;
    nodes.resize(2);
    GetPlayer()->SetCanTeleport(true);
    recvData >> guid >> nodes[0] >> nodes[1];
    LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXI from {} to {}", nodes[0], nodes[1]);
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        LOG_DEBUG("network", "WORLD: HandleActivateTaxiOpcode - Unit ({}) not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }

    if (!GetPlayer()->isTaxiCheater())
    {
        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[0]) || !GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[1]))
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            return;
        }
    }

    GetPlayer()->ActivateTaxiPathTo(nodes, npc, 0);
}

void WorldSession::SendActivateTaxiReply(ActivateTaxiReply reply)
{
    GetPlayer()->SetCanTeleport(true);
    WorldPacket data(SMSG_ACTIVATETAXIREPLY, 4);
    data << uint32(reply);
    SendPacket(&data);

    LOG_DEBUG("network", "WORLD: Sent SMSG_ACTIVATETAXIREPLY");
}
