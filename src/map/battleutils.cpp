﻿/*
===========================================================================

  Copyright (c) 2010-2012 Darkstar Dev Teams

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses/

  This file is part of DarkStar-server source code.

===========================================================================
*/

#include "../common/timer.h"
#include "../common/utils.h"

#include <math.h>
#include <string.h>
#include <algorithm>

#include "packets/char_health.h"
#include "packets/char_update.h"
#include "packets/entity_update.h"
#include "packets/message_basic.h"

#include "ability.h"
#include "charutils.h"
#include "battleutils.h"
#include "map.h"
#include "spell.h"
#include "trait.h" 
#include "weapon_skill.h"
#include "mobskill.h"
#include "mobentity.h"
#include "petentity.h"
#include "enmity_container.h"


/************************************************************************
*	lists used in battleutils											*
************************************************************************/

uint16 g_SkillTable[100][12];									// All Skills by level/skilltype
uint8  g_EnmityTable[100][2];		                            // Holds Enmity Modifier Values
uint8  g_SkillRanks[MAX_SKILLTYPE][MAX_JOBTYPE];				// Holds skill ranks by skilltype and job
uint16 g_SkillChainDamageModifiers[MAX_SKILLCHAIN_LEVEL + 1][MAX_SKILLCHAIN_COUNT + 1]; // Holds damage modifiers for skill chains [chain level][chain count]

CAbility*	  g_PAbilityList[MAX_ABILITY_ID];					// Complete Abilities List
CWeaponSkill* g_PWeaponSkillList[MAX_WEAPONSKILL_ID];			// Holds all Weapon skills
CMobSkill*    g_PMobSkillList[MAX_MOBSKILL_ID];					// List of mob skills

std::list<CAbility*>     g_PAbilitiesList[MAX_JOBTYPE];			// Abilities List By Job Type
std::list<CWeaponSkill*> g_PWeaponSkillsList[MAX_SKILLTYPE];	// Holds Weapon skills by type
std::vector<CMobSkill*>  g_PMobFamilySkills[MAX_MOB_FAMILY];	// Mob Skills By Family

/************************************************************************
*  battleutils															*
************************************************************************/

