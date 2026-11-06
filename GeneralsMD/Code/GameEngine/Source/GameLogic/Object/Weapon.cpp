/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Weapon.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   Weapon descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_DEATH_NAMES
#define DEFINE_WEAPONBONUSCONDITION_NAMES
#define DEFINE_WEAPONBONUSFIELD_NAMES
#define DEFINE_WEAPONCOLLIDEMASK_NAMES
#define DEFINE_WEAPONAFFECTSMASK_NAMES
#define DEFINE_WEAPONRELOAD_NAMES
#define DEFINE_WEAPONPREFIRE_NAMES
#define DEFINE_INTERCEPTORCLASS_NAMES

#include "Common/crc.h"
#include "Common/CRCDebug.h"
#include "Common/GameAudio.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"

#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"

#include "GameLogic/Damage.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/LaserUpdate.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/SpecialPowerCompletionDie.h"
#include "GameLogic/Module/AssaultTransportAIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AssistedTargetingUpdate.h"
#include "GameLogic/Module/ProjectileStreamUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/HitChanceBehavior.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/TerrainLogic.h"

#define RATIONALIZE_ATTACK_RANGE
#define ATTACK_RANGE_IS_2D

#ifdef ATTACK_RANGE_IS_2D
	const DistanceCalculationType ATTACK_RANGE_CALC_TYPE = FROM_BOUNDINGSPHERE_2D;
#else
	const DistanceCalculationType ATTACK_RANGE_CALC_TYPE = FROM_BOUNDINGSPHERE_3D;
#endif


// damage is ALWAYS 3d
const DistanceCalculationType DAMAGE_RANGE_CALC_TYPE = FROM_BOUNDINGSPHERE_3D;

//-------------------------------------------------------------------------------------------------
static void parsePerVetLevelAsciiString( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	AsciiString* s = (AsciiString*)store;
	VeterancyLevel v = (VeterancyLevel)INI::scanIndexList(ini->getNextToken(), TheVeterancyNames);
	s[v] = ini->getNextAsciiString();
}

