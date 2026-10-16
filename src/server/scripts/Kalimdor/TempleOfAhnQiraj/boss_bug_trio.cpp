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

#include "CreatureScript.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "temple_of_ahnqiraj.h"

enum Spells
{
    SPELL_BLOODY_DEATH   = 25770,
    SPELL_FULL_HEAL      = 17683,

    // Kri
    SPELL_CLEAVE         = 26350,
    SPELL_THRASH         = 3391,
    SPELL_TOXIC_VOLLEY   = 25812,
    SPELL_POISON_CLOUD   = 26590,

    // Vem
    SPELL_CHARGE         = 26561,
    SPELL_KNOCKBACK      = 18670,
    SPELL_KNOCKDOWN      = 19128,
    SPELL_VENGEANCE      = 25790,

    // Yauj
    SPELL_HEAL           = 25807,
    SPELL_FEAR           = 26580,
    SPELL_RAVAGE         = 3242,
    SPELL_DISPEL         = 25808,

    NPC_YAUJ_BROOD       = 15621
};

enum Misc
{
    ACTION_CONSUME       = 0,
    ACTION_EXPLODE       = 1,

    EMOTE_DEVOURED       = 0,

    POINT_CONSUME        = 0,

    GROUP_EAT            = 1,

    VEM_WAYPOINT_PATH    = 876030
};

const Position resetPoint = { -8582.0f, 2047.0f, -1.62f }; // Taken from CMangos

struct boss_bug_trio : public BossAI
{
public:
    boss_bug_trio(Creature* creature) : BossAI(creature, DATA_BUG_TRIO) { Reset(); }

    bool CheckInRoom() override
    {
        if (me->GetExactDist2d(resetPoint) <= 10.f)
        {
            EnterEvadeMode(EVADE_REASON_BOUNDARY);
            return false;
        }

        return true;
    }

    template <typename Func>
    void ForEachOtherBug(Func&& func)
    {
        for (uint32 data : { DATA_VEM, DATA_KRI, DATA_YAUJ })
            if (Creature* bug = instance->GetCreature(data))
                if (bug->GetGUID() != me->GetGUID())
                    func(bug);
    }

    // Counted from the creatures themselves, so a lost DATA_BUG_TRIO_DEATH update cannot leave the last bug waiting to be eaten.
    bool OtherBugsAlive()
    {
        bool alive = false;
        ForEachOtherBug([&alive](Creature* bug)
        {
            if (bug->IsAlive())
                alive = true;
        });
        return alive;
    }

    Creature* GetDyingBug()
    {
        Creature* dying = nullptr;
        ForEachOtherBug([&dying](Creature* bug)
        {
            if (boss_bug_trio* ai = dynamic_cast<boss_bug_trio*>(bug->AI()))
                if (bug->IsAlive() && ai->_dying)
                    dying = bug;
        });
        return dying;
    }

    void EnterCombatWithTrio(Unit* who)
    {
        BossAI::JustEngagedWith(who);

        if (Creature* vem = instance->GetCreature(DATA_VEM))
        {
            if (vem->GetGUID() != me->GetGUID())
            {
                vem->GetAI()->AttackStart(who);
            }
        }
        if (Creature* kri = instance->GetCreature(DATA_KRI))
        {
            if (kri->GetGUID() != me->GetGUID())
            {
                kri->GetAI()->AttackStart(who);
            }
        }
        if (Creature* yauj = instance->GetCreature(DATA_YAUJ))
        {
            if (yauj->GetGUID() != me->GetGUID())
            {
                yauj->GetAI()->AttackStart(who);
            }
        }
    }

    void EvadeAllBosses(EvadeReason why)
    {
        if (Creature* vem = instance->GetCreature(DATA_VEM))
        {
            if (vem->GetGUID() != me->GetGUID())
            {
                if (vem->IsAlive() && !vem->IsInEvadeMode())
                    vem->AI()->EnterEvadeMode(why);
                else
                    vem->Respawn();
            }
        }

        if (Creature* kri = instance->GetCreature(DATA_KRI))
        {
            if (kri->GetGUID() != me->GetGUID())
            {
                if (kri->IsAlive() && !kri->IsInEvadeMode())
                    kri->AI()->EnterEvadeMode(why);
                else
                    kri->Respawn();
            }
        }

        if (Creature* yauj = instance->GetCreature(DATA_YAUJ))
        {
            if (yauj->GetGUID() != me->GetGUID())
            {
                if (yauj->IsAlive() && !yauj->IsInEvadeMode())
                    yauj->AI()->EnterEvadeMode(why);
                else
                    yauj->Respawn();
            }
        }
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_CONSUME && !_dying)
        {
            _isEating = true;
            _consumeReached = false;
            me->SetSpeed(MOVE_RUN, 45.f / 7.f); // From sniffs
            me->SetReactState(REACT_PASSIVE);
            _scheduler.CancelGroup(GROUP_EAT);
            _scheduler.DelayAll(6s);

            // If the walk to the dying bug never finishes, eat from here rather than stay passive and unkillable.
            _scheduler.Schedule(10s, GROUP_EAT, [this](TaskContext /*context*/)
            {
                FinishEat();
            });
        }