namespace battleutils
{

/************************************************************************
*                                                                       *
*  Generate Enmity Table                                                *
*                                                                       *
************************************************************************/

void LoadEnmityTable()
{
    for (uint32 x = 0; x < 100; ++x)
    {
        g_EnmityTable[x][0] = (uint8)abs(0.5441*x + 13.191);     // cmod
        g_EnmityTable[x][1] = (uint8)abs(0.6216*x + 5.4363);     // dmod
    }
}

/************************************************************************
*                                                                       *
*                                                                       *
*                                                                       *
************************************************************************/

void LoadSkillTable()
{
	memset(g_SkillTable,0, sizeof(g_SkillTable));
	memset(g_SkillRanks,0, sizeof(g_SkillRanks));

	const int8* fmtQuery = "SELECT r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11 \
						    FROM skill_caps \
							ORDER BY level \
							LIMIT 100";

	int32 ret = Sql_Query(SqlHandle,fmtQuery);
	
	if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
	{
		for (uint32 x = 0; x < 100 && Sql_NextRow(SqlHandle) == SQL_SUCCESS; ++x)
		{
			for (uint32 y = 0; y < 12; ++y) 
			{
				g_SkillTable[x][y] = (uint16)Sql_GetIntData(SqlHandle,y);
			}
		}
	}

	fmtQuery = "SELECT skillid,war,mnk,whm,blm,rdm,thf,pld,drk,bst,brd,rng,sam,nin,drg,smn,blu,cor,pup,dnc,sch \
				FROM skill_ranks \
				LIMIT 64";

	ret = Sql_Query(SqlHandle,fmtQuery);
	
	if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
	{
		for (uint32 x = 0; x < MAX_SKILLTYPE && Sql_NextRow(SqlHandle) == SQL_SUCCESS; ++x)
		{
			uint8 SkillID = cap_value((uint8)Sql_GetIntData(SqlHandle,0), 0, MAX_SKILLTYPE-1);

			for (uint32 y = 1; y < MAX_JOBTYPE; ++y) 
			{
				g_SkillRanks[SkillID][y] = cap_value((uint16)Sql_GetIntData(SqlHandle,y), 0, 11);
			}
		}
	}
}

/************************************************************************
*  Load Abilities from Database											*
************************************************************************/

void LoadAbilitiesList()
{
	memset(g_PAbilityList,0,sizeof(g_PAbilityList));

	const int8* fmtQuery = "SELECT abilityId, name, job, level, validTarget, recastTime, animation, `range`, isAOE, recastId, CE, VE \
							FROM abilities \
                            WHERE job > 0 AND job < %u AND abilityId < %u \
							ORDER BY job, level ASC";

	int32 ret = Sql_Query(SqlHandle, fmtQuery, MAX_JOBTYPE, MAX_ABILITY_ID);

	if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
	{
		while(Sql_NextRow(SqlHandle) == SQL_SUCCESS) 
		{
			CAbility* PAbility = new CAbility(Sql_GetIntData(SqlHandle,0));
		
			PAbility->setName(Sql_GetData(SqlHandle,1));
			PAbility->setJob((JOBTYPE)Sql_GetIntData(SqlHandle,2));
			PAbility->setLevel(Sql_GetIntData(SqlHandle,3));
			PAbility->setValidTarget(Sql_GetIntData(SqlHandle,4));
			PAbility->setRecastTime(Sql_GetIntData(SqlHandle,5));
			PAbility->setAnimationID(Sql_GetIntData(SqlHandle,6));
			PAbility->setRange(Sql_GetIntData(SqlHandle,7));
			PAbility->setAOE(Sql_GetIntData(SqlHandle,8));
			PAbility->setRecastId(Sql_GetIntData(SqlHandle,9));
			PAbility->setCE(Sql_GetIntData(SqlHandle,10));
			PAbility->setVE(Sql_GetIntData(SqlHandle,11));

			g_PAbilityList[PAbility->getID()] = PAbility;
			g_PAbilitiesList[PAbility->getJob()].push_back(PAbility);
		}
	}
}

/************************************************************************
*  Load Weapon Skills from database										*
************************************************************************/

void LoadWeaponSkillsList()
{
	memset(g_PWeaponSkillList,0,sizeof(g_PWeaponSkillList));

	const int8* fmtQuery = "SELECT weaponskillid, name, jobs, type, skilllevel, element, animation, `range`, aoe \
							FROM weapon_skills \
							WHERE weaponskillid < %u \
							ORDER BY type, skilllevel ASC";

	int32 ret = Sql_Query(SqlHandle, fmtQuery, MAX_WEAPONSKILL_ID);

	if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
	{
		while(Sql_NextRow(SqlHandle) == SQL_SUCCESS) 
		{
			CWeaponSkill* PWeaponSkill = new CWeaponSkill(Sql_GetIntData(SqlHandle,0));
			
			PWeaponSkill->setName(Sql_GetData(SqlHandle,1));
			PWeaponSkill->setJob(Sql_GetData(SqlHandle,2));
			PWeaponSkill->setType(Sql_GetIntData(SqlHandle,3));
			PWeaponSkill->setSkillLevel(Sql_GetIntData(SqlHandle,4));
			PWeaponSkill->setElement(Sql_GetIntData(SqlHandle,5));
			PWeaponSkill->setAnimationId(Sql_GetIntData(SqlHandle,6));
			PWeaponSkill->setRange(Sql_GetIntData(SqlHandle,7));
			PWeaponSkill->setAoe(Sql_GetIntData(SqlHandle,8));
			
			g_PWeaponSkillList[PWeaponSkill->getID()] = PWeaponSkill;
			g_PWeaponSkillsList[PWeaponSkill->getType()].push_back(PWeaponSkill);
		}
	}
}


/************************************************************************
*                                                                       *
*  Load Mob Skills from database                                        *
*                                                                       *
************************************************************************/

void LoadMobSkillsList()
{
	memset(g_PMobSkillList, 0, sizeof(g_PMobSkillList));

	const int8* fmtQuery = "SELECT mob_skill_id, family_id, mob_anim_id, mob_skill_name, \
						   mob_skill_aoe, mob_skill_distance, mob_anim_time, mob_prepare_time, \
						   mob_valid_targets, mob_skill_flag \
						   FROM mob_skill \
						   WHERE mob_skill_id < %u \
						   ORDER BY family_Id, mob_skill_id ASC";

	int32 ret = Sql_Query(SqlHandle, fmtQuery, MAX_MOBSKILL_ID);

	if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
	{
		while(Sql_NextRow(SqlHandle) == SQL_SUCCESS) 
		{
			CMobSkill* PMobSkill = new CMobSkill(Sql_GetIntData(SqlHandle,0));
			PMobSkill->setfamilyID(Sql_GetIntData(SqlHandle,1));
			PMobSkill->setAnimationID(Sql_GetIntData(SqlHandle,2));
			PMobSkill->setName(Sql_GetData(SqlHandle,3));
			PMobSkill->setAoe(Sql_GetIntData(SqlHandle,4));
			PMobSkill->setDistance(Sql_GetFloatData(SqlHandle,5));
			PMobSkill->setAnimationTime(Sql_GetIntData(SqlHandle,6));
			PMobSkill->setActivationTime(Sql_GetIntData(SqlHandle,7));
			PMobSkill->setValidTargets(Sql_GetIntData(SqlHandle,8));
			PMobSkill->setFlag(Sql_GetIntData(SqlHandle,9));
			PMobSkill->setMsg(185); //standard damage message. Scripters will change this.
			PMobSkill->m_SkillCondition = SKILLBEHAVIOUR_NONE;
			PMobSkill->m_SkillConditionValue = 0;
			g_PMobSkillList[PMobSkill->getID()] = PMobSkill;
			g_PMobFamilySkills[PMobSkill->getfamilyID()].push_back(PMobSkill);
		}
	}
}

void LoadSkillChainDamageModifiers()
{
    memset(g_SkillChainDamageModifiers, 0, sizeof(g_SkillChainDamageModifiers));

    const int8* fmtQuery = "SELECT chain_level, chain_count, initial_modifier, magic_burst_modifier \
                           FROM skillchain_damage_modifiers \
                           ORDER BY chain_level, chain_count \
                           LIMIT 15";

    int32 ret = Sql_Query(SqlHandle, fmtQuery);

    if( ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
    {
        for (uint32 x = 0; x <= 15 && Sql_NextRow(SqlHandle) == SQL_SUCCESS; ++x)
        {
            uint16 level = (uint16)Sql_GetIntData(SqlHandle, 0);
            uint16 count = (uint16)Sql_GetIntData(SqlHandle, 1);
            uint16 value = (uint16)Sql_GetIntData(SqlHandle, 2);
            g_SkillChainDamageModifiers[level][count] = value;
        }
    }

    return;
}

/************************************************************************
*	Clear Abilities List												*
************************************************************************/

void FreeAbilitiesList()
{
	for(int32 AbilityID = 0; AbilityID < MAX_ABILITY_ID; ++AbilityID)
	{
		delete g_PAbilityList[AbilityID];
	}
}


/************************************************************************
*  Clear Weapon Skills List												*
************************************************************************/

void FreeWeaponSkillsList()
{
	for(int32 SkillId= 0; SkillId < MAX_WEAPONSKILL_ID; ++SkillId)
	{
		delete g_PWeaponSkillList[SkillId];
	}
}

/************************************************************************
*  Clear Mob Skills List												*
************************************************************************/
void FreeMobSkillList()
{
	for(int32 SkillID= 0; SkillID < MAX_MOBSKILL_ID; ++SkillID)
	{
		delete g_PMobSkillList[SkillID];
	}
}

void FreeSkillChainDamageModifiers()
{
    // These aren't dynamicly allocated at this point so no need to free them.
}

/************************************************************************
*	Get Skill Rank By SkillId and JobId									*
************************************************************************/

uint8 GetSkillRank(SKILLTYPE SkillID, JOBTYPE JobID)
{
	return g_SkillRanks[SkillID][JobID];
}

/************************************************************************
*	Return Max Skill by SkillType, JobType, and level					*
************************************************************************/

uint16 GetMaxSkill(SKILLTYPE SkillID, JOBTYPE JobID, uint8 level)
{
	return g_SkillTable[level][g_SkillRanks[SkillID][JobID]];
}

bool isValidSelfTargetWeaponskill(int wsid){
	switch(wsid){
	case 163: //starlight
	case 164: //moonlight
	case 173: //dagan
	case 190: //myrkr
		return true;
	}
	return false;
}

/************************************************************************
*	Get Ability By ID													*
************************************************************************/

CAbility* GetAbility(uint16 AbilityID)
{
	if (AbilityID < MAX_ABILITY_ID)
	{
		//ShowDebug(CL_GREEN"Getting CurrentAbility %u \n" CL_RESET, g_PAbilityList[AbilityID]->getID());
		return g_PAbilityList[AbilityID];
	}
	ShowFatalError(CL_RED"AbilityID <%u> is out of range\n" CL_RESET, AbilityID);
	return NULL;
}

/************************************************************************
*	Get Abilities By JobID												*
************************************************************************/

std::list<CAbility*> GetAbilities(JOBTYPE JobID)
{
	return g_PAbilitiesList[JobID];
}

/************************************************************************
*	Function may not be needed											*
************************************************************************/

bool CanUseAbility(CBattleEntity* PAttacker, uint16 AbilityID)
{
	if (GetAbility(AbilityID) != NULL)
	{
		//...
	}
	return false;
}

/************************************************************************
*                                                                       *
*  Get Enmity Modifier                                                  *
*                                                                       *
************************************************************************/

uint8 GetEnmityMod(uint8 level, uint8 modType)
{
    DSP_DEBUG_BREAK_IF(level >= 100);
    DSP_DEBUG_BREAK_IF(modType >= 2);
    
	return g_EnmityTable[level][modType];
}

/************************************************************************
*                                                                       *
*  Get Weapon Skill by ID                                               *
*                                                                       *
************************************************************************/

CWeaponSkill* GetWeaponSkill(uint16 WSkillID)
{
    DSP_DEBUG_BREAK_IF(WSkillID >= MAX_WEAPONSKILL_ID);
	
    return g_PWeaponSkillList[WSkillID];
}

/************************************************************************
*                                                                       *
* Get List of Weapon Skills from skill type								*
*                                                                       *
************************************************************************/

std::list<CWeaponSkill*> GetWeaponSkills(uint8 skill)
{
    DSP_DEBUG_BREAK_IF(skill >= MAX_SKILLTYPE);

	return g_PWeaponSkillsList[skill];
}

/************************************************************************
*                                                                       *
*  Get Mob Skill by Id													*
*                                                                       *
************************************************************************/

CMobSkill* GetMobSkill(uint16 SkillID)
{
    DSP_DEBUG_BREAK_IF(SkillID >= MAX_MOBSKILL_ID);

    return g_PMobSkillList[SkillID];
}

/************************************************************************
*                                                                       *
*  Get Mob Skills by family id                                          *
*                                                                       *
************************************************************************/

std::vector<CMobSkill*> GetMobSkillsByFamily(uint16 FamilyID)
{
    DSP_DEBUG_BREAK_IF(FamilyID >= sizeof(g_PMobFamilySkills));

	return g_PMobFamilySkills[FamilyID];
}

uint16	CalculateEnspellDamage(CBattleEntity* PAttacker, CBattleEntity* PDefender, uint8 Tier, uint8 element){
	//Tier 1 enspells have their damaged pre-calculated AT CAST TIME and is stored in MOD_ENSPELL_DMG
	if(Tier==1){return PAttacker->getMod(MOD_ENSPELL_DMG);}

	//Tier 2 enspells calculate the damage on each hit and increment the potency in MOD_ENSPELL_DMG per hit
	uint16 skill = PAttacker->GetSkill(SKILL_ENH) + PAttacker->getMod(MOD_ENHANCE);
	uint16 cap = 3 + ((6*skill)/100);
	if(skill>200){
		cap = 5 + ((5*skill)/100);
	}
	cap *= 2;
	if(PAttacker->getMod(MOD_ENSPELL_DMG) > cap){
		PAttacker->setModifier(MOD_ENSPELL_DMG,cap);
		return cap;
	}
	if(PAttacker->getMod(MOD_ENSPELL_DMG) == cap) { return cap; }
	if(PAttacker->getMod(MOD_ENSPELL_DMG) < cap){
		PAttacker->addModifier(MOD_ENSPELL_DMG,1);
		return PAttacker->getMod(MOD_ENSPELL_DMG)-1;
	}
}

/************************************************************************
*                                                                       *
*  Handles Enspell effect and damage						            *
*                                                                       *
************************************************************************/

void HandleEnspell(CCharEntity* PAttacker, CBattleEntity* PDefender,apAction_t* Action,uint8 hitNumber){
	//DEBUG: REMOVE WHEN ACTION PACKET ISSUE IS RESOLVED (multi hits do not display correctly)
	if(hitNumber>0){return;}

	switch(PAttacker->getMod(MOD_ENSPELL)){
	case ENSPELL_I_FIRE:
		Action->subeffect = SUBEFFECT_FIRE_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,FIRE);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_EARTH:
		Action->subeffect = SUBEFFECT_EARTH_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,EARTH);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_WATER:
		Action->subeffect = SUBEFFECT_WATER_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,WATER);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_WIND:
		Action->subeffect = SUBEFFECT_WIND_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,WIND);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_ICE:
		Action->subeffect = SUBEFFECT_ICE_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,ICE);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_THUNDER:
		Action->subeffect = SUBEFFECT_LIGHTNING_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,THUNDER);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_LIGHT:
		Action->subeffect = SUBEFFECT_LIGHT_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,LIGHT);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_I_DARK:
		Action->subeffect = SUBEFFECT_DARKNESS_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,1,DARK);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_BLOOD_WEAPON:
		Action->subeffect = SUBEFFECT_BLOOD_WEAPON;
		Action->submessageID = 167;
		Action->flag = 1;
		Action->subparam = PAttacker->addHP(Action->param);
		if(PAttacker->objtype == TYPE_PC){
			charutils::UpdateHealth((CCharEntity*)PAttacker);
		}
		break;
	case ENSPELL_II_FIRE:
		if(hitNumber>0){return;}//only main hand hit (no da/multihit) works for enspell 2s
		Action->subeffect = SUBEFFECT_FIRE_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,FIRE);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_II_EARTH:
		if(hitNumber>0){return;}
		Action->subeffect = SUBEFFECT_EARTH_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,EARTH);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_II_WATER:
		if(hitNumber>0){return;}
		Action->subeffect = SUBEFFECT_WATER_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,WATER);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_II_WIND:
		if(hitNumber>0){return;}
		Action->subeffect = SUBEFFECT_WIND_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,WIND);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_II_ICE:
		if(hitNumber>0){return;}
		Action->subeffect = SUBEFFECT_ICE_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 1;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,ICE);
		PDefender->addHP(-Action->subparam);
		break;
	case ENSPELL_II_THUNDER:
		if(hitNumber>0){return;}
		Action->subeffect = SUBEFFECT_LIGHTNING_DAMAGE;
		Action->submessageID = 163;
		Action->flag = 3;
		Action->subparam = CalculateEnspellDamage(PAttacker,PDefender,2,THUNDER);
		PDefender->addHP(-Action->subparam);
		break;
	}
}

