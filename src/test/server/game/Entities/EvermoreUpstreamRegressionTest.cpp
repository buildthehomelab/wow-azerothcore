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

#include "DynamicObject.h"
#include "GameTime.h"
#include "EvermoreInstanceSave.h"
#include "IntegrationTestFixture.h"
#include "ObjectAccessor.h"
#include "SpellAuras.h"
#include "SpellInfoTestHelper.h"
#include "gtest/gtest.h"
#include <thread>

namespace
{
class EvermoreUpstreamRegressionTest : public IntegrationTestFixture { };

TEST_F(EvermoreUpstreamRegressionTest, ChannelPushbackClampsAtZero)
{
    DynamicObject object;
    object.SetDuration(500);
    object.Delay(200);
    EXPECT_EQ(object.GetDuration(), 300);
    object.Delay(300);
    EXPECT_EQ(object.GetDuration(), 0);
    object.Delay(100);
    EXPECT_EQ(object.GetDuration(), 0);
    object.SetDuration(100);
    object.Delay(500);
    EXPECT_EQ(object.GetDuration(), 0);
}

TEST_F(EvermoreUpstreamRegressionTest, CritSuppressionAcrossCreatureLevelBoundaries)
{
    TestPlayer* player = CreateTestPlayer();
    player->SetLevel(60);
    TestCreature* victim = CreateTestCreature(100, 99001, TEST_FACTION_HOSTILE_TO_ALL);
    for (auto const& [level, expected] : std::vector<std::pair<uint8, float>>{
        {59, 0.2f}, {60, 0.0f}, {61, -0.6f}, {62, -1.2f}, {63, -4.8f}})
    {
        victim->SetLevel(level);
        EXPECT_NEAR(player->GetCritChanceFromSkillDiff(victim), expected, 0.0001f) << unsigned(level);
    }
}

TEST_F(EvermoreUpstreamRegressionTest, PreDeathAuraDistinguishesOwnerAndUnresolvedCaster)
{
    // Initialize the millisecond clock before the death/refresh interval.
    GameTime::UpdateGameTimers();
    auto info = SpellInfoBuilder().WithId(900810)
        .WithEffect(0, SPELL_EFFECT_APPLY_AURA, SPELL_AURA_MOD_RESISTANCE)
        .WithEffectBasePoints(0, 2).WithEffectMiscValue(0, SPELL_SCHOOL_MASK_FIRE).BuildUnique();
    TestPlayer* caster = CreateTestPlayer();
    TestPlayer* target = CreateTestPlayer(2, "Target");
    ObjectAccessor::AddObject(static_cast<Player*>(caster));
    auto unregister = [](Player* player) { ObjectAccessor::RemoveObject(player); };
    std::unique_ptr<Player, decltype(unregister)> registeredCaster(caster, unregister);
    Aura* aura = Aura::TryCreate(info.get(), 1, target, caster);
    ASSERT_NE(aura, nullptr);
    EXPECT_FALSE(aura->HasCasterDiedSinceApply(nullptr));
    EXPECT_FALSE(aura->HasCasterDiedSinceApply(caster));
    EXPECT_FALSE(aura->HasCasterDiedSinceApply(target));
    // Exercise Unit's production death timestamp without Player's achievement/
    // corpse services, which this isolated fixture deliberately does not start.
    caster->Unit::setDeathState(DeathState::Dead);
    EXPECT_TRUE(aura->HasCasterDiedSinceApply(caster));
    caster->Unit::setDeathState(DeathState::Alive);
    EXPECT_TRUE(aura->HasCasterDiedSinceApply(caster));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    GameTime::UpdateGameTimers();
    aura->RefreshDuration();
    EXPECT_FALSE(aura->HasCasterDiedSinceApply(caster));
    aura->Remove();
}

TEST(EvermoreInstanceSaveTest, EyeRetainsAsymmetricLegacyBossCompletions)
{
    EXPECT_EQ(Evermore::ConvertEyeSave("T E 0 3 0 0 "), "T E 0 0 3 0 ");
    EXPECT_EQ(Evermore::ConvertEyeSave("T E 0 0 3 0 "), "T E 0 3 0 0 ");
    std::string saved = Evermore::ConvertEyeSave("T E 3 0 3 0 ");
    EXPECT_EQ(Evermore::ConvertEyeSave(saved.c_str()), "T E 3 0 3 0 ");
}

TEST(EvermoreInstanceSaveTest, UlduarPreservesEligibilityAndOtherPersistentFields)
{
    // 14 boss states, then watchers, conservatory, Unbroken, Algalon and 4 more fields.
    char const* eligible = "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 15 42 1 60 7 2 1 1 ";
    char const* upstream = "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 15 42 0 60 7 2 1 1 ";
    EXPECT_EQ(Evermore::ConvertUlduarSave(eligible), upstream);
    EXPECT_EQ(Evermore::ConvertUlduarSave(upstream), eligible);
    std::string saved = Evermore::ConvertUlduarSave(eligible);
    EXPECT_EQ(Evermore::ConvertUlduarSave(saved.c_str()), eligible);
}

TEST(EvermoreInstanceSaveTest, HistoricalUlduarSavesDefaultMissingPersistentFields)
{
    char const* bosses = "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 ";
    EXPECT_EQ(Evermore::ConvertUlduarSave(bosses),
        "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 ");
    EXPECT_EQ(Evermore::ConvertUlduarSave((std::string(bosses) + "15 42 1").c_str()),
        "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 15 42 0 0 0 0 0 0 ");
    EXPECT_EQ(Evermore::ConvertUlduarSave((std::string(bosses) + "15 42 0 60 7 2 1").c_str()),
        "U U 3 0 3 0 0 0 0 0 0 0 0 0 0 0 15 42 1 60 7 2 1 0 ");
    EXPECT_TRUE(Evermore::ConvertUlduarSave((std::string(bosses) + "15 x").c_str()).empty());
}

TEST(EvermoreInstanceSaveTest, InvalidSavesDoNotPartiallyLoad)
{
    EXPECT_TRUE(Evermore::ConvertEyeSave(nullptr).empty());
    EXPECT_TRUE(Evermore::ConvertEyeSave("U U 0 3 0 0").empty());
    EXPECT_TRUE(Evermore::ConvertEyeSave("T U 0 3 0 0").empty());
    EXPECT_TRUE(Evermore::ConvertEyeSave("T E 0 3 0").empty());
    EXPECT_TRUE(Evermore::ConvertEyeSave("T E 0 x 0 0").empty());
    EXPECT_TRUE(Evermore::ConvertEyeSave("T E 0 3 0 0 4").empty());
    EXPECT_TRUE(Evermore::ConvertUlduarSave("U U 0 0 0 0").empty());
}
}