        if (action == ACTION_EXPLODE && _dying)
        {
            DoCastSelf(SPELL_BLOODY_DEATH, true);
            if (me->IsAlive())
                me->KillSelf();
            _dying = false;
        }
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != POINT_CONSUME || !_isEating || _consumeReached)
            return;

        _scheduler.CancelGroup(GROUP_EAT);
        FinishEat();
    }

    void FinishEat()
    {
        if (!_isEating || _consumeReached)
            return;

        _consumeReached = true;
        me->GetMotionMaster()->MoveIdle();
        me->SetSpeed(MOVE_RUN, 15.f/7.f); // From sniffs
        DoCastSelf(SPELL_FULL_HEAL, true);
        if (me->GetThreatMgr().GetThreatListSize())
            DoResetThreatList();
        if (Creature* dying = GetDyingBug())
        {
            dying->AI()->DoAction(ACTION_EXPLODE);
            me->SetTarget(dying->GetGUID());
        }

        _scheduler.Schedule(2s, GROUP_EAT, [this](TaskContext /*context*/)
        {
            me->SetReactState(REACT_AGGRESSIVE);
            _isEating = false;
            if (Unit* target = me->GetVictim())
            {
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveChase(target);
                AttackStart(target);
            }
        });
    }

    void EnterEvadeMode(EvadeReason why) override
    {
        BossAI::EnterEvadeMode(why);
        EvadeAllBosses(why);
    }

    void Reset() override
    {
        BossAI::Reset();
        _scheduler.CancelAll();
        _dying = false;
        _isEating = false;
        _consumeReached = false;
        instance->SetData(DATA_BUG_TRIO_DEATH, 0);
        me->SetSpeed(MOVE_RUN, 15.f / 7.f); // From sniffs

        if (me->GetEntry() == NPC_VEM)
        {
            me->GetMotionMaster()->MoveWaypoint(VEM_WAYPOINT_PATH, true);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || !CheckInRoom())
            return;

        _scheduler.Update(diff, [this]
        {
            if (!_dying && !_isEating)
                DoMeleeAttackIfReady();
        });
    }

    void DamageTaken(Unit* who, uint32& damage, DamageEffectType, SpellSchoolMask) override
    {
        if (_dying && who && who->GetGUID() != me->GetGUID())
            damage = 0;

        if (damage >= me->GetHealth() && OtherBugsAlive() && !_dying)
        {
            damage = 0;
            if (_isEating)
                return;

            _scheduler.CancelAll();
            me->SetReactState(REACT_PASSIVE);
            _dying = true;
            float x, y, z;
            // Move the other bugs to this bug position
            if (Creature* vem = instance->GetCreature(DATA_VEM))
            {
                if (vem->GetGUID() != me->GetGUID())
                {
                    if (vem->IsAlive())
                    {
                        vem->AI()->DoAction(ACTION_CONSUME);
                        me->GetRandomContactPoint(vem, x, y, z);
                        vem->GetMotionMaster()->MovePoint(POINT_CONSUME, x, y, z);
                    }
                }
            }
            if (Creature* kri = instance->GetCreature(DATA_KRI))
            {
                if (kri->GetGUID() != me->GetGUID())
                {
                    if (kri->IsAlive())
                    {
                        kri->AI()->DoAction(ACTION_CONSUME);
                        me->GetRandomContactPoint(kri, x, y, z);
                        kri->GetMotionMaster()->MovePoint(POINT_CONSUME, x, y, z);
                    }
                }
            }
            if (Creature* yauj = instance->GetCreature(DATA_YAUJ))
            {
                if (yauj->GetGUID() != me->GetGUID())
                {
                    if (yauj->IsAlive())
                    {
                        yauj->AI()->DoAction(ACTION_CONSUME);
                        me->GetRandomContactPoint(yauj, x, y, z);
                        yauj->GetMotionMaster()->MovePoint(POINT_CONSUME, x, y, z);
                    }
                }
            }

            // Eaters time out after 10s; if none is left to eat this bug, it explodes on its own.
            _scheduler.Schedule(12s, [this](TaskContext /*context*/)
            {
                DoAction(ACTION_EXPLODE);
            });
        }
    }

    void DoFinalSpell()
    {
        switch (me->GetEntry())
        {
            case NPC_KRI:
                DoCastSelf(SPELL_POISON_CLOUD, true);
                break;
            case NPC_VEM:
                DoCastSelf(SPELL_VENGEANCE, true);
                break;
            case NPC_YAUJ:
                for (uint8 i = 0; i < 10; ++i)
                {
                    Position randomPos;
                    me->GetRandomContactPoint(me, randomPos.m_positionX, randomPos.m_positionY, randomPos.m_positionZ);
                    if (Creature* summon = me->SummonCreature(NPC_YAUJ_BROOD, randomPos))
                        DoZoneInCombat(summon);
                }
                break;
            default:
                break;
        }
    }

    void JustDied(Unit* killer) override
    {
        instance->SetData(DATA_BUG_TRIO_DEATH, 1);
        if (OtherBugsAlive())
        {
            me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            DoFinalSpell();
            Talk(EMOTE_DEVOURED);
            me->DespawnOrUnsummon(3s);
            return;
        }

        BossAI::JustDied(killer);
    }

    TaskScheduler _scheduler;
    bool _dying;
    bool _isEating;
    bool _consumeReached;
};