/************************************************************************
*                                                                       *
*  Handles Ranged weapon's additional effects (e.g. Bolts)              *
*                                                                       *
************************************************************************/

void HandleRangedAdditionalEffect(CCharEntity* PAttacker, CBattleEntity* PDefender,apAction_t* Action){
	CItemWeapon* PAmmo = (CItemWeapon*)PAttacker->getStorage(LOC_INVENTORY)->GetItem(PAttacker->equip[SLOT_AMMO]);
	//add effects dont have 100% proc, presume level dependant. 95% chance but -5% for each level diff.
	//capped at 5% proc when mob is 18 (!!!) levels higher than you.
	uint8 chance = 95;
	if(PDefender->GetMLevel() > PAttacker->GetMLevel()){
		chance -= 5*(PDefender->GetMLevel() - PAttacker->GetMLevel());
		chance = cap_value(chance,5,95);
	}
	if(rand()%100 >= chance){return;}
	if(PAmmo==NULL){return;}

	switch(PAmmo->getID()){
	case 18700:{ //Wind Arrow
	//damage doesn't exceed ~67 unless wearing wind staff/iceday/weather
	//there isn't a formula, but INT affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply AGI with a degree of randomisation
			Action->subeffect = SUBEFFECT_WIND_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 3;
			//calculate damage
			uint8 damage = (PAttacker->AGI() - PDefender->AGI())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 18699:{ //Earth Arrow
	//damage doesn't exceed ~67 unless wearing Earth staff/earthsday/weather
	//there isn't a formula, but VIT affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply VIT with a degree of randomisation
			Action->subeffect = SUBEFFECT_EARTH_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 1;
			//calculate damage
			uint8 damage = (PAttacker->VIT() - PDefender->VIT())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 18698:{ //Water Arrow
	//damage doesn't exceed ~67 unless wearing light staff/iceday/weather
	//there isn't a formula, but INT affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply MND with a degree of randomisation
			Action->subeffect = SUBEFFECT_WATER_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 1;
			//calculate damage
			uint8 damage = (PAttacker->MND() - PDefender->MND())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 18158:{//Sleep Arrow
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_SLEEP) &&
				!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_SLEEP_II) &&
				!PDefender->isDead()){
			Action->subeffect = SUBEFFECT_SLEEP;
			Action->subparam  = EFFECT_SLEEP;
			Action->submessageID = 160;
			Action->flag = 3;
			int duration = 25 - (PDefender->GetMLevel() - PAttacker->GetMLevel());
			duration = cap_value(duration,1,25);
			PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_SLEEP,EFFECT_SLEEP,1,0,duration));
			}
		}
		break;
	case 18157:{ //Poison Arrow
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_POISON)){
				Action->subeffect = SUBEFFECT_POISON;
				Action->subparam  = EFFECT_POISON;
				Action->submessageID = 160;
				Action->flag = 1;
				//4hp/tick for 30secs
				PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_POISON,EFFECT_POISON,4,3,30));
			}
		}
		break;
	case 18153:{ //Holy Bolt
	//damage doesn't exceed ~67 unless wearing light staff/lightsday/weather
	//there isn't a formula, but MND affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply MND with a degree of randomisation
			Action->subeffect = SUBEFFECT_LIGHT_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 3;
			//calculate damage
			uint8 damage = (PAttacker->MND() - PDefender->MND())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 18151:{ //Bloody Bolt
	//INT/2 is a semi-confirmed damage calculation. Also affected by level of target. Resists strongly
	//and even doesn't proc on mobs strong to dark e.g. bats/skeles.
			Action->subeffect = SUBEFFECT_HP_DRAIN;
			Action->submessageID = 161;
			Action->flag = 3;
			int damage = (PAttacker->INT() - PDefender->INT())/2;
			damage += (PAttacker->GetMLevel() - PDefender->GetMLevel());
			damage = cap_value(damage,0,50);
			damage += PAttacker->GetMLevel()/2;
			damage += rand()%20; //At 75 -> 37~56 low or 87~106 high
			Action->subparam  = damage;
			PDefender->addHP(-damage);
			PAttacker->addHP(damage);
			charutils::UpdateHealth(PAttacker);
		}
		break;
	case 18152:{ //Venom Bolt
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_POISON)){
				Action->subeffect = SUBEFFECT_POISON;
				Action->subparam  = EFFECT_POISON;
				Action->submessageID = 160;
				Action->flag = 1;
				//4hp/tick for 30secs
				PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_POISON,EFFECT_POISON,4,3,30));
			}
		}
		break;
	case 18150:{//Blind Bolt
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_BLINDNESS)){
				Action->subeffect = SUBEFFECT_BLIND;
				Action->subparam  = EFFECT_BLINDNESS;
				Action->submessageID = 160;
				Action->flag = 1;
				PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_BLINDNESS,EFFECT_BLINDNESS,10,0,30));
			}
		}
		break;
	case 18149:{//Sleep Bolt
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_SLEEP) &&
				!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_SLEEP_II) &&
				!PDefender->isDead()){
			Action->subeffect = SUBEFFECT_SLEEP;
			Action->subparam  = EFFECT_SLEEP;
			Action->submessageID = 160;
			Action->flag = 3;
			int duration = 25 - (PDefender->GetMLevel() - PAttacker->GetMLevel());
			duration = cap_value(duration,1,25);
			PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_SLEEP,EFFECT_SLEEP,1,0,duration));
			}
		}
		break;
	case 18148:{ //Acid Bolt
			if(!PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_DEFENSE_DOWN)){
			Action->subeffect = SUBEFFECT_DEFENS_DOWN;
			Action->subparam  = EFFECT_DEFENSE_DOWN;
			Action->submessageID = 160;
			Action->flag = 1;
			PDefender->StatusEffectContainer->AddStatusEffect(
					new CStatusEffect(EFFECT_DEFENSE_DOWN,EFFECT_DEFENSE_DOWN,12,0,60));
			}
		}
		break;
	case 17324:{ //Lightning Arrow
	//damage doesn't exceed ~67
	//there isn't a formula. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply DEX with a degree of randomisation
			Action->subeffect = SUBEFFECT_LIGHTNING_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 3;
			//calculate damage
			uint8 damage = (PAttacker->DEX() - PDefender->DEX())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 17323:{ //Ice Arrow
	//damage doesn't exceed ~67 unless wearing ice staff/iceday/weather
	//there isn't a formula, but INT affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply INT with a degree of randomisation
			Action->subeffect = SUBEFFECT_ICE_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 1;
			//calculate damage
			uint8 damage = (PAttacker->INT() - PDefender->INT())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	case 17322:{ //Fire Arrow
	//damage doesn't exceed ~67 unless wearing ice staff/iceday/weather
	//there isn't a formula, but INT affects damage, so this is guesstimated. It seems to be level
	//invarient since its used on harder monsters for damage occasionally. Assuming the modifier
	//is simply INT with a degree of randomisation
			Action->subeffect = SUBEFFECT_FIRE_DAMAGE;
			Action->submessageID = 163;
			Action->flag = 3;
			//calculate damage
			uint8 damage = (PAttacker->INT() - PDefender->INT())/2;
			damage = cap_value(damage,0,50);
			damage += 10; //10~60
			damage += rand()%8; //10~67 randomised
			//set damage TODO: handle resist/staff/day
			Action->subparam  = damage;
			PDefender->addHP(-damage);
		}
		break;
	}
}

uint8 GetRangedHitRate(CBattleEntity* PAttacker, CBattleEntity* PDefender){
	int acc = 0;
	uint8 hitrate = 75;
	if(PAttacker->objtype == TYPE_PC){
		CCharEntity* PChar = (CCharEntity*)PAttacker;
		CItemWeapon* PItem = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_RANGED]);
		if(PItem!=NULL && (PItem->getType() & ITEM_WEAPON)){
			int skill = PChar->GetSkill(PItem->getSkillType());
			acc = skill;
			if(skill>200){ acc = 200 + (skill-200)*0.9;}
			acc += PChar->getMod(MOD_RACC);
			acc += PChar->AGI()/2;
			acc = ((100 +  PChar->getMod(MOD_RACCP)) * acc)/100 + 
				dsp_min(((100 +  PChar->getMod(MOD_FOOD_RACCP)) * acc)/100,  PChar->getMod(MOD_FOOD_RACC_CAP));
		}
	}
	else{//monster racc not handled yet
		return 95;
	}

	int eva = (PDefender->getMod(MOD_EVA) * (100 + PDefender->getMod(MOD_EVAP)))/100 + PDefender->AGI()/2;
	hitrate = hitrate + (acc - eva) / 2 + (PAttacker->GetMLevel() - PDefender->GetMLevel())*2;
	hitrate = cap_value(hitrate, 20, 95);
	return hitrate;
}

