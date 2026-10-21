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

#include "GameObjectAI.h"
#include "GameObjectScript.h"
#include "InstanceMapScript.h"
#include "InstanceScript.h"
#include "Player.h"
#include "zulgurub.h"

namespace
{
    // Stored per character, independently of the raid's instance save.
    std::string const MadnessSettingsSource = "EvermoreEdgeOfMadness";
    enum MadnessSettings
    {
        SETTING_DEFEATED_BOSSES = 0
    };

    constexpr uint32 MadnessBosses[] = { NPC_GRILEK, NPC_HAZZARAH, NPC_RENATAKI, NPC_WUSHOOLAY };
    constexpr uint32 AllMadnessBosses = 0xF;
}

DoorData const doorData[] =
{
    { GO_FORCEFIELD, DATA_ARLOKK, DOOR_TYPE_ROOM },
    { 0,             0,           DOOR_TYPE_ROOM }
};

ObjectData const creatureData[] =
{
    { NPC_HIGH_PRIEST_THEKAL, DATA_THEKAL  },
    { NPC_ZEALOT_LORKHAN,     DATA_LORKHAN },
    { NPC_ZEALOT_ZATH,        DATA_ZATH    },
    { NPC_PRIESTESS_MARLI,    DATA_MARLI   },
    { 0,                      0            }
};

class instance_zulgurub : public InstanceMapScript
{
public:
    instance_zulgurub(): InstanceMapScript(ZGScriptName, MAP_ZUL_GURUB) { }

    struct instance_zulgurub_InstanceMapScript : public InstanceScript
    {
        instance_zulgurub_InstanceMapScript(Map* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadDoorData(doorData);
            LoadObjectData(creatureData, nullptr);
        }

        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_JINDO_THE_HEXXER:
                    _jindoTheHexxerGUID = creature->GetGUID();
                    break;
                case NPC_VILEBRANCH_SPEAKER:
                    _vilebranchSpeakerGUID = creature->GetGUID();
                    break;
                case NPC_ARLOKK:
                    _arlokkGUID = creature->GetGUID();
                    break;
                case NPC_HAKKAR:
                    _hakkarGUID = creature->GetGUID();
                    break;
                case NPC_SPAWN_OF_MARLI:
                    if (Creature* marli = GetCreature(DATA_MARLI))
                    {
                        marli->AI()->JustSummoned(creature);
                    }
                    break;
                case NPC_GAHZRANKA:
                    _gahzrankaGUID = creature->GetGUID();
                    break;
                case NPC_GRILEK:
                case NPC_HAZZARAH:
                case NPC_RENATAKI:
                case NPC_WUSHOOLAY:
                    _edgeOfMadnessGUID = creature->GetGUID();
                    _edgeOfMadnessEntry = creature->GetEntry();
                    break;
                default:
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            InstanceScript::OnGameObjectCreate(go);

            switch (go->GetEntry())
            {
                case GO_GONG_OF_BETHEKK:
                    _goGongOfBethekkGUID = go->GetGUID();
                    if (GetBossState(DATA_ARLOKK) == DONE)
                        go->SetGameObjectFlag(GO_FLAG_NOT_SELECTABLE);
                    else
                        go->RemoveGameObjectFlag(GO_FLAG_NOT_SELECTABLE);
                    break;
                default:
                    break;
            }
        }

        ObjectGuid GetGuidData(uint32 uiData) const override
        {
            switch (uiData)
            {
                case DATA_JINDO:
                    return _jindoTheHexxerGUID;
                case NPC_ARLOKK:
                    return _arlokkGUID;
                case GO_GONG_OF_BETHEKK:
                    return _goGongOfBethekkGUID;
                case DATA_HAKKAR:
                    return _hakkarGUID;
                case DATA_EDGE_OF_MADNESS:
                    return _edgeOfMadnessGUID;
            }

            return ObjectGuid::Empty;
        }

        uint32 GetData(uint32 type) const override
        {
            if (type == DATA_GAHZRANKA)
            {
                return _gahzrankaGUID || GetBossState(DATA_GAHZRANKA) == DONE;
            }

            if (type == TYPE_EDGE_OF_MADNESS)
                return _edgeOfMadnessEntry;

            return 0;
        }

        void WriteSaveDataMore(std::ostringstream& data) override
        {
            data << _edgeOfMadnessEntry;
        }