//-------------------------------------------------------------------------------------------------
static void parseAllVetLevelsAsciiString( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	AsciiString* s = (AsciiString*)store;
	AsciiString a = ini->getNextAsciiString();
	for (Int i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
		s[i] = a;
}

//-------------------------------------------------------------------------------------------------
static void parsePerVetLevelFXList( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	typedef const FXList* ConstFXListPtr;
	ConstFXListPtr* s = (ConstFXListPtr*)store;
	VeterancyLevel v = (VeterancyLevel)INI::scanIndexList(ini->getNextToken(), TheVeterancyNames);
	const FXList* fx = nullptr;
	INI::parseFXList(ini, nullptr, &fx, nullptr);
	s[v] = fx;
}

//-------------------------------------------------------------------------------------------------
static void parseAllVetLevelsFXList( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	typedef const FXList* ConstFXListPtr;
	ConstFXListPtr* s = (ConstFXListPtr*)store;
	const FXList* fx = nullptr;
	INI::parseFXList(ini, nullptr, &fx, nullptr);
	for (Int i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
		s[i] = fx;
}

//-------------------------------------------------------------------------------------------------
static void parsePerVetLevelPSys( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	typedef const ParticleSystemTemplate* ConstParticleSystemTemplatePtr;
	ConstParticleSystemTemplatePtr* s = (ConstParticleSystemTemplatePtr*)store;
	VeterancyLevel v = (VeterancyLevel)INI::scanIndexList(ini->getNextToken(), TheVeterancyNames);
	ConstParticleSystemTemplatePtr pst = nullptr;
	INI::parseParticleSystemTemplate(ini, nullptr, &pst, nullptr);
	s[v] = pst;
}

//-------------------------------------------------------------------------------------------------
static void parseAllVetLevelsPSys( INI* ini, void* /*instance*/, void * store, const void* /*userData*/ )
{
	typedef const ParticleSystemTemplate* ConstParticleSystemTemplatePtr;
	ConstParticleSystemTemplatePtr* s = (ConstParticleSystemTemplatePtr*)store;
	ConstParticleSystemTemplatePtr pst = nullptr;
	INI::parseParticleSystemTemplate(ini, nullptr, &pst, nullptr);
	for (Int i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
		s[i] = pst;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
WeaponStore *TheWeaponStore = nullptr;					///< the weapon store definition


///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
const FieldParse WeaponTemplate::TheWeaponTemplateFieldParseTable[] =
{

	{ "PrimaryDamage",						INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_primaryDamage) },
	{ "PrimaryDamageRadius",			INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_primaryDamageRadius) },
	{ "SecondaryDamage",					INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_secondaryDamage) },
	{ "SecondaryDamageRadius",		INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_secondaryDamageRadius) },
	{ "TertiaryDamage",					INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_tertiaryDamage) },	// Hypersonic @feature third ring (Generals Hypersonic D-222)
	{ "TertiaryDamageRadius",		INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_tertiaryDamageRadius) },	// Hypersonic @feature (D-222)
	{ "ShockWaveAmount",					INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_shockWaveAmount) },
	{ "ShockWaveRadius",					INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_shockWaveRadius) },
	{ "ShockWaveTaperOff",				INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_shockWaveTaperOff) },
	{ "AttackRange",							INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_attackRange) },
	{ "MinimumAttackRange",				INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_minimumAttackRange) },
	{ "RequestAssistRange",				INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_requestAssistRange) },
	{ "AcceptableAimDelta",				INI::parseAngleReal,										nullptr,							offsetof(WeaponTemplate, m_aimDelta) },
	{ "ScatterRadius",						INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_scatterRadius) },
	{ "ScatterTargetScalar",			INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_scatterTargetScalar) },
	{ "ScatterRadiusVsInfantry",	INI::parseReal,													nullptr,							offsetof( WeaponTemplate, m_infantryInaccuracyDist ) },
	{ "DamageType",								DamageTypeFlags::parseSingleBitFromINI,	nullptr,							offsetof(WeaponTemplate, m_damageType) },
	{ "DamageStatusType",					ObjectStatusMaskType::parseSingleBitFromINI,	nullptr,				offsetof(WeaponTemplate, m_damageStatusType) },
	{ "DeathType",								INI::parseIndexList,										TheDeathNames,		offsetof(WeaponTemplate, m_deathType) },
	{ "WeaponSpeed",							INI::parseVelocityReal,									nullptr,							offsetof(WeaponTemplate, m_weaponSpeed) },
	{ "MinWeaponSpeed",						INI::parseVelocityReal,									nullptr,							offsetof(WeaponTemplate, m_minWeaponSpeed) },
	{ "ScaleWeaponSpeed",					INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_isScaleWeaponSpeed) },
	{ "WeaponRecoil",							INI::parseAngleReal,										nullptr,							offsetof(WeaponTemplate, m_weaponRecoil) },
	{ "MinTargetPitch",						INI::parseAngleReal,										nullptr,							offsetof(WeaponTemplate, m_minTargetPitch) },
	{ "MaxTargetPitch",						INI::parseAngleReal,										nullptr,							offsetof(WeaponTemplate, m_maxTargetPitch) },
	{ "RadiusDamageAngle",				INI::parseAngleReal,										nullptr,							offsetof(WeaponTemplate, m_radiusDamageAngle) },
	{ "ProjectileObject",					INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_projectileName) },
	{ "FireSound",								INI::parseAudioEventRTS,								nullptr,							offsetof(WeaponTemplate, m_fireSound) },
	{ "FireSoundLoopTime",				INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_fireSoundLoopTime) },
	{ "FireFX",											parseAllVetLevelsFXList,							nullptr,							offsetof(WeaponTemplate, m_fireFXs) },
	{ "ProjectileDetonationFX",			parseAllVetLevelsFXList,							nullptr,							offsetof(WeaponTemplate, m_projectileDetonateFXs) },
	{ "FireOCL",										parseAllVetLevelsAsciiString,					nullptr,							offsetof(WeaponTemplate, m_fireOCLNames) },
	{ "ProjectileDetonationOCL",		parseAllVetLevelsAsciiString,					nullptr,							offsetof(WeaponTemplate, m_projectileDetonationOCLNames) },
	{ "ProjectileExhaust",					parseAllVetLevelsPSys,								nullptr,							offsetof(WeaponTemplate, m_projectileExhausts) },
	{ "VeterancyFireFX",										parsePerVetLevelFXList,				nullptr,							offsetof(WeaponTemplate, m_fireFXs) },
	{ "VeterancyProjectileDetonationFX",		parsePerVetLevelFXList,				nullptr,							offsetof(WeaponTemplate, m_projectileDetonateFXs) },
	{ "VeterancyFireOCL",										parsePerVetLevelAsciiString,	nullptr,							offsetof(WeaponTemplate, m_fireOCLNames) },
	{ "VeterancyProjectileDetonationOCL",		parsePerVetLevelAsciiString,	nullptr,							offsetof(WeaponTemplate, m_projectileDetonationOCLNames) },
	{ "VeterancyProjectileExhaust",					parsePerVetLevelPSys,					nullptr,							offsetof(WeaponTemplate, m_projectileExhausts) },
	{ "ClipSize",									INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_clipSize) },
	{ "ContinuousFireOne",				INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_continuousFireOneShotsNeeded) },
	{ "ContinuousFireTwo",				INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_continuousFireTwoShotsNeeded) },
	{ "ContinuousFireCoast",			INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_continuousFireCoastFrames) },
 	{ "AutoReloadWhenIdle",				INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_autoReloadWhenIdleFrames) },
	{ "ClipReloadTime",						INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clipReloadTime) },
	{ "DelayBetweenShots",				WeaponTemplate::parseShotDelay,					nullptr,							0 },
	{ "ShotsPerBarrel",						INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_shotsPerBarrel) },
	{ "DamageDealtAtSelfPosition",INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_damageDealtAtSelfPosition) },
	{ "RadiusDamageAffects",			INI::parseBitString32,	TheWeaponAffectsMaskNames,				offsetof(WeaponTemplate, m_affectsMask) },
	{ "ProjectileCollidesWith",		INI::parseBitString32,	TheWeaponCollideMaskNames,				offsetof(WeaponTemplate, m_collideMask) },
	{ "AntiAirborneVehicle",			INI::parseBitInInt32,										(void*)WEAPON_ANTI_AIRBORNE_VEHICLE,	offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiGround",								INI::parseBitInInt32,										(void*)WEAPON_ANTI_GROUND,						offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiProjectile",						INI::parseBitInInt32,										(void*)WEAPON_ANTI_PROJECTILE,				offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiSmallMissile",					INI::parseBitInInt32,										(void*)WEAPON_ANTI_SMALL_MISSILE,			offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiMine",									INI::parseBitInInt32,										(void*)WEAPON_ANTI_MINE,							offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiParachute",						INI::parseBitInInt32,										(void*)WEAPON_ANTI_PARACHUTE,					offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiAirborneInfantry",			INI::parseBitInInt32,										(void*)WEAPON_ANTI_AIRBORNE_INFANTRY, offsetof(WeaponTemplate, m_antiMask) },
	{ "AntiBallisticMissile",			INI::parseBitInInt32,										(void*)WEAPON_ANTI_BALLISTIC_MISSILE, offsetof(WeaponTemplate, m_antiMask) },
	{ "AutoReloadsClip",					INI::parseIndexList,										TheWeaponReloadNames,							offsetof(WeaponTemplate, m_reloadType) },
	{ "ProjectileStreamName",			INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_projectileStreamName) },
	{ "LaserName",								INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_laserName) },
	{ "LaserBoneName",						INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_laserBoneName) },
	{ "WeaponBonus",							WeaponTemplate::parseWeaponBonusSet,		nullptr,							0 },
	{ "HistoricBonusTime",				INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_historicBonusTime) },
	{ "HistoricBonusRadius",			INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_historicBonusRadius) },
	{ "HistoricBonusCount",				INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_historicBonusCount) },
	{ "HistoricBonusWeapon",			INI::parseWeaponTemplate,								nullptr,							offsetof(WeaponTemplate, m_historicBonusWeapon) },
	{ "LeechRangeWeapon",					INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_leechRangeWeapon) },
	{ "ScatterTarget",						WeaponTemplate::parseScatterTarget,			nullptr,							0 },
	{ "CapableOfFollowingWaypoints", INI::parseBool,											nullptr,							offsetof(WeaponTemplate, m_capableOfFollowingWaypoint) },
	{ "ShowsAmmoPips",						INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_isShowsAmmoPips) },
	{ "AllowAttackGarrisonedBldgs", INI::parseBool,												nullptr,							offsetof(WeaponTemplate, m_allowAttackGarrisonedBldgs) },
	{ "PlayFXWhenStealthed",			INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_playFXWhenStealthed) },
	{ "PreAttackDelay",						INI::parseDurationUnsignedInt,					nullptr,							offsetof( WeaponTemplate, m_preAttackDelay ) },
	{ "PreAttackType",						INI::parseIndexList,										TheWeaponPrefireNames, offsetof(WeaponTemplate, m_prefireType) },
	{ "ContinueAttackRange",			INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_continueAttackRange) },
	{ "SuspendFXDelay",						INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_suspendFXDelay) },
	{ "MissileCallsOnDie",			INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_dieOnDetonate) },
	// Hypersonic @feature Generals Hypersonic combat rules (IRAN_DESIGN 7b, 7c, 10b)
	{ "InterceptorClass",				INI::parseIndexList,										TheInterceptorClassNames,	offsetof(WeaponTemplate, m_interceptorClass) },
	{ "MissileHitsToKill",			INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_missileHitsToKill) },	// Hypersonic (D-195)
	{ "DroneHitChanceScale",		INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_droneHitChanceScale) },	// Hypersonic (D-195)
	{ "MissileHitChanceScale",		INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_missileHitChanceScale) },	// Hypersonic (D-195)
	{ "ScatterRadiusUniform",		INI::parseBool,													nullptr,							offsetof(WeaponTemplate, m_scatterRadiusUniform) },
	{ "ScatterRadiusUpgrade",		WeaponTemplate::parseScatterRadiusUpgrade,	nullptr,							0 },
	{ "ZeroScatterMarker",			INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_zeroScatterMarkerName) },
	// Hypersonic @feature cluster warhead (Generals Hypersonic D-048)
	{ "ClusterCount",						INI::parseInt,													nullptr,							offsetof(WeaponTemplate, m_clusterCount) },
	{ "ClusterRadius",					INI::parseReal,													nullptr,							offsetof(WeaponTemplate, m_clusterRadius) },
	{ "ClusterDelay",						INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clusterDelay) },
	{ "ClusterWeapon",					INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_clusterWeaponName) },
	// Hypersonic @feature (Generals Hypersonic D-055) the projectile is never diverted by countermeasure flares
	{ "IgnoresCountermeasures",	INI::parseBool,											nullptr,							offsetof(WeaponTemplate, m_ignoresCountermeasures) },
	// Hypersonic @feature (Generals Hypersonic D-060) the detonation FX scales with the blast's damage
	{ "ScaleDetonationFX",				INI::parseBool,											nullptr,							offsetof(WeaponTemplate, m_scaleDetonationFX) },
	// Hypersonic @feature (Generals Hypersonic D-148) the FX play at the target point, not at the firing object
	{ "PlayFXAtTarget",						INI::parseBool,											nullptr,							offsetof(WeaponTemplate, m_fxAtTarget) },
	// Hypersonic @feature (Generals Hypersonic D-148) a fixed size factor for the FireFX and the detonation FX
	{ "FXScale",									INI::parseReal,											nullptr,							offsetof(WeaponTemplate, m_fxScale) },
	// Hypersonic @feature (Generals Hypersonic D-158) split warhead: the centre warhead's own weapon (no ring, no spread)
	{ "ClusterCenterWeapon",			INI::parseAsciiString,									nullptr,							offsetof(WeaponTemplate, m_clusterCenterWeaponName) },
	// Hypersonic @feature (Generals Hypersonic D-162) the carrier releases its projectile warheads one at a time
	{ "ClusterReleaseInterval",	INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clusterReleaseInterval) },
	{ "ClusterReleaseCarrierSpeed",	INI::parsePercentToReal,							nullptr,							offsetof(WeaponTemplate, m_clusterReleaseCarrierSpeed) },
	{ "ClusterReleaseFinalSpeed",		INI::parsePercentToReal,							nullptr,							offsetof(WeaponTemplate, m_clusterReleaseFinalSpeed) },	// Hypersonic @feature (D-215)
	{ "ClusterReleaseStart",			INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clusterReleaseStart) },
	{ "ClusterReleaseLead",			INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clusterReleaseLead) },
	// Hypersonic @feature (Generals Hypersonic D-245) terminal cluster release: the carrier is gone after its last release
	{ "ClusterReleaseCarrierVanishes",	INI::parseBool,										nullptr,							offsetof(WeaponTemplate, m_clusterReleaseCarrierVanishes) },
	{ "ClusterReleaseVanishFX",		INI::parseFXList,										nullptr,							offsetof(WeaponTemplate, m_clusterReleaseVanishFX) },
	// Hypersonic @feature (Generals Hypersonic D-261) released warheads leave level and fall this long (the D-062 drop arc)
	{ "ClusterReleaseDropTime",		INI::parseDurationUnsignedInt,					nullptr,							offsetof(WeaponTemplate, m_clusterReleaseDropTime) },
	// Hypersonic @feature (Generals Hypersonic D-267) cone release: the warheads carry on with the missile, spreading in a cone
	{ "ClusterReleaseConeSpeed",		INI::parseVelocityReal,									nullptr,							offsetof(WeaponTemplate, m_clusterReleaseConeSpeed) },
	{ "ClusterReleaseConeInherit",	INI::parsePercentToReal,								nullptr,							offsetof(WeaponTemplate, m_clusterReleaseConeInherit) },
	// Hypersonic @feature (Generals Hypersonic D-147) hit chance against airborne targets without a HitChanceBehavior
	{ "HitChance",								INI::parsePercentToReal,								nullptr,							offsetof(WeaponTemplate, m_hitChance) },
	{ nullptr,												nullptr,																		nullptr,							0 }

};

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
WeaponTemplate::WeaponTemplate() : m_nextTemplate(nullptr)
{

	m_name													= "NoNameWeapon";
	m_nameKey												= NAMEKEY_INVALID;
	m_primaryDamage									= 0.0f;
	m_primaryDamageRadius						= 0.0f;
	m_secondaryDamage								= 0.0f;
	m_secondaryDamageRadius					= 0.0f;
	m_tertiaryDamage								= 0.0f;	// Hypersonic @feature (D-222) off
	m_tertiaryDamageRadius						= 0.0f;	// Hypersonic @feature (D-222) off
	m_attackRange										= 0.0f;
	m_minimumAttackRange						= 0.0f;
	m_requestAssistRange						= 0.0f;
	m_aimDelta											= 0.0f;
	m_scatterRadius									= 0.0f;
	m_scatterTargetScalar						= 0.0f;
	m_shockWaveAmount								= 0.0f;
	m_shockWaveRadius								= 0.0f;
	m_shockWaveTaperOff							= 0.0f;
	m_damageType										= DAMAGE_EXPLOSION;
	m_deathType											= DEATH_NORMAL;
	m_weaponSpeed										= 999999.0f;	// effectively instant
	m_minWeaponSpeed								= 999999.0f;	// effectively instant
	m_isScaleWeaponSpeed						= FALSE;
	m_weaponRecoil									= 0.0f;		// no recoil
	m_minTargetPitch								= -PI;
	m_maxTargetPitch								= PI;
	m_radiusDamageAngle							= PI;	// PI each way, so full circle
	m_projectileName.clear();					// no projectile
	m_projectileTmpl								= nullptr;
	for (Int i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
	{
		m_fireOCLNames[i].clear();
		m_projectileDetonationOCLNames[i].clear();
		m_projectileExhausts[i]					= nullptr;
		m_fireOCLs[i]										= nullptr;
		m_projectileDetonationOCLs[i]		= nullptr;
		m_fireFXs[i]										= nullptr;
		m_projectileDetonateFXs[i]			= nullptr;
	}
	m_damageDealtAtSelfPosition			= false;
	m_affectsMask										= (WEAPON_AFFECTS_ALLIES | WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_NEUTRALS);
	// most projectile weapons don't want to collide with nontargeted enemies/allies or trees...
	m_collideMask										= (WEAPON_COLLIDE_STRUCTURES);
	m_reloadType										= AUTO_RELOAD;
	m_prefireType										= PREFIRE_PER_SHOT;
	m_clipSize											= 0;
	m_continuousFireOneShotsNeeded	= INT_MAX;
	m_continuousFireTwoShotsNeeded	= INT_MAX;
	m_continuousFireCoastFrames			= 0;
 	m_autoReloadWhenIdleFrames			= 0;
	m_clipReloadTime								= 0;
	m_minDelayBetweenShots					= 0;
	m_maxDelayBetweenShots					= 0;
	m_fireSoundLoopTime							= 0;
	m_extraBonus										= nullptr;
	m_shotsPerBarrel								= 1;
	m_antiMask											= WEAPON_ANTI_GROUND;	// but not air or projectile.
	m_projectileStreamName.clear();
	m_laserName.clear();
	m_laserBoneName.clear();
	m_historicBonusTime							= 0;
	m_historicBonusCount						= 0;
	m_historicBonusRadius						= 0;
	m_historicBonusWeapon						= nullptr;
	m_leechRangeWeapon							= FALSE;
	m_capableOfFollowingWaypoint		= FALSE;
	m_isShowsAmmoPips								= FALSE;
	m_allowAttackGarrisonedBldgs		= FALSE;
	m_playFXWhenStealthed						= FALSE;
	m_preAttackDelay								= 0;
	m_continueAttackRange						= 0.0f;
	m_infantryInaccuracyDist				= 0.0f;
	m_damageStatusType							= OBJECT_STATUS_NONE;
	m_suspendFXDelay								= 0;
	m_dieOnDetonate						= FALSE;
	m_interceptorClass							= INTERCEPTOR_GUN;
	m_missileHitsToKill							= 0;
	m_droneHitChanceScale						= 1.0f;
	m_missileHitChanceScale						= 1.0f;
	m_scatterRadiusUniform					= FALSE;
	m_zeroScatterMarkerName.clear();
	m_zeroScatterMarkerTmpl					= nullptr;
	m_leadProfile.m_valid						= FALSE;
	m_clusterCount									= 0;
	m_clusterRadius									= 0.0f;
	m_clusterDelay									= 0;
	m_clusterWeaponName.clear();
	m_clusterWeapon									= nullptr;
	m_ignoresCountermeasures				= FALSE;
	m_scaleDetonationFX							= FALSE;
	m_fxScale												= 1.0f;
	m_fxAtTarget										= FALSE;
	m_clusterCenterWeaponName.clear();
	m_clusterCenterWeapon						= nullptr;
	m_clusterReleaseInterval				= 0;
	m_clusterReleaseLead						= 0;
	m_clusterReleaseStart						= 0;
	m_clusterReleaseCarrierSpeed		= 1.0f;
	m_clusterReleaseFinalSpeed			= 0.0f;	// Hypersonic @feature (D-215)
	m_clusterReleaseCarrierVanishes	= FALSE;	// Hypersonic @feature (D-245)
	m_clusterReleaseVanishFX				= nullptr;
	m_clusterReleaseDropTime				= 0;	// Hypersonic @feature (D-261)
	m_clusterReleaseConeSpeed				= 0.0f;	// Hypersonic @feature (D-267)
	m_clusterReleaseConeInherit			= 0.0f;
	m_hitChance											= 1.0f;
	m_leadProfile.m_launchSpeed			= 0.0f;
	m_leadProfile.m_maxSpeed				= 0.0f;
	m_leadProfile.m_speedGain				= 0.0f;
	m_leadProfile.m_ignitionFrames	= 0;
	m_leadProfile.m_maxFrames				= 0;
	m_leadProfile.m_fuseRadius			= 0.0f;

	m_historicDamageTriggerId = 0;
}

//-------------------------------------------------------------------------------------------------
WeaponTemplate::~WeaponTemplate()
{
	deleteInstance(m_nextTemplate);

	// delete any extra-bonus that's present
	deleteInstance(m_extraBonus);
}

// ------------------------------------------------------------------------------------------------
void WeaponTemplate::reset()
{
	m_historicDamage.clear();
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponTemplate::parseWeaponBonusSet( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	WeaponTemplate* self = (WeaponTemplate*)instance;

	if (!self->m_extraBonus)
		self->m_extraBonus = newInstance(WeaponBonusSet);

	self->m_extraBonus->parseWeaponBonusSet(ini);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponTemplate::parseScatterTarget( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	// Accept multiple listings of Coord2D's.
	WeaponTemplate* self = (WeaponTemplate*)instance;

	Coord2D target;
	target.x = 0;
	target.y = 0;
	INI::parseCoord2D( ini, nullptr, &target, nullptr );

	self->m_scatterTargets.push_back(target);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponTemplate::parseShotDelay( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	// This smart parser allows both a single number for traditional delay, and a labeled pair of numbers for a delay range
	WeaponTemplate* self = (WeaponTemplate*)instance;
	static const char *MIN_LABEL = "Min";
	static const char *MAX_LABEL = "Max";

	const char* token = ini->getNextToken(ini->getSepsColon());
	if( stricmp(token, MIN_LABEL) == 0 )
	{
		// Two entry min/max
		self->m_minDelayBetweenShots = INI::scanInt(ini->getNextToken(ini->getSepsColon()));
		token = ini->getNextToken(ini->getSepsColon());
		if( stricmp(token, MAX_LABEL) != 0 )
		{
			// Messed up double entry
			self->m_maxDelayBetweenShots = self->m_minDelayBetweenShots;
		}
		else
			self->m_maxDelayBetweenShots = INI::scanInt(ini->getNextToken(ini->getSepsColon()));
	}
	else
	{
		// single entry, as in no label so the first token is just a number
		self->m_minDelayBetweenShots = INI::scanInt(token);
		self->m_maxDelayBetweenShots = self->m_minDelayBetweenShots;
	}

	// No matter what we have now, we want to convert it to frames from msec.
	// ShotDelay used to use parseDurationUnsignedInt, and we are expanding on that.
	self->m_minDelayBetweenShots = ceilf(ConvertDurationFromMsecsToFrames((Real)self->m_minDelayBetweenShots));
	self->m_maxDelayBetweenShots = ceilf(ConvertDurationFromMsecsToFrames((Real)self->m_maxDelayBetweenShots));

}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature ScatterRadiusUpgrade = <UpgradeName> <ScatterRadius>. May be listed several times; while the
// firer's player has completed one or more of the upgrades, the smallest of their radii replaces ScatterRadius.
/*static*/ void WeaponTemplate::parseScatterRadiusUpgrade( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	WeaponTemplate* self = (WeaponTemplate*)instance;

	ScatterRadiusUpgrade entry;
	entry.m_upgradeName = ini->getNextToken();
	entry.m_scatterRadius = INI::scanReal(ini->getNextToken());

	self->m_scatterRadiusUpgrades.push_back(entry);
}

//-------------------------------------------------------------------------------------------------
void WeaponTemplate::postProcessLoad()
{
	if (!TheThingFactory)
	{
		DEBUG_CRASH(("you must call this after TheThingFactory is inited"));
		return;
	}

	if (m_projectileName.isEmpty())
	{
		m_projectileTmpl = nullptr;
	}
	else
	{
		m_projectileTmpl = TheThingFactory->findTemplate(m_projectileName);
		DEBUG_ASSERTCRASH(m_projectileTmpl, ("projectile %s not found!",m_projectileName.str()));
	}

	for (Int i = LEVEL_FIRST; i <= LEVEL_LAST; ++i)
	{
		// And the OCL if there is one
		if (m_fireOCLNames[i].isEmpty())
		{
			m_fireOCLs[i] = nullptr;
		}
		else
		{
			m_fireOCLs[i] = TheObjectCreationListStore->findObjectCreationList(m_fireOCLNames[i].str() );
			DEBUG_ASSERTCRASH(m_fireOCLs[i], ("OCL %s not found in a weapon!",m_fireOCLNames[i].str()));
		}
		m_fireOCLNames[i].clear();

		// And the other OCL if there is one
		if (m_projectileDetonationOCLNames[i].isEmpty() )
		{
			m_projectileDetonationOCLs[i] = nullptr;
		}
		else
		{
			m_projectileDetonationOCLs[i] = TheObjectCreationListStore->findObjectCreationList(m_projectileDetonationOCLNames[i].str() );
			DEBUG_ASSERTCRASH(m_projectileDetonationOCLs[i], ("OCL %s not found in a weapon!",m_projectileDetonationOCLNames[i].str()));
		}
		m_projectileDetonationOCLNames[i].clear();
	}

	// Hypersonic @feature designation marker for zero scatter
	if (m_zeroScatterMarkerName.isEmpty())
	{
		m_zeroScatterMarkerTmpl = nullptr;
	}
	else
	{
		m_zeroScatterMarkerTmpl = TheThingFactory->findTemplate(m_zeroScatterMarkerName);
		DEBUG_ASSERTCRASH(m_zeroScatterMarkerTmpl, ("ZeroScatterMarker %s not found in weapon %s!",m_zeroScatterMarkerName.str(),m_name.str()));
	}

	// Hypersonic @feature SAM lead interception profile (D-038)
	computeLeadInterceptProfile();

	// Hypersonic @feature cluster warhead (D-048)
	m_clusterWeapon = nullptr;
	if (m_clusterWeaponName.isNotEmpty() && m_clusterWeaponName.compareNoCase("None") != 0)
	{
		m_clusterWeapon = TheWeaponStore->findWeaponTemplate(m_clusterWeaponName);
		DEBUG_ASSERTCRASH(m_clusterWeapon, ("ClusterWeapon %s not found in weapon %s!",m_clusterWeaponName.str(),m_name.str()));
	}
	// Hypersonic @feature split warhead centre (D-158)
	m_clusterCenterWeapon = nullptr;
	if (m_clusterCenterWeaponName.isNotEmpty() && m_clusterCenterWeaponName.compareNoCase("None") != 0)
	{
		m_clusterCenterWeapon = TheWeaponStore->findWeaponTemplate(m_clusterCenterWeaponName);
		DEBUG_ASSERTCRASH(m_clusterCenterWeapon, ("ClusterCenterWeapon %s not found in weapon %s!",m_clusterCenterWeaponName.str(),m_name.str()));
	}

}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature SAM lead interception (Generals Hypersonic D-038).
// A SAM-class weapon (InterceptorClass = SAM) whose projectile is a MissileAIUpdate missile with TryToFollowTarget
// engages SAMHitIsLethal targets (the Iranian missiles) at a predicted intercept point: it fires when that point lies
// within AttackRange, flies to it and re-solves it every frame. This derives the interceptor's flight profile from
// the projectile's MissileAIUpdate and its SET_NORMAL locomotor. Nothing here is runtime state.
//-------------------------------------------------------------------------------------------------

// Upper bound on the flight time we look ahead when the interceptor has no fuel limit (10 s).
static const UnsignedInt LEAD_INTERCEPT_MAX_FRAMES = 10 * LOGICFRAMES_PER_SECOND;
// Fastest target the acquisition search allows for (Fattah with Engine Improvement: 2000 units/s). Only sizes the
// search radius; every candidate is still tested with the exact intercept solution.
static const Real LEAD_INTERCEPT_MAX_TARGET_SPEED = 2000.0f / LOGICFRAMES_PER_SECOND;

void WeaponTemplate::computeLeadInterceptProfile()
{
	m_leadProfile.m_valid = FALSE;

	if (m_interceptorClass != INTERCEPTOR_SAM || m_projectileTmpl == nullptr)
		return;

	const MissileAIUpdateModuleData* missileData = nullptr;
	const ModuleInfo& moduleInfo = m_projectileTmpl->getBehaviorModuleInfo();
	for (Int i = 0; i < moduleInfo.getCount(); ++i)
	{
		if (moduleInfo.getNthName(i) == "MissileAIUpdate")
		{
			missileData = static_cast<const MissileAIUpdateModuleData*>(moduleInfo.getNthData(i));
			break;
		}
	}
	if (missileData == nullptr || !missileData->m_tryToFollowTarget)
		return;

	const LocomotorTemplateVector* locos = missileData->findLocomotorTemplateVector(LOCOMOTORSET_NORMAL);
	if (locos == nullptr || locos->empty() || (*locos)[0] == nullptr)
		return;

	const LocomotorTemplate* loco = (*locos)[0];
	Real maxSpeed = loco->getMaxSpeed();
	Real accel = loco->getAcceleration();
	Real launchSpeed = missileData->m_initialVel;
	if (missileData->m_useWeaponSpeed)
	{
		// MissileAIUpdate caps both speed and acceleration at the weapon speed and launches at it.
		launchSpeed = m_weaponSpeed;
		maxSpeed = min(maxSpeed, m_weaponSpeed);
		accel = min(accel, m_weaponSpeed);
	}
	if (maxSpeed <= 0.0f)
		return;

	// A THRUST locomotor thrusts with accel minus a drag of vel * (accel / maxSpeed), so every frame the missile gains
	// that fraction of the speed it still lacks (Locomotor::moveTowardsPositionThrust). Other appearances: full speed.
	Real gain = 1.0f;
	if (loco->getAppearance() == LOCO_THRUST)
		gain = clamp(0.0f, accel / maxSpeed, 1.0f);

	m_leadProfile.m_launchSpeed = launchSpeed;
	m_leadProfile.m_maxSpeed = maxSpeed;
	m_leadProfile.m_speedGain = gain;
	m_leadProfile.m_ignitionFrames = missileData->m_ignitionDelay;
	m_leadProfile.m_maxFrames = LEAD_INTERCEPT_MAX_FRAMES;
	if (missileData->m_fuelLifetime > 0 && missileData->m_fuelLifetime + missileData->m_ignitionDelay < LEAD_INTERCEPT_MAX_FRAMES)
		m_leadProfile.m_maxFrames = missileData->m_fuelLifetime + missileData->m_ignitionDelay;
	m_leadProfile.m_fuseRadius = missileData->m_lockDistance > 0.0f ? missileData->m_lockDistance : maxSpeed;
	m_leadProfile.m_valid = TRUE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ Bool WeaponTemplate::isLeadInterceptTarget(const Object* victim)
{
	if (victim == nullptr || victim->isEffectivelyDead())
		return FALSE;

	const HitChanceBehavior* hitChance = HitChanceBehavior::findHitChanceBehavior(victim);
	return hitChance != nullptr && hitChance->isSAMHitLethal() && hitChance->isActive();
}

//-------------------------------------------------------------------------------------------------
// The target's velocity (dist/frame). A braking projectile (a missile in its final lock) does not move by its physics
// velocity: its locomotor moves it straight at its goal at its current speed (Locomotor::locoUpdate_moveTowardsPosition).
/*static*/ void WeaponTemplate::getLeadTargetVelocity(const Object* victim, Coord3D* vel)
{
	vel->zero();
	// Hypersonic @fix (D-048): a missile on its ballistic arc is placed by its AI; its physics velocity is zero
	const MissileAIUpdate* arc = victim->getAI() != nullptr ? victim->getAI()->getMissileAIUpdate() : nullptr;
	if (arc != nullptr && arc->isOnBallisticArc())
	{
		arc->getBallisticVelocity(vel);
		return;
	}
	const PhysicsBehavior* physics = victim->getPhysics();
	if (physics == nullptr)
		return;

	*vel = *physics->getVelocity();

	const AIUpdateInterface* ai = victim->getAI();
	if (ai != nullptr && victim->isKindOf(KINDOF_PROJECTILE) && victim->testStatus(OBJECT_STATUS_BRAKING))
	{
		const Coord3D* pos = victim->getPosition();
		const Coord3D* goal = ai->getGoalPosition();
		Real dx = goal->x - pos->x;
		Real dy = goal->y - pos->y;
		Real dz = goal->z - pos->z;
		Real dist = sqrtf(dx*dx + dy*dy + dz*dz);
		Real speed = physics->getVelocityMagnitude();
		if (speed > dist)
			speed = dist;
		vel->zero();
		if (dist > 0.001f)
		{
			vel->x = dx * speed / dist;
			vel->y = dy * speed / dist;
			vel->z = dz * speed / dist;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Intercept triangle, solved frame by frame: the target flies on in a straight line at its current velocity (3D, so
// a climb or dive counts), the interceptor flies at 'speed', gaining m_speedGain of its missing speed per frame once
// 'coastFrames' have passed. The first frame at which the interceptor's reach covers the distance to the target's
// predicted position is the intercept; the fraction within that frame is interpolated. The flying interceptor calls
// this again every frame, so velocity changes (a dive) are followed. A missile on a ballistic arc (D-042) is predicted
// along its arc instead, with the steps it will fly; the intercept must come before it lands.
Bool WeaponTemplate::solveLeadIntercept(const Coord3D* from, Real speed, UnsignedInt coastFrames, UnsignedInt maxFrames,
	const Object* victim, Coord3D* interceptPoint) const
{
	if (!m_leadProfile.m_valid || from == nullptr || victim == nullptr || interceptPoint == nullptr)
		return FALSE;

	Coord3D targetPos;
	victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), targetPos);
	Coord3D vel;
	getLeadTargetVelocity(victim, &vel);

	const Real dx = targetPos.x - from->x;
	const Real dy = targetPos.y - from->y;
	const Real dz = targetPos.z - from->z;
	const Real maxSpeed = m_leadProfile.m_maxSpeed;

	// Hypersonic @feature ballistic arc (D-042): predict along the arc (centre = arc point + the current centre offset)
	const MissileAIUpdate* arc = victim->getAI() != nullptr ? victim->getAI()->getMissileAIUpdate() : nullptr;
	if (arc != nullptr && !arc->isOnBallisticArc())
		arc = nullptr;
	MissileAIUpdate::BallisticFlight arcFlight;	// D-051: climb, pitch-over and arc, stepped as the missile flies them
	arcFlight.segment = MissileAIUpdate::BALLISTIC_ARC;
	arcFlight.param = 0.0f;
	arcFlight.speed = 0.0f;
	arcFlight.speedFactor = 1.0f;
	arcFlight.finalFactor = 0.0f;
	arcFlight.finalIn = -1;
	Coord3D arcOffset;
	arcOffset.zero();
	Coord3D prevArcPos = targetPos;
	if (arc != nullptr)
	{
		arc->getBallisticFlight(&arcFlight);
		arcOffset.x = targetPos.x - victim->getPosition()->x;
		arcOffset.y = targetPos.y - victim->getPosition()->y;
		arcOffset.z = targetPos.z - victim->getPosition()->z;
	}

	Real prevDist = sqrtf(dx*dx + dy*dy + dz*dz);
	Real prevGap = prevDist;
	Real reach = 0.0f;
	for (UnsignedInt n = 1; n <= maxFrames; ++n)
	{
		if (n > coastFrames)
			speed += (maxSpeed - speed) * m_leadProfile.m_speedGain;
		reach += speed;

		Real px, py, pz;
		Coord3D arcPos;
		if (arc != nullptr)
		{
			if (!arc->stepBallisticFlight(&arcFlight, &arcPos))
				return FALSE;	// it lands before we can meet it
			arcPos.x += arcOffset.x;
			arcPos.y += arcOffset.y;
			arcPos.z += arcOffset.z;
			px = arcPos.x - from->x;
			py = arcPos.y - from->y;
			pz = arcPos.z - from->z;
		}
		else
		{
			const Real t = (Real)n;
			px = dx + vel.x * t;
			py = dy + vel.y * t;
			pz = dz + vel.z * t;
		}
		const Real dist = sqrtf(px*px + py*py + pz*pz);
		const Real gap = dist - reach;
		if (gap <= 0.0f)
		{
			const Real frac = (prevGap > gap) ? prevGap / (prevGap - gap) : 1.0f;
			if (arc != nullptr)
			{
				interceptPoint->x = prevArcPos.x + (arcPos.x - prevArcPos.x) * frac;
				interceptPoint->y = prevArcPos.y + (arcPos.y - prevArcPos.y) * frac;
				interceptPoint->z = prevArcPos.z + (arcPos.z - prevArcPos.z) * frac;
			}
			else
			{
				const Real tHit = (Real)(n - 1) + frac;
				interceptPoint->x = targetPos.x + vel.x * tHit;
				interceptPoint->y = targetPos.y + vel.y * tHit;
				interceptPoint->z = targetPos.z + vel.z * tHit;
			}

			// the target reaches the ground before we can meet it
			if (interceptPoint->z < TheTerrainLogic->getGroundHeight(interceptPoint->x, interceptPoint->y))
				return FALSE;
			return TRUE;
		}

		// The distance to a target on a straight line grows by a non-decreasing amount per frame, and the interceptor
		// never flies faster than max(speed, maxSpeed): once the target recedes that fast, it can never be caught.
		// (Not on a curved arc, which may turn back toward us; its steps end when it lands.)
		if (arc == nullptr && dist - prevDist >= max(speed, maxSpeed))
			return FALSE;

		prevDist = dist;
		prevGap = gap;
		if (arc != nullptr)
			prevArcPos = arcPos;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool WeaponTemplate::solveLeadInterceptFromLauncher(const Object* launcher, const Object* victim, Coord3D* interceptPoint) const
{
	if (launcher == nullptr)
		return FALSE;

	Coord3D from;
	launcher->getGeometryInfo().getCenterPosition(*launcher->getPosition(), from);
	return solveLeadIntercept(&from, m_leadProfile.m_launchSpeed, m_leadProfile.m_ignitionFrames, m_leadProfile.m_maxFrames, victim, interceptPoint);
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getLeadInterceptSearchRange(Real attackRange) const
{
	const Real range = attackRange;
	if (!m_leadProfile.m_valid)
		return range;

	// frames the interceptor needs to fly out to its attack range
	Real speed = m_leadProfile.m_launchSpeed;
	Real reach = 0.0f;
	UnsignedInt n = 0;
	while (n < m_leadProfile.m_maxFrames && reach < range)
	{
		++n;
		if (n > m_leadProfile.m_ignitionFrames)
			speed += (m_leadProfile.m_maxSpeed - speed) * m_leadProfile.m_speedGain;
		reach += speed;
	}
	return range + LEAD_INTERCEPT_MAX_TARGET_SPEED * (Real)n;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature The scatter radius for a shot by this source: ScatterRadius, or the smallest ScatterRadiusUpgrade
// radius whose upgrade the source's player has completed (Generals Hypersonic IRAN_DESIGN 7b Guidance I/II).
Real WeaponTemplate::getScatterRadiusForSource(const Object* sourceObj) const
{
	Real radius = m_scatterRadius;
	if (m_scatterRadiusUpgrades.empty() || sourceObj == nullptr || TheUpgradeCenter == nullptr)
		return radius;

	const Player* player = sourceObj->getControllingPlayer();
	if (player == nullptr)
		return radius;

	for (ScatterRadiusUpgradeVec::const_iterator it = m_scatterRadiusUpgrades.begin(); it != m_scatterRadiusUpgrades.end(); ++it)
	{
		const UpgradeTemplate* upgrade = TheUpgradeCenter->findUpgrade(it->m_upgradeName);
		DEBUG_ASSERTCRASH(upgrade, ("ScatterRadiusUpgrade %s not found in weapon %s!",it->m_upgradeName.str(),m_name.str()));
		if (upgrade != nullptr && it->m_scatterRadius < radius && player->hasUpgradeComplete(upgrade))
			radius = it->m_scatterRadius;
	}
	return radius;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature If the shot is aimed at an object that carries this weapon's ZeroScatterMarker (a StickyBombUpdate
// object placed by the firer's player or an ally), return that object: the shot then lands on it with no scatter
// (Generals Hypersonic IRAN_DESIGN 10b Target Designation). A shot at a position counts when the position lies
// within the marked object's bounding circle.
const Object* WeaponTemplate::findZeroScatterTarget(const Object* sourceObj, const Object* victimObj, const Coord3D* victimPos) const
{
	if (m_zeroScatterMarkerTmpl == nullptr || sourceObj == nullptr)
		return nullptr;

	const Coord3D* center = victimObj ? victimObj->getPosition() : victimPos;
	if (center == nullptr)
		return nullptr;

	// Markers sit on their target's position; a position shot can be up to the target's bounding radius away.
	const Real ZERO_SCATTER_SEARCH_RADIUS = 200.0f;
	static const NameKeyType key_StickyBombUpdate = NAMEKEY("StickyBombUpdate");

	SimpleObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(center, ZERO_SCATTER_SEARCH_RADIUS, FROM_CENTER_2D, nullptr, ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);
	for (Object* marker = iter->first(); marker != nullptr; marker = iter->next())
	{
		if (!marker->getTemplate()->isEquivalentTo(m_zeroScatterMarkerTmpl) || marker->isEffectivelyDead())
			continue;

		if (sourceObj->getRelationship(marker) != ALLIES)
			continue;

		const StickyBombUpdate* sticky = (const StickyBombUpdate*)marker->findUpdateModule(key_StickyBombUpdate);
		if (sticky == nullptr)
			continue;

		const Object* target = sticky->getTargetObject();
		if (target == nullptr || target->isEffectivelyDead())
			continue;

		if (victimObj != nullptr)
		{
			if (target == victimObj)
				return target;
			continue;
		}

		Real dx = target->getPosition()->x - victimPos->x;
		Real dy = target->getPosition()->y - victimPos->y;
		Real radius = target->getGeometryInfo().getBoundingCircleRadius();
		if (dx * dx + dy * dy <= radius * radius)
			return target;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (Generals Hypersonic D-042) Where a strike at a ground position lands: the position shot of
// fireWeaponTemplate (designation first, else the upgraded, optionally uniform scatter), done once up front for a
// munition that flies to its impact point by itself (CruiseFlightUpdate).
void WeaponTemplate::computeStrikeAimPoint(const Object* sourceObj, const Coord3D* pos, Coord3D* aim) const
{
	*aim = *pos;

	const Object* designated = findZeroScatterTarget(sourceObj, nullptr, pos);
	if (designated != nullptr)
	{
		*aim = *designated->getPosition();
	}
	else
	{
		Real scatterRadius = getScatterRadiusForSource(sourceObj);
		if (scatterRadius > 0.0f)
		{
			if (m_scatterRadiusUniform)
				scatterRadius *= sqrtf(GameLogicRandomValueReal(0.0f, 1.0f));
			else
				scatterRadius = GameLogicRandomValueReal(0, scatterRadius);
			Real scatterAngleRadian = GameLogicRandomValueReal(0, 2*PI);
			aim->x += scatterRadius * Cos(scatterAngleRadian);
			aim->y += scatterRadius * Sin(scatterAngleRadian);
		}
	}
	aim->z = TheTerrainLogic->getGroundHeight(aim->x, aim->y);
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getAttackRange(const WeaponBonus& bonus) const
{
#ifdef RATIONALIZE_ATTACK_RANGE
	// Note - undersize by 1/4 of a pathfind cell, so that the goal is not teetering on the edge
	// of firing range.  jba.
	const Real UNDERSIZE = PATHFIND_CELL_SIZE_F*0.25f;
	Real r = m_attackRange * bonus.getField(WeaponBonus::RANGE) - UNDERSIZE;
	if (r < 0.0f) r = 0.0f;
	return r;
#else
// fudge this a little to account for pathfinding roundoff & such
	const Real ATTACK_RANGE_FUDGE = 1.05f;
	return m_attackRange * bonus.getField(WeaponBonus::RANGE) * ATTACK_RANGE_FUDGE;
#endif
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getMinimumAttackRange() const
{
#ifdef RATIONALIZE_ATTACK_RANGE
	// Note - undersize by 1/4 of a pathfind cell, so that the goal is not teetering on the edge
	// of firing range.  jba.
	const Real UNDERSIZE = PATHFIND_CELL_SIZE_F*0.25f;
	Real r = m_minimumAttackRange - UNDERSIZE;
	if (r < 0.0f) r = 0.0f;
	return r;
#else
	return m_minimumAttackRange;
#endif
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getUnmodifiedAttackRange() const
{
	return m_attackRange;
}

//-------------------------------------------------------------------------------------------------
Int WeaponTemplate::getDelayBetweenShots(const WeaponBonus& bonus) const
{
	// yes, divide, not multiply; the larger the rate-of-fire bonus, the shorter
	// we want the delay time to be.
	Int delayToUse;
	if( m_minDelayBetweenShots == m_maxDelayBetweenShots )
		delayToUse = m_minDelayBetweenShots; // Random number thing doesn't like this case
	else
		delayToUse = GameLogicRandomValue( m_minDelayBetweenShots, m_maxDelayBetweenShots );

	Real bonusROF = bonus.getField(WeaponBonus::RATE_OF_FIRE);
	//CRCDEBUG_LOG(("WeaponTemplate::getDelayBetweenShots() - min:%d max:%d val:%d, bonusROF=%g/%8.8X",
		//m_minDelayBetweenShots, m_maxDelayBetweenShots, delayToUse, bonusROF, AS_INT(bonusROF)));

	return REAL_TO_INT_FLOOR(delayToUse / bonusROF);
}

//-------------------------------------------------------------------------------------------------
Int WeaponTemplate::getClipReloadTime(const WeaponBonus& bonus) const
{
	// yes, divide, not multiply; the larger the rate-of-fire bonus, the shorter
	// we want the reload time to be.
	return REAL_TO_INT_FLOOR(m_clipReloadTime / bonus.getField(WeaponBonus::RATE_OF_FIRE));
}

//-------------------------------------------------------------------------------------------------
Int WeaponTemplate::getPreAttackDelay( const WeaponBonus& bonus ) const
{
	return m_preAttackDelay * bonus.getField( WeaponBonus::PRE_ATTACK );
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getPrimaryDamage(const WeaponBonus& bonus) const
{
	return m_primaryDamage * bonus.getField(WeaponBonus::DAMAGE);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature scaled explosions (Generals Hypersonic D-060): see Weapon.h. Clamped to 0.1 .. 4 so a stray
// damage value cannot make an effect vanish or cover the map.
Real WeaponTemplate::getDetonationFXScale(const FXList* fx, const WeaponBonus& bonus) const
{
	if (!m_scaleDetonationFX || fx == nullptr || fx->getReferenceDamage() <= 0.0f)
		return 1.0f;

	Real damage = getPrimaryDamage(bonus);
	if (isClusterWeapon())
	{
		damage = m_clusterCount * m_clusterWeapon->getPrimaryDamage(bonus);
		if (m_clusterCenterWeapon != nullptr)	// D-158: the centre warhead replaces one bomblet
			damage += m_clusterCenterWeapon->getPrimaryDamage(bonus) - m_clusterWeapon->getPrimaryDamage(bonus);
	}
	if (damage <= 0.0f)
		return 1.0f;

	Real scale = sqrtf(damage / fx->getReferenceDamage());
	if (scale < 0.1f)
		scale = 0.1f;
	if (scale > 4.0f)
		scale = 4.0f;
	return scale;
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getPrimaryDamageRadius(const WeaponBonus& bonus) const
{
	return m_primaryDamageRadius * bonus.getField(WeaponBonus::RADIUS);
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getSecondaryDamage(const WeaponBonus& bonus) const
{
	return m_secondaryDamage * bonus.getField(WeaponBonus::DAMAGE);
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getSecondaryDamageRadius(const WeaponBonus& bonus) const
{
	return m_secondaryDamageRadius * bonus.getField(WeaponBonus::RADIUS);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature third ring (Generals Hypersonic D-222): the same DAMAGE and RADIUS bonuses as the secondary
Real WeaponTemplate::getTertiaryDamage(const WeaponBonus& bonus) const
{
	return m_tertiaryDamage * bonus.getField(WeaponBonus::DAMAGE);
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::getTertiaryDamageRadius(const WeaponBonus& bonus) const
{
	return m_tertiaryDamageRadius * bonus.getField(WeaponBonus::RADIUS);
}

//-------------------------------------------------------------------------------------------------
Bool WeaponTemplate::isContactWeapon() const
{
#ifdef RATIONALIZE_ATTACK_RANGE
	// Note - undersize by 1/4 of a pathfind cell, so that the goal is not teetering on the edge
	// of firing range.  jba.
	const Real UNDERSIZE = PATHFIND_CELL_SIZE_F*0.25f;
	return (m_attackRange - UNDERSIZE) < PATHFIND_CELL_SIZE_F;
#else
// fudge this a little to account for pathfinding roundoff & such
	const Real ATTACK_RANGE_FUDGE = 1.05f;
	return m_attackRange * ATTACK_RANGE_FUDGE < PATHFIND_CELL_SIZE_F;
#endif
}

//-------------------------------------------------------------------------------------------------
Real WeaponTemplate::estimateWeaponTemplateDamage(
	const Object *sourceObj,
	const Object *victimObj,
	const Coord3D* victimPos,
	const WeaponBonus& bonus
) const
{
	if (sourceObj == nullptr || (victimObj == nullptr && victimPos == nullptr))
	{
		DEBUG_CRASH(("bad args to estimate"));
		return 0.0f;
	}

	const Real damageAmount = getPrimaryDamage(bonus);
	if ( victimObj == nullptr )
	{
		return damageAmount;
	}

	DamageType damageType = getDamageType();
	DeathType deathType = getDeathType();

	if ( victimObj->isKindOf(KINDOF_SHRUBBERY) )
	{
		if (deathType == DEATH_BURNED)
		{
			// this is just a nonzero value, to ensure we can target shrubbery with flame weapons, regardless...
			return 1.0f;
		}
		else
		{
			return 0.0f;
		}
	}


  // hmm.. must be shooting a firebase or such, if there is noone home to take the bullet, return 0!
  if ( victimObj->isKindOf( KINDOF_STRUCTURE) && damageType == DAMAGE_SNIPER )
  {

#if RETAIL_COMPATIBLE_CRC || PRESERVE_SNIPING_EMPTY_STINGER_SITES
    if ( victimObj->getContain() )
    {
      if ( victimObj->getContain()->getContainCount() == 0 )
        return 0.0f;
    }
#else
		// TheSuperHackers @bugfix Stubbjax 22/06/2026 Only allow targeting Stinger Sites when they contain Soldiers.
		const Bool hasOccupants = victimObj->getContain() && victimObj->getContain()->getContainCount() > 0;
		const Bool hasSlaves = victimObj->getSpawnBehaviorInterface() && victimObj->getSpawnBehaviorInterface()->getSlaveCount() > 0;

		if (!hasOccupants && !hasSlaves)
			return 0.0f;
#endif
  }




	if ( damageType == DAMAGE_SURRENDER || m_allowAttackGarrisonedBldgs )
	{
		ContainModuleInterface* contain = victimObj->getContain();
		if( contain && contain->getContainCount() > 0 && contain->isGarrisonable() && !contain->isImmuneToClearBuildingAttacks() )
		{
			// this is just a nonzero value, to ensure we can target garrisoned things with surrender weapons, regardless...
			return 1.0f;
		}
	}

	if( damageType == DAMAGE_DISARM )
	{
		if( victimObj->isKindOf( KINDOF_MINE ) || victimObj->isKindOf( KINDOF_BOOBY_TRAP ) || victimObj->isKindOf( KINDOF_DEMOTRAP ) )
		{
			// this is just a nonzero value, to ensure we can target mines with disarm weapons, regardless...
			return 1.0f;
		}
		return 0.0f;
	}
	if( damageType == DAMAGE_DEPLOY && !victimObj->isAirborneTarget() )
	{
		return 1.0f;
	}

	//@todo Kris need to examine the DAMAGE_HACK type for damage estimation purposes.
	//Likely this damage type will have threat implications that won't properly be dealt with until resolved.

	DamageInfoInput damageInfo;
	damageInfo.m_damageType = damageType;
	damageInfo.m_deathType = deathType;
	damageInfo.m_sourceID = sourceObj->getID();
	damageInfo.m_amount = damageAmount;
	return victimObj->estimateDamage(damageInfo);
}

//-------------------------------------------------------------------------------------------------
Bool WeaponTemplate::shouldProjectileCollideWith(
	const Object* projectileLauncher,
	const Object* projectile,
	const Object* thingWeCollidedWith,
	ObjectID intendedVictimID	// could be INVALID_ID for a position-shot
) const
{
 	if (!projectile || !thingWeCollidedWith)
 		return false;

	// if it's our intended victim, we want to collide with it, regardless of any other considerations.
	if (intendedVictimID == thingWeCollidedWith->getID())
		return true;

 	if (projectileLauncher != nullptr)
 	{

 		// Don't hit your own launcher, ever.
 		if (projectileLauncher == thingWeCollidedWith)
 			return false;

 		// If our launcher is inside something, and that something is 'thingWeCollidedWith' we won't collide
 		const Object *launcherContainedBy = projectileLauncher->getContainedBy();
 		if( launcherContainedBy == thingWeCollidedWith )
 			return false;

 	}

	// never bother burning already-burned things. (srj)
	if (getDamageType() == DAMAGE_FLAME || getDamageType() == DAMAGE_PARTICLE_BEAM)
	{
		if (thingWeCollidedWith->testStatus(OBJECT_STATUS_BURNED))
		{
			return false;
		}
	}

	// horrible special case for airplanes sitting on airfields: the projectile might
	// "collide" with the airfield's (invisible) collision geometry when a resting plane
	// is targeted. we don't want this. special case it:
	if (thingWeCollidedWith->isKindOf(KINDOF_FS_AIRFIELD))
	{
		//
		// ok, so if we are an airfield, and our intended victim has a reserved space
		// with us, it "belongs" to us and collisions intended for it should not detonate
		// as a result of colliding with us.
		//
		// notes:
		//	-- we have already established that "thingWeCollidedWith" is not the intended victim (above)
		//	-- this does NOT verify that the plane is actually parked at the airfield; it might be elsewhere
		//			(but if it is, it's highly unlikely that this sort of collision could occur)
		//
		for (BehaviorModule** i = thingWeCollidedWith->getBehaviorModules(); *i; ++i)
		{
			ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
			if (pp != nullptr && pp->hasReservedSpace(intendedVictimID))
				return false;
		}
	}

	// if something has a Sneaky Target offset, it is momentarily immune to being hit...
	// normally this shouldn't happen, but occasionally can by accident. so avoid it. (srj)
	const AIUpdateInterface* ai = thingWeCollidedWith->getAI();
	if (ai != nullptr && ai->getSneakyTargetingOffset(nullptr))
	{
		return false;
	}

	Int requiredMask = 0;

	Relationship r = projectile->getRelationship(thingWeCollidedWith);
	if (r == ALLIES) requiredMask = WEAPON_COLLIDE_ALLIES;
	else if (r == ENEMIES) requiredMask = WEAPON_COLLIDE_ENEMIES;

	if (thingWeCollidedWith->isKindOf(KINDOF_STRUCTURE))
	{
		if (thingWeCollidedWith->getControllingPlayer() == projectile->getControllingPlayer())
			requiredMask |= WEAPON_COLLIDE_CONTROLLED_STRUCTURES;
		else
			requiredMask |= WEAPON_COLLIDE_STRUCTURES;
	}
	if (thingWeCollidedWith->isKindOf(KINDOF_SHRUBBERY))					requiredMask |= WEAPON_COLLIDE_SHRUBBERY;
	if (thingWeCollidedWith->isKindOf(KINDOF_PROJECTILE))					requiredMask |= WEAPON_COLLIDE_PROJECTILE;
	if (thingWeCollidedWith->getTemplate()->getFenceWidth() > 0)	requiredMask |= WEAPON_COLLIDE_WALLS;
	if (thingWeCollidedWith->isKindOf(KINDOF_SMALL_MISSILE))			requiredMask |= WEAPON_COLLIDE_SMALL_MISSILES;			//All missiles are also projectiles!
	if (thingWeCollidedWith->isKindOf(KINDOF_BALLISTIC_MISSILE))	requiredMask |= WEAPON_COLLIDE_BALLISTIC_MISSILES;	//All missiles are also projectiles!

	// if any in requiredMask are present in collidemask, do the collision. (srj)
	if ((getProjectileCollideMask() & requiredMask) != 0)
		return true;

	//DEBUG_LOG(("Rejecting projectile collision between %s and %s!",projectile->getTemplate()->getName().str(),thingWeCollidedWith->getTemplate()->getName().str()));
	return false;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WeaponTemplate::fireWeaponTemplate
(
	const Object *sourceObj,
	WeaponSlotType wslot,
	Int specificBarrelToUse,
	Object *victimObj,
	const Coord3D* victimPos,
	const WeaponBonus& bonus,
	Bool isProjectileDetonation,
	Bool ignoreRanges,
	Weapon *firingWeapon,
	ObjectID* projectileID,
	Bool inflictDamage
) const
{

	//-extraLogging
	#if defined(RTS_DEBUG)
		AsciiString targetStr;
		if( TheGlobalData->m_extraLogging )
		{
			if( victimObj )
				targetStr.format( "%s", victimObj->getTemplate()->getName().str() );
			else if( victimPos )
				targetStr.format( "%d,%d,%d", victimPos->x, victimPos->y, victimPos->z );
			else
				targetStr.format( "SELF." );

			DEBUG_LOG( ("%d - WeaponTemplate::fireWeaponTemplate() begin - %s attacking %s",
				TheGameLogic->getFrame(), sourceObj->getTemplate()->getName().str(), targetStr.str() ) );
		}
	#endif
	//end -extraLogging

	//CRCDEBUG_LOG(("WeaponTemplate::fireWeaponTemplate() from %s", DescribeObject(sourceObj).str()));
	DEBUG_ASSERTCRASH(specificBarrelToUse >= 0, ("specificBarrelToUse should no longer be -1"));

	if (sourceObj == nullptr || (victimObj == nullptr && victimPos == nullptr))
	{
		//-extraLogging
		#if defined(RTS_DEBUG)
			if( TheGlobalData->m_extraLogging )
				DEBUG_LOG( ("FAIL 1 (sourceObj %d == nullptr || (victimObj %d == nullptr && victimPos %d == nullptr)", sourceObj != 0, victimObj != 0, victimPos != 0) );
		#endif
		//end -extraLogging

		return 0;
	}

	DEBUG_ASSERTCRASH((m_primaryDamage > 0)  ||  (victimObj == nullptr), ("You can't really shoot a zero damage weapon at an Object.") );

	ObjectID sourceID = sourceObj->getID();
	const Coord3D* sourcePos = sourceObj->getPosition();

	Real distSqr;
	ObjectID victimID;
	TBridgeAttackInfo info;
	Coord3D victimPosStorage;
	if (victimObj)
	{
		DEBUG_ASSERTLOG(sourceObj != victimObj, ("*** firing weapon at self -- is this really what you want?"));
		victimPos = victimObj->getPosition();
		victimID = victimObj->getID();

		Coord3D sneakyOffset;
		const AIUpdateInterface* ai = victimObj->getAI();
		if (ai != nullptr && ai->getSneakyTargetingOffset(&sneakyOffset))
		{
			victimPosStorage = *victimPos;
			victimPosStorage.x += sneakyOffset.x;
			victimPosStorage.y += sneakyOffset.y;
			victimPosStorage.z += sneakyOffset.z;

			victimPos = &victimPosStorage;
			// for a sneaky offset, we always target a position rather than an object
			victimObj = nullptr;
			victimID = INVALID_ID;
			distSqr = ThePartitionManager->getDistanceSquared(sourceObj, victimPos, ATTACK_RANGE_CALC_TYPE);
		}
		else
		{
			if (victimObj->isKindOf(KINDOF_BRIDGE))
			{
				// Bridges are kind of oddball - they have 2 target points at either end.
				TheTerrainLogic->getBridgeAttackPoints(victimObj, &info);
				distSqr = ThePartitionManager->getDistanceSquared( sourceObj, &info.attackPoint1, ATTACK_RANGE_CALC_TYPE );
				victimPos = &info.attackPoint1;
 				Real distSqr2 = ThePartitionManager->getDistanceSquared( sourceObj, &info.attackPoint2, ATTACK_RANGE_CALC_TYPE );
				if (distSqr > distSqr2)
				{
					// Try the other one.
					distSqr = distSqr2;
					victimPos = &info.attackPoint2;
				}
			}
			else
			{
				distSqr = ThePartitionManager->getDistanceSquared(sourceObj, victimObj, ATTACK_RANGE_CALC_TYPE);
			}
		}
	}
	else
	{
		victimID = INVALID_ID;
		distSqr = ThePartitionManager->getDistanceSquared(sourceObj, victimPos, ATTACK_RANGE_CALC_TYPE);
	}

//	DEBUG_LOG(("WeaponTemplate::fireWeaponTemplate: firing weapon %s (source=%s, victim=%s)",
//		m_name.str(),sourceObj->getTemplate()->getName().str(),victimObj?victimObj->getTemplate()->getName().str():"null"));

	// Hypersonic @feature a SAM engaging an Iranian missile fires when the predicted intercept point is within range;
	// the missile itself may still be outside it (D-038). Re-solve the intercept now and measure the range checks
	// below to that point. No reachable intercept point: do not fire, as for a target out of range.
	if( !ignoreRanges && !isProjectileDetonation && victimObj != nullptr && isLeadInterceptEngagement(victimObj) )
	{
		Coord3D interceptPoint;
		if( !solveLeadInterceptFromLauncher(sourceObj, victimObj, &interceptPoint) )
			return 0;
		distSqr = ThePartitionManager->getDistanceSquared(sourceObj, &interceptPoint, ATTACK_RANGE_CALC_TYPE);
	}

	//Only perform this check if the weapon isn't a leech range weapon (which can have unlimited range!)
	if( !ignoreRanges && !isLeechRangeWeapon() )
	{
		Real attackRangeSqr = sqr(getAttackRange(bonus));
		if (distSqr > attackRangeSqr)
		{
			//DEBUG_ASSERTCRASH(distSqr < 5*5 || distSqr < attackRangeSqr*1.2f, ("*** victim is out of range (%f vs %f) of this weapon -- why did we attempt to fire?",sqrtf(distSqr),sqrtf(attackRangeSqr)));

			//-extraLogging
			#if defined(RTS_DEBUG)
				if( TheGlobalData->m_extraLogging )
					DEBUG_LOG( ("FAIL 2 (distSqr %.2f > attackRangeSqr %.2f)", distSqr, attackRangeSqr ) );
			#endif
			//end -extraLogging

			return 0;
		}
	}

	if (!ignoreRanges)
	{
		Real minAttackRangeSqr = sqr(getMinimumAttackRange());
#ifdef RATIONALIZE_ATTACK_RANGE
		if (distSqr < minAttackRangeSqr && !isProjectileDetonation)
#else
		if (distSqr < minAttackRangeSqr-0.5f && !isProjectileDetonation)
#endif
		{
			DEBUG_ASSERTCRASH(distSqr > minAttackRangeSqr*0.8f, ("*** victim is closer than min attack range (%f vs %f) of this weapon -- why did we attempt to fire?",sqrtf(distSqr),sqrtf(minAttackRangeSqr)));

			//-extraLogging
			#if defined(RTS_DEBUG)
				if( TheGlobalData->m_extraLogging )
					DEBUG_LOG( ("FAIL 3 (distSqr %.2f< minAttackRangeSqr %.2f - 0.5f && !isProjectileDetonation %d)", distSqr, minAttackRangeSqr, isProjectileDetonation ) );
			#endif
			//end -extraLogging

			return 0;
		}
	}

	// call this even if FXList is null, because this also handles stuff like Gun Barrel Recoil
	if (sourceObj && sourceObj->getDrawable())
	{
		Coord3D targetPos;
		if( victimObj )
		{
			victimObj->getGeometryInfo().getCenterPosition( *victimObj->getPosition(), targetPos );
		}
		else
		{
			targetPos.set( *victimPos );
		}
		Real reAngle = getWeaponRecoilAmount();
		Real reDir = reAngle != 0.0f ? (atan2(victimPos->y - sourcePos->y, victimPos->x - sourcePos->x)) : 0.0f;
		VeterancyLevel v = sourceObj->getVeterancyLevel();
		const FXList* fx = isProjectileDetonation ? getProjectileDetonateFX(v) : getFireFX(v);
		// Hypersonic @feature (D-060) a scaled detonation FX: every nugget played below scales by the blast's size
		// Hypersonic @feature (D-148) FXScale sizes the FireFX and the detonation FX alike
		FXList::ScaleScope fxScale((isProjectileDetonation ? getDetonationFXScale(fx, bonus) : 1.0f) * m_fxScale);

		if ( TheGameLogic->getFrame() < firingWeapon->getSuspendFXFrame() )
			fx = nullptr;

		Bool handled;

		// TheSuperHackers @todo: Remove hardcoded KINDOF_MINE check and apply PlayFXWhenStealthed = Yes to the mine weapons instead.

		if (!sourceObj->isLogicallyVisible()									// if user watching cannot see us
			&& !sourceObj->isKindOf(KINDOF_MINE)								// and not a mine (which always do the FX, even if hidden)...
			&& !isPlayFXWhenStealthed()													// and not a weapon marked to playwhenstealthed
			)
		{
			handled = TRUE;		// then let's just pretend like we did the fx by returning true
		}
		else if (m_fxAtTarget)
		{
			handled = FALSE;	// Hypersonic @feature (D-148) played below at the target point, not by the source's drawable
		}
		else
		{
			handled = sourceObj->getDrawable()->handleWeaponFireFX(wslot,
																															specificBarrelToUse,
																															fx,
																															getWeaponSpeed(),
																															reAngle,
																															reDir,
																															&targetPos,
																															getPrimaryDamageRadius(bonus)
																															);
		}

		if (handled == false && fx != nullptr)
		{
			// bah. just play it at the drawable's pos.
			//DEBUG_LOG(("*** WeaponFireFX not fully handled by the client"));
			const Coord3D* where = (isContactWeapon() || m_fxAtTarget) ? &targetPos : sourceObj->getDrawable()->getPosition();
			const Matrix3D* mtx = m_fxAtTarget ? nullptr : sourceObj->getDrawable()->getTransformMatrix();	// D-148
			FXList::doFXPos(fx, where, mtx, getWeaponSpeed(), &targetPos, getPrimaryDamageRadius(bonus));
		}
	}

	// Now do the FireOCL if there is one
	if( sourceObj )
	{
		VeterancyLevel v = sourceObj->getVeterancyLevel();
		const ObjectCreationList *oclToUse = isProjectileDetonation ? getProjectileDetonationOCL(v) : getFireOCL(v);
		if( oclToUse )
			ObjectCreationList::create( oclToUse, sourceObj, nullptr );
	}

	Coord3D projectileDestination = *victimPos; //Need to copy this, as we have a pointer to their actual position
	Real scatterRadius = 0.0f;

	// Hypersonic @feature Strike accuracy (Generals Hypersonic IRAN_DESIGN 7b, 10b): player upgrades can tighten the
	// scatter, and a target designated for the firer is hit with no scatter at all. Without the new weapon fields
	// this is the stock scatter radius and no designated target.
	const Real baseScatterRadius = getScatterRadiusForSource( sourceObj );
	const Object *zeroScatterTarget = findZeroScatterTarget( sourceObj, victimObj, victimPos );
	if( zeroScatterTarget != nullptr )
	{
		// A position shot is moved onto the designated object; an object shot already aims at it.
		projectileDestination = *zeroScatterTarget->getPosition();
	}
	else if( baseScatterRadius > 0.0f || (m_infantryInaccuracyDist > 0.0f && victimObj && victimObj->isKindOf( KINDOF_INFANTRY )) )
	{
		// This weapon scatters, so clear the victimObj, as we are no longer shooting it directly,
		// and find a random point within the radius to shoot at as victimPos
		scatterRadius = baseScatterRadius;

		// if it's an object, aim at the center, not the ground part (srj)
		PathfindLayerEnum targetLayer = LAYER_GROUND;
		if( victimObj )
		{
			if( victimObj->isKindOf( KINDOF_STRUCTURE ) )
			{
				victimObj->getGeometryInfo().getCenterPosition(*victimObj->getPosition(), projectileDestination);
			}
			if( m_infantryInaccuracyDist > 0.0f && victimObj->isKindOf( KINDOF_INFANTRY ) )
			{
				//If we are firing a weapon that is considered inaccurate against infantry, then add it to
				//the scatter radius!
				scatterRadius += m_infantryInaccuracyDist;
			}
			targetLayer = victimObj->getLayer();
		}

		//victimObj = nullptr; // his position is already in victimPos, if he existed

		//Randomize the scatter radius (sometimes it can be more accurate than others)
		if( m_scatterRadiusUniform )
		{
			// Hypersonic @feature uniform over the disc area: the square root of the random radius fraction
			// (a plain random radius clusters the points toward the centre).
			scatterRadius *= sqrtf( GameLogicRandomValueReal( 0.0f, 1.0f ) );
		}
		else
		{
			scatterRadius = GameLogicRandomValueReal( 0, scatterRadius );
		}
		Real scatterAngleRadian = GameLogicRandomValueReal( 0, 2*PI );

		Coord3D firingOffset;
		firingOffset.zero();
		firingOffset.x = scatterRadius * Cos( scatterAngleRadian );
		firingOffset.y = scatterRadius * Sin( scatterAngleRadian );

		projectileDestination.x += firingOffset.x;
		projectileDestination.y += firingOffset.y;

		//What's suddenly become crucially important is to FIRE at the ground at this location!!!
		//If we aim for the center point of our target and miss, the shot will go much farther than
		//we expect!
		// srj sez: we should actually fire at the layer the victim is on, if possible, in case it is on a bridge...
		projectileDestination.z = TheTerrainLogic->getLayerHeight( projectileDestination.x, projectileDestination.y, targetLayer );
	}

	if (getProjectileTemplate() == nullptr || isProjectileDetonation)
	{
		// see if we need to be called back at a later point to deal the damage.
		Coord3D v;
		v.x = victimPos->x - sourcePos->x;
		v.y = victimPos->y - sourcePos->y;
		v.z = victimPos->z - sourcePos->z;
		// don't round the result; we WANT a fractional-frame-delay in this case.
		Real delayInFrames = (v.length() / getWeaponSpeed());

		ObjectID damageID = getDamageDealtAtSelfPosition() ? INVALID_ID : victimID;

		if( firingWeapon->isLaser() )
		{
			if( scatterRadius <= getPrimaryDamageRadius( bonus ) || scatterRadius <= getSecondaryDamageRadius( bonus ) )
			{
				//The laser is close enough to damage the object, so just hit it directly. Some victim objects
				//adjust the laser's position to prevent it from hitting the ground.
				if( victimObj )
				{
					projectileDestination.set( *victimObj->getPosition() );
				}
				firingWeapon->createLaser( sourceObj, victimObj, &projectileDestination );
			}
			else
			{
				//We are missing our intended target, so now we want to aim at the ground at the projectile offset.
				damageID = INVALID_ID;
				firingWeapon->createLaser( sourceObj, nullptr, &projectileDestination );
			}
			if( inflictDamage )
			{
				dealDamageInternal( sourceID, damageID, &projectileDestination, bonus, isProjectileDetonation );
			}
			return TheGameLogic->getFrame();
		}

		const Coord3D* damagePos = getDamageDealtAtSelfPosition() ? sourcePos : victimPos;
		// Hypersonic @bugfix split warhead (D-062): the split launches its warheads from the bursting carrier, so it must
		// happen now, while the carrier exists; a delayed split (apex far from the aim point) found no carrier and fell
		// back to the D-048 ground bomblets.
		if (isClusterWeapon() && m_clusterWeapon->getProjectileTemplate() != nullptr)
			delayInFrames = 0.0f;
		if (delayInFrames < 1.0f)
		{
			// go ahead and do it now
			//DEBUG_LOG(("WeaponTemplate::fireWeaponTemplate: firing weapon immediately!"));
			if( inflictDamage )
			{
				dealDamageInternal(sourceID, damageID, damagePos, bonus, isProjectileDetonation);
			}

			//-extraLogging
			#if defined(RTS_DEBUG)
				if( TheGlobalData->m_extraLogging )
					DEBUG_LOG( ("EARLY 4 (delayed damage applied now)") );
			#endif
			//end -extraLogging


			return TheGameLogic->getFrame();
		}
		else
		{
			UnsignedInt when = 0;
			if( TheWeaponStore && inflictDamage )
			{
				UnsignedInt delayInWholeFrames = REAL_TO_INT_CEIL(delayInFrames);
				when = TheGameLogic->getFrame() + delayInWholeFrames;
				//DEBUG_LOG(("WeaponTemplate::fireWeaponTemplate: firing weapon in %d frames (= %d)!", delayInWholeFrames,when));
				TheWeaponStore->setDelayedDamage(this, damagePos, when, sourceID, damageID, bonus);
			}

			//-extraLogging
			#if defined(RTS_DEBUG)
				if( TheGlobalData->m_extraLogging )
					DEBUG_LOG( ("EARLY 5 (delaying damage applied until frame %d)", when ) );
			#endif
			//end -extraLogging


			return when;
		}
	}
	else	// must be a projectile
	{
		Player *owningPlayer = sourceObj->getControllingPlayer(); //Need to know so missiles don't collide with firer
		Object *projectile = TheThingFactory->newObject( getProjectileTemplate(), owningPlayer->getDefaultTeam() );
		projectile->setProducer(sourceObj);

		//If the player has battle plans (America Strategy Center), then apply those bonuses
		//to this object if applicable. Internally it validates certain kinds of objects.
		//When projectiles are created, weapon bonuses such as damage may get applied.
		if( owningPlayer->getNumBattlePlansActive() > 0 )
		{
			owningPlayer->applyBattlePlanBonusesForObject( projectile );
		}


		//Store the project ID in the object as the last projectile fired!
		if (projectileID)
			*projectileID = projectile->getID();

		// Notify special power tracking
		SpecialPowerCompletionDie *die = sourceObj->findSpecialPowerCompletionDie();
		if (die)
		{
			die->notifyScriptEngine();
			die = projectile->findSpecialPowerCompletionDie();
			if (die)
			{
				die->setCreator(INVALID_ID);
			}
		}
		else
		{
			die = projectile->findSpecialPowerCompletionDie();
			if (die)
			{
				die->setCreator(sourceObj->getID());
			}
		}

		firingWeapon->newProjectileFired( sourceObj, projectile, victimObj, victimPos );//The actual logic weapon needs to know this was created.

		ProjectileUpdateInterface* pui = nullptr;
		for (BehaviorModule** u = projectile->getBehaviorModules(); *u; ++u)
		{
			if ((pui = (*u)->getProjectileUpdateInterface()) != nullptr)
				break;
		}
		if (pui)
		{
			VeterancyLevel v = sourceObj->getVeterancyLevel();
			if( scatterRadius > 0.0f )
			{
				//With a scatter radius, don't follow the victim (overriding the intent).
				pui->projectileLaunchAtObjectOrPosition( nullptr, &projectileDestination, sourceObj, wslot, specificBarrelToUse, this, m_projectileExhausts[v] );
			}
			else
			{
				pui->projectileLaunchAtObjectOrPosition(victimObj, &projectileDestination, sourceObj, wslot, specificBarrelToUse, this, m_projectileExhausts[v]);
			}
		}
		else
		{
			//DEBUG_CRASH(("Projectiles should implement ProjectileUpdateInterface!"));
			// actually, this is ok, for things like Firestorm.... (srj)
			projectile->setPosition(&projectileDestination);
		}

		//If we're launching a missile at a unit with valid countermeasures, then communicate it
		// Hypersonic @feature (D-055): a weapon with IgnoresCountermeasures never reports its missile, so it is not diverted
		if( projectile->isKindOf( KINDOF_SMALL_MISSILE ) && !m_ignoresCountermeasures && victimObj && victimObj->hasCountermeasures() )
		{
			const AIUpdateInterface *ai = victimObj->getAI();
			//Only allow jets not currently supersonic to launch countermeasures
			if( ai && ai->getCurLocomotorSetType() != LOCOMOTORSET_SUPERSONIC )
			{
				//This function will determine now whether or not the fired projectile will be diverted to
				//an available decoy flare.
				victimObj->reportMissileForCountermeasures( projectile );
			}

		}
		//-extraLogging
		#if defined(RTS_DEBUG)
			if( TheGlobalData->m_extraLogging )
				DEBUG_LOG( ("DONE") );
		#endif
		//end -extraLogging

		return 0;
	}
}

//-------------------------------------------------------------------------------------------------
#if RETAIL_COMPATIBLE_CRC || PRESERVE_UNRELIABLE_FIRESTORMS
void WeaponTemplate::trimOldHistoricDamage() const
{
	UnsignedInt expirationDate = TheGameLogic->getFrame() - TheGlobalData->m_historicDamageLimit;
	while (!m_historicDamage.empty())
	{
		HistoricWeaponDamageInfo& h = m_historicDamage.front();
		if (h.frame <= expirationDate)
		{
			m_historicDamage.pop_front();
			continue;
		}
		else
		{
			// since they are in strict chronological order,
			// stop as soon as we get to a nonexpired one
			break;
		}
	}
}
#else
void WeaponTemplate::trimOldHistoricDamage() const
{
	if (m_historicDamage.empty())
		return;

	const UnsignedInt currentFrame = TheGameLogic->getFrame();
	const UnsignedInt expirationFrame = currentFrame - m_historicBonusTime;

	HistoricWeaponDamageList::iterator it = m_historicDamage.begin();

	while (it != m_historicDamage.end())
	{
		if (it->frame <= expirationFrame)
			it = m_historicDamage.erase(it);
		else
			break;
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void WeaponTemplate::trimTriggeredHistoricDamage() const
{
	HistoricWeaponDamageList::iterator it = m_historicDamage.begin();

	while (it != m_historicDamage.end())
	{
		if (it->triggerId == m_historicDamageTriggerId)
			it = m_historicDamage.erase(it);
		else
			++it;
	}
}

//-------------------------------------------------------------------------------------------------
static Bool is2DDistSquaredLessThan(const Coord3D& a, const Coord3D& b, Real distSqr)
{
	Real da = sqr(a.x - b.x) + sqr(a.y - b.y);
	return da <= distSqr;
}

//-------------------------------------------------------------------------------------------------
#if RETAIL_COMPATIBLE_CRC || PRESERVE_UNRELIABLE_FIRESTORMS
void WeaponTemplate::processHistoricDamage(const Object* source, const Coord3D* pos) const
{
	//
	/** @todo We need to rewrite the historic stuff ... if you fire 5 missiles, and the 5th,
	// one creates a firestorm ... and then half a second later another volley of 5 missiles
	// come in, the second wave of 5 missiles would all do a historic weapon, making 5 more
	// firestorms (CBD) */
	//

	if( m_historicBonusCount > 0 && m_historicBonusWeapon != this )
	{
		trimOldHistoricDamage();

		Real radSqr = m_historicBonusRadius * m_historicBonusRadius;
		Int count = 0;
		UnsignedInt frameNow = TheGameLogic->getFrame();
		UnsignedInt oldestThatWillCount = frameNow - m_historicBonusTime; // Anything before this frame is "more than two seconds ago" eg
		for( HistoricWeaponDamageList::const_iterator it = m_historicDamage.begin(); it != m_historicDamage.end(); ++it )
		{
			if( it->frame >= oldestThatWillCount &&
					is2DDistSquaredLessThan( *pos, it->location, radSqr ) )
			{
				// This one is close enough in time and distance, so count it. This is tracked by template since it applies
				// across units, so don't try to clear historicDamage on success in here.
				++count;
			}
		}

		if( count >= m_historicBonusCount - 1 )	// minus 1 since we include ourselves implicitly
		{
		  TheWeaponStore->createAndFireTempWeapon(m_historicBonusWeapon, source, pos);

			/** @todo E3 hack! Clear the list for now to make sure we don't have multiple firestorms
				* remove this when the branches merge back into one.  What is causing the
				* multiple firestorms, who is to say ... this is a plug, not a fix! */
			m_historicDamage.clear();

		}
		else
		{

			// add AFTER checking for historic stuff
			m_historicDamage.push_back( HistoricWeaponDamageInfo(frameNow, *pos) );

		}

	}
}
#else
void WeaponTemplate::processHistoricDamage(const Object* source, const Coord3D* pos) const
{
	if (m_historicBonusCount > 0 && m_historicBonusWeapon != this)
	{
		trimOldHistoricDamage();

		++m_historicDamageTriggerId;

		const Int requiredCount = m_historicBonusCount - 1; // minus 1 since we include ourselves implicitly
		if (m_historicDamage.size() >= requiredCount)
		{
			const Real radSqr = m_historicBonusRadius * m_historicBonusRadius;
			Int count = 0;

			for (HistoricWeaponDamageList::iterator it = m_historicDamage.begin(); it != m_historicDamage.end(); ++it)
			{
				if (is2DDistSquaredLessThan(*pos, it->location, radSqr))
				{
					// This one is close enough in time and distance, so count it. This is tracked by template since it applies
					// across units, so don't try to clear historicDamage on success in here.
					it->triggerId = m_historicDamageTriggerId;

					if (++count == requiredCount)
					{
						TheWeaponStore->createAndFireTempWeapon(m_historicBonusWeapon, source, pos);
						trimTriggeredHistoricDamage();
						return;
					}
				}
			}
		}

		// add AFTER checking for historic stuff
		m_historicDamage.push_back(HistoricWeaponDamageInfo(TheGameLogic->getFrame(), *pos));
	}
}
#endif

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature split warhead (Generals Hypersonic D-062): a cluster weapon whose ClusterWeapon has a
// ProjectileObject releases that many warhead projectiles from the carrier (the detonating missile, bursting in the air)
// at once. Landing points: one near the centre, the others evenly round a ring at 85-100% of ClusterRadius (random turn,
// small jitter); then each warhead misses its point by its own random amount within the warhead weapon's ScatterRadius
// (sideways at most half the ring spacing, so two rarely land on one spot); all synced random. Landing times: nearest
// to the carrier first (horizontally), then outward, aiming at ClusterDelay / (count - 1) apart, the first as soon as
// no warhead has to fly faster than the warhead weapon's speed; no warhead flies slower than 75% of it (a short drop
// then lands its warheads closer together in time). Each warhead flies its path in exactly its time
// (projectileSetFlightFrames; with DumbProjectileBehavior BallisticDrop, the carrier's own ballistic descent from the
// split, fanned out to each warhead's point). The warheads are ordinary projectiles:
// launched by the carrier (its weapon bonus conditions, e.g. Improved Warheads, pass on), produced by and crediting
// 'launcherID' (the silo) when it still exists; each detonates with the warhead weapon where it lands.
//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-158) with a 'centerWeapon' the first warhead is that weapon's projectile, aimed exactly at the
// centre (only its own ScatterRadius, no ring); the other count - 1 form the ring as before.
static void launchClusterWarheads(const WeaponTemplate* warheadWeapon, Int count, Real radius, UnsignedInt spreadFrames,
	const Object* carrier, ObjectID launcherID, const Coord3D* center, const WeaponTemplate* centerWeapon)
{
	const ThingTemplate* tmpl = warheadWeapon->getProjectileTemplate();
	if (centerWeapon != nullptr && centerWeapon->getProjectileTemplate() == nullptr)
		centerWeapon = nullptr;
	Player* owner = carrier->getControllingPlayer();
	if (tmpl == nullptr || owner == nullptr || count <= 0)
		return;
	enum { MAX_WARHEADS = 64 };
	if (count > MAX_WARHEADS)
		count = MAX_WARHEADS;
	const Coord3D from = *carrier->getPosition();
	const Real speed = max(1.0f, warheadWeapon->getWeaponSpeed());
	Object* launcher = TheGameLogic->findObjectByID(launcherID);

	Coord3D points[MAX_WARHEADS];
	Int order[MAX_WARHEADS];
	const Real turn = GameLogicRandomValueReal(0.0f, 2.0f * PI);
	const Int ringCount = count > 1 ? count - 1 : 1;
	const Real ringScatter = warheadWeapon->getScatterRadius();
	Real dist[MAX_WARHEADS];
	Int i;
	for (i = 0; i < count; ++i)
	{
		Real r, a;
		Real scatter = ringScatter;
		if (i == 0 && centerWeapon != nullptr)
		{
			r = 0.0f;	// D-158: the centre warhead on the aim point
			a = 0.0f;
			scatter = centerWeapon->getScatterRadius();
		}
		else if (i == 0)
		{
			r = radius * GameLogicRandomValueReal(0.0f, 0.08f);
			a = GameLogicRandomValueReal(0.0f, 2.0f * PI);
		}
		else
		{
			r = radius * GameLogicRandomValueReal(0.85f, 1.0f);
			a = turn + 2.0f * PI * (Real)(i - 1) / (Real)ringCount + GameLogicRandomValueReal(-0.04f, 0.04f);
		}
		// each warhead's own miss (owner, D-062): uniform in the warhead weapon's ScatterRadius disc; on the ring its
		// sideways part is kept within half the ring spacing, so neighbours rarely land on one spot
		Real dr = 0.0f;
		Real dt = 0.0f;
		if (scatter > 0.0f)
		{
			const Real sr = scatter * sqrtf(GameLogicRandomValueReal(0.0f, 1.0f));
			const Real sa = GameLogicRandomValueReal(0.0f, 2.0f * PI);
			dr = sr * Cos(sa);
			dt = sr * Sin(sa);
			if (i > 0)
			{
				const Real limit = 0.5f * 2.0f * r * (Real)fabs(Sin(PI / (Real)ringCount));
				if (dt > limit)
					dt = limit;
				else if (dt < -limit)
					dt = -limit;
			}
		}
		points[i].x = center->x + (r + dr) * Cos(a) - dt * Sin(a);
		points[i].y = center->y + (r + dr) * Sin(a) + dt * Cos(a);
		points[i].z = TheTerrainLogic->getGroundHeight(points[i].x, points[i].y);
		order[i] = i;
		Coord3D d;
		d.x = points[i].x - from.x;
		d.y = points[i].y - from.y;
		d.z = 0.0f;
		dist[i] = d.length();	// horizontal: the flight speed is the horizontal speed (ballistic drop)
	}
	// nearest first (insertion sort: stable, deterministic)
	for (i = 1; i < count; ++i)
	{
		const Int t = order[i];
		Int j = i - 1;
		while (j >= 0 && dist[order[j]] > dist[t])
		{
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = t;
	}

	// the first landing: when no warhead has to fly faster than 'speed'
	const Real gap = count > 1 ? (Real)spreadFrames / (Real)(count - 1) : 0.0f;
	Real first = 1.0f;
	for (i = 0; i < count; ++i)
		first = max(first, dist[order[i]] / speed - (Real)i * gap);
	const UnsignedInt firstFrames = (UnsignedInt)REAL_TO_INT_CEIL(first) + 1;
	const Real MIN_SPEED_FRACTION = 0.75f;
	for (Int k = 0; k < count; ++k)
	{
		const Bool isCenter = (centerWeapon != nullptr && order[k] == 0);	// D-158
		const WeaponTemplate* weapon = isCenter ? centerWeapon : warheadWeapon;
		Object* warhead = TheThingFactory->newObject(isCenter ? centerWeapon->getProjectileTemplate() : tmpl, owner->getDefaultTeam());
		warhead->setProducer(launcher != nullptr ? (const Object*)launcher : carrier);
		ProjectileUpdateInterface* pui = nullptr;
		for (BehaviorModule** u = warhead->getBehaviorModules(); *u; ++u)
		{
			if ((pui = (*u)->getProjectileUpdateInterface()) != nullptr)
				break;
		}
		if (pui == nullptr)
		{
			TheGameLogic->destroyObject(warhead);
			continue;
		}
		pui->projectileLaunchAtObjectOrPosition(nullptr, &points[order[k]], carrier, PRIMARY_WEAPON, 0, weapon, nullptr);
		UnsignedInt frames = firstFrames + (UnsignedInt)REAL_TO_INT_FLOOR((Real)k * gap + 0.5f);
		const UnsignedInt slowest = (UnsignedInt)REAL_TO_INT_FLOOR(dist[order[k]] / (MIN_SPEED_FRACTION * speed)) + 1;
		if (frames > slowest)
			frames = max(slowest, (UnsignedInt)2);
		pui->projectileSetFlightFrames(frames);
		if (launcher != nullptr)
			warhead->getExperienceTracker()->setExperienceSink(launcher->getID());
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature one-by-one release (Generals Hypersonic D-162): one warhead of this cluster weapon, launched from the
// carrier (its weapon bonus conditions pass on, e.g. Improved Warheads) to 'point', flying its ballistic drop in
// exactly 'frames' frames and leaving with the carrier's vertical speed; produced by and crediting the launcher (the
// silo) when it still exists, like launchClusterWarheads.
//-------------------------------------------------------------------------------------------------
void WeaponTemplate::releaseClusterWarhead(const Object* carrier, ObjectID launcherID, const Coord3D* point, UnsignedInt frames, Real startVZ, const Coord3D* startVel) const
{
	if (!isClusterWeapon() || carrier == nullptr || point == nullptr)
		return;
	const ThingTemplate* tmpl = m_clusterWeapon->getProjectileTemplate();
	Player* owner = carrier->getControllingPlayer();
	if (tmpl == nullptr || owner == nullptr)
		return;
	Object* launcher = TheGameLogic->findObjectByID(launcherID);
	Object* warhead = TheThingFactory->newObject(tmpl, owner->getDefaultTeam());
	warhead->setProducer(launcher != nullptr ? (const Object*)launcher : carrier);
	ProjectileUpdateInterface* pui = nullptr;
	for (BehaviorModule** u = warhead->getBehaviorModules(); *u; ++u)
	{
		if ((pui = (*u)->getProjectileUpdateInterface()) != nullptr)
			break;
	}
	if (pui == nullptr)
	{
		TheGameLogic->destroyObject(warhead);
		return;
	}
	pui->projectileLaunchAtObjectOrPosition(nullptr, point, carrier, PRIMARY_WEAPON, 0, m_clusterWeapon, nullptr);
	pui->projectileSetStartVerticalSpeed(startVZ);
	pui->projectileSetFlightFrames(max(frames, (UnsignedInt)2));
	if (startVel != nullptr)	// Hypersonic @feature cone release (D-267)
		pui->projectileSetStartHorizontalSpeed(startVel->x, startVel->y);
	if (launcher != nullptr)
		warhead->getExperienceTracker()->setExperienceSink(launcher->getID());
}

//-------------------------------------------------------------------------------------------------
void WeaponTemplate::dealDamageInternal(ObjectID sourceID, ObjectID victimID, const Coord3D *pos, const WeaponBonus& bonus, Bool isProjectileDetonation) const
{
	if (sourceID == 0)	// must have a source
		return;

	if (victimID == 0 && pos == nullptr)	// must have some sort of destination
		return;

	Object *source = TheGameLogic->findObjectByID(sourceID);	// might be null...

	processHistoricDamage(source, pos);

//DEBUG_LOG(("WeaponTemplate::dealDamageInternal: dealing damage %s at frame %d",m_name.str(),TheGameLogic->getFrame()));

	// if there's a specific victim, use it's pos (overriding the value passed in)
	Object *primaryVictim = victimID ? TheGameLogic->findObjectByID(victimID) : nullptr;	// might be null...
	if (primaryVictim)
	{
		pos = primaryVictim->getPosition();
	}

	// Hypersonic @feature cluster warhead (Generals Hypersonic D-048): instead of one blast, spread ClusterCount bomblets
	// evenly over the ClusterRadius disc around 'pos' (a golden-angle spiral with a random turn and jitter: every ring of
	// the disc gets its share, synced random), each landing at a random frame within ClusterDelay. They are not objects:
	// each is a delayed ClusterWeapon blast (its damage, radius and ProjectileDetonationFX) with this shot's bonus
	// (Improved Warheads), dealt by the launcher of the projectile (the projectile is gone by then).
	if (isClusterWeapon() && pos != nullptr && TheWeaponStore != nullptr)
	{
		ObjectID bombletSourceID = sourceID;
		if (source != nullptr && source->isKindOf(KINDOF_PROJECTILE))
		{
			for (BehaviorModule** u = source->getBehaviorModules(); *u; ++u)
			{
				ProjectileUpdateInterface* pui = (*u)->getProjectileUpdateInterface();
				if (pui != nullptr)
				{
					if (TheGameLogic->findObjectByID(pui->projectileGetLauncherID()) != nullptr)
						bombletSourceID = pui->projectileGetLauncherID();
					break;
				}
			}
		}
		// Hypersonic @feature split warhead (D-062): projectile warheads fly from the bursting carrier to their points
		if (m_clusterWeapon->getProjectileTemplate() != nullptr && source != nullptr)
		{
			launchClusterWarheads(m_clusterWeapon, m_clusterCount, m_clusterRadius, m_clusterDelay, source, bombletSourceID, pos, m_clusterCenterWeapon);
			return;
		}
		const Real GOLDEN_ANGLE = 2.39996323f;
		const Real turn = GameLogicRandomValueReal(0.0f, 2.0f * PI);
		const UnsignedInt now = TheGameLogic->getFrame();
		for (Int i = 0; i < m_clusterCount; ++i)
		{
			const Real ring = sqrtf(((Real)i + GameLogicRandomValueReal(0.0f, 1.0f)) / (Real)m_clusterCount);
			const Real angle = turn + GOLDEN_ANGLE * (Real)i + GameLogicRandomValueReal(-0.3f, 0.3f);
			Coord3D bomblet;
			bomblet.x = pos->x + m_clusterRadius * ring * Cos(angle);
			bomblet.y = pos->y + m_clusterRadius * ring * Sin(angle);
			bomblet.z = TheTerrainLogic->getGroundHeight(bomblet.x, bomblet.y);
			const UnsignedInt when = now + 1 + (UnsignedInt)GameLogicRandomValue(0, (Int)m_clusterDelay);
			TheWeaponStore->setDelayedDamage(m_clusterWeapon, &bomblet, when, bombletSourceID, INVALID_ID, bonus, TRUE);
		}
		return;
	}

	DamageType damageType = getDamageType();
	DeathType deathType = getDeathType();
	ObjectStatusTypes damageStatusType = getDamageStatusType();
	if (getProjectileTemplate() == nullptr || isProjectileDetonation)
	{
		SimpleObjectIterator *iter;
		Object *curVictim;
		Real curVictimDistSqr;

		Real primaryRadius = getPrimaryDamageRadius(bonus);
		Real secondaryRadius = getSecondaryDamageRadius(bonus);
		Real primaryDamage = getPrimaryDamage(bonus);
		Real secondaryDamage = getSecondaryDamage(bonus);
		// Hypersonic @feature third ring (Generals Hypersonic D-222): beyond the secondary radius, within this one
		Real tertiaryRadius = getTertiaryDamageRadius(bonus);
		Real tertiaryDamage = getTertiaryDamage(bonus);
		Int affects = getAffectsMask();

		DEBUG_ASSERTCRASH(secondaryRadius >= primaryRadius || secondaryRadius == 0.0f, ("secondary radius should be >= primary radius (or zero)"));

		Real primaryRadiusSqr = sqr(primaryRadius);
		Real radius = max(primaryRadius, secondaryRadius);
		Real tertiaryInnerSqr = sqr(radius);	// Hypersonic @feature (D-222): the third ring starts where the others end
		if (tertiaryRadius > radius)
			radius = tertiaryRadius;
		else
			tertiaryRadius = 0.0f;
		if (radius > 0.0f)
		{
			iter = ThePartitionManager->iterateObjectsInRange(pos, radius, DAMAGE_RANGE_CALC_TYPE);
			curVictim = iter->firstWithNumeric(&curVictimDistSqr);
		}
		else
		{
			//DEBUG_ASSERTCRASH(primaryVictim != nullptr, ("weapons without radii should always pass in specific victims"));
			// check against victimID rather than primaryVictim, since we may have targeted a legitimate victim
			// that got killed before the damage was dealt... (srj)
			//DEBUG_ASSERTCRASH(victimID != 0, ("weapons without radii should always pass in specific victims"));
			iter = nullptr;
			curVictim = primaryVictim;
			curVictimDistSqr = 0.0f;

			if( affects & WEAPON_KILLS_SELF )
			{
				DamageInfo damageInfo;
				damageInfo.in.m_damageType = damageType;
				damageInfo.in.m_deathType = deathType;
				damageInfo.in.m_sourceID = sourceID;
				damageInfo.in.m_sourcePlayerMask = 0;
				damageInfo.in.m_damageStatusType = damageStatusType;
				damageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
				source->attemptDamage( &damageInfo );
				return;
			}
		}
		MemoryPoolObjectHolder hold(iter);

		for (; curVictim != nullptr; curVictim = iter ? iter->nextWithNumeric(&curVictimDistSqr) : nullptr)
		{
			Bool killSelf = false;
			if (source != nullptr)
			{
				// anytime something is designated as the "primary victim" (ie, the direct target
				// of the weapon), we ignore all the "affects" flags.
				if (curVictim != primaryVictim)
				{

					if( (affects & WEAPON_KILLS_SELF) && source == curVictim )
					{
						killSelf = true;
					}
					else
					{

						// should object ever be allowed to damage themselves? methinks not...
						// exception: a few weapons allow this (eg, for suicide bombers).
						if( (affects & WEAPON_AFFECTS_SELF) == 0 )
						{
							// Remember that source is a missile for some units, and they don't want to injure them'selves' either
							if( source == curVictim || source->getProducerID() == curVictim->getID() )
							{
								//DEBUG_LOG(("skipping damage done to SELF..."));
								continue;
							}
						}

						if( affects & WEAPON_DOESNT_AFFECT_SIMILAR )
						{
							//This means we probably are affecting allies, but don't want to kill nearby members that are the same type as us.
							//A good example are a group of terrorists blowing themselves up. We don't want to cause a domino effect that kills
							//all of them.
							if( source->getTemplate()->isEquivalentTo(curVictim->getTemplate()) && source->getRelationship( curVictim ) == ALLIES )
							{
								continue;
							}
						}

						if ((affects & WEAPON_DOESNT_AFFECT_AIRBORNE) != 0 && curVictim->isSignificantlyAboveTerrain())
						{
							continue;
						}

						/*
							The idea here is: if its our ally(/enemies), AND it's not the direct target, AND the weapon doesn't
							do radius-damage to allies(/enemies)... skip it.
						*/
						Relationship r = curVictim->getRelationship(source);
						Int requiredMask;
						if (r == ALLIES)
							requiredMask = WEAPON_AFFECTS_ALLIES;
						else if (r == ENEMIES)
							requiredMask = WEAPON_AFFECTS_ENEMIES;
						else /* r == NEUTRAL */
							requiredMask = WEAPON_AFFECTS_NEUTRALS;

						if( !(affects & requiredMask) )
						{
							//Skip if we aren't affected by this weapon.
							continue;
						}
					}
				}
			}

			DamageInfo damageInfo;
			damageInfo.in.m_damageType = damageType;
			damageInfo.in.m_deathType = deathType;
			damageInfo.in.m_sourceID = sourceID;
			damageInfo.in.m_sourcePlayerMask = 0;
			damageInfo.in.m_damageStatusType = damageStatusType;

			Coord3D damageDirection;
			damageDirection.zero();
			if( curVictim && source )
			{
				damageDirection.set( *curVictim->getPosition() );
				damageDirection.sub( *source->getPosition() );
			}

			Real allowedAngle = getRadiusDamageAngle();
			if( allowedAngle < PI )
			{
				if( curVictim == nullptr  ||  source == nullptr )
					continue; // We are directional damage, but can't figure out our direction.  Just bail.

				// People can only be hit in a cone oriented as the firer is oriented
				Vector3 sourceVector = source->getTransformMatrix()->Get_X_Vector();
				Vector3 damageVector(damageDirection.x, damageDirection.y, damageDirection.z);
				sourceVector.Normalize();
				damageVector.Normalize();

				// These are now normalized, so the dot productis actually the Cos of the angle they form
				// A smaller Cos would mean a more obtuse angle
				if( Vector3::Dot_Product(sourceVector, damageVector) < Cos(allowedAngle) )
					continue;// Too far to the side, can't hurt them.
			}

			// Grab the vector between the source object causing the damage and the victim in order that we can
			// simulate a shockwave pushing objects around
			damageInfo.in.m_shockWaveAmount = m_shockWaveAmount;
			if (damageInfo.in.m_shockWaveAmount > 0.0f)
			{
				// Calculate the vector of the shockwave
				Coord3D shockWaveVector = damageDirection;

				// Guard against zero vector. Make vector straight up if that is the case
				if (fabs(shockWaveVector.x) < WWMATH_EPSILON &&
						fabs(shockWaveVector.y) < WWMATH_EPSILON &&
						fabs(shockWaveVector.z) < WWMATH_EPSILON)
				{
					shockWaveVector.z = 1.0f;
				}

				// Populate the damage information with the shockwave information
				damageInfo.in.m_shockWaveVector = shockWaveVector;
				damageInfo.in.m_shockWaveRadius = m_shockWaveRadius;
				damageInfo.in.m_shockWaveTaperOff = m_shockWaveTaperOff;
			}

      if (source && source->getControllingPlayer()) {
				damageInfo.in.m_sourcePlayerMask = source->getControllingPlayer()->getPlayerMask();
			}
			// note, don't bother with damage multipliers here...
			// that's handled internally by the attemptDamage() method.
			damageInfo.in.m_amount = (curVictimDistSqr <= primaryRadiusSqr) ? primaryDamage : secondaryDamage;
			if (tertiaryRadius > 0.0f && curVictimDistSqr > tertiaryInnerSqr)
				damageInfo.in.m_amount = tertiaryDamage;	// Hypersonic @feature third ring (D-222)

			if( killSelf )
			{
				//Deal enough damage to kill yourself. I thought about getting the current health and applying
				//enough unresistable damage to die... however it's possible that we have different types of
				//deaths based on damage type and/or the possibility to resist certain damage types and
				//surviving -- so instead, I'm blindly inflicting a very high value of the intended damage type.
				damageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
				//BodyModuleInterface* body = curVictim->getBodyModule();
				//if( body )
				//{
				//	Real curVictimHealth = curVictim->getBodyModule()->getHealth();
				//	damageInfo.in.m_amount = __max( damageInfo.in.m_amount, curVictimHealth );
				//}
			}

			// Hypersonic @feature Interception rules (Generals Hypersonic IRAN_DESIGN 7c): every shot that would damage an
			// object with an active HitChanceBehavior rolls its hit chance (synced random) and does nothing to it on a
			// miss. A SAM-class hit on a target marked SAMHitIsLethal destroys it regardless of its health.
			if( !killSelf && curVictim != source && (curVictim->isKindOf( KINDOF_PROJECTILE ) || curVictim->isAirborneTarget()) )
			{
				// D-245 Update 8: a vanished carrier cannot roll an interception or award a kill during KILL_SELF.
				// Regular self-killing missiles are not MASKED, so their impact behavior stays unchanged.
				if( curVictim->testStatus( OBJECT_STATUS_MISSILE_KILLING_SELF ) && curVictim->testStatus( OBJECT_STATUS_MASKED ) )
					continue;
				const HitChanceBehavior *hitChance = HitChanceBehavior::findHitChanceBehavior( curVictim );
				if( hitChance != nullptr && hitChance->isActive() )
				{
					// Hypersonic @feature (D-059) Point Defense Laser shots roll the target's LaserHitChance
					// Hypersonic (D-195, owner: drones die fastest to China's gun, then the Patriot, then GLA; only the
					// interception odds change, not the defense): the weapon's DroneHitChanceScale at an Iran drone,
					// MissileHitChanceScale at an Iran missile (2026-10-21: one hit kills, a lower chance per shot, so guns scale
					// with their numbers like SAMs instead of adding up hits)
					if( !hitChance->rollHit( m_interceptorClass == INTERCEPTOR_LASER,
							hitChance->isSAMHitLethal() ? m_missileHitChanceScale : m_droneHitChanceScale ) )
						continue;

					// Hypersonic (D-195, owner 2026-10-20: missile defense Patriot > China's gun > GLA): a weapon with
					// MissileHitsToKill N destroys an Iran missile (SAMHitIsLethal) with its N-th hit, whatever its class:
					// each hit takes 1/N of the missile's max health, unresisted (ProjectileArmor would quarter it)
					BodyModuleInterface *missileBody = hitChance->isSAMHitLethal() ? curVictim->getBodyModule() : nullptr;
					if( m_missileHitsToKill > 0 && missileBody != nullptr )
					{
						damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
						damageInfo.in.m_amount = missileBody->getMaxHealth() / (Real)m_missileHitsToKill + 0.01f;
					}
					else if( m_interceptorClass == INTERCEPTOR_SAM && hitChance->isSAMHitLethal() )
						damageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
				}
				// Hypersonic @feature (D-147) the weapon's own HitChance (e.g. the Bavar-373's 90%) for targets without one
				else if( m_hitChance < 1.0f && !(GameLogicRandomValueReal( 0.0f, 1.0f ) < m_hitChance) )
				{
					continue;
				}
			}

			// if the damage-dealer is a projectile, designate the damage as done by its launcher, not the projectile.
			// this is much more useful for the AI...
			if (source && source->isKindOf(KINDOF_PROJECTILE))
			{
				for (BehaviorModule** u = source->getBehaviorModules(); *u; ++u)
				{
					ProjectileUpdateInterface* pui = (*u)->getProjectileUpdateInterface();
					if (pui != nullptr)
					{
						damageInfo.in.m_sourceID = pui->projectileGetLauncherID();
						break;
					}
				}
			}

			curVictim->attemptDamage(&damageInfo);
			//DEBUG_ASSERTLOG(damageInfo.out.m_noEffect, ("WeaponTemplate::dealDamageInternal: dealt to %s %08lx: attempted %f, actual %f (%f)",
			//	curVictim->getTemplate()->getName().str(),curVictim,
			//	damageInfo.in.m_amount, damageInfo.out.m_actualDamageDealt, damageInfo.out.m_actualDamageClipped));
		}
	}
	else
	{
		DEBUG_CRASH(("projectile weapons should never get dealDamage called directly"));
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WeaponStore::WeaponStore()
{
}

//-------------------------------------------------------------------------------------------------
WeaponStore::~WeaponStore()
{
	deleteAllDelayedDamage();

	for (size_t i = 0; i < m_weaponTemplateVector.size(); i++)
	{
		WeaponTemplate* wt = m_weaponTemplateVector[i];
		deleteInstance(wt);
	}
	m_weaponTemplateVector.clear();
	m_weaponTemplateHashMap.clear();
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::handleProjectileDetonation(const WeaponTemplate* wt, const Object *source, const Coord3D* pos, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage )
{
	Weapon* w = allocateNewWeapon(wt, PRIMARY_WEAPON);
	w->loadAmmoNow(source);
	w->fireProjectileDetonationWeapon( source, pos, extraBonusFlags, inflictDamage );
	deleteInstance(w);
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::createAndFireTempWeapon(const WeaponTemplate* wt, const Object *source, const Coord3D* pos)
{
	if (wt == nullptr)
		return;
	Weapon* w = allocateNewWeapon(wt, PRIMARY_WEAPON);
	w->loadAmmoNow(source);
	w->fireWeapon(source, pos);
	deleteInstance(w);
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::createAndFireTempWeapon(const WeaponTemplate* wt, const Object *source, Object *target)
{
	//CRCDEBUG_LOG(("WeaponStore::createAndFireTempWeapon() for %s", DescribeObject(source)));
	if (wt == nullptr)
		return;
	Weapon* w = allocateNewWeapon(wt, PRIMARY_WEAPON);
	w->loadAmmoNow(source);
	w->fireWeapon(source, target);
	deleteInstance(w);
}

//-------------------------------------------------------------------------------------------------
const WeaponTemplate *WeaponStore::findWeaponTemplate( const AsciiString& name ) const
{
	if (name.compareNoCase("None") == 0)
		return nullptr;
	const WeaponTemplate * wt = findWeaponTemplatePrivate( TheNameKeyGenerator->nameToKey( name ) );
	DEBUG_ASSERTCRASH(wt != nullptr, ("Weapon %s not found!", name.str()));
	return wt;
}

//-------------------------------------------------------------------------------------------------
const WeaponTemplate *WeaponStore::findWeaponTemplate( const char* name ) const
{
	if (stricmp(name, "None") == 0)
		return nullptr;
	const WeaponTemplate * wt = findWeaponTemplatePrivate( TheNameKeyGenerator->nameToKey( name ) );
	DEBUG_ASSERTCRASH(wt != nullptr, ("Weapon %s not found!",name));
	return wt;
}

//-------------------------------------------------------------------------------------------------
WeaponTemplate *WeaponStore::findWeaponTemplatePrivate( NameKeyType key ) const
{
	// search weapon list for name
	WeaponTemplateMap::const_iterator it = m_weaponTemplateHashMap.find(key);
	if(it != m_weaponTemplateHashMap.end())
		return it->second;

	return nullptr;

}

//-------------------------------------------------------------------------------------------------
WeaponTemplate *WeaponStore::newWeaponTemplate(AsciiString name)
{

	// sanity
	if(name.isEmpty())
		return nullptr;

	// allocate a new weapon
	WeaponTemplate *wt = newInstance(WeaponTemplate);
	wt->m_name = name;
	wt->m_nameKey = TheNameKeyGenerator->nameToKey( name );
	m_weaponTemplateVector.push_back(wt);
	m_weaponTemplateHashMap[wt->m_nameKey] = wt;

	return wt;
}

//-------------------------------------------------------------------------------------------------
WeaponTemplate *WeaponStore::newOverride(WeaponTemplate *weaponTemplate)
{
	if (!weaponTemplate)
		return nullptr;

	// allocate a new weapon
	WeaponTemplate *wt = newInstance(WeaponTemplate);
	(*wt) = (*weaponTemplate);
	(wt)->friend_setNextTemplate(weaponTemplate);

	return wt;
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::update()
{
	for (std::list<WeaponDelayedDamageInfo>::iterator ddi = m_weaponDDI.begin(); ddi != m_weaponDDI.end(); )
	{
		UnsignedInt curFrame = TheGameLogic->getFrame();
		if (curFrame >= ddi->m_delayDamageFrame)
		{
			// we never do projectile-detonation-damage via this code path.
			const Bool isProjectileDetonation = false;
			// Hypersonic @feature a cluster bomblet (D-048) plays its detonation FX where it lands
			if (ddi->m_playDetonationFX)
			{
				const FXList* fx = ddi->m_delayedWeapon->getProjectileDetonateFX(LEVEL_REGULAR);
				FXList::ScaleScope fxScale(ddi->m_delayedWeapon->getDetonationFXScale(fx, ddi->m_bonus) * ddi->m_delayedWeapon->getFXScale());	// D-060, D-148
				FXList::doFXPos(fx, &ddi->m_delayDamagePos);
			}
			ddi->m_delayedWeapon->dealDamageInternal(ddi->m_delaySourceID, ddi->m_delayIntendedVictimID, &ddi->m_delayDamagePos, ddi->m_bonus, isProjectileDetonation);
			ddi = m_weaponDDI.erase(ddi);
		}
		else
		{
			++ddi;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::deleteAllDelayedDamage()
{
	m_weaponDDI.clear();
}

// ------------------------------------------------------------------------------------------------
void WeaponStore::resetWeaponTemplates()
{

	for (size_t i = 0; i < m_weaponTemplateVector.size(); i++)
	{
		WeaponTemplate* wt = m_weaponTemplateVector[i];
		wt->reset();
	}

}

//-------------------------------------------------------------------------------------------------
void WeaponStore::reset()
{
	// clean up any overrides.
	for (size_t i = 0; i < m_weaponTemplateVector.size(); ++i)
	{
		WeaponTemplate *wt = m_weaponTemplateVector[i];
		if (wt->isOverride())
		{
			WeaponTemplate *overrideData = wt;
			wt = wt->friend_clearNextTemplate();
			deleteInstance(overrideData);
		}
	}

	deleteAllDelayedDamage();
	resetWeaponTemplates();
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::setDelayedDamage(const WeaponTemplate *weapon, const Coord3D* pos, UnsignedInt whichFrame, ObjectID sourceID, ObjectID victimID, const WeaponBonus& bonus, Bool playDetonationFX)
{
	WeaponDelayedDamageInfo wi;
	wi.m_delayedWeapon = weapon;
	wi.m_delayDamagePos = *pos;
	wi.m_delayDamageFrame = whichFrame;
	wi.m_delaySourceID = sourceID;
	wi.m_delayIntendedVictimID = victimID;
	wi.m_bonus = bonus;
	wi.m_playDetonationFX = playDetonationFX;
	m_weaponDDI.push_back(wi);
}

//-------------------------------------------------------------------------------------------------
void WeaponStore::postProcessLoad()
{
	if (!TheThingFactory)
	{
		DEBUG_CRASH(("you must call this after TheThingFactory is inited"));
		return;
	}

	for (size_t i = 0; i < m_weaponTemplateVector.size(); i++)
	{
		WeaponTemplate* wt = m_weaponTemplateVector[i];
		if (wt)
			wt->postProcessLoad();
	}

}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponStore::parseWeaponTemplateDefinition(INI* ini)
{
	AsciiString name;

	// read the weapon name
	const char* c = ini->getNextToken();
	name.set(c);

	// find existing item if present
	WeaponTemplate *weapon = TheWeaponStore->findWeaponTemplatePrivate( TheNameKeyGenerator->nameToKey( name ) );
	if (weapon)
	{
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
			weapon = TheWeaponStore->newOverride(weapon);
		else
		{
			DEBUG_CRASH(("Weapon '%s' already exists, but OVERRIDE not specified", c));
			return;
		}

	}
	else
	{
		// no item is present, create a new one
		weapon = TheWeaponStore->newWeaponTemplate(name);
	}

	// parse the ini weapon definition
	ini->initFromINI(weapon, weapon->getFieldParse());

	if (weapon->m_projectileName.isNone())
		weapon->m_projectileName.clear();

#if defined(RTS_DEBUG)
	if (!weapon->getFireSound().getEventName().isEmpty() && weapon->getFireSound().getEventName().compareNoCase("NoSound") != 0)
	{
		DEBUG_ASSERTCRASH(TheAudio->isValidAudioEvent(&weapon->getFireSound()), ("Invalid FireSound %s in Weapon '%s'.", weapon->getFireSound().getEventName().str(), weapon->getName().str()));
	}
#endif

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
Weapon::Weapon(const WeaponTemplate* tmpl, WeaponSlotType wslot)
{
	// Weapons start empty; you must reload before use.
	// (however, there is no delay for reloading the first time.)
	m_template = tmpl;
	m_wslot = wslot;
	m_status = OUT_OF_AMMO;
	m_ammoInClip = 0;
	m_whenWeCanFireAgain = 0;
	m_whenPreAttackFinished = 0;
	m_whenLastReloadStarted = 0;
	m_projectileStreamID = INVALID_ID;
	m_leechWeaponRangeActive = false;
	m_pitchLimited = (m_template->getMinTargetPitch() > -PI || m_template->getMaxTargetPitch() < PI);
	m_maxShotCount = NO_MAX_SHOTS_LIMIT;
	m_curBarrel = 0;
	m_numShotsForCurBarrel = 	m_template->getShotsPerBarrel();
	m_lastFireFrame = 0;
	m_suspendFXFrame = TheGameLogic->getFrame() + m_template->getSuspendFXDelay();
}

//-------------------------------------------------------------------------------------------------
Weapon::Weapon(const Weapon& that)
{
	// Weapons lose all ammo when copied.
	this->m_template = that.m_template;
	this->m_wslot = that.m_wslot;
	this->m_status = OUT_OF_AMMO;
	this->m_ammoInClip = 0;
	this->m_whenPreAttackFinished = 0;
	this->m_whenLastReloadStarted = 0;
	this->m_whenWeCanFireAgain = 0;
	this->m_projectileStreamID = INVALID_ID;
	this->m_leechWeaponRangeActive = false;
	this->m_pitchLimited = (m_template->getMinTargetPitch() > -PI || m_template->getMaxTargetPitch() < PI);
	this->m_maxShotCount = NO_MAX_SHOTS_LIMIT;
	this->m_curBarrel = 0;
	this->m_numShotsForCurBarrel = m_template->getShotsPerBarrel();
	this->m_lastFireFrame = 0;
	this->m_suspendFXFrame = that.getSuspendFXFrame();
}

//-------------------------------------------------------------------------------------------------
Weapon& Weapon::operator=(const Weapon& that)
{
	if (this != &that)
	{
		// Weapons lose all ammo when copied.
		this->m_template = that.m_template;
		this->m_wslot = that.m_wslot;
		this->m_status = OUT_OF_AMMO;
		this->m_ammoInClip = 0;
		this->m_whenPreAttackFinished = 0;
		this->m_whenLastReloadStarted = 0;
		this->m_whenWeCanFireAgain = 0;
		this->m_leechWeaponRangeActive = false;
		this->m_pitchLimited = (m_template->getMinTargetPitch() > -PI || m_template->getMaxTargetPitch() < PI);
		this->m_maxShotCount = NO_MAX_SHOTS_LIMIT;
		this->m_curBarrel = 0;
		this->m_lastFireFrame = 0;
		this->m_suspendFXFrame = that.getSuspendFXFrame();
		this->m_numShotsForCurBarrel = m_template->getShotsPerBarrel();
		this->m_projectileStreamID = INVALID_ID;
	}
	return *this;
}

//-------------------------------------------------------------------------------------------------
Weapon::~Weapon()
{
}

//-------------------------------------------------------------------------------------------------
void Weapon::computeBonus(const Object *source, WeaponBonusConditionFlags extraBonusFlags, WeaponBonus& bonus) const
{
	bonus.clear();
	WeaponBonusConditionFlags flags = source->getWeaponBonusCondition();
	//CRCDEBUG_LOG(("Weapon::computeBonus() - flags are %X for %s", flags, DescribeObject(source).str()));
	flags |= extraBonusFlags;

	if( source->getContainedBy() )
	{
		// We may be able to add in our container's flags
		const ContainModuleInterface *theirContain = source->getContainedBy()->getContain();
		if( theirContain && theirContain->isWeaponBonusPassedToPassengers() )
			flags |= theirContain->getWeaponBonusPassedToPassengers();
	}

	if (TheGlobalData->m_weaponBonusSet)
		TheGlobalData->m_weaponBonusSet->appendBonuses(flags, bonus);
	const WeaponBonusSet* extra = m_template->getExtraBonus();
	if (extra)
		extra->appendBonuses(flags, bonus);
}

//-------------------------------------------------------------------------------------------------
void Weapon::loadAmmoNow(const Object *sourceObj)
{
	WeaponBonus bonus;
	computeBonus(sourceObj, 0, bonus);
	reloadWithBonus(sourceObj, bonus, true);
}

//-------------------------------------------------------------------------------------------------
void Weapon::reloadAmmo(const Object *sourceObj)
{

	WeaponBonus bonus;
	computeBonus(sourceObj, 0, bonus);
	reloadWithBonus(sourceObj, bonus, false);
}

//-------------------------------------------------------------------------------------------------
Int Weapon::getClipReloadTime(const Object *source) const
{
	WeaponBonus bonus;
	computeBonus(source, 0, bonus);
	return m_template->getClipReloadTime(bonus);
}

//-------------------------------------------------------------------------------------------------
void Weapon::setClipPercentFull(Real percent, Bool allowReduction)
{
	if (m_template->getClipSize() == 0)
		return;

	Int ammo = REAL_TO_INT_FLOOR(m_template->getClipSize() * percent);
	if (ammo > m_ammoInClip || (allowReduction && ammo < m_ammoInClip))
	{
		m_ammoInClip = ammo;
		m_status = m_ammoInClip ? OUT_OF_AMMO : READY_TO_FIRE;
		//CRCDEBUG_LOG(("Weapon::setClipPercentFull() just set m_status to %d (ammo in clip is %d)", m_status, m_ammoInClip));
		m_whenLastReloadStarted = TheGameLogic->getFrame();
		m_whenWeCanFireAgain = m_whenLastReloadStarted;
		//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d in Weapon::setClipPercentFull", m_whenWeCanFireAgain));
		rebuildScatterTargets();
	}
}

//-------------------------------------------------------------------------------------------------
void Weapon::rebuildScatterTargets()
{
	m_scatterTargetsUnused.clear();
	Int scatterTargetsCount = m_template->getScatterTargetsVector().size();
	if (scatterTargetsCount)
	{
		// When I reload, I need to rebuild the list of ScatterTargets to shoot at.
		for (Int targetIndex = 0; targetIndex < scatterTargetsCount; targetIndex++)
			m_scatterTargetsUnused.push_back( targetIndex );
	}
}

//-------------------------------------------------------------------------------------------------
void Weapon::reloadWithBonus(const Object *sourceObj, const WeaponBonus& bonus, Bool loadInstantly)
{
	if (m_template->getClipSize() > 0
			&& m_ammoInClip == m_template->getClipSize()
			&& !sourceObj->isReloadTimeShared())
		return;	// don't restart our reload delay.

	m_ammoInClip = m_template->getClipSize();
	if (m_ammoInClip <= 0)
		m_ammoInClip = 0x7fffffff;	// 0 == unlimited (or effectively so)

	m_status = RELOADING_CLIP;
	Real reloadTime = loadInstantly ? 0 : m_template->getClipReloadTime(bonus);
	m_whenLastReloadStarted = TheGameLogic->getFrame();
	m_whenWeCanFireAgain = m_whenLastReloadStarted + reloadTime;
	//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d in Weapon::reloadWithBonus 1", m_whenWeCanFireAgain));

			// if we are sharing reload times
			// go through other weapons in weapon set
			// set their m_whenWeCanFireAgain to this guy's delay
			// set their m_status to this guy's status
	if (sourceObj->isReloadTimeShared())
	{
		for (Int wt = 0; wt<WEAPONSLOT_COUNT; wt++)
		{
			Weapon *weapon = sourceObj->getWeaponInWeaponSlot((WeaponSlotType)wt);
			if (weapon)
			{
				weapon->setPossibleNextShotFrame(m_whenWeCanFireAgain);
				//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d in Weapon::reloadWithBonus 2", m_whenWeCanFireAgain));
				weapon->setStatus(RELOADING_CLIP);
			}
		}
	}

	rebuildScatterTargets();
}

//-------------------------------------------------------------------------------------------------
static void clipToTerrainExtent(Coord3D& approachTargetPos)
{
	Region3D bounds;
	TheTerrainLogic->getExtent(&bounds);
	if (approachTargetPos.x < bounds.lo.x+PATHFIND_CELL_SIZE_F) {
		approachTargetPos.x = bounds.lo.x+PATHFIND_CELL_SIZE_F;
	}
	if (approachTargetPos.y < bounds.lo.y+PATHFIND_CELL_SIZE_F) {
		approachTargetPos.y = bounds.lo.y+PATHFIND_CELL_SIZE_F;
	}
	if (approachTargetPos.x > bounds.hi.x-PATHFIND_CELL_SIZE_F) {
		approachTargetPos.x = bounds.hi.x-PATHFIND_CELL_SIZE_F;
	}
	if (approachTargetPos.y > bounds.hi.y-PATHFIND_CELL_SIZE_F) {
		approachTargetPos.y = bounds.hi.y-PATHFIND_CELL_SIZE_F;
	}
}

//-------------------------------------------------------------------------------------------------
void Weapon::onWeaponBonusChange(const Object *source)
{
	// We are concerned with our reload times being off if our ROF just changed.

	WeaponBonus bonus;
	computeBonus(source, 0, bonus); // The middle arg is for projectiles to inherit damage bonus from launcher

	Int newDelay;
	Bool needUpdate = FALSE;

	if( getStatus() == RELOADING_CLIP )
	{
		newDelay = m_template->getClipReloadTime(bonus);
		needUpdate = TRUE;
	}
	else if( getStatus() == BETWEEN_FIRING_SHOTS )
	{
		newDelay = m_template->getDelayBetweenShots(bonus);
		needUpdate = TRUE;
	}

	if( needUpdate )
	{
		m_whenLastReloadStarted = TheGameLogic->getFrame();
		m_whenWeCanFireAgain = m_whenLastReloadStarted + newDelay;

		if (source->isReloadTimeShared())
		{
			for (Int wt = 0; wt<WEAPONSLOT_COUNT; wt++)
			{
				Weapon *weapon = source->getWeaponInWeaponSlot((WeaponSlotType)wt);
				if (weapon)
				{
					weapon->setPossibleNextShotFrame(m_whenWeCanFireAgain);
					weapon->setStatus(RELOADING_CLIP);
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::computeApproachTarget(const Object *source, const Object *target, const Coord3D *pos, Real angleOffset, Coord3D& approachTargetPos) const
{
	// compute unit direction vector from us to our victim
	const Coord3D *targetPos;
	Coord3D dir;
	if (target)
	{
		targetPos = target->getPosition();
		ThePartitionManager->getVectorTo( target, source, ATTACK_RANGE_CALC_TYPE, dir );
	}
	else if (pos)
	{
		targetPos = pos;
		ThePartitionManager->getVectorTo( source, pos, ATTACK_RANGE_CALC_TYPE, dir );
		// Flip the vector to get from source to pos.
		dir.x = -dir.x;
		dir.y = -dir.y;
		dir.z = -dir.z;
	}
	else
	{
		DEBUG_CRASH(("error"));
		approachTargetPos.zero();
		return false;
	}

	Real dist = dir.length();
	Real minAttackRange = m_template->getMinimumAttackRange();
	if (minAttackRange > PATHFIND_CELL_SIZE_F && dist < minAttackRange)
	{
		// We aret too close, so move away from the target.
		DEBUG_ASSERTCRASH((minAttackRange<0.9f*getAttackRange(source)), ("Min attack range is too near attack range."));
		// Recompute dir, cause if the bounding spheres touch, it will be 0.
		Coord3D srcPos = *source->getPosition();
		dir.x = srcPos.x-targetPos->x;
		dir.y = srcPos.y-targetPos->y;
#ifdef ATTACK_RANGE_IS_2D
		dir.z = 0.0f;
#else
		dir.z = srcPos.z-targetPos->z;
#endif
		dir.normalize();

		// if we're airborne and too close, just head for the opposite side.
		if (source->isAboveTerrain())
		{
			// Don't do a 180 degree turn.
			Real angle = atan2(-dir.y, -dir.x);
			Real relAngle = source->getOrientation()- angle;
			if (relAngle>2*PI) relAngle -= 2*PI;
			if (relAngle<-2*PI) relAngle += 2*PI;
			if (fabs(relAngle)<PI/2) {
				dir.x = -dir.x;
				dir.y = -dir.y;
				dir.z = -dir.z;
			}
		}

		if (angleOffset != 0.0f)
		{
			Real angle = atan2(dir.y, dir.x);
			dir.x = (Real)Cos(angle + angleOffset);
			dir.y = (Real)Sin(angle + angleOffset);
		}

		// select a spot along the line between us, halfway between the min & max range.
		Real attackRange = (getAttackRange(source) + minAttackRange)/2.0f;
#ifdef ATTACK_RANGE_IS_2D
		if (target)
			attackRange += target->getGeometryInfo().getBoundingCircleRadius();
		attackRange += source->getGeometryInfo().getBoundingCircleRadius();
#else
		if (target)
			attackRange += target->getGeometryInfo().getBoundingSphereRadius();
		attackRange += source->getGeometryInfo().getBoundingSphereRadius();
#endif
		approachTargetPos.x = attackRange * dir.x + targetPos->x;
		approachTargetPos.y = attackRange * dir.y + targetPos->y;
		approachTargetPos.z = attackRange * dir.z + targetPos->z;
		///@todo - make sure we can get to the approach position.
		clipToTerrainExtent(approachTargetPos);
		return false;
	}

	const Real FUDGE = 0.001f;
	if (dist < FUDGE)
	{
		// we're close enough!
		approachTargetPos = *source->getPosition();
		return true;
	}
	else
	{
		if (isContactWeapon())
		{
			// Weapon is basically a contact weapon, like a car bomb.  The approach target logic
			// has been modified to let it approach the object, so just return the target position.	jba.
			approachTargetPos = *targetPos;
			return false;
		}

		dir.x /= dist;
		dir.y /= dist;
		dir.z /= dist;

		if (angleOffset != 0.0f)
		{
			Real angle = atan2(dir.y, dir.x);
			dir.x = (Real)Cos(angle + angleOffset);
			dir.y = (Real)Sin(angle + angleOffset);
		}

		// select a spot along the line between us, in range of our weapon
		const Real ATTACK_RANGE_APPROACH_FUDGE = 0.9f;
		Real attackRange = getAttackRange(source) * ATTACK_RANGE_APPROACH_FUDGE;
		approachTargetPos.x = attackRange * dir.x + targetPos->x;
		approachTargetPos.y = attackRange * dir.y + targetPos->y;
		approachTargetPos.z = attackRange * dir.z + targetPos->z;

		if (source->getAI() && source->getAI()->isAircraftThatAdjustsDestination()) {
			// Adjust the target so that we are not stacked atop another aircraft.
			TheAI->pathfinder()->adjustTargetDestination(source, target, pos, this, &approachTargetPos);
		}

		return false;
	}
}

//-------------------------------------------------------------------------------------------------
//Special case attack range calculate that fakes moving the object (to a garrisoned point) without
//actually moving the object. This is used to help determine if a garrisoned unit not yet
//positioned can attack someone.
//-------------------------------------------------------------------------------------------------
Bool Weapon::isSourceObjectWithGoalPositionWithinAttackRange( const Object *source, const Coord3D *goalPos, const Object *target, const Coord3D *targetPos ) const
{

	Real distSqr;
	if( target )
		distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, target, ATTACK_RANGE_CALC_TYPE );
	else if( targetPos )
		distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, targetPos, ATTACK_RANGE_CALC_TYPE );
	else
		return false;

	Real attackRangeSqr = sqr( getAttackRange( source ) );
	Real minAttackRangeSqr = sqr(m_template->getMinimumAttackRange());
#ifdef RATIONALIZE_ATTACK_RANGE
	if (distSqr < minAttackRangeSqr)
#else
	if (distSqr < minAttackRangeSqr-0.5f)
#endif
	{
		return false;
	}
	return (distSqr <= attackRangeSqr);
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isWithinAttackRange(const Object *source, const Coord3D* pos) const
{
	Real distSqr = ThePartitionManager->getDistanceSquared( source, pos, ATTACK_RANGE_CALC_TYPE );
	Real attackRangeSqr = sqr(getAttackRange(source));
	Real minAttackRangeSqr = sqr(m_template->getMinimumAttackRange());
#ifdef RATIONALIZE_ATTACK_RANGE
	if (distSqr < minAttackRangeSqr)
#else
	if (distSqr < minAttackRangeSqr-0.5f)
#endif
	{
		return false;
	}
	return (distSqr <= attackRangeSqr);
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isWithinAttackRange(const Object *source, const Object *target) const
{
	// Hypersonic @feature SAM early launch (Generals Hypersonic D-038): against an Iranian missile, a SAM-class missile
	// weapon is "in range" when the predicted intercept point is within its attack range, and never when no reachable
	// intercept point exists (receding or crossing too fast, or the intercept lies beyond its range or flight time).
	if( m_template->isLeadInterceptEngagement( target ) )
	{
		Coord3D interceptPoint;
		if( !m_template->solveLeadInterceptFromLauncher( source, target, &interceptPoint ) )
			return false;
		return isWithinAttackRange( source, &interceptPoint );
	}

	Real distSqr;
	Real attackRangeSqr = sqr(getAttackRange(source));

	if( !target->isKindOf(KINDOF_BRIDGE) )
	{
		distSqr = ThePartitionManager->getDistanceSquared( source, target, ATTACK_RANGE_CALC_TYPE );
	}
	else
	{
		// Special case - bridges have two attackable points at either end.
		TBridgeAttackInfo info;
		TheTerrainLogic->getBridgeAttackPoints(target, &info);
		distSqr = ThePartitionManager->getDistanceSquared( source, &info.attackPoint1, ATTACK_RANGE_CALC_TYPE );
		if (distSqr>attackRangeSqr)
		{
			// Try the other one.
			distSqr = ThePartitionManager->getDistanceSquared( source, &info.attackPoint2, ATTACK_RANGE_CALC_TYPE );
		}
	}

	Real minAttackRangeSqr = sqr(m_template->getMinimumAttackRange());
#ifdef RATIONALIZE_ATTACK_RANGE
	if (distSqr < minAttackRangeSqr)
#else
	if (distSqr < minAttackRangeSqr-0.5f)
#endif
	{
		// too close. can't attack.
		return false;
	}

	if( distSqr <= attackRangeSqr )
	{
		// Note - only compare contact weapons with structures.  If you do the collision check
		// against vehicles, the attacker may get close enough to the vehicle to get crushed
		// before it fires its weapon.  jba.
		if( isContactWeapon() && target->isKindOf(KINDOF_STRUCTURE))
		{
			//We're close enough to fire off ranged weapons -- but in the case of contact weapons
			//we want to do a more detailed check to see if we're actually colliding with the target.
			ObjectIterator *iter = ThePartitionManager->iteratePotentialCollisions( source->getPosition(), source->getGeometryInfo(), 0.0f );
			MemoryPoolObjectHolder hold( iter );
			for( Object *them = iter->first(); them; them = iter->next() )
			{
				if( target == them )
				{
					return true;
				}
			}
			return false;
		}
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isTooClose(const Object *source, const Object *target) const
{
	Real minAttackRange = m_template->getMinimumAttackRange();
	if (minAttackRange == 0.0f)
		return false;

	Real distSqr = ThePartitionManager->getDistanceSquared( source, target, ATTACK_RANGE_CALC_TYPE );
	if (distSqr < sqr(minAttackRange))
	{
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isTooClose( const Object *source, const Coord3D *pos ) const
{
	Real minAttackRange = m_template->getMinimumAttackRange();
	if (minAttackRange == 0.0f)
		return false;

	Real distSqr = ThePartitionManager->getDistanceSquared( source, pos, ATTACK_RANGE_CALC_TYPE );
	if (distSqr < sqr(minAttackRange))
	{
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isGoalPosWithinAttackRange(const Object *source, const Coord3D* goalPos, const Object *target, const Coord3D* targetPos)	const
{
	Real distSqr;
	// Note - undersize by 1/4 of a pathfind cell, so that the goal is not teetering on the edge
	// of firing range.  jba.
	// Note 2 - even with RATIONALIZE_ATTACK_RANGE, we still need to subtract 1/4 of a pathfind cell,
	// otherwise if it teters on the edge, attacks can fail.  jba.
	Real attackRangeSqr = sqr(getAttackRange(source)-(PATHFIND_CELL_SIZE_F*0.25f));

	if (target != nullptr)
	{
		if (target->isKindOf(KINDOF_BRIDGE))
		{
			// Special case - bridges have two attackable points at either end.
			TBridgeAttackInfo info;
			TheTerrainLogic->getBridgeAttackPoints(target, &info);
			distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, &info.attackPoint1, ATTACK_RANGE_CALC_TYPE );
			if (distSqr>attackRangeSqr)
			{
				// Try the other one.
				distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, &info.attackPoint2, ATTACK_RANGE_CALC_TYPE );
			}
		}
		else
		{
			distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, target, ATTACK_RANGE_CALC_TYPE );
		}
	}
	else
	{
		distSqr = ThePartitionManager->getGoalDistanceSquared( source, goalPos, targetPos, ATTACK_RANGE_CALC_TYPE );
	}

	// Note - oversize by 1/4 of a pathfind cell, so that the goal is not teetering on the edge
	// of firing range.  jba.
	// Note 2 - even with RATIONALIZE_ATTACK_RANGE, we still need to add 1/4 of a pathfind cell,
	// otherwise if it teters on the edge, attacks can fail.  jba.
	Real minAttackRangeSqr = sqr(m_template->getMinimumAttackRange()+(PATHFIND_CELL_SIZE_F*0.25f));
#ifdef RATIONALIZE_ATTACK_RANGE
	if (distSqr < minAttackRangeSqr)
#else
	if (distSqr < minAttackRangeSqr-0.5f)
#endif
	{
		return false;
	}
	return (distSqr <= attackRangeSqr);
}

//-------------------------------------------------------------------------------------------------
Real Weapon::getPercentReadyToFire() const
{
	switch (getStatus())
	{
		case OUT_OF_AMMO:
		case PRE_ATTACK:
			return 0.0f;

		case READY_TO_FIRE:
			return 1.0f;

		case BETWEEN_FIRING_SHOTS:
		case RELOADING_CLIP:
		{
			UnsignedInt now = TheGameLogic->getFrame();
			UnsignedInt nextShot = getPossibleNextShotFrame();
			DEBUG_ASSERTCRASH(now >= m_whenLastReloadStarted, ("now >= m_whenLastReloadStarted"));
			if (now >= nextShot)
				return 1.0f;

			DEBUG_ASSERTCRASH(nextShot >= m_whenLastReloadStarted, ("nextShot >= m_whenLastReloadStarted"));
			UnsignedInt totalTime = nextShot - m_whenLastReloadStarted;
			if (totalTime == 0)
			{
				return 1.0f;
			}

			UnsignedInt timeLeft = nextShot - now;
			DEBUG_ASSERTCRASH(timeLeft <= totalTime, ("timeLeft <= totalTime"));
			UnsignedInt timeSoFar = totalTime - timeLeft;
			if (timeSoFar >= totalTime)
			{
				return 1.0f;
			}
			else
			{
				return (Real)timeSoFar / (Real)totalTime;
			}
		}
	}
	DEBUG_CRASH(("should not get here"));
	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
Real Weapon::getAttackRange(const Object *source) const
{
	WeaponBonus bonus;
	computeBonus(source, 0, bonus);
	return m_template->getAttackRange(bonus);

	//Contained objects have longer ranges.
	//const Object *container = source->getContainedBy();
	//if( container )
	//{
	//	attackRange += container->getGeometryInfo().getBoundingCircleRadius();
	//}
	//return attackRange;
}

//-------------------------------------------------------------------------------------------------
Real Weapon::getAttackDistance(const Object *source, const Object *victimObj, const Coord3D* victimPos) const
{
	Real range = getAttackRange(source);

	if (victimObj != nullptr)
	{
	#ifdef ATTACK_RANGE_IS_2D
		range += source->getGeometryInfo().getBoundingCircleRadius();
		range += victimObj->getGeometryInfo().getBoundingCircleRadius();
	#else
		range += source->getGeometryInfo().getBoundingSphereRadius();
		range += victimObj->getGeometryInfo().getBoundingSphereRadius();
	#endif
	}

	return range;
}

//-------------------------------------------------------------------------------------------------
Real Weapon::estimateWeaponDamage(const Object *sourceObj, const Object *victimObj, const Coord3D* victimPos)
{
	if (!m_template)
		return 0.0f;

	// if the weapon is just reloading, it's ok. if it's out of ammo
	// (and won't autoreload), then we aren't gonna do any damage.
	if (getStatus() == OUT_OF_AMMO && !m_template->getAutoReloadsClip())
		return 0.0f;

	WeaponBonus bonus;
	computeBonus(sourceObj, 0, bonus);

	return m_template->estimateWeaponTemplateDamage(sourceObj, victimObj, victimPos, bonus);
}

//-------------------------------------------------------------------------------------------------
void Weapon::newProjectileFired(const Object *sourceObj, const Object *projectile, const Object *victimObj, const Coord3D *victimPos )
{
	// If I have a stream, I need to tell it about this new guy
	if( m_template->getProjectileStreamName().isEmpty() )
		return; // nope, no streak logic to do

	Object* projectileStream = TheGameLogic->findObjectByID(m_projectileStreamID);
	if( projectileStream == nullptr )
	{
		m_projectileStreamID = INVALID_ID;	// reset, since it might have been "valid" but deleted out from under us
		const ThingTemplate* pst = TheThingFactory->findTemplate(m_template->getProjectileStreamName());
		projectileStream = TheThingFactory->newObject( pst, sourceObj->getControllingPlayer()->getDefaultTeam() );
		if( projectileStream == nullptr )
			return;
		m_projectileStreamID = projectileStream->getID();
	}

	//Check for projectile stream update
	static NameKeyType key_ProjectileStreamUpdate = NAMEKEY("ProjectileStreamUpdate");
	ProjectileStreamUpdate* update = (ProjectileStreamUpdate*)projectileStream->findUpdateModule(key_ProjectileStreamUpdate);
	if( update )
	{
		update->setPosition( sourceObj->getPosition() );
		update->addProjectile( sourceObj->getID(), projectile->getID(), victimObj ? victimObj->getID() : INVALID_ID, victimPos );
		return;
	}

}

//-------------------------------------------------------------------------------------------------
void Weapon::createLaser( const Object *sourceObj, const Object *victimObj, const Coord3D *victimPos )
{
	const ThingTemplate* pst = TheThingFactory->findTemplate(m_template->getLaserName());
	if( !pst )
	{
		DEBUG_CRASH( ("Weapon::createLaser(). %s could not find template for its laser %s.",
			sourceObj->getTemplate()->getName().str(), m_template->getLaserName().str() ) );
		return;
	}
	Object* laser = TheThingFactory->newObject( pst, sourceObj->getControllingPlayer()->getDefaultTeam() );
	if( laser == nullptr )
		return;

	// Give it a good basis in reality to ensure it can draw when on screen.
	laser->setPosition(sourceObj->getPosition());

	//Check for laser update
	Drawable *draw = laser->getDrawable();
	if( draw )
	{
		static NameKeyType key_LaserUpdate = NAMEKEY( "LaserUpdate" );
		LaserUpdate *update = (LaserUpdate*)draw->findClientUpdateModule( key_LaserUpdate );
		if( update )
		{
			Coord3D pos = *victimPos;
			if( victimObj && !victimObj->isKindOf( KINDOF_PROJECTILE ) && !victimObj->isAirborneTarget() )
			{
				//Targets are positioned on the ground, so raise the beam up so we're not shooting their feet.
				//Projectiles are a different story, target their exact position.
				pos.z += 10.0f;
			}
			update->initLaser( sourceObj, victimObj, sourceObj->getPosition(), &pos, m_template->getLaserBoneName() );
		}
	}
}

//-------------------------------------------------------------------------------------------------
// return true if we auto-reloaded our clip after firing.
//DECLARE_PERF_TIMER(fireWeapon)
Bool Weapon::privateFireWeapon(
	const Object *sourceObj,
	Object *victimObj,
	const Coord3D* victimPos,
	Bool isProjectileDetonation,
	Bool ignoreRanges,
	WeaponBonusConditionFlags extraBonusFlags,
	ObjectID* projectileID,
	Bool inflictDamage
)
{
	//CRCDEBUG_LOG(("Weapon::privateFireWeapon() for %s", DescribeObject(sourceObj).str()));
	//USE_PERF_TIMER(fireWeapon)
	if (projectileID)
		*projectileID = INVALID_ID;

	if (!m_template)
		return false;

	// If we are a networked weapon, tell everyone nearby they might want to get in on this shot
	if( m_template->getRequestAssistRange()  &&  victimObj )
		processRequestAssistance( sourceObj, victimObj );

	//For weapon templates that have the leech range weapon flag set, it essentially grants
	//the weapon unlimited range for the remainder of the attack. While it's triggered here
	//it's the AIAttackState machine that actually uses and resets this value.
	//This makes the ASSUMPTION that it is IMPOSSIBLE TO FIRE A WEAPON WITHOUT BEING IN AN AIATTACKSTATE
	//
	// @todo srj -- this isn't a universally true assertion! eg, FireWeaponDie lets you do this easily.
	//
	if( m_template->isLeechRangeWeapon() )
	{
		setLeechRangeActive( TRUE );
	}

	//Special case damage type overrides requiring special handling.
	switch( m_template->getDamageType() )
	{
		case DAMAGE_DEPLOY:
		{
			const AIUpdateInterface *ai = sourceObj->getAI();
			if( ai )
			{
				const AssaultTransportAIInterface *atInterface = ai->getAssaultTransportAIInterface();
				if( atInterface )
				{
					atInterface->beginAssault( victimObj );
				}
			}
			break;
		}

		case DAMAGE_DISARM:
		{
			if (sourceObj && victimObj)
			{
				Bool found = false;
				for (BehaviorModule** bmi = victimObj->getBehaviorModules(); *bmi; ++bmi)
				{
					LandMineInterface* lmi = (*bmi)->getLandMineInterface();
					if (lmi)
					{
						VeterancyLevel v = sourceObj->getVeterancyLevel();
						FXList::doFXPos(m_template->getFireFX(v), victimObj->getPosition(), victimObj->getTransformMatrix(), 0, victimObj->getPosition(), 0);
						lmi->disarm();
						found = true;
						break;
					}
				}

				// it's a mine, but doesn't have LandMineInterface...
				if( (!found && victimObj->isKindOf( KINDOF_MINE )) || victimObj->isKindOf( KINDOF_BOOBY_TRAP ) || victimObj->isKindOf( KINDOF_DEMOTRAP ) )
				{
					VeterancyLevel v = sourceObj->getVeterancyLevel();
					FXList::doFXPos(m_template->getFireFX(v), victimObj->getPosition(), victimObj->getTransformMatrix(), 0, victimObj->getPosition(), 0);
					TheGameLogic->destroyObject( victimObj );// douse this thing before somebody gets hurt!
					found = true;
				}

				if( found )
				{
					sourceObj->getControllingPlayer()->getAcademyStats()->recordMineCleared();
				}
			}

			--m_maxShotCount;
			--m_ammoInClip;	// so we can use the delay between shots on the mine clearing weapon
			if (m_ammoInClip <= 0 && m_template->getAutoReloadsClip())
			{
				reloadAmmo(sourceObj);
				return TRUE;	// reloaded
			}
			else
			{
				return FALSE;	// did not reload
			}
		}

		case DAMAGE_HACK:
		{
			//We're using a hacker unit to hack a target. Hacking has various effects and
			//instead of inflicting damage, we are waiting for a period of time until the hack takes effect.
			//return FALSE;
		}
	}

	WeaponBonus bonus;
	computeBonus(sourceObj, extraBonusFlags, bonus);

	DEBUG_ASSERTCRASH(getStatus() != OUT_OF_AMMO, ("Hmm, firing weapon that is OUT_OF_AMMO"));
	DEBUG_ASSERTCRASH(getStatus() == READY_TO_FIRE, ("Hmm, Weapon is firing more often than should be possible"));
	DEBUG_ASSERTCRASH(m_ammoInClip > 0, ("Hmm, firing an empty weapon"));

	if (getStatus() != READY_TO_FIRE)
		return false;

	UnsignedInt now = TheGameLogic->getFrame();
	Bool reloaded = false;
	if (m_ammoInClip > 0)
	{
		// TheSuperHackers @logic-client-separation helmutbuhler 11/04/2025
		// barrelCount shouln't depend on Drawable, which belongs to client.
		Int barrelCount = sourceObj->getDrawable()->getBarrelCount(m_wslot);
		if (m_curBarrel >= barrelCount)
		{
			m_curBarrel = 0;
			m_numShotsForCurBarrel = m_template->getShotsPerBarrel();
		}

		if( !m_scatterTargetsUnused.empty() )
		{
			// If I have a set scatter pattern, I need to offset the target by a random pick from that pattern
			if( victimObj )
			{
				victimPos = victimObj->getPosition();
				victimObj = nullptr;
			}
			Coord3D targetPos = *victimPos; // need to copy, as this pointer is actually inside somebody potentially
			Int randomPick = GameLogicRandomValue( 0, m_scatterTargetsUnused.size() - 1 );
			Int targetIndex = m_scatterTargetsUnused[randomPick];

			Real scatterTargetScalar = getScatterTargetScalar();// essentially a radius, but operates only on this scatterTarget table
			Coord2D scatterOffset = m_template->getScatterTargetsVector().at( targetIndex );

			scatterOffset.x *= scatterTargetScalar;
			scatterOffset.y *= scatterTargetScalar;

			targetPos.x += scatterOffset.x;
			targetPos.y += scatterOffset.y;

			targetPos.z = TheTerrainLogic->getGroundHeight(targetPos.x, targetPos.y);

			// To erase from a vector, put the last on the one you used and pop the back.
			m_scatterTargetsUnused[randomPick] = m_scatterTargetsUnused.back();
			m_scatterTargetsUnused.pop_back();
			m_template->fireWeaponTemplate(sourceObj, m_wslot, m_curBarrel, victimObj, &targetPos, bonus, isProjectileDetonation, ignoreRanges, this, projectileID, inflictDamage );
		}
		else
		{
			m_template->fireWeaponTemplate(sourceObj, m_wslot, m_curBarrel, victimObj, victimPos, bonus, isProjectileDetonation, ignoreRanges, this, projectileID, inflictDamage );
		}

		m_lastFireFrame = now;
		--m_ammoInClip;
		--m_maxShotCount;
		--m_numShotsForCurBarrel;
		if (m_numShotsForCurBarrel <= 0)
		{
			++m_curBarrel;
			m_numShotsForCurBarrel = m_template->getShotsPerBarrel();
		}

		if (m_ammoInClip <= 0)
		{
			if (m_template->getAutoReloadsClip())
			{
				reloadAmmo(sourceObj);
				reloaded = true;
			}
			else
			{
				m_status = OUT_OF_AMMO;
				m_whenWeCanFireAgain = 0x7fffffff;
				//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d in Weapon::privateFireWeapon 1", m_whenWeCanFireAgain));
			}
		}
		else
		{
			m_status = BETWEEN_FIRING_SHOTS;
			//CRCDEBUG_LOG(("Weapon::privateFireWeapon() just set m_status to BETWEEN_FIRING_SHOTS"));
			Int delay = m_template->getDelayBetweenShots(bonus);
			m_whenLastReloadStarted = now;
			m_whenWeCanFireAgain = now + delay;
			//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d (delay is %d) in Weapon::privateFireWeapon", m_whenWeCanFireAgain, delay));

			// if we are sharing reload times
			// go through other weapons in weapon set
			// set their m_whenWeCanFireAgain to this guy's delay
			// set their m_status to this guy's status

			if ( sourceObj->isReloadTimeShared() )
			{
				for (Int wt = 0; wt<WEAPONSLOT_COUNT; wt++)
				{
					Weapon *weapon = sourceObj->getWeaponInWeaponSlot((WeaponSlotType)wt);
					if (weapon)
					{
						weapon->setPossibleNextShotFrame(m_whenWeCanFireAgain);
						//CRCDEBUG_LOG(("Just set m_whenWeCanFireAgain to %d in Weapon::privateFireWeapon 3", m_whenWeCanFireAgain));
						weapon->setStatus(BETWEEN_FIRING_SHOTS);
					}
				}
			}

		}
	}

	return reloaded;
}


//-------------------------------------------------------------------------------------------------
void Weapon::preFireWeapon( const Object *source, const Object *victim )
{
	Int delay = getPreAttackDelay( source, victim );
	if( delay > 0 )
	{
		setStatus( PRE_ATTACK );
		setPreAttackFinishedFrame( TheGameLogic->getFrame() + delay );
		if( m_template->isLeechRangeWeapon() )
		{
			setLeechRangeActive( TRUE );
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::fireWeapon(const Object *source, Object *target, ObjectID* projectileID)
{
	//CRCDEBUG_LOG(("Weapon::fireWeapon() for %s at %s", DescribeObject(source).str(), DescribeObject(target).str()));
	return privateFireWeapon( source, target, nullptr, false, false, 0, projectileID, TRUE );
}

//-------------------------------------------------------------------------------------------------
// return true if we auto-reloaded our clip after firing.
Bool Weapon::fireWeapon(const Object *source, const Coord3D* pos, ObjectID* projectileID)
{
	//CRCDEBUG_LOG(("Weapon::fireWeapon() for %s", DescribeObject(source).str()));
	return privateFireWeapon( source, nullptr, pos, false, false, 0, projectileID, TRUE );
}

//-------------------------------------------------------------------------------------------------
void Weapon::fireProjectileDetonationWeapon(const Object *source, Object *target, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage )
{
	//CRCDEBUG_LOG(("Weapon::fireProjectileDetonationWeapon() for %sat %s", DescribeObject(source).str(), DescribeObject(target).str()));
	privateFireWeapon( source, target, nullptr, true, false, extraBonusFlags, nullptr, inflictDamage );
}

//-------------------------------------------------------------------------------------------------
void Weapon::fireProjectileDetonationWeapon(const Object *source, const Coord3D* pos, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage )
{
	//CRCDEBUG_LOG(("Weapon::fireProjectileDetonationWeapon() for %s", DescribeObject(source).str()));
	privateFireWeapon( source, nullptr, pos, true, false, extraBonusFlags, nullptr, inflictDamage );
}

//-------------------------------------------------------------------------------------------------
//Currently, this function was added to allow a script to force fire a weapon,
//and immediately gain control of the weapon that was fired to give it special orders...
Object* Weapon::forceFireWeapon( const Object *source, const Coord3D *pos)
{
	//CRCDEBUG_LOG(("Weapon::forceFireWeapon() for %s", DescribeObject(source).str()));
	//Force the ammo to load instantly.
	//loadAmmoNow( source );
	//Fire the weapon at the position. Internally, it'll store the weapon projectile ID if so created.
	ObjectID projectileID = INVALID_ID;
	const Bool ignoreRange = true;
	privateFireWeapon(source, nullptr, pos, false, ignoreRange, 0, &projectileID, TRUE );
	return TheGameLogic->findObjectByID( projectileID );
}

//-------------------------------------------------------------------------------------------------
WeaponStatus Weapon::getStatus() const
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( now < m_whenPreAttackFinished )
	{
		return PRE_ATTACK;
	}
	if( now >= m_whenWeCanFireAgain )
	{
		if (m_ammoInClip > 0)
			m_status = READY_TO_FIRE;
		else
			m_status = OUT_OF_AMMO;
		//CRCDEBUG_LOG(("Weapon::getStatus() just set m_status to %d (ammo in clip is %d)", m_status, m_ammoInClip));
	}
	return m_status;
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isWithinTargetPitch(const Object *source, const Object *victim) const
{
	if (isContactWeapon() || !isPitchLimited())
		return true;

	const Coord3D* src = source->getPosition();
	const Coord3D* dst = victim->getPosition();

	const Real ACCEPTABLE_DZ = 10.0f;
	if (fabs(dst->z - src->z) < ACCEPTABLE_DZ)
		return true;	// always good enough if dz is small, regardless of pitch

	Real minPitch, maxPitch;
	source->getGeometryInfo().calcPitches(*src, victim->getGeometryInfo(), *dst, minPitch, maxPitch);

	// if there's any intersection between the the two pitch ranges, we're good to go.
	if ((minPitch >= m_template->getMinTargetPitch() && minPitch <= m_template->getMaxTargetPitch()) ||
			(maxPitch >= m_template->getMinTargetPitch() && maxPitch <= m_template->getMaxTargetPitch()) ||
			(minPitch <= m_template->getMinTargetPitch() && maxPitch >= m_template->getMaxTargetPitch()))
		return true;

	//DEBUG_LOG(("pitch %f-%f is out of range",rad2deg(minPitch),rad2deg(maxPitch),rad2deg(m_template->getMinTargetPitch()),rad2deg(m_template->getMaxTargetPitch())));
	return false;
}

//-------------------------------------------------------------------------------------------------
Real Weapon::getPrimaryDamageRadius(const Object *source) const
{
	WeaponBonus bonus;
	computeBonus(source, 0, bonus);
	return m_template->getPrimaryDamageRadius(bonus);
}

//-------------------------------------------------------------------------------------------------
Bool Weapon::isDamageWeapon() const
{
	//These damage types are special attacks that don't do damage directly, even
	//if they can indirectly. These are here to prevent the UI from allowing the
	//user to mouseover a target and think it can attack it using these types.
	switch( m_template->getDamageType() )
	{
		case DAMAGE_DEPLOY:
			//Kris @todo
			//Evaluate a better way to handle this weapon type... doesn't fit being a damage weapon.
			//May want to check if cargo can attack!
			return TRUE;

		case DAMAGE_DISARM:
			return TRUE;	// hmm, can only "damage" mines, but still...

		case DAMAGE_HACK:
			return FALSE;
	}

	//Use no bonus
	WeaponBonus whoCares;
	if( m_template->getPrimaryDamage( whoCares ) > 0.0f || m_template->getSecondaryDamage( whoCares ) > 0.0f
		|| m_template->getTertiaryDamage( whoCares ) > 0.0f )	// Hypersonic @feature (D-222)
	{
		return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Int Weapon::getPreAttackDelay( const Object *source, const Object *victim ) const
{
	// Look for a reason to return zero and have no delay.
	WeaponPrefireType type = m_template->getPrefireType();
	if( type == PREFIRE_PER_CLIP )
	{
		if( m_template->getClipSize() > 0  &&  m_ammoInClip < m_template->getClipSize() )
			return 0;// I only delay once a clip, and this is not the first shot
	}
	else if( type == PREFIRE_PER_ATTACK )
	{
		if( source->getNumConsecutiveShotsFiredAtTarget( victim ) > 0 )
			return 0;// I only delay once an attack, and I have already shot this guy
	}
	//else it is per shot, so it always applies

	WeaponBonus bonus;
	computeBonus(source, 0, bonus);
	return m_template->getPreAttackDelay( bonus );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class AssistanceRequestData
{
public:
	AssistanceRequestData();

	const Object *m_requestingObject;
	Object *m_victimObject;
	Real m_requestDistanceSquared;
};

//-------------------------------------------------------------------------------------------------
AssistanceRequestData::AssistanceRequestData()
{
	m_requestingObject = nullptr;
	m_victimObject = nullptr;
	m_requestDistanceSquared = 0.0f;
}

//-------------------------------------------------------------------------------------------------
static void makeAssistanceRequest( Object *requestOf, void *userData )
{
	AssistanceRequestData *requestData = (AssistanceRequestData *)userData;

	// Don't ask ourselves (can't believe I forgot this one)
	if( requestOf == requestData->m_requestingObject )
		return;

	// Only request of our kind of people
	if( !requestOf->getTemplate()->isEquivalentTo( requestData->m_requestingObject->getTemplate() ) )
		return;

	// Who are close enough
	Real distSq = ThePartitionManager->getDistanceSquared( requestOf, requestData->m_requestingObject, FROM_CENTER_2D );
	if( distSq > requestData->m_requestDistanceSquared )
		return;

	// and respond to requests
	static const NameKeyType key_assistUpdate = NAMEKEY("AssistedTargetingUpdate");
	AssistedTargetingUpdate *assistModule = (AssistedTargetingUpdate*)requestOf->findUpdateModule(key_assistUpdate);
	if( assistModule == nullptr )
		return;

	// and say yes
	if( !assistModule->isFreeToAssist() )
		return;

	assistModule->assistAttack( requestData->m_requestingObject, requestData->m_victimObject );
}

//-------------------------------------------------------------------------------------------------
void Weapon::processRequestAssistance( const Object *requestingObject, Object *victimObject )
{
	// Iterate through our player's objects, and tell everyone like us within our assistance range
	// who is free to do so to assist us on this shot.
	Player *ourPlayer = requestingObject->getControllingPlayer();
	if( !ourPlayer )
		return;

	AssistanceRequestData requestData;
	requestData.m_requestingObject = requestingObject;
	requestData.m_victimObject = victimObject;
	requestData.m_requestDistanceSquared = m_template->getRequestAssistRange() * m_template->getRequestAssistRange();

	ourPlayer->iterateObjects( makeAssistanceRequest, &requestData );
}

//-------------------------------------------------------------------------------------------------
/*static*/ void Weapon::calcProjectileLaunchPosition(
	const Object* launcher,
	WeaponSlotType wslot,
	Int specificBarrelToUse,
	Matrix3D& worldTransform,
	Coord3D& worldPos
)
{
	if( launcher->getContainedBy() )
	{
		// If we are in an enclosing container, our launch position is our actual position.  Yes, I am putting
		// a minor case and an oft used function, but the major case is huge and full of math.
		if(launcher->getContainedBy()->getContain()->isEnclosingContainerFor(launcher))
		{
			worldTransform = *launcher->getTransformMatrix();
			Vector3 tmp = worldTransform.Get_Translation();
			worldPos.x = tmp.X;
			worldPos.y = tmp.Y;
			worldPos.z = tmp.Z;

			// Hypersonic @bugfix enclosed garrison launch height: a rider enclosed in a structure (e.g. a Drone Operator in
			// the Dehlavieh bunker) stands at the structure's centre at ground level, so its projectile started on the
			// terrain and slid up a rising slope until a bump detonated it. Launch from the structure's roof instead.
			const Object *container = launcher->getContainedBy();
			if( container->isKindOf( KINDOF_STRUCTURE ) )
			{
				Real roof = container->getPosition()->z + container->getGeometryInfo().getMaxHeightAbovePosition();
				if( roof > worldPos.z )
				{
					worldPos.z = roof;
					worldTransform.Set_Z_Translation( roof );
				}
			}
			return;
		}
	}

	Real turretAngle = 0.0f;
	Real turretPitch = 0.0f;
	const AIUpdateInterface* ai = launcher->getAIUpdateInterface();
	WhichTurretType tur = ai ? ai->getWhichTurretForWeaponSlot(wslot, &turretAngle, &turretPitch) : TURRET_INVALID;
	//CRCDEBUG_LOG(("calcProjectileLaunchPosition(): Turret %d, slot %d, barrel %d for %s", tur, wslot, specificBarrelToUse, DescribeObject(launcher).str()));

	Matrix3D attachTransform(true);
	Coord3D turretRotPos = {0.0f, 0.0f, 0.0f};
	Coord3D turretPitchPos = {0.0f, 0.0f, 0.0f};
	const Drawable* draw = launcher->getDrawable();
	//CRCDEBUG_LOG(("Do we have a drawable? %d", (draw != nullptr)));
	if (!draw || !draw->getProjectileLaunchOffset(wslot, specificBarrelToUse, &attachTransform, tur, &turretRotPos, &turretPitchPos))
	{
		//CRCDEBUG_LOG(("ProjectileLaunchPos %d %d not found!",wslot, specificBarrelToUse));
		DEBUG_CRASH(("ProjectileLaunchPos %d %d not found!",wslot, specificBarrelToUse));
		attachTransform.Make_Identity();
		turretRotPos.zero();
		turretPitchPos.zero();
	}
	if (tur != TURRET_INVALID)
	{
		// The attach transform is the pristine front and center position of the fire point
		// We can't read from the client, so we need to reproduce the actual point that
		// takes turn and pitch into account.
		Matrix3D turnAdjustment(1);
		Matrix3D pitchAdjustment(1);

		// To rotate about a point, move that point to 0,0, rotate, then move it back.
		// Pre rotate will keep the first twist from screwing the angle of the second pitch
		pitchAdjustment.Translate( turretPitchPos.x, turretPitchPos.y, turretPitchPos.z );
		pitchAdjustment.In_Place_Pre_Rotate_Y(-turretPitch);
		pitchAdjustment.Translate( -turretPitchPos.x, -turretPitchPos.y, -turretPitchPos.z );

		turnAdjustment.Translate( turretRotPos.x, turretRotPos.y, turretRotPos.z );
		turnAdjustment.In_Place_Pre_Rotate_Z(turretAngle);
		turnAdjustment.Translate( -turretRotPos.x, -turretRotPos.y, -turretRotPos.z );

#ifdef ALLOW_TEMPORARIES
		attachTransform = turnAdjustment * pitchAdjustment * attachTransform;
#else
		Matrix3D tmp = attachTransform;
		attachTransform.mul(turnAdjustment, pitchAdjustment);
		attachTransform.postMul(tmp);
#endif
	}

//#if defined(RTS_DEBUG)
//  Real muzzleHeight = attachTransform.Get_Z_Translation();
//  DEBUG_ASSERTCRASH( muzzleHeight > 0.001f, ("YOUR TURRET HAS A VERY LOW PROJECTILE LAUNCH POSITION, BUT FOUND A VALID BONE. DID YOU PICK THE WRONG ONE? %s", launcher->getTemplate()->getName().str()));
//#endif

  launcher->convertBonePosToWorldPos(nullptr, &attachTransform, nullptr, &worldTransform);

	Vector3 tmp = worldTransform.Get_Translation();
	worldPos.x = tmp.X;
	worldPos.y = tmp.Y;
	worldPos.z = tmp.Z;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void Weapon::positionProjectileForLaunch(
	Object* projectile,
	const Object* launcher,
	WeaponSlotType wslot,
	Int specificBarrelToUse
)
{
	//CRCDEBUG_LOG(("Weapon::positionProjectileForLaunch() for %s from %s",
		//DescribeObject(projectile).str(), DescribeObject(launcher).str()));

	// if our launch vehicle is gone, destroy ourselves
	if (launcher == nullptr)
	{
		TheGameLogic->destroyObject( projectile );
		return;
	}

	Matrix3D worldTransform(true);
	Coord3D worldPos;

	Weapon::calcProjectileLaunchPosition(launcher, wslot, specificBarrelToUse, worldTransform, worldPos);

	projectile->getDrawable()->setDrawableHidden(false);
	projectile->setTransformMatrix(&worldTransform);
	projectile->setPosition(&worldPos);
	projectile->getExperienceTracker()->setExperienceSink( launcher->getID() );

	const PhysicsBehavior* launcherPhys = launcher->getPhysics();
	PhysicsBehavior* missilePhys = projectile->getPhysics();
	if (launcherPhys && missilePhys)
	{
		launcherPhys->transferVelocityTo(missilePhys);
		missilePhys->setIgnoreCollisionsWith(launcher);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void Weapon::getFiringLineOfSightOrigin(const Object* source, Coord3D& origin) const
{
	//GS 1-6-03
	// Sorry, but we have to simplify this.  If we take the actual projectile launch pos, then
	// that point can change. Take a Ranger with his gun on his shoulder.  His point is very high so
	// he clears this check and transitions to attacking.  This puts his gun at waist level and
	// now he fails this check so he transitions back.  Our height won't change.
	origin.z += source->getGeometryInfo().getMaxHeightAbovePosition();

/*
	if (m_template->getProjectileTemplate() == nullptr)
	{
		// note that we want to measure from the top of the collision
		// shape, not the bottom! (most objects have eyes a lot closer
		// to their head than their feet. if we have really odd critters
		// with eye-feet, we'll need to change this assumption.)
		origin.z += source->getGeometryInfo().getMaxHeightAbovePosition();
	}
	else
	{
		Matrix3D tmp(true);
		Coord3D launchPos = {0.0f, 0.0f, 0.0f};
		calcProjectileLaunchPosition(source, m_wslot, m_curBarrel, tmp, launchPos);
		origin.x += launchPos.x - source->getPosition()->x;
		origin.y += launchPos.y - source->getPosition()->y;
		origin.z += launchPos.z - source->getPosition()->z;
	}
*/
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool Weapon::isClearFiringLineOfSightTerrain(const Object* source, const Object* victim) const
{
	Coord3D origin;
	origin = *source->getPosition();
	//CRCDEBUG_LOG(("Weapon::isClearFiringLineOfSightTerrain(Object) for %s", DescribeObject(source).str()));
	//DUMPCOORD3D(&origin);
	getFiringLineOfSightOrigin(source, origin);
	Coord3D victimPos;
	victim->getGeometryInfo().getCenterPosition( *victim->getPosition(), victimPos );
	//CRCDEBUG_LOG(("Weapon::isClearFiringLineOfSightTerrain() - victimPos is (%g,%g,%g) (%X,%X,%X)",
	//	victimPos.x, victimPos.y, victimPos.z,
	//	AS_INT(victimPos.x),AS_INT(victimPos.y),AS_INT(victimPos.z)));
	return ThePartitionManager->isClearLineOfSightTerrain(nullptr, origin, nullptr, victimPos);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool Weapon::isClearFiringLineOfSightTerrain(const Object* source, const Coord3D& victimPos) const
{
	Coord3D origin;
	origin = *source->getPosition();
	//CRCDEBUG_LOG(("Weapon::isClearFiringLineOfSightTerrain(Coord3D) for %s", DescribeObject(source).str()));
	//DUMPCOORD3D(&origin);
	getFiringLineOfSightOrigin(source, origin);
	return ThePartitionManager->isClearLineOfSightTerrain(nullptr, origin, nullptr, victimPos);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Determine whether if source was at goalPos whether it would have clear line of sight. */
Bool Weapon::isClearGoalFiringLineOfSightTerrain(const Object* source, const Coord3D& goalPos, const Object* victim) const
{
	Coord3D origin=goalPos;
	//CRCDEBUG_LOG(("Weapon::isClearGoalFiringLineOfSightTerrain(Object) for %s", DescribeObject(source).str()));
	//DUMPCOORD3D(&origin);
	getFiringLineOfSightOrigin(source, origin);
	Coord3D victimPos;
	victim->getGeometryInfo().getCenterPosition( *victim->getPosition(), victimPos );
	return ThePartitionManager->isClearLineOfSightTerrain(nullptr, origin, nullptr, victimPos);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** Determine whether if source was at goalPos whether it would have clear line of sight. */
Bool Weapon::isClearGoalFiringLineOfSightTerrain(const Object* source, const Coord3D& goalPos, const Coord3D& victimPos) const
{
	Coord3D origin=goalPos;
	//CRCDEBUG_LOG(("Weapon::isClearGoalFiringLineOfSightTerrain(Coord3D) for %s", DescribeObject(source).str()));
	//DUMPCOORD3D(&origin);
	getFiringLineOfSightOrigin(source, origin);
	//CRCDEBUG_LOG(("Weapon::isClearFiringLineOfSightTerrain() - victimPos is (%g,%g,%g) (%X,%X,%X)",
	//	victimPos.x, victimPos.y, victimPos.z,
	//	AS_INT(victimPos.x),AS_INT(victimPos.y),AS_INT(victimPos.z)));
	return ThePartitionManager->isClearLineOfSightTerrain(nullptr, origin, nullptr, victimPos);
}

//-------------------------------------------------------------------------------------------------
//Kris: Patch 1.01 - November 10, 2003
//This function was added to transfer key weapon stats for Jarmen Kell to and from the bike for
//the sniper attack, so he can share the stats.
//-------------------------------------------------------------------------------------------------
void Weapon::transferNextShotStatsFrom( const Weapon &weapon )
{
	m_whenWeCanFireAgain = weapon.getPossibleNextShotFrame();
	m_whenLastReloadStarted = weapon.getLastReloadStartedFrame();
	m_status = weapon.getStatus();
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void Weapon::crc( Xfer *xfer )
{
#ifdef DEBUG_CRC
	AsciiString logString;
	AsciiString tmp;
	Bool doLogging = g_logObjectCRCs;
	if (doLogging)
	{
		tmp.format("CRC of weapon %s: ", m_template->getName().str());
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	AsciiString tmplName = m_template->getName();
	xfer->xferAsciiString(&tmplName);

	// slot
	xfer->xferUser( &m_wslot, sizeof( WeaponSlotType ) );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_wslot %d ", m_wslot);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// status
	/*
	xfer->xferUser( &m_status, sizeof( WeaponStatus ) );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_status %d ", m_status);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC
	*/

	// ammo
	xfer->xferUnsignedInt( &m_ammoInClip );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_ammoInClip %d ", m_ammoInClip);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// when can fire again
	xfer->xferUnsignedInt( &m_whenWeCanFireAgain );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_whenWeCanFireAgain %d ", m_whenWeCanFireAgain);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// when pre attack finished
	xfer->xferUnsignedInt( &m_whenPreAttackFinished );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_whenPreAttackFinished %d ", m_whenPreAttackFinished);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// when last reload started
	xfer->xferUnsignedInt( &m_whenLastReloadStarted );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_whenLastReloadStarted %d ", m_whenLastReloadStarted);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// last fire frame
	xfer->xferUnsignedInt( &m_lastFireFrame );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_lastFireFrame %d ", m_lastFireFrame);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// projectile stream object
	xfer->xferObjectID( &m_projectileStreamID );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("projectileStreamID %d ", m_projectileStreamID);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// laser object (defunct)
	ObjectID laserIDUnused = INVALID_ID;
	xfer->xferObjectID( &laserIDUnused );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("laserID %d ", laserIDUnused);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// max shot count
	xfer->xferInt( &m_maxShotCount );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_maxShotCount %d ", m_maxShotCount);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// current barrel
	xfer->xferInt( &m_curBarrel );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_curBarrel %d ", m_curBarrel);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// num shots for current barrel
	xfer->xferInt( &m_numShotsForCurBarrel );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_numShotsForCurBarrel %d ", m_numShotsForCurBarrel);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// scatter targets unused
	UnsignedShort scatterCount = m_scatterTargetsUnused.size();
	xfer->xferUnsignedShort( &scatterCount );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("scatterCount %d ", scatterCount);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	Int intData;

	std::vector< Int >::const_iterator it;

	for( it = m_scatterTargetsUnused.begin(); it != m_scatterTargetsUnused.end(); ++it )
	{

		intData = *it;
		xfer->xferInt( &intData );
#ifdef DEBUG_CRC
		if (doLogging)
		{
			tmp.format("%d ", intData);
			logString.concat(tmp);
		}
#endif // DEBUG_CRC

	}

	// pitch limited
	xfer->xferBool( &m_pitchLimited );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_pitchLimited %d ", m_pitchLimited);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC

	// leech weapon range active
	xfer->xferBool( &m_leechWeaponRangeActive );
#ifdef DEBUG_CRC
	if (doLogging)
	{
		tmp.format("m_leechWeaponRangeActive %d ", m_leechWeaponRangeActive);
		logString.concat(tmp);
	}
#endif // DEBUG_CRC


#ifdef DEBUG_CRC
	if (doLogging)
	{
		CRCDEBUG_LOG(("%s", logString.str()));
	}
#endif // DEBUG_CRC

}

// ------------------------------------------------------------------------------------------------
/** Xfer
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Weapon::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if (version >= 2)
	{
		AsciiString tmplName = m_template->getName();
		xfer->xferAsciiString(&tmplName);
		if (xfer->getXferMode() == XFER_LOAD)
		{
			m_template = TheWeaponStore->findWeaponTemplate(tmplName);
			if (m_template == nullptr)
				throw INI_INVALID_DATA;
		}
	}

	// slot
	xfer->xferUser( &m_wslot, sizeof( WeaponSlotType ) );

	// status
	xfer->xferUser( &m_status, sizeof( WeaponStatus ) );

	// ammo
	xfer->xferUnsignedInt( &m_ammoInClip );

	// when can fire again
	xfer->xferUnsignedInt( &m_whenWeCanFireAgain );

	// when pre attack finished
	xfer->xferUnsignedInt( &m_whenPreAttackFinished );

	// when last reload started
	xfer->xferUnsignedInt( &m_whenLastReloadStarted );

	// last fire frame
	xfer->xferUnsignedInt( &m_lastFireFrame );

	// suspendFXFrame, this affects client only
	if ( version >= 3 )
		xfer->xferUnsignedInt( &m_suspendFXFrame );
	else
		m_suspendFXFrame = 0;

	// projectile stream object
	xfer->xferObjectID( &m_projectileStreamID );

	// laser object
	ObjectID laserIDUnused = INVALID_ID;
	xfer->xferObjectID( &laserIDUnused );

	// max shot count
	xfer->xferInt( &m_maxShotCount );

	// current barrel
	xfer->xferInt( &m_curBarrel );

	// num shots for current barrel
	xfer->xferInt( &m_numShotsForCurBarrel );

	// scatter targets unused
	UnsignedShort scatterCount = m_scatterTargetsUnused.size();
	xfer->xferUnsignedShort( &scatterCount );
	Int intData;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		std::vector< Int >::const_iterator it;

		for( it = m_scatterTargetsUnused.begin(); it != m_scatterTargetsUnused.end(); ++it )
		{

			intData = *it;
			xfer->xferInt( &intData );

		}

	}
	else
	{

		// sanity, the scatter targets must be empty
		m_scatterTargetsUnused.clear();

		for( UnsignedShort i = 0; i < scatterCount; ++i )
		{

			xfer->xferInt( &intData );
			m_scatterTargetsUnused.push_back( intData );

		}

	}

	// pitch limited
	xfer->xferBool( &m_pitchLimited );

	// leech weapon range active
	xfer->xferBool( &m_leechWeaponRangeActive );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Weapon::loadPostProcess()
{
	if( m_projectileStreamID != INVALID_ID )
	{
		Object* projectileStream = TheGameLogic->findObjectByID( m_projectileStreamID );
		if( projectileStream == nullptr )
		{
			m_projectileStreamID = INVALID_ID;
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
void WeaponBonus::appendBonuses(WeaponBonus& bonus) const
{
	for (int f = 0; f < WeaponBonus::FIELD_COUNT; ++f)
	{
		bonus.m_field[f] += this->m_field[f] - 1.0f;
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponBonusSet::parseWeaponBonusSet(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	WeaponBonusSet* self = (WeaponBonusSet*)store;
	self->parseWeaponBonusSet(ini);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void WeaponBonusSet::parseWeaponBonusSetPtr(INI* ini, void* /*instance*/, void* store, const void* /*userData*/)
{
	WeaponBonusSet** selfPtr = (WeaponBonusSet**)store;
	(*selfPtr)->parseWeaponBonusSet(ini);
}

//-------------------------------------------------------------------------------------------------
void WeaponBonusSet::parseWeaponBonusSet(INI* ini)
{
	WeaponBonusConditionType wb = (WeaponBonusConditionType)INI::scanIndexList(ini->getNextToken(), TheWeaponBonusNames);
	WeaponBonus::Field wf = (WeaponBonus::Field)INI::scanIndexList(ini->getNextToken(), TheWeaponBonusFieldNames);
	m_bonus[wb].setField(wf, INI::scanPercentToReal(ini->getNextToken()));
}

//-------------------------------------------------------------------------------------------------
void WeaponBonusSet::appendBonuses(WeaponBonusConditionFlags flags, WeaponBonus& bonus) const
{
	if (flags == 0)
		return;	// my, that was easy

	for (int i = 0; i < WEAPONBONUSCONDITION_COUNT; ++i)
	{
		if ((flags & (1 << i)) == 0)
			continue;

		this->m_bonus[i].appendBonuses(bonus);
	}
}