//todo: need to penalise attacker's RangedAttack depending on distance from mob. (% decrease)
float GetRangedPDIF(CBattleEntity* PAttacker, CBattleEntity* PDefender){
	//get ranged attack value
	uint16 rAttack = 1;
	if(PAttacker->objtype == TYPE_PC){
		CCharEntity* PChar = (CCharEntity*)PAttacker;
		CItemWeapon* PItem = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_RANGED]);
		if (PItem != NULL && (PItem->getType() & ITEM_WEAPON)){
			rAttack = PChar->RATT(PItem->getSkillType());
		}
		else{
			PItem = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_AMMO]);
			if (PItem == NULL || !(PItem->getType() & ITEM_WEAPON) || (PItem->getSkillType() != SKILL_THR)){
				ShowDebug("battleutils::GetRangedPDIF Cannot find a valid ranged weapon to calculate PDIF for. \n");
			}
			else{
				rAttack = PChar->RATT(PItem->getSkillType());
			}
		}
	}
	else{//assume mobs capped
		rAttack = battleutils::GetMaxSkill(SKILL_ARC,JOB_RNG,PAttacker->GetMLevel());
	}

	//get ratio (not capped for RAs)
	float ratio = (float)rAttack / (float)PDefender->DEF();

	//level correct (0.025 not 0.05 like for melee)
	if(PDefender->GetMLevel() > PAttacker->GetMLevel()){
		ratio -= 0.025f * (PDefender->GetMLevel() - PAttacker->GetMLevel());
	}
	if(ratio < 0) { ratio = 0; }
	if(ratio > 3) { ratio = 3; }

	//calculate min/max PDIF
	float minPdif = 0;
	float maxPdif = 0;
	if(ratio < 0.9){
		minPdif = ratio;
		maxPdif = (10.0f/9.0f) * ratio;
	}
	else if(ratio <= 1.1){
		minPdif = 1;
		maxPdif = 1;
	}
	else if(ratio <= 3){
		minPdif = (-3.0f/19.0f) + ((20.0f/19.0f) * ratio);
		maxPdif = ratio;
	}

	//return random number between the two
	return ((maxPdif-minPdif) * ((float)rand()/RAND_MAX)) + minPdif;
}

float CalculateBaseTP(int delay){
	float x = 1;
	if(delay<=180){
		x = 5.0f + (((float)delay-180.0f)*1.5f)/180.0f;
	}
	else if(delay<=450){
		x = 5.0f + (((float)delay-180.0f)*6.5f)/270.0f;
	}
	else if(delay<=480){
		x = 11.5f + (((float)delay-450.0f)*1.5f)/30.0f;
	}
	else if(delay<=530){
		x = 13.0f + (((float)delay-480.0f)*1.5f)/50.0f;
	}
	else{
		x = 14.5f + (((float)delay-530.0f)*3.5f)/470.0f;
	}
	return x;
}

bool IsParried(CBattleEntity* PAttacker, CBattleEntity* PDefender){
	if( PAttacker->GetMJob() == JOB_NIN || PAttacker->GetMJob() == JOB_SAM || 
		PAttacker->GetMJob() == JOB_THF || PAttacker->GetMJob() == JOB_BST || PAttacker->GetMJob() == JOB_DRG ||
		PAttacker->GetMJob() == JOB_PLD || PAttacker->GetMJob() == JOB_WAR || PAttacker->GetMJob() == JOB_BRD || 
		PAttacker->GetMJob() == JOB_DRK || PAttacker->GetMJob() == JOB_RDM || PAttacker->GetMJob() == JOB_COR){
		int skill = PDefender->GetSkill(SKILL_PAR)+PDefender->getMod(MOD_PARRY); //max A-, so need gear+ for 20% parry
		int max = GetMaxSkill(SKILL_SHL,JOB_PLD,PDefender->GetMLevel()); //A+ skill
		int parryrate = 20 * ((double)skill/(double)max);
		parryrate = cap_value(parryrate,1,20);//20% max parry rate
		return  (rand()%100 < parryrate);
	}
	return false;
}

bool TryInterruptSpell(CBattleEntity* PAttacker, CBattleEntity* PDefender){
	int base = 40; //Reasonable assumption for the time being.
	int diff = PAttacker->GetMLevel() - PDefender->GetMLevel();
	float check = base + diff;

	if(PDefender->objtype==TYPE_PC) { //Check player's skill.  
		//For mobs, we can assume their skill is capped at their level, so this term is 1 anyway.
		CCharEntity* PChar = (CCharEntity*) PDefender;
		float skill = PChar->GetSkill(PChar->PBattleAI->GetCurrentSpell()->getSkillType());
		if(skill <= 0) {
			skill = 1;
		}
			
		float cap = GetMaxSkill((SKILLTYPE)PChar->PBattleAI->GetCurrentSpell()->getSkillType(),
			PChar->GetMJob(),PChar->GetMLevel());
		if(skill > cap) {
			skill = cap;
		}
		float ratio = (float)cap/skill;
		check *= ratio;
	} 

	float aquaveil = ((float)((100.0f - (float)PDefender->getMod(MOD_SPELLINTERRUPT))/100.0f));
	check *= aquaveil;

	if(rand()%100 < check)  {
		//Mark for interruption.
		return true;
	}
	return false;
}

/***********************************************************************
		Calculates the block rate of the defender
Generally assumed to be a linear relationship involving shield skill and
'projected' skill (like with spell interruption). According to
www.bluegartr.com/threads/103597-Random-Facts-Thread/page22 it appears
to be BASE+(PLD_Skill - MOB_Skill)/4.6 where base is the base activation
for the given shield type (unknown). These are subject to caps (65% max
for non-Ochain shields) and presumably 5% min cap *untested*
Presuming base values 10%/20%/30%/40% (big->low)
They don't mention anything about caps on PLD_Skill-MOB_Skill but there
has to be, else a Lv75 PLD with 0 skill would never be able to skillup
as they need to be HIT to skillup, meaning they can't really lvl up on
low level monsters as they miss too much. Presuming a min cap of -10%.
************************************************************************/
uint8 GetBlockRate(CBattleEntity* PAttacker,CBattleEntity* PDefender){
	if(PDefender->objtype == TYPE_PC){
		CCharEntity* PChar = (CCharEntity*)PDefender;
		CItemArmor* PItem = (CItemArmor*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_SUB]);
		if(PItem!=NULL && PItem->getID()!=65535 && PItem->getShieldSize()>0 && PItem->getShieldSize()<=5){
			float chance = ((5-PItem->getShieldSize())*10.0f)+ //base
				dsp_max(((float)(PChar->GetSkill(SKILL_SHL)+PChar->getMod(MOD_SHIELD)-GetMaxSkill(SKILL_SHL,JOB_PLD,PAttacker->GetMLevel()))/4.6f),-10);
			//TODO: HANDLE OCHAIN
			if(PItem->getShieldSize()==5){return 65;}//aegis, presume capped? need info.
			//65% cap
			return cap_value(chance,5,65);
		}
	}
	return 0;
}

/************************************************************************
*																		*
*  Calculates damage based on damage and resistance to damage type		*
*																		*
************************************************************************/