        void ReadSaveDataMore(std::istringstream& data) override
        {
            uint32 entry = 0;
            if (data >> entry)
                for (uint32 boss : MadnessBosses)
                    if (entry == boss)
                        _edgeOfMadnessEntry = entry;
        }

        void RemoveHakkarPowerStack()
        {
            if (Creature* hakkar = instance->GetCreature(_hakkarGUID))
            {
                hakkar->CastSpell(hakkar, SPELL_HAKKAR_POWER_DOWN, true);
            }
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_JEKLIK:
                case DATA_VENOXIS:
                case DATA_MARLI:
                case DATA_ARLOKK:
                case DATA_THEKAL:
                    if (state == DONE)
                        RemoveHakkarPowerStack();
                    break;
                case DATA_EDGE_OF_MADNESS:
                    if (state == DONE)
                        CreditMadnessBoss();
                    break;
                default:
                    break;
            }

            return true;
        }

    private:
        void CreditMadnessBoss()
        {
            Creature* boss = instance->GetCreature(_edgeOfMadnessGUID);
            if (!boss)
                return;

            uint32 bossMask = 0;
            for (uint32 i = 0; i < std::size(MadnessBosses); ++i)
                if (boss->GetEntry() == MadnessBosses[i])
                    bossMask = 1u << i;
            if (!bossMask)
                return;

            for (auto const& ref : instance->GetPlayers())
            {
                Player* player = ref.GetSource();
                if (!player || !player->IsAtGroupRewardDistance(boss))
                    continue;

                uint32 mask = player->GetPlayerSetting(MadnessSettingsSource, SETTING_DEFEATED_BOSSES).value;
                // Begin a new cycle only after completing all four encounters.
                if ((mask & AllMadnessBosses) == AllMadnessBosses)
                    mask = 0;
                player->UpdatePlayerSetting(MadnessSettingsSource, SETTING_DEFEATED_BOSSES,
                    (mask | bossMask) & AllMadnessBosses);
            }
        }

        // If all High Priest bosses were killed. Ohgan is added too.
        // Jindo is needed for healfunction.

        ObjectGuid _jindoTheHexxerGUID;
        ObjectGuid _vilebranchSpeakerGUID;
        ObjectGuid _arlokkGUID;
        ObjectGuid _goGongOfBethekkGUID;
        ObjectGuid _hakkarGUID;
        ObjectGuid _gahzrankaGUID;
        ObjectGuid _edgeOfMadnessGUID;
        uint32 _edgeOfMadnessEntry = 0;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_zulgurub_InstanceMapScript(map);
    }
};

Position const edgeOfMagnessSummonPos = { -11901.229f, -1906.366f, 65.358f, 0.942f };

struct go_brazier_of_madness : public GameObjectAI
{
    go_brazier_of_madness(GameObject* go) : GameObjectAI(go) { }

    bool GossipHello(Player* player, bool reportUse) override
    {
        if (reportUse)
        {
            return true;
        }

        InstanceScript* instanceScript = me->GetInstanceScript();
        if (!instanceScript || instanceScript->GetBossState(DATA_EDGE_OF_MADNESS) == DONE)
            return false;

        // Keep one encounter per raid save, including across wipes and instance unloads.
        if (me->GetMap()->GetCreature(instanceScript->GetGuidData(DATA_EDGE_OF_MADNESS)))
            return false;

        uint32 bossEntry = instanceScript->GetData(TYPE_EDGE_OF_MADNESS);
        if (!bossEntry)
        {
            uint32 mask = player->GetPlayerSetting(MadnessSettingsSource, SETTING_DEFEATED_BOSSES).value;
            if ((mask & AllMadnessBosses) == AllMadnessBosses)
                mask = 0;
            for (uint32 i = 0; i < std::size(MadnessBosses); ++i)
                if (!(mask & (1u << i)))
                {
                    bossEntry = MadnessBosses[i];
                    break;
                }
        }

        if (me->SummonCreature(bossEntry, edgeOfMagnessSummonPos,
            TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2 * HOUR * IN_MILLISECONDS))
            instanceScript->SaveToDB();

        return false;
    }
};

void AddSC_instance_zulgurub()
{
    new instance_zulgurub();
    RegisterGameObjectAI(go_brazier_of_madness);
}