struct boss_kri : public boss_bug_trio
{
    boss_kri(Creature* creature) : boss_bug_trio(creature)
    {
    }

    void JustEngagedWith(Unit* who) override
    {
        EnterCombatWithTrio(who);

        _scheduler.Schedule(7s, 18s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_CLEAVE);
            context.Repeat();
        })
        .Schedule(8s, 17s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_TOXIC_VOLLEY);
            context.Repeat();
        })
        .Schedule(7s, 16s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_THRASH);
            context.Repeat();
        });
    }
};

struct boss_vem : public boss_bug_trio
{
    boss_vem(Creature* creature) : boss_bug_trio(creature)
    {
    }

    void JustEngagedWith(Unit* who) override
    {
        EnterCombatWithTrio(who);

        _scheduler.Schedule(15s, 27s, [this](TaskContext context)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, [this](Unit* target) -> bool
                {
                    if (!target->IsPlayer())
                        return false;
                    if (me->IsWithinMeleeRange(target) || target == me->GetVictim())
                        return false;
                    if (!me->IsWithinLOS(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ()))
                        return false;

                    return true;
                }))
            {
                DoCast(target, SPELL_CHARGE);
            }
            context.Repeat();
        })
        .Schedule(10s, 24s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_KNOCKBACK);
            context.Repeat();
        })
        .Schedule(10s, 23s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_KNOCKDOWN);
            context.Repeat();
        })
        .Schedule(1s, [this](TaskContext context)
        {
            if (!OtherBugsAlive() && !me->HasAura(SPELL_VENGEANCE)) // Vem is the only one left.
            {
                DoCastSelf(SPELL_VENGEANCE, true);
            }
            context.Repeat(1s);
        });
    }
};

struct boss_yauj : public boss_bug_trio
{
    boss_yauj(Creature* creature) : boss_bug_trio(creature)
    {
    }

    void JustEngagedWith(Unit* who) override
    {
        EnterCombatWithTrio(who);

        _scheduler.Schedule(12100ms, [this](TaskContext context)
        {
            if (me->GetHealthPct() <= 93.f)
            {
                DoCastSelf(SPELL_HEAL);
            }
            else if (Unit* friendly = DoSelectLowestHpFriendly(100.f))
            {
                DoCast(friendly, SPELL_HEAL);
            }
            context.Repeat();
        })
        .Schedule(12s, [this](TaskContext context)
        {
            DoCastAOE(SPELL_FEAR);
            DoResetThreatList();
            context.Repeat(20600ms);
        })
        .Schedule(11s, 14500ms, [this](TaskContext context)
        {
            if (DoCastVictim(SPELL_RAVAGE) == SPELL_CAST_OK)
                context.Repeat(10s, 15s);
            else
                context.Repeat(1200ms);
        })
        .Schedule(10s, 30s, [this](TaskContext context)
        {
            std::list<Creature*> targets = DoFindFriendlyCC(50.f);
            if (!targets.empty())
            {
                if (Creature* target = *(targets.begin()))
                    me->CastSpell(target, SPELL_DISPEL);
            }
            context.Repeat(10s, 15s);
        });
    }
};

class spell_vem_knockback : public SpellScript
{
    PrepareSpellScript(spell_vem_knockback);

    void HandleScriptEffect(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (Unit* target = GetHitUnit())
        {
            if (Creature* cCaster = GetCaster()->ToCreature())
            {
                cCaster->GetThreatMgr().ModifyThreatByPercent(target, -80);
            }
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_vem_knockback::HandleScriptEffect, EFFECT_2, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class spell_vem_vengeance : public SpellScript
{
    PrepareSpellScript(spell_vem_vengeance);

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([&](WorldObject const* target) -> bool
            {
                return target->GetEntry() != NPC_YAUJ && target->GetEntry() != NPC_VEM && target->GetEntry() != NPC_KRI;
            });
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_vem_vengeance::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENTRY);
    }
};

void AddSC_bug_trio()
{
    RegisterTempleOfAhnQirajCreatureAI(boss_kri);
    RegisterTempleOfAhnQirajCreatureAI(boss_vem);
    RegisterTempleOfAhnQirajCreatureAI(boss_yauj);
    RegisterSpellScript(spell_vem_knockback);
    RegisterSpellScript(spell_vem_vengeance);
}