uint16 TakePhysicalDamage(CBattleEntity* PAttacker, CBattleEntity* PDefender, int16 damage, bool isBlocked, uint8 slot, bool isUserTPGain)
{
	if (PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_INVINCIBLE) ||
		PDefender->StatusEffectContainer->HasStatusEffect(EFFECT_INVINCIBLE, 0))
	{
		damage = 0;
	}

    damage = (damage * (100 + PDefender->getMod(MOD_DMG) + PDefender->getMod(MOD_DMGPHYS))) / 100;
	
	switch(PAttacker->m_Weapons[slot]->getDmgType())
	{
		case DAMAGE_CROSSBOW:
		case DAMAGE_GUN:
		case DAMAGE_PIERCING: damage = (damage * (PDefender->getMod(MOD_PIERCERES))) / 1000; break;
		case DAMAGE_SLASHING: damage = (damage * (PDefender->getMod(MOD_SLASHRES)))	 / 1000; break;
		case DAMAGE_IMPACT:	  damage = (damage * (PDefender->getMod(MOD_IMPACTRES))) / 1000; break;
		case DAMAGE_HTH:	  damage = (damage * (PDefender->getMod(MOD_HTHRES)))	 / 1000; break;
	}

	//apply block
	if(isBlocked){
		// reduction calc source: www.bluegartr.com/threads/84830-Shield-Asstery
		if(PDefender->objtype == TYPE_PC){
			charutils::TrySkillUP((CCharEntity*)PDefender, SKILL_SHL, PAttacker->GetMLevel());
			CItemArmor* PItem = (CItemArmor*)((CCharEntity*)PDefender)->getStorage(LOC_INVENTORY)->GetItem(
											((CCharEntity*)PDefender)->equip[SLOT_SUB]);
			if(PItem!=NULL && PItem->getID()!=65535 &&  PItem->getShieldSize()>0){
				//get def amount (todo: find a better way?)
				uint8 shield_def = 0;
				for(int i=0; i<PItem->modList.size(); i++){
					if(PItem->modList[i]->getModID()==MOD_DEF){
						shield_def = PItem->modList[i]->getModAmount();
						break;
					}
				}
				float pdt = 0.5f * (float)shield_def;
				switch(PItem->getShieldSize()){
					case 1: pdt += 22; break; //Bucker 22%
					case 2: pdt += 40; break; //Round 40%
					case 3: pdt += 50; break; //Kite 50%
					case 4: pdt += 55; break; //Tower 55%
					case 5: pdt += 55; break; //Aegis
				}
				if(pdt>100){pdt=100;}
				damage = damage * ((100.0f - (float)pdt) / 100.0f);
			}
		}
	}

	damage = damage - PDefender->getMod(MOD_PHALANX);
	if(damage<0){
		damage = 0;
	}

	if(damage>0 && PDefender->getMod(MOD_STONESKIN) >= damage){
		PDefender->addModifier(MOD_STONESKIN, -damage);
		damage = 0;
		if(PDefender->getMod(MOD_STONESKIN)==0){
			//wear off
			PDefender->StatusEffectContainer->DelStatusEffect(EFFECT_STONESKIN);
		}
	}
	else if(damage>0 && PDefender->getMod(MOD_STONESKIN)>0 && PDefender->getMod(MOD_STONESKIN) < damage){
		damage = damage - PDefender->getMod(MOD_STONESKIN);
		PDefender->StatusEffectContainer->DelStatusEffect(EFFECT_STONESKIN);
	}

    PDefender->addHP(-damage);

    if (PAttacker->PMaster != NULL)
    {
        PDefender->m_OwnerID.id = PAttacker->PMaster->id;
        PDefender->m_OwnerID.targid = PAttacker->PMaster->targid; 
    }
    else
    {
        PDefender->m_OwnerID.id = PAttacker->id;
        PDefender->m_OwnerID.targid = PAttacker->targid; 
    }

    float TP = 0;

    if (damage > 0)
    {
        if (PDefender->PBattleAI->GetCurrentAction() == ACTION_MAGIC_CASTING &&
            PDefender->PBattleAI->GetCurrentSpell()->getSpellGroup() != SPELLGROUP_SONG)
        { //try to interrupt the spell
            if (TryInterruptSpell(PAttacker, PDefender))
            {
				if(PDefender->objtype == TYPE_PC){
					CCharEntity* PChar = (CCharEntity*) PDefender;
					PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, 0, 0, 16)); 
				}
                PDefender->PBattleAI->SetCurrentAction(ACTION_MAGIC_INTERRUPT);
            }
        }
		float baseTp = 0;
		if(slot==SLOT_RANGED && PAttacker->objtype == TYPE_PC){
			CCharEntity* PChar = (CCharEntity*)PAttacker;
			CItemWeapon* PRange = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_RANGED]);
			CItemWeapon* PAmmo = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_AMMO]);
			int delay = 0; uint16 offset = 240;
			if(PRange != NULL) { delay += PRange->getDelay(); }
			if(PAmmo != NULL) { delay += PAmmo->getDelay(); offset += 240; }
			baseTp = CalculateBaseTP(offset + ((delay * 60) / 1000));
		}
		else if(slot==SLOT_AMMO && PAttacker->objtype == TYPE_PC){
			//todo: e.g. pebbles
		}
		else{
			baseTp = CalculateBaseTP((PAttacker->m_Weapons[slot]->getDelay() * 60) / 1000);
		}

		if(isUserTPGain)
		{
			PAttacker->addTP(baseTp * (1.0f + 0.01f * (float)PAttacker->getMod(MOD_STORETP)));
		}
		//PAttacker->addTP(20);
		//account for attacker's subtle blow which reduces the baseTP gain for the defender
		baseTp = baseTp * ((100.0f - cap_value((float)PAttacker->getMod(MOD_SUBTLE_BLOW), 0.0f, 50.0f)) / 100.0f);

		//mobs hit get basetp+3 whereas pcs hit get basetp/3
		if(PDefender->objtype == TYPE_PC){
			//yup store tp counts on hits taken too!
			PDefender->addTP((baseTp / 3) * (1.0f + 0.01f * (float)PDefender->getMod(MOD_STORETP)));
		}
		else{
			PDefender->addTP((baseTp + 3) * (1.0f + 0.01f * (float)PDefender->getMod(MOD_STORETP)));
		}

        if (PAttacker->objtype == TYPE_PC)
        {
            charutils::UpdateHealth((CCharEntity*)PAttacker);
        }
    }
    switch (PDefender->objtype)
    {
        case TYPE_PC:
	    {
		   PDefender->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_DAMAGE);

		    if(PDefender->animation == ANIMATION_SIT)
		    {
			    PDefender->animation = ANIMATION_NONE;
                ((CCharEntity*)PDefender)->pushPacket(new CCharUpdatePacket((CCharEntity*)PDefender));
            }
            charutils::UpdateHealth((CCharEntity*)PDefender);
	    }
        break;
        case TYPE_MOB:
        {
			PDefender->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_DAMAGE);
            if (PDefender->PMaster == NULL)
            {
                PDefender->addTP(TP);
            }
            ((CMobEntity*)PDefender)->PEnmityContainer->UpdateEnmityFromDamage(PAttacker, damage);
        }
        break;
		case TYPE_PET:
        {
			((CPetEntity*)PDefender)->loc.zone->PushPacket(PDefender, CHAR_INRANGE, new CEntityUpdatePacket(PDefender, ENTITY_UPDATE));
		}
		break;
    }
    if (PAttacker->objtype == TYPE_PC)
    {
        PDefender->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_ATTACK);
    }
	return damage;
}

/************************************************************************
*																		*
*  Calculate Power of Damage Spell										*
*																		*
************************************************************************/

uint16 TakeMagicDamage(CBattleEntity* PAttacker, CBattleEntity* PDefender) 
{
	DSP_DEBUG_BREAK_IF(PAttacker->PBattleAI->GetCurrentSpell() == NULL);
    DSP_DEBUG_BREAK_IF(PAttacker->PBattleAI->GetCurrentAction() != ACTION_MAGIC_FINISH);
	
	CSpell* PSpell = PAttacker->PBattleAI->GetCurrentSpell();

	int32 INT  = PAttacker->INT() - PDefender->INT();
	int32 base = PSpell->getBase();
	float M    = PSpell->getMultiplier();

	int32 damage = INT < 0 ? base + INT : base + (INT * M); 
	
    damage = damage * (100 - (10 * PAttacker->m_ActionList.size() / 2)) / 100;
	damage = damage * (1000 - PDefender->getMod(MOD_FIRERES + PSpell->getElement())) / 1000;
	
	PDefender->addHP(-damage);
	
    if (PAttacker->PMaster != NULL)
    {
        PDefender->m_OwnerID.id = PAttacker->PMaster->id;
        PDefender->m_OwnerID.targid = PAttacker->PMaster->targid; 
    }
    else
    {
        PDefender->m_OwnerID.id = PAttacker->id;
        PDefender->m_OwnerID.targid = PAttacker->targid; 
    }
	
	switch (PDefender->objtype)
	{
		case TYPE_PC:
		{
            PDefender->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_DAMAGE);
			
			if(PDefender->animation == ANIMATION_SIT)
		    {
			    PDefender->animation = ANIMATION_NONE;
                ((CCharEntity*)PDefender)->pushPacket(new CCharUpdatePacket((CCharEntity*)PDefender));
            }
            charutils::UpdateHealth((CCharEntity*)PDefender);
		}
		break;
		case TYPE_MOB:
		{
			((CMobEntity*)PDefender)->PEnmityContainer->UpdateEnmityFromDamage(PAttacker, damage); 
		}
		break;
	}
	return damage;
}

/************************************************************************
*																		*
*  Calculate Probability attack will hit (20% min cap - 95% max cap)	*
*																		*
************************************************************************/

uint8 GetHitRate(CBattleEntity* PAttacker, CBattleEntity* PDefender) 
{
    int32 hitrate = 75;

    if (PAttacker->objtype == TYPE_PC && PAttacker->StatusEffectContainer->HasStatusEffect(EFFECT_SNEAK_ATTACK))
    {
		hitrate = 100; //attack with SA active cannot miss
	}
    else
    {
		int32 defendereva = (PDefender->getMod(MOD_EVA) * (100 + PDefender->getMod(MOD_EVAP)))/100 + PDefender->AGI()/2;
		int32 attackeracc = (PAttacker->getMod(MOD_ACC) * (100 + PAttacker->getMod(MOD_ACCP)))/100 + PAttacker->DEX()/2;
		
		hitrate = hitrate + (attackeracc - defendereva) / 2 + (PAttacker->GetMLevel() - PDefender->GetMLevel())*2;

		hitrate = cap_value(hitrate, 20, 95);
    }
	return (uint8)hitrate;
}

/************************************************************************
*																		*
*  Crit Rate															*
*																		*
************************************************************************/

uint8 GetCritHitRate(CBattleEntity* PAttacker, CBattleEntity* PDefender)
{
	int32 crithitrate = 5;
	if(PAttacker->StatusEffectContainer->HasStatusEffect(EFFECT_MIGHTY_STRIKES,0) || 
		PAttacker->StatusEffectContainer->HasStatusEffect(EFFECT_MIGHTY_STRIKES)){
			return 100;
	}
	else if (PAttacker->objtype == TYPE_PC && PAttacker->StatusEffectContainer->HasStatusEffect(EFFECT_SNEAK_ATTACK))
	{	
        // TODO: WRONG CALCULATION OF A POSITION OF THE CHARACTER

		if(abs(PDefender->loc.p.rotation - PAttacker->loc.p.rotation) < 23)
		{
			crithitrate = 100;
		}
	}
	else
	{
		int32 attackerdex = PAttacker->DEX();
		int32 defenderagi = PDefender->AGI();

		int32 dDEX = cap_value(attackerdex - defenderagi,0,50);

		crithitrate += (dDEX * 30) / 100 + PAttacker->getMod(MOD_CRITHITRATE) + PDefender->getMod(MOD_ENEMYCRITRATE);
		crithitrate  = cap_value(crithitrate,0,100);
	}
	return (uint8)crithitrate;
}

/************************************************************************
*																		*
*	Formula for calculating damage ratio								*
*																		*
************************************************************************/

float GetDamageRatio(CBattleEntity* PAttacker, CBattleEntity* PDefender, bool isCritical)  
{
	//wholly possible for DEF to be near 0 with the amount of debuffs/effects now.
    float ratio = (float)PAttacker->ATT() / (float)((PDefender->DEF()==0) ? 1 : PDefender->DEF());
	float cRatioMax = 0;
	float cRatioMin = 0;

	float cap = 2.0f;
	if(PAttacker->objtype==TYPE_PC){
		switch(PAttacker->m_Weapons[SLOT_MAIN]->getSkillType()){
		case SKILL_GAX:
		case SKILL_GSD:
		case SKILL_GKT:
		case SKILL_POL:
		case SKILL_SYH:
		case SKILL_STF:
			cap = 2.2f;
			break;
		}
	}
	if(PAttacker->objtype == TYPE_MOB){
		cap = 2.2f; //simply set for the 2h calc further on
	}


	ratio = cap_value(ratio,0,cap);
	//2hs have more of a 'buffer' (0.2 more) for level correction than 1hs
	float cRatio = ratio;
	if(PAttacker->objtype == TYPE_PC) 
	{
		if(PAttacker->GetMLevel() < PDefender->GetMLevel()) 
		{
			cRatio -= 0.050f * (PDefender->GetMLevel() - PAttacker->GetMLevel());
		}
	}
	//but its still capped
	cRatio = cap_value(cRatio,0,2);

	if(cap==2.0f){//1h weapon algorithm source: PChan @ BG (aka reliable)
		if((0 <= cRatio) && (cRatio < 0.5)) {
			cRatioMax = 1.0f + ((10.0f/9.0f)*(cRatio-0.5f));
		} else if((0.5 <= cRatio) && (cRatio <= 0.75f)) {
			cRatioMax = 1.0f;
		} else if((0.75f < cRatio) && (cRatio <= 2)) {
			cRatioMax = 1.0f + ((10.0f/9.0f)*(cRatio-0.75f));
		}

		if((0 <= cRatio) && (cRatio < 0.5)) {
			cRatioMin =  (float)(1.0f/6.0f);
		} else if((0.5 <= cRatio) && (cRatio <= 1.25)) {
			cRatioMin = 1.0f + ((10.0f/9.0f)*(cRatio-1.25));
		} else if((1.25 < cRatio) && (cRatio <= 1.5)) {
			cRatioMin = 1.0f;
		} else if((1.5 < cRatio) && (cRatio <= 2)) {
			cRatioMin = 1.0f + ((10.0f/9.0f)*(cRatio-1.5));
		}
	}
	else{//2h weapon
		if((0 <= cRatio) && (cRatio < 0.5)) {
			cRatioMax = 0.4f + 1.2f * cRatio;
		} else if((0.5 <= cRatio) && (cRatio <= (5.0f/6.0f))) {
			cRatioMax = 1;
		} else if(((5.0f/6.0f) < cRatio) && (cRatio <= (10.0f/6.0f))) {
			cRatioMax = 1.25f * (cRatio);
		} else if(((10.0f/6.0f) < cRatio) && (cRatio <= 2)) {
			cRatioMax = 1.2f * (cRatio);
		}

		if((0 <= cRatio) && (cRatio < 1.25)) {
			cRatioMin =  (float)(-0.5 + 1.2 * cRatio);
		} else if((1.25 <= cRatio) && (cRatio <= 1.5)) {
			cRatioMin = 1;
		} else if((1.5 < cRatio) && (cRatio <= 2)) {
			cRatioMin = (float)(-0.8 + 1.2 * cRatio);
		}
	}

	cRatioMin = (cRatioMin < 0 ? 0 : cRatioMin);

	if(isCritical){
		cRatioMin += 1;
		cRatioMax += 1;
	}

	cRatioMax = (cRatioMax > 3 ? 3 : cRatioMax);
	float pDIF = ((cRatioMax-cRatioMin) * ((float)rand()/RAND_MAX)) + cRatioMin;

	//x1.00 ~ x1.05 final multiplier, giving max value 3*1.05 -> 3.15
	return pDIF * (1+((0.5f) * ((float)rand()/RAND_MAX)));
}

/************************************************************************
*  	Formula for Strength												*
************************************************************************/

int32 GetFSTR(CBattleEntity* PAttacker, CBattleEntity* PDefender, uint8 SlotID) 
{
	int32 rank = PAttacker->m_Weapons[SlotID]->getDamage() / 9; 

	float dif = PAttacker->STR() - PDefender->VIT();

	int32 fstr = 1.95 + 0.195 * dif;

	if(SlotID==SLOT_RANGED){ //different caps than melee weapons
		fstr /= 2; //fSTR2
		if(fstr <= (-rank*2)){
			return (-rank*2);
		}
		if((fstr > (-rank*2)) && (fstr <= (2*(rank + 8)))) {
			return fstr;
		} else {
			return 2*(rank + 8);
		}
	}
	else{
		if(fstr <= (-rank)) {
			return (-rank);
		}
		if((fstr > (-rank)) && (fstr <= rank + 8)) {
			return fstr;
		} else {
			return rank + 8;
		}
	}
}
/*****************************************************************************
	Handles song buff effects. Returns true if the song has been handled
	or false if the song effect has not been implemented. This is used in
	luautils to check if it needs to load a spell script or not.
******************************************************************************/
bool SingSong(CBattleEntity* PCaster,CBattleEntity* PTarget,CSpell* PSpell){
	uint8 tier = 1;
	EFFECT effect = EFFECT_NONE;
	uint8 tick = 0;

	//calculate strengths. Need to know TIER and EFFECTTYPE (Minuet, Paeon, etc) for icon
	if(PSpell->getID() >= 394 && PSpell->getID() <= 398){ 
		effect = EFFECT_MINUET;
		tier = PSpell->getID()-393;
	}
	else if(PSpell->getID() >= 389 && PSpell->getID() <= 393){ 
		effect = EFFECT_MINNE;
		tier = PSpell->getID()-388;
	}
	else if(PSpell->getID() >= 399 && PSpell->getID() <= 400){ 
		effect = EFFECT_MADRIGAL;
		tier = PSpell->getID()-398;
	}
	else if(PSpell->getID() >= 403 && PSpell->getID() <= 404){ 
		effect = EFFECT_MAMBO;
		tier = PSpell->getID()-382;
	}
	else if(PSpell->getID() >= 386 && PSpell->getID() <= 388){ 
		effect = EFFECT_BALLAD;
		tier = PSpell->getID()-385;
		tick = 3;
	}
	else if(PSpell->getID() >= 419 && PSpell->getID() <= 420){ 
		effect = EFFECT_MARCH;
		tier = PSpell->getID()-418;
	}
	else if(PSpell->getID() >= 378 && PSpell->getID() <= 385){ 
		effect = EFFECT_PAEON;
		tier = PSpell->getID()-377;
		tick = 3;
	}

	if(effect==EFFECT_NONE){
		return false;
	}
	//TODO: Handle instruments!

	CStatusEffect* PStatus = new CStatusEffect(effect,effect,tier,tick,120,PCaster->targid);
	PStatus->SetFlag(EFFECTFLAG_ON_ZONE);//wears on zone
	
	uint8 maxSongs = 2;
	if(PCaster->objtype==TYPE_PC){
		CCharEntity* PChar = (CCharEntity*)PCaster;
		CItemWeapon* PItem = (CItemWeapon*)PChar->getStorage(LOC_INVENTORY)->GetItem(PChar->equip[SLOT_RANGED]);
		if(PItem==NULL || PItem->getID()==65535 || !(PItem->getSkillType()==SKILL_STR || PItem->getSkillType()==SKILL_WND || PItem->getSkillType()==47) ){
			//TODO: Remove check for Skilltype=47, its a DB error should be 41 (String)!!
			maxSongs = 1;
		}
		else{
			//handle skillups
			if(PItem->getSkillType()==SKILL_STR || PItem->getSkillType()==47){
				charutils::TrySkillUP(PChar,SKILL_STR,PChar->GetMLevel());
			}
			else if(PItem->getSkillType()==SKILL_WND){
				charutils::TrySkillUP(PChar,SKILL_WND,PChar->GetMLevel());
			}
		}
	}

	if(PTarget->StatusEffectContainer->ApplyBardEffect(PStatus,maxSongs)){
		//ShowDebug("Applied %s! \n",PSpell->getName()); 
	}
	return true;
}


/************************************************************************
*                                                                       *
*  Chance paralysis will cause you to be paralyzed                      *
*                                                                       *
************************************************************************/

bool IsParalised(CBattleEntity* PAttacker)
{
	return (rand()%100 < cap_value(PAttacker->getMod(MOD_PARALYZE) - PAttacker->getMod(MOD_PARALYZERES), 0, 100));
}

/************************************************************************
*                                                                       *
*                                                                       *
*                                                                       *
************************************************************************/

bool IsAbsorbByShadow(CBattleEntity* PDefender)
{
	//utsus always overwrites blink, so if utsus>0 then we know theres no blink.
    uint16 Shadow = PDefender->getMod(MOD_UTSUSEMI);
	uint16 modShadow = MOD_UTSUSEMI;
	if(Shadow==0){
		Shadow=PDefender->getMod(MOD_BLINK);
		modShadow = MOD_BLINK;
		//random chance, assume 80% proc
		if(rand()%100 < 20){
			return false;
		}
	}

    if (Shadow > 0) 
    {
        PDefender->setModifier(modShadow, --Shadow);

        if (Shadow == 0)
        {
			switch(modShadow){
			case MOD_UTSUSEMI:
				PDefender->StatusEffectContainer->DelStatusEffect(EFFECT_COPY_IMAGE);
				break;
			case MOD_BLINK:
				PDefender->StatusEffectContainer->DelStatusEffect(EFFECT_BLINK);
				break;
			}
        }
        else if (Shadow < 4 && MOD_UTSUSEMI==modShadow)
        {
            if (PDefender->objtype == TYPE_PC)
            {
                CStatusEffect* PStatusEffect = PDefender->StatusEffectContainer->GetStatusEffect(EFFECT_COPY_IMAGE, 0);

                if (PStatusEffect != NULL)
                {
                    uint16 icon = EFFECT_COPY_IMAGE_3;
                    switch (PDefender->getMod(MOD_UTSUSEMI))
                    {
                        case 1: icon = EFFECT_COPY_IMAGE_1; break;
                        case 2: icon = EFFECT_COPY_IMAGE_2; break;
                    }
                    PStatusEffect->SetIcon(icon);
                    PDefender->StatusEffectContainer->UpdateStatusIcons();
                }
            }
        }
        return true;
    }

    return false;
}

/************************************************************************
*																		*
*  Intimidation from Killer Effects (chance to intimidate)				*
*																		*
************************************************************************/

bool IsIntimidated(CBattleEntity* PAttacker, CBattleEntity* PDefender)
{
	uint16 KillerEffect = 0;

	switch (PAttacker->m_EcoSystem)
	{
		case SYSTEM_AMORPH:		KillerEffect = PDefender->getMod(MOD_AMORPH_KILLER);   break;
		case SYSTEM_AQUAN:		KillerEffect = PDefender->getMod(MOD_AQUAN_KILLER);    break;
		case SYSTEM_ARCANA:		KillerEffect = PDefender->getMod(MOD_ARCANA_KILLER);   break;
		case SYSTEM_BEAST:		KillerEffect = PDefender->getMod(MOD_BEAST_KILLER);    break;
		case SYSTEM_BIRD:		KillerEffect = PDefender->getMod(MOD_BIRD_KILLER);     break;
		case SYSTEM_DEMON:		KillerEffect = PDefender->getMod(MOD_DEMON_KILLER);    break;
		case SYSTEM_DRAGON:		KillerEffect = PDefender->getMod(MOD_DRAGON_KILLER);   break;
		case SYSTEM_EMPTY:		KillerEffect = PDefender->getMod(MOD_EMPTY_KILLER);    break;
        case SYSTEM_HUMANOID:	KillerEffect = PDefender->getMod(MOD_HUMANOID_KILLER); break;
		case SYSTEM_LIZARD:		KillerEffect = PDefender->getMod(MOD_LIZARD_KILLER);   break;
        case SYSTEM_LUMINION:   KillerEffect = PDefender->getMod(MOD_LUMINION_KILLER); break;
        case SYSTEM_LUMORIAN:   KillerEffect = PDefender->getMod(MOD_LUMORIAN_KILLER); break;
		case SYSTEM_PLANTOID:	KillerEffect = PDefender->getMod(MOD_PLANTOID_KILLER); break;
		case SYSTEM_UNDEAD:		KillerEffect = PDefender->getMod(MOD_UNDEAD_KILLER);   break;
		case SYSTEM_VERMIN:		KillerEffect = PDefender->getMod(MOD_VERMIN_KILLER);   break;
	}
	return (rand()%100 < KillerEffect);
}

/************************************************************************
*                                                                       *
*  Moves mob  - mode 1 = walk / 2 = run                                 *
*                                                                       *
************************************************************************/

void MoveTo(CBattleEntity* PEntity, position_t pos, uint8 mode)
{
	DSP_DEBUG_BREAK_IF(mode < 1 || mode > 2);

    // TODO: не учитывается модификатор передвижения PEntity->getMod(MOD_MOVE)

	if (PEntity->speed != 0)
	{
		float angle = (1 - (float)PEntity->loc.p.rotation / 255) * 6.28318f;

		PEntity->loc.p.x += (cosf(angle) * ((float)PEntity->speed/0x28) * (mode) * 1.08);
		PEntity->loc.p.y = pos.y;
		PEntity->loc.p.z += (sinf(angle) * ((float)PEntity->speed/0x28) * (mode) * 1.08);

		PEntity->loc.p.moving += ((0x36*((float)PEntity->speed/0x28)) - (0x14*(mode - 1)));

		if(PEntity->loc.p.moving > 0x2fff) 
		{
			PEntity->loc.p.moving = 0;
		}
	}
}

/****************************************************************
*	Determine if an enfeeble spell will land - untested			*
****************************************************************/

bool EnfeebleHit(CBattleEntity* PCaster, CBattleEntity* PDefender, EFFECT Effect)
{

	int16 dlvl = (PCaster->GetMLevel() - PDefender->GetMLevel());
	int16 maxCap = 90;
	int16 minCap = 10; 
	int16 chance = 40 + (dlvl*5);

	chance = (chance > maxCap ? maxCap : chance);
	chance = (chance < minCap ? minCap : chance);
	if (Effect > 1 && Effect < 15)
	{
		chance = chance + (PDefender->getMod((MODIFIER)(Effect + 238)) / 10);
	}

	if (rand()%100 < chance)
	{
		return true;
	}

	return false;
}

/************************************************************************
*																		*
*  Gets SkillChain Effect												*
*																		*
************************************************************************/

SUBEFFECT GetSkillChainEffect(CBattleEntity* PDefender, CWeaponSkill* PWeaponSkill, uint16* outChainCount)
{
    CStatusEffect* PEffect = PDefender->StatusEffectContainer->GetStatusEffect(EFFECT_SKILLCHAIN, 0);

    if (PEffect == NULL)
    {
        PDefender->StatusEffectContainer->AddStatusEffect(new CStatusEffect(EFFECT_SKILLCHAIN, 0, PWeaponSkill->getElement() + NO_CHAIN, 0, 6));
        return SUBEFFECT_NONE;
    }
    else
    {
        PEffect->SetStartTime(gettick());

        uint16 chainCountMask = battleutils::GetSkillChainCountFlag(PEffect->GetPower());

        if(!(chainCountMask & CHAIN5))
            chainCountMask = chainCountMask << 1; // Shift left by one to increment the skill chain counter;

        (*outChainCount) = battleutils::GetSkillChainCount(chainCountMask);

        if (PEffect->GetPower() & LIGHT)
        {
            if (PEffect->GetPower() & FIRE)
            {
                if (PWeaponSkill->hasElement(DARK + EARTH)) 
                {
                    PEffect->SetPower(DARK + EARTH + chainCountMask);
                    return SUBEFFECT_GRAVITATION;
                }
                if (PWeaponSkill->hasElement(THUNDER + WIND)) 
                {
                    PEffect->SetPower(LIGHT + FIRE + THUNDER + WIND + chainCountMask);
                    return SUBEFFECT_LIGHT;
                }
            }
            if (PWeaponSkill->hasElement(EARTH)) 
            {
                PEffect->SetPower(WATER + ICE + chainCountMask);
                return SUBEFFECT_DISTORTION;
            }
            if (PWeaponSkill->hasElement(DARK)) 
            {
                PEffect->SetPower(DARK + chainCountMask);
                return SUBEFFECT_COMPRESSION;
            }
            if (PWeaponSkill->hasElement(WATER)) 
            {
                PEffect->SetPower(WATER + chainCountMask);
                return SUBEFFECT_REVERBERATION;
            }
	    }
        if (PEffect->GetPower() & DARK)
        {
            if (PEffect->GetPower() & EARTH)
            {
                if (PWeaponSkill->hasElement(THUNDER + WIND)) 
                {
                    PEffect->SetPower(THUNDER + WIND + chainCountMask);
                    return SUBEFFECT_FRAGMENTATION;
                }
                if (PWeaponSkill->hasElement(WATER + ICE)) 
                {
                    PEffect->SetPower(THUNDER + WIND + WATER + ICE + chainCountMask);
                    return SUBEFFECT_DARKNESS;
                }
            }
            if (PWeaponSkill->hasElement(WIND)) 
            {
                PEffect->SetPower(WIND + chainCountMask);
                return SUBEFFECT_DETONATION;
            }
            if (PWeaponSkill->hasElement(LIGHT)) 
            {
                PEffect->SetPower(LIGHT + chainCountMask);
                return SUBEFFECT_TRANSFIXION;
            }
	    }
        if (PEffect->GetPower() & FIRE)
        {
            if (PWeaponSkill->hasElement(THUNDER)) 
            {
                PEffect->SetPower(LIGHT + FIRE + chainCountMask);
                return SUBEFFECT_FUSION;
            }
            if (PWeaponSkill->hasElement(EARTH)) 
            {
                PEffect->SetPower(EARTH + chainCountMask);
                return SUBEFFECT_SCISSION;
            }
	    }
        if (PEffect->GetPower() & EARTH)
        {
            if (PWeaponSkill->hasElement(WIND)) 
            {
                PEffect->SetPower(WIND + chainCountMask);
                return SUBEFFECT_DETONATION;
            }
            if (PWeaponSkill->hasElement(WATER)) 
            {
                PEffect->SetPower(WATER + chainCountMask);
                return SUBEFFECT_REVERBERATION;
            }
            if (PWeaponSkill->hasElement(FIRE)) 
            {
                PEffect->SetPower(FIRE + chainCountMask);
                return SUBEFFECT_LIQUEFACATION;
            }
	    }
        if (PEffect->GetPower() & THUNDER)
        {
            if (PEffect->GetPower() & WIND &&
                PWeaponSkill->hasElement(WATER + ICE)) 
            {
                PEffect->SetPower(WATER + ICE + chainCountMask);
                return SUBEFFECT_DISTORTION;
            }
            if (PWeaponSkill->hasElement(WIND)) 
            {
                PEffect->SetPower(WIND + chainCountMask);
                return SUBEFFECT_DETONATION;
            }
            if (PWeaponSkill->hasElement(FIRE)) 
            {
                PEffect->SetPower(FIRE + chainCountMask);
                return SUBEFFECT_LIQUEFACATION;
            }
        }
        if (PEffect->GetPower() & WATER)
        {
            if (PEffect->GetPower() & ICE &&
                PWeaponSkill->hasElement(LIGHT + FIRE))
            {
                PEffect->SetPower(LIGHT + FIRE + chainCountMask);
                return SUBEFFECT_FUSION;
            }
            if (PWeaponSkill->hasElement(ICE)) 
            {
                PEffect->SetPower(ICE + chainCountMask);
                return SUBEFFECT_INDURATION;
            }
            if (PWeaponSkill->hasElement(THUNDER)) 
            {
                PEffect->SetPower(THUNDER + chainCountMask);
                return SUBEFFECT_IMPACTION;
            }
        }
        if (PEffect->GetPower() & WIND)
        {
            if (PWeaponSkill->hasElement(DARK)) 
            {
                PEffect->SetPower(DARK + EARTH + chainCountMask);
                return SUBEFFECT_GRAVITATION;
            }
            if (PWeaponSkill->hasElement(EARTH)) 
            {
                PEffect->SetPower(EARTH + chainCountMask);
                return SUBEFFECT_SCISSION;
            }
        }
        if (PEffect->GetPower() & ICE)
        {
            if (PWeaponSkill->hasElement(WATER))
            {
                PEffect->SetPower(THUNDER + WIND + chainCountMask);
                return SUBEFFECT_FRAGMENTATION;
            }
            if (PWeaponSkill->hasElement(DARK)) 
            {
                PEffect->SetPower(DARK + chainCountMask);
                return SUBEFFECT_COMPRESSION;
            }
            if (PWeaponSkill->hasElement(THUNDER))
            {
                PEffect->SetPower(THUNDER + chainCountMask);
                return SUBEFFECT_IMPACTION;
            }
        }
        PEffect->SetPower(PWeaponSkill->getElement() + NO_CHAIN);
        (*outChainCount) = 0;
    }
    (*outChainCount) = 0;
    return SUBEFFECT_NONE;;
}

SKILLCHAINFLAG GetSkillChainCountFlag(uint16 flags)
{
    if(flags & CHAIN1) return CHAIN1;
    if(flags & CHAIN2) return CHAIN2;
    if(flags & CHAIN3) return CHAIN3;
    if(flags & CHAIN4) return CHAIN4;
    if(flags & CHAIN5) return CHAIN5;

    return NO_CHAIN;
}

uint8 GetSkillChainCount(uint16 flags)
{
    if(flags & CHAIN1) return 1;
    if(flags & CHAIN2) return 2;
    if(flags & CHAIN3) return 3;
    if(flags & CHAIN4) return 4;
    if(flags & CHAIN5) return 5;

    return 0;
}

uint16 TakeSkillchainDamage(CBattleEntity* PAttacker, CBattleEntity* PDefender, SUBEFFECT effect, uint16 chainCount, uint16 lastSkillDamage)
{
    DSP_DEBUG_BREAK_IF(PAttacker == NULL);
    DSP_DEBUG_BREAK_IF(PDefender == NULL);
    DSP_DEBUG_BREAK_IF(chainCount <= 0 || chainCount > 5);

    // Determine the skill chain level and elemental resistance.
    int16 resistance = 0;
    uint16 chainLevel = 0;
    switch(effect)
    {
        // Level 1 skill chains
        case SUBEFFECT_LIQUEFACATION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_FIRERES);
            break;

        case SUBEFFECT_IMPACTION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_THUNDERRES);
            break;

        case SUBEFFECT_DETONATION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_WINDRES);
            break;

        case SUBEFFECT_SCISSION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_EARTHRES);
            break;

        case SUBEFFECT_REVERBERATION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_WATERRES);
            break;

        case SUBEFFECT_INDURATION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_ICERES);
            break;

        case SUBEFFECT_COMPRESSION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_DARKRES);
            break;

        case SUBEFFECT_TRANSFIXION:
            chainLevel = 1;
            resistance = PDefender->getMod(MOD_LIGHTRES);
            break;

        // Level 2 skill chains
        case SUBEFFECT_FUSION:
            chainLevel = 2;
            resistance = std::max(PDefender->getMod(MOD_FIRERES), PDefender->getMod(MOD_LIGHTRES));
            break;

        case SUBEFFECT_FRAGMENTATION:
            chainLevel = 2;
            resistance = std::max(PDefender->getMod(MOD_WINDRES), PDefender->getMod(MOD_THUNDERRES));
            break;

        case SUBEFFECT_GRAVITATION:
            chainLevel = 2;
            resistance = std::max(PDefender->getMod(MOD_EARTHRES), PDefender->getMod(MOD_DARKRES));
            break;

        case SUBEFFECT_DISTORTION:
            chainLevel = 2;
            resistance = std::max(PDefender->getMod(MOD_ICERES), PDefender->getMod(MOD_WATERRES));
            break;
    
        // Level 3 skill chains
        case SUBEFFECT_LIGHT:
            chainLevel = 3;
            resistance = std::max(std::max(PDefender->getMod(MOD_FIRERES), PDefender->getMod(MOD_WINDRES)), std::max(PDefender->getMod(MOD_THUNDERRES), PDefender->getMod(MOD_LIGHTRES)));
            break;

        case SUBEFFECT_DARKNESS:
            chainLevel = 3;
            resistance = std::max(std::max(PDefender->getMod(MOD_ICERES), PDefender->getMod(MOD_EARTHRES)), std::max(PDefender->getMod(MOD_WATERRES), PDefender->getMod(MOD_DARKRES)));
            break;
    
        default:
            DSP_DEBUG_BREAK_IF(true);
            break;
    }

    DSP_DEBUG_BREAK_IF(chainLevel <= 0 || chainLevel > 3);

    // Skill chain damage = (Closing Damage) 
    //                      × (Skill chain Level/Number from Table) 
    //                      × (1 + Skill chain Bonus ÷ 100) 
    //                      × (1 + Skill chain Damage + %/100) 
    //            TODO:     × (1 + Day/Weather bonuses) 
    //            TODO:     × (1 + Staff Affinity)

    uint32 damage = floor((double)lastSkillDamage
                          * g_SkillChainDamageModifiers[chainLevel][chainCount] / 1000
                          * (100 + PAttacker->getMod(MOD_SKILLCHAINBONUS)) / 100
                          * (100 + PAttacker->getMod(MOD_SKILLCHAINDMG)) / 100);

    damage = damage * (1000 - resistance) / 1000;

    PDefender->addHP(-damage);

    if (PAttacker->PMaster != NULL)
    {
        PDefender->m_OwnerID.id = PAttacker->PMaster->id;
        PDefender->m_OwnerID.targid = PAttacker->PMaster->targid; 
    }
    else
    {
        PDefender->m_OwnerID.id = PAttacker->id;
        PDefender->m_OwnerID.targid = PAttacker->targid; 
    }

    switch (PDefender->objtype)
    {
        case TYPE_PC:
        {
            PDefender->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_DAMAGE);

            if(PDefender->animation == ANIMATION_SIT)
            {
                PDefender->animation = ANIMATION_NONE;
                ((CCharEntity*)PDefender)->pushPacket(new CCharUpdatePacket((CCharEntity*)PDefender));
            }

            charutils::UpdateHealth((CCharEntity*)PDefender);
        }
        break;

        case TYPE_MOB:
        {
            ((CMobEntity*)PDefender)->PEnmityContainer->UpdateEnmityFromDamage(PAttacker, damage); 
        }
        break;
    }

    return damage;
}

}; 
