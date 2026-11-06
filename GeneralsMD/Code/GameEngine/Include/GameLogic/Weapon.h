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

// FILE: Weapon.h /////////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   Weapon Descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/GameCommon.h"

#include "GameLogic/Damage.h"

#include "WWMath/matrix3d.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
struct FieldParse;
class FXList;
class ObjectCreationList;
class ThingTemplate;
class Object;
class Weapon;
class WeaponTemplate;
class INI;
class ParticleSystemTemplate;
enum NameKeyType CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
const Int NO_MAX_SHOTS_LIMIT = 0x7fffffff;

//-------------------------------------------------------------------------------------------------
enum WeaponReloadType CPP_11(: Int)
{
	AUTO_RELOAD,
	NO_RELOAD,
	RETURN_TO_BASE_TO_RELOAD,

	WEAPON_RELOAD_COUNT
};

#ifdef DEFINE_WEAPONRELOAD_NAMES
static const char *const TheWeaponReloadNames[] =
{
	"YES",
	"NO",
	"RETURN_TO_BASE",
	nullptr
};
static_assert(ARRAY_SIZE(TheWeaponReloadNames) == WEAPON_RELOAD_COUNT + 1, "Incorrect array size");
#endif

//-------------------------------------------------------------------------------------------------
enum WeaponPrefireType CPP_11(: Int)
{
	PREFIRE_PER_SHOT,		///< Use the prefire delay for every shot we make
	PREFIRE_PER_ATTACK,	///< Use the prefire delay each time we attack a new target
	PREFIRE_PER_CLIP,		///< Use the prefire attack delay for each new clip

	PREFIRE_COUNT
};

#ifdef DEFINE_WEAPONPREFIRE_NAMES
static const char *const TheWeaponPrefireNames[] =
{
	"PER_SHOT",
	"PER_ATTACK",
	"PER_CLIP",
	nullptr
};
static_assert(ARRAY_SIZE(TheWeaponPrefireNames) == PREFIRE_COUNT + 1, "Incorrect array size");
#endif

//-------------------------------------------------------------------------------------------------
enum WeaponAntiMaskType CPP_11(: Int)
{
	WEAPON_ANTI_AIRBORNE_VEHICLE	= 0x01,
	WEAPON_ANTI_GROUND						= 0x02,
	WEAPON_ANTI_PROJECTILE				= 0x04,
	WEAPON_ANTI_SMALL_MISSILE			= 0x08, // All missiles are also projectiles (but not all projectiles are missiles)
	WEAPON_ANTI_MINE							= 0x10,
	WEAPON_ANTI_AIRBORNE_INFANTRY	= 0x20, // When set, anti-air weapons will not target infantry paratroopers.
	WEAPON_ANTI_BALLISTIC_MISSILE	= 0x40, // Specifically large missiles that are vulnerable to base defenses
	WEAPON_ANTI_PARACHUTE					= 0x80, // Parachutes can be targeted directly
};

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Interceptor class of a weapon (Generals Hypersonic IRAN_DESIGN 7c). What a hit does to a
// target that has a HitChanceBehavior: a SAM hit destroys it outright when the target says SAMHitIsLethal,
// a gun hit (the default) deals the weapon's normal damage. D-059: a LASER hit (the stock Point Defense Laser
// weapons) deals normal damage but rolls the target's LaserHitChance instead of its HitChance.
enum InterceptorClassType CPP_11(: Int)
{
	INTERCEPTOR_GUN,
	INTERCEPTOR_SAM,
	INTERCEPTOR_LASER,

	INTERCEPTOR_CLASS_COUNT
};

#ifdef DEFINE_INTERCEPTORCLASS_NAMES
static const char *const TheInterceptorClassNames[] =
{
	"GUN",
	"SAM",
	"LASER",
	nullptr
};
static_assert(ARRAY_SIZE(TheInterceptorClassNames) == INTERCEPTOR_CLASS_COUNT + 1, "Incorrect array size");
#endif

//-------------------------------------------------------------------------------------------------
enum WeaponAffectsMaskType CPP_11(: Int)
{
	WEAPON_AFFECTS_SELF						= 0x01,
	WEAPON_AFFECTS_ALLIES					= 0x02,
	WEAPON_AFFECTS_ENEMIES				= 0x04,	// that is, enemies that aren't the primary target
	WEAPON_AFFECTS_NEUTRALS				= 0x08,	// ditto
	WEAPON_KILLS_SELF							= 0x10,	// ensures that it's not possible to survive self damage
	WEAPON_DOESNT_AFFECT_SIMILAR	= 0x20, // Doesn't affect others that are the same as us (like other terrorists for a terrorist bomb to prevent chain reaction)
	WEAPON_DOESNT_AFFECT_AIRBORNE	= 0x40, // Radius damage doesn't affect airborne units, unless they are the primary target. (used for poison fields.)
};

//#ifdef DEFINE_WEAPONAFFECTSMASK_NAMES ; Removed protection so other classes can use these strings... not sure why this was protected in the 1st place
static const char *const TheWeaponAffectsMaskNames[] =
{
	"SELF",
	"ALLIES",
	"ENEMIES",
	"NEUTRALS",
	"SUICIDE",
	"NOT_SIMILAR",
	"NOT_AIRBORNE",
	nullptr
};
//#endif

//-------------------------------------------------------------------------------------------------
enum WeaponCollideMaskType CPP_11(: Int)
{
	// all of these apply to *nontargeted* things that might just happen to get in the way...
	// the target can always be collided with, regardless of flags
	WEAPON_COLLIDE_ALLIES									= 0x0001,
	WEAPON_COLLIDE_ENEMIES								= 0x0002,
	WEAPON_COLLIDE_STRUCTURES							= 0x0004,	// this is "all structures EXCEPT for structures belonging to the projectile's controller".
	WEAPON_COLLIDE_SHRUBBERY							= 0x0008,
	WEAPON_COLLIDE_PROJECTILE							= 0x0010,
	WEAPON_COLLIDE_WALLS									= 0x0020,
	WEAPON_COLLIDE_SMALL_MISSILES					= 0x0040, //All missiles are also projectiles!
	WEAPON_COLLIDE_BALLISTIC_MISSILES			= 0x0080, //All missiles are also projectiles!
	WEAPON_COLLIDE_CONTROLLED_STRUCTURES	= 0x0100	//this is "ONLY structures belonging to the projectile's controller".
};

#ifdef DEFINE_WEAPONCOLLIDEMASK_NAMES
static const char *const TheWeaponCollideMaskNames[] =
{
	"ALLIES",
	"ENEMIES",
	"STRUCTURES",
	"SHRUBBERY",
	"PROJECTILES",
	"WALLS",
	"SMALL_MISSILES",			//All missiles are also projectiles!
	"BALLISTIC_MISSILES", //All missiles are also projectiles!
	"CONTROLLED_STRUCTURES",
	nullptr
};
#endif

//-------------------------------------------------------------------------------------------------
//
// Note: these values are saved in save files, so you MUST NOT REMOVE OR CHANGE
// existing values!
//
enum WeaponBonusConditionType CPP_11(: Int)
{
	// The access and use of this enum has the bit shifting built in, so this is a 0,1,2,3,4,5 enum
	WEAPONBONUSCONDITION_INVALID = -1,

	WEAPONBONUSCONDITION_GARRISONED = 0,
	WEAPONBONUSCONDITION_HORDE,
	WEAPONBONUSCONDITION_CONTINUOUS_FIRE_MEAN,
	WEAPONBONUSCONDITION_CONTINUOUS_FIRE_FAST,
	WEAPONBONUSCONDITION_NATIONALISM,
	WEAPONBONUSCONDITION_PLAYER_UPGRADE,
	WEAPONBONUSCONDITION_DRONE_SPOTTING,
#ifdef ALLOW_DEMORALIZE
	WEAPONBONUSCONDITION_DEMORALIZED,
#else
	WEAPONBONUSCONDITION_DEMORALIZED_OBSOLETE,
#endif
	WEAPONBONUSCONDITION_ENTHUSIASTIC,
	WEAPONBONUSCONDITION_VETERAN,
	WEAPONBONUSCONDITION_ELITE,
	WEAPONBONUSCONDITION_HERO,
	WEAPONBONUSCONDITION_BATTLEPLAN_BOMBARDMENT,
	WEAPONBONUSCONDITION_BATTLEPLAN_HOLDTHELINE,
	WEAPONBONUSCONDITION_BATTLEPLAN_SEARCHANDDESTROY,
	WEAPONBONUSCONDITION_SUBLIMINAL,
	WEAPONBONUSCONDITION_SOLO_HUMAN_EASY,
	WEAPONBONUSCONDITION_SOLO_HUMAN_NORMAL,
	WEAPONBONUSCONDITION_SOLO_HUMAN_HARD,
	WEAPONBONUSCONDITION_SOLO_AI_EASY,
	WEAPONBONUSCONDITION_SOLO_AI_NORMAL,
	WEAPONBONUSCONDITION_SOLO_AI_HARD,
	WEAPONBONUSCONDITION_TARGET_FAERIE_FIRE,
  WEAPONBONUSCONDITION_FANATICISM, // FOR THE NEW GC INFANTRY GENERAL... adds to nationalism
	WEAPONBONUSCONDITION_FRENZY_ONE,
	WEAPONBONUSCONDITION_FRENZY_TWO,
	WEAPONBONUSCONDITION_FRENZY_THREE,

	WEAPONBONUSCONDITION_COUNT
};
#ifdef DEFINE_WEAPONBONUSCONDITION_NAMES
static const char *const TheWeaponBonusNames[] =
{
	// This is a RHS enum (weapon.ini will have WeaponBonus = IT) so it is all caps
	"GARRISONED",
	"HORDE",
	"CONTINUOUS_FIRE_MEAN",
	"CONTINUOUS_FIRE_FAST",
	"NATIONALISM",
	"PLAYER_UPGRADE",
	"DRONE_SPOTTING",
#ifdef ALLOW_DEMORALIZE
	"DEMORALIZED",
#else
	"DEMORALIZED_OBSOLETE",
#endif
	"ENTHUSIASTIC",
	"VETERAN",
	"ELITE",
	"HERO",
	"BATTLEPLAN_BOMBARDMENT",
	"BATTLEPLAN_HOLDTHELINE",
	"BATTLEPLAN_SEARCHANDDESTROY",
	"SUBLIMINAL",
	"SOLO_HUMAN_EASY",
	"SOLO_HUMAN_NORMAL",
	"SOLO_HUMAN_HARD",
	"SOLO_AI_EASY",
	"SOLO_AI_NORMAL",
	"SOLO_AI_HARD",
	"TARGET_FAERIE_FIRE",
  "FANATICISM", // FOR THE NEW GC INFANTRY GENERAL... adds to nationalism
	"FRENZY_ONE",
	"FRENZY_TWO",
	"FRENZY_THREE",

	nullptr
};
static_assert(ARRAY_SIZE(TheWeaponBonusNames) == WEAPONBONUSCONDITION_COUNT + 1, "Incorrect array size");
#endif

// For WeaponBonusConditionFlags
// part of detangling
#include "GameLogic/WeaponBonusConditionFlags.h"
#include "GameLogic/WeaponStatus.h"

//-------------------------------------------------------------------------------------------------
class WeaponBonus
{
public:

	enum Field
	{
		DAMAGE = 0,
		RADIUS,
		RANGE,
		RATE_OF_FIRE,
		PRE_ATTACK,

		FIELD_COUNT
	};

	WeaponBonus()
	{
		clear();
	}

	void clear()
	{
		for (int i = 0; i < FIELD_COUNT; ++i)
			m_field[i] = 1.0f;
	}

	Real getField(Field f) const { return m_field[f]; }
	void setField(Field f, Real v) { m_field[f] = v; }

	void appendBonuses(WeaponBonus& bonus) const;

private:
	Real m_field[FIELD_COUNT];

};

#ifdef DEFINE_WEAPONBONUSFIELD_NAMES
static const char *const TheWeaponBonusFieldNames[] =
{
	"DAMAGE",
	"RADIUS",
	"RANGE",
	"RATE_OF_FIRE",
	"PRE_ATTACK",
	nullptr
};
static_assert(ARRAY_SIZE(TheWeaponBonusFieldNames) == WeaponBonus::FIELD_COUNT + 1, "Incorrect array size");
#endif


//-------------------------------------------------------------------------------------------------
class WeaponBonusSet : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WeaponBonusSet, "WeaponBonusSet" )
private:
	WeaponBonus m_bonus[WEAPONBONUSCONDITION_COUNT];

public:
	void appendBonuses(WeaponBonusConditionFlags flags, WeaponBonus& bonus) const;

	void parseWeaponBonusSet(INI* ini);
	static void parseWeaponBonusSet(INI* ini, void *instance, void* /*store*/, const void* /*userData*/);
	static void parseWeaponBonusSetPtr(INI* ini, void *instance, void* /*store*/, const void* /*userData*/);
};
EMPTY_DTOR(WeaponBonusSet)

//-------------------------------------------------------------------------------------------------
struct HistoricWeaponDamageInfo
{
	// The time and location this weapon was fired
	UnsignedInt						frame;
	Coord3D								location;
	UnsignedInt						triggerId; ///< Unique Id assigned to any grouped damage instances

	HistoricWeaponDamageInfo(UnsignedInt f, const Coord3D& l) :
		frame(f), location(l), triggerId(0)
	{
	}
};

typedef std::list<HistoricWeaponDamageInfo> HistoricWeaponDamageList;

//-------------------------------------------------------------------------------------------------
class WeaponTemplate : public MemoryPoolObject
{
	friend class WeaponStore;

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WeaponTemplate, "WeaponTemplate" )

public:

	WeaponTemplate();
	// virtual destructor declared by memory pool

	void reset();

	void friend_setNextTemplate(WeaponTemplate *nextTemplate) { m_nextTemplate = nextTemplate; }
	WeaponTemplate *friend_clearNextTemplate() {	WeaponTemplate *ret = m_nextTemplate; m_nextTemplate = nullptr; return ret; }
	Bool isOverride() { return m_nextTemplate != nullptr; }

	/// field table for loading the values from an INI
	const FieldParse *getFieldParse() const { return TheWeaponTemplateFieldParseTable; }

	/**
		fire the weapon. return the logic-frame in which the damage will be dealt.

		If the damage will be determined at an indeterminate later date (eg, via Projectile),
		or will never be dealt (eg, target was out of range), return zero.

		You may not pass null for source or target.
	*/
	UnsignedInt fireWeaponTemplate
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
	) const;

	/**
		return the estimate damage that would be done to the given target, taking bonuses, armor, etc
		into account. (this isn't guaranteed to be 100% accurate; it is intended to be used to
		decide which weapon should be used when a unit has multiple weapons.) Note that it DOES NOT
		take weapon range into account -- it ASSUMES that the victim is within range!
	*/
	Real estimateWeaponTemplateDamage(
		const Object *sourceObj,
		const Object *victimObj,
		const Coord3D* victimPos,
		const WeaponBonus& bonus
	) const;

	Real getAttackRange(const WeaponBonus& bonus) const;
	Real getUnmodifiedAttackRange() const;
	Real getMinimumAttackRange() const;
	Int getDelayBetweenShots(const WeaponBonus& bonus) const;
	Int getClipReloadTime(const WeaponBonus& bonus) const;
	Real getPrimaryDamage(const WeaponBonus& bonus) const;
	Real getPrimaryDamageRadius(const WeaponBonus& bonus) const;
	Real getSecondaryDamage(const WeaponBonus& bonus) const;
	Real getSecondaryDamageRadius(const WeaponBonus& bonus) const;
	Real getTertiaryDamage(const WeaponBonus& bonus) const;				///< Hypersonic @feature third ring (Generals Hypersonic D-222)
	Real getTertiaryDamageRadius(const WeaponBonus& bonus) const;	///< Hypersonic @feature third ring (Generals Hypersonic D-222)
	Int getPreAttackDelay(const WeaponBonus& bonus) const;
	Bool isContactWeapon() const;

	Real getShockWaveAmount() const { return m_shockWaveAmount; }
	Real getShockWaveRadius() const { return m_shockWaveRadius; }
	Real getShockWaveTaperOff() const { return m_shockWaveTaperOff; }

	Real getRequestAssistRange() const {return m_requestAssistRange;}
	AsciiString getName() const { return m_name; }
	AsciiString getProjectileStreamName() const { return m_projectileStreamName; }
	AsciiString getLaserName() const { return m_laserName; }
	const AsciiString& getLaserBoneName() const { return m_laserBoneName; }
	NameKeyType getNameKey() const { return m_nameKey; }
	Real getWeaponSpeed() const { return m_weaponSpeed; }
	Real getMinWeaponSpeed() const { return m_minWeaponSpeed; }
	Bool isScaleWeaponSpeed() const { return m_isScaleWeaponSpeed; }
	Real getWeaponRecoilAmount() const { return m_weaponRecoil; }
	Real getMinTargetPitch() const { return m_minTargetPitch; }
	Real getMaxTargetPitch() const { return m_maxTargetPitch; }
	Real getRadiusDamageAngle() const { return m_radiusDamageAngle; }
	DamageType getDamageType() const { return m_damageType; }
	ObjectStatusTypes getDamageStatusType() const { return m_damageStatusType; }
	DeathType getDeathType() const { return m_deathType; }
	Real getContinueAttackRange() const { return m_continueAttackRange; }
	Real getInfantryInaccuracyDist() const { return m_infantryInaccuracyDist; }
	Real getAimDelta() const { return m_aimDelta; }
	Real getScatterRadius() const { return m_scatterRadius; }
	Real getScatterTargetScalar() const { return m_scatterTargetScalar; }
	const ThingTemplate* getProjectileTemplate() const { return m_projectileTmpl; }
	Bool getDamageDealtAtSelfPosition() const { return m_damageDealtAtSelfPosition; }
	Int getAffectsMask() const { return m_affectsMask; }
	Int getProjectileCollideMask() const { return m_collideMask; }
	WeaponReloadType getReloadType() const { return m_reloadType; }
	WeaponPrefireType getPrefireType() const { return m_prefireType; }
	Bool getAutoReloadsClip() const { return m_reloadType == AUTO_RELOAD; }
	Int getClipSize() const { return m_clipSize; }
	Int getContinuousFireOneShotsNeeded() const { return m_continuousFireOneShotsNeeded; }
	Int getContinuousFireTwoShotsNeeded() const { return m_continuousFireTwoShotsNeeded; }
	UnsignedInt getContinuousFireCoastFrames() const { return m_continuousFireCoastFrames; }
 	UnsignedInt getAutoReloadWhenIdleFrames() const { return m_autoReloadWhenIdleFrames; }
	UnsignedInt getSuspendFXDelay() const { return m_suspendFXDelay; }

	const FXList* getFireFX(VeterancyLevel v) const { return m_fireFXs[v]; }
	const FXList* getProjectileDetonateFX(VeterancyLevel v) const { return m_projectileDetonateFXs[v]; }
	const ObjectCreationList* getFireOCL(VeterancyLevel v) const { return m_fireOCLs[v]; }
	const ObjectCreationList* getProjectileDetonationOCL(VeterancyLevel v) const { return m_projectileDetonationOCLs[v]; }
	const ParticleSystemTemplate* getProjectileExhaust(VeterancyLevel v) const { return m_projectileExhausts[v]; }

	const AudioEventRTS& getFireSound() const { return m_fireSound; }
	UnsignedInt getFireSoundLoopTime() const { return m_fireSoundLoopTime; }
	const std::vector<Coord2D>& getScatterTargetsVector() const { return m_scatterTargets; }
	const WeaponBonusSet* getExtraBonus() const { return m_extraBonus; }
	Int getShotsPerBarrel() const { return m_shotsPerBarrel; }
	Int getAntiMask() const { return m_antiMask; }
	Bool isLeechRangeWeapon() const { return m_leechRangeWeapon; }
	Bool isCapableOfFollowingWaypoint() const { return m_capableOfFollowingWaypoint; }
	Bool isShowsAmmoPips() const { return m_isShowsAmmoPips; }
	Bool isPlayFXWhenStealthed() const { return m_playFXWhenStealthed; }
	Bool getDieOnDetonate() const { return m_dieOnDetonate; }
	InterceptorClassType getInterceptorClass() const { return m_interceptorClass; }
	Int getMissileHitsToKill() const { return m_missileHitsToKill; }
	Real getDroneHitChanceScale() const { return m_droneHitChanceScale; }
	Real getMissileHitChanceScale() const { return m_missileHitChanceScale; }

	// Hypersonic @feature SAM lead interception (Generals Hypersonic D-038). A SAM-class weapon whose projectile is a
	// target-following MissileAIUpdate missile engages SAMHitIsLethal targets at a predicted intercept point.
	Bool isLeadInterceptWeapon() const { return m_leadProfile.m_valid; }
	static Bool isLeadInterceptTarget(const Object* victim);	///< an airborne/projectile target whose HitChanceBehavior says SAMHitIsLethal
	Bool isLeadInterceptEngagement(const Object* victim) const { return isLeadInterceptWeapon() && isLeadInterceptTarget(victim); }
	Real getLeadInterceptFuseRadius() const { return m_leadProfile.m_fuseRadius; }
	UnsignedInt getLeadInterceptMaxFrames() const { return m_leadProfile.m_maxFrames; }
	/// Where an interceptor at 'from', flying at 'speed' (dist/frame, thrust after 'coastFrames'), can meet 'victim'
	/// within 'maxFrames' frames. False if no intercept point exists.
	Bool solveLeadIntercept(const Coord3D* from, Real speed, UnsignedInt coastFrames, UnsignedInt maxFrames,
		const Object* victim, Coord3D* interceptPoint) const;
	/// Solve from a launcher that has not fired yet (the interceptor's launch speed and ignition delay).
	Bool solveLeadInterceptFromLauncher(const Object* launcher, const Object* victim, Coord3D* interceptPoint) const;
	/// The target's actual velocity in dist/frame (a braking projectile moves straight at its goal, not by its physics).
	static void getLeadTargetVelocity(const Object* victim, Coord3D* vel);

	// Hypersonic @feature cluster warhead (Generals Hypersonic D-048): ClusterCount > 0 with a ClusterWeapon makes the
	// weapon's damage a spread of bomblets instead of one blast (see dealDamageInternal).
	Bool isClusterWeapon() const { return m_clusterCount > 0 && m_clusterWeapon != nullptr; }
	// Hypersonic @feature one-by-one release (Generals Hypersonic D-162): ClusterReleaseInterval > 0 on a cluster weapon whose
	// ClusterWeapon has a ProjectileObject: the ballistic missile carrying it does not burst; past its apex it releases the
	// ClusterCount warheads one at a time (MissileAIUpdate::updateClusterRelease), the last one ClusterReleaseLead before
	// it lands itself, then detonates on the aim point with ClusterCenterWeapon (the missile is the centre warhead).
	Bool isClusterReleaseWeapon() const { return isClusterWeapon() && m_clusterReleaseInterval > 0 && m_clusterWeapon->getProjectileTemplate() != nullptr; }
	UnsignedInt getClusterReleaseInterval() const { return m_clusterReleaseInterval; }
	UnsignedInt getClusterReleaseLead() const { return m_clusterReleaseLead; }
	UnsignedInt getClusterReleaseStart() const { return m_clusterReleaseStart; }	///< (D-162) > 0: release early, this long into the arc
	Real getClusterReleaseCarrierSpeed() const { return m_clusterReleaseCarrierSpeed; }	///< (D-162) carrier's top speed while releasing
	Real getClusterReleaseFinalSpeed() const { return m_clusterReleaseFinalSpeed; }	///< (D-215) > 0: carrier's top speed after the last release
	Bool getClusterReleaseCarrierVanishes() const { return m_clusterReleaseCarrierVanishes; }	///< (D-245) the carrier is gone after its last release: no centre warhead
	const FXList* getClusterReleaseVanishFX() const { return m_clusterReleaseVanishFX; }	///< (D-245) played where the carrier vanished
	UnsignedInt getClusterReleaseDropTime() const { return m_clusterReleaseDropTime; }	///< (D-261) > 0: each released warhead leaves level and falls this many frames
	Real getClusterReleaseConeSpeed() const { return m_clusterReleaseConeSpeed; }	///< (D-267) > 0: all warheads leave together and fly a cone at about this mean speed (dist/frame)
	Real getClusterReleaseConeInherit() const { return m_clusterReleaseConeInherit; }	///< (D-267) how much of the carrier's velocity their start keeps (rest: straight at the spot)
	Int getClusterCount() const { return m_clusterCount; }
	Real getClusterRadius() const { return m_clusterRadius; }
	const WeaponTemplate* getClusterWeapon() const { return m_clusterWeapon; }
	const WeaponTemplate* getClusterCenterWeapon() const { return m_clusterCenterWeapon; }
	/// (D-162) launch one ClusterWeapon warhead from 'carrier' to 'point', landing in 'frames' frames, leaving with the
	/// carrier's vertical speed 'startVZ' (dist/frame); credited to 'launcherID' when it still exists.
	void releaseClusterWarhead(const Object* carrier, ObjectID launcherID, const Coord3D* point, UnsignedInt frames, Real startVZ, const Coord3D* startVel = nullptr) const;

	// Hypersonic @feature scaled explosions (Generals Hypersonic D-060): with ScaleDetonationFX = Yes, the scale at which
	// this weapon's detonation plays 'fx' (an FXList with a ReferenceDamage): sqrt(blast damage / ReferenceDamage), where
	// the blast damage is PrimaryDamage with the shot's bonus (a cluster weapon: ClusterCount x its bomblet's damage).
	// 1 when the weapon does not opt in or the list has no ReferenceDamage.
	Real getDetonationFXScale(const FXList* fx, const WeaponBonus& bonus) const;
	// Hypersonic @feature (Generals Hypersonic D-148) FXScale = <factor>: this weapon's FireFX and detonation FX play at that
	// size (times the damage scale above), e.g. a cook-off's half-size explosion. Default 1.
	Real getFXScale() const { return m_fxScale; }
	/// How far out a launcher with this weapon should look for missiles to intercept (range + reach of the fastest target).
	Real getLeadInterceptSearchRange(Real attackRange) const;

	// Hypersonic @feature (Generals Hypersonic D-042) The ground point a strike by this weapon at 'pos' lands on, by
	// the rules fireWeaponTemplate uses for a position shot: a designated target (ZeroScatterMarker) is hit with no
	// scatter, else a random point in the scatter disc (ScatterRadiusUpgrade, ScatterRadiusUniform). Synced random.
	void computeStrikeAimPoint(const Object* sourceObj, const Coord3D* pos, Coord3D* aim) const;
	// Hypersonic @feature (D-110, public for the Iran AI strike targeting) the scatter radius with the source player's upgrades
	Real getScatterRadiusForSource(const Object* sourceObj) const;

	Bool shouldProjectileCollideWith(
		const Object* projectileLauncher,
		const Object* projectile,
		const Object* thingWeCollidedWith,
		ObjectID intendedVictimID	// could be INVALID_ID for a position-shot
	) const;

	void postProcessLoad();

protected:

	// actually deal out the damage.
	void dealDamageInternal(ObjectID sourceID, ObjectID victimID, const Coord3D *pos, const WeaponBonus& bonus, Bool isProjectileDetonation) const;
	void trimOldHistoricDamage() const;
	void trimTriggeredHistoricDamage() const;
	void processHistoricDamage(const Object* source, const Coord3D* pos) const;

	// Hypersonic @feature strike accuracy: upgraded scatter radius and designated targets
	const Object* findZeroScatterTarget(const Object* sourceObj, const Object* victimObj, const Coord3D* victimPos) const;

private:

	// Hypersonic @feature a player upgrade that sets the scatter radius (the smallest completed one wins)
	struct ScatterRadiusUpgrade
	{
		AsciiString m_upgradeName;
		Real m_scatterRadius;
	};
	typedef std::vector<ScatterRadiusUpgrade> ScatterRadiusUpgradeVec;

	// NOTE: m_nextTemplate will be cleaned up if it is NON-nullptr.
	WeaponTemplate *m_nextTemplate;

	static void parseWeaponBonusSet( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ );
	static void parseScatterTarget( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ );
	static void parseShotDelay( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ );
	static void parseScatterRadiusUpgrade( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ );

	// Hypersonic @feature flight profile of this weapon's interceptor missile (D-038), derived from the projectile's
	// MissileAIUpdate and SET_NORMAL locomotor in postProcessLoad. Static data; all speeds in dist/frame.
	struct LeadInterceptProfile
	{
		Bool m_valid;							///< SAM class, projectile follows its target with a MissileAIUpdate
		Real m_launchSpeed;				///< InitialVelocity (or the weapon speed with UseWeaponSpeed)
		Real m_maxSpeed;					///< locomotor Speed
		Real m_speedGain;					///< fraction of the missing speed gained per frame (Acceleration / Speed, THRUST locomotor)
		UnsignedInt m_ignitionFrames;	///< IgnitionDelay: no thrust before this
		UnsignedInt m_maxFrames;	///< powered flight time (FuelLifetime), capped
		Real m_fuseRadius;				///< DistanceToTargetForLock: proximity fuse radius
	};
	void computeLeadInterceptProfile();

	static const FieldParse TheWeaponTemplateFieldParseTable[];		///< the parse table for INI definition

	AsciiString m_name;											///< name for this weapon
	NameKeyType m_nameKey;									///< unique name key for this weapon template
	AsciiString m_projectileStreamName;			///< Name of object that tracks are stream, if we have one
	AsciiString m_laserName;								///< Name of the laser object that persists.
	AsciiString m_laserBoneName;						///< Where to put the laser object
	Real m_primaryDamage;										///< primary damage amount
	Real m_primaryDamageRadius;							///< primary damage radius range
	Real m_secondaryDamage;									///< secondary damage amount
	Real m_secondaryDamageRadius;						///< secondary damage radius range
	Real m_tertiaryDamage;									///< Hypersonic @feature (D-222) outer ring damage, beyond the secondary radius (0 = off)
	Real m_tertiaryDamageRadius;						///< Hypersonic @feature (D-222) outer ring radius (0 = off)
	Real m_shockWaveAmount;									///( How much shockwave generated
	Real m_shockWaveRadius;									///( How far shockwave effect affects objects
	Real m_shockWaveTaperOff;								///( How much shockwave is left at the tip of the shockwave radius
	Real m_attackRange;											///< max distance the weapon can deal damage
	Real m_minimumAttackRange;							///< Min distance the weapon should be fired from
	Real m_requestAssistRange;							///< My object will look this far around to get people to join in the attack.
	Real m_aimDelta;												///< when aiming, consider yourself "aimed" if you are within +/- this much of an angle
	Real m_scatterRadius;										///< Radius of area actual fire point will be in, default is zero for no deviation
	Real m_scatterTargetScalar;							///< Radius of area covered by the coordinates in the scatterTarget table
	std::vector<Coord2D> m_scatterTargets;	///< instead of pure randomness, this is the list of places I will randomly choose from to attack
	DamageType m_damageType;								///< damage type enum
	DeathType m_deathType;									///< death type enum
	Real m_weaponSpeed;											///< speed of damage travel, in dist/frame
	Real m_minWeaponSpeed;									///< speed of damage travel, in dist/frame
	Bool m_isScaleWeaponSpeed;							///< Scale from min to normal based on range (for lobbers)
	Real m_weaponRecoil;										///< amt of recoil caused to firer, in rads
	Real m_minTargetPitch;									///< min pitch from source->victim allowable in order to target
	Real m_maxTargetPitch;									///< max pitch from source->victim allowable in order to target
	Real m_radiusDamageAngle;								///< Damage is directional, so max defelection of straight at target (cone) you do damage
	AsciiString m_projectileName;																			///< if projectile, object name to "fire"
	const ThingTemplate* m_projectileTmpl;														///< direct access to projectile object type to "fire"
	AsciiString m_fireOCLNames[LEVEL_COUNT];														///< Name of OCL to create at firing
	AsciiString m_projectileDetonationOCLNames[LEVEL_COUNT];						///< Name of OCL to create at firing at missile's end
	const ParticleSystemTemplate* m_projectileExhausts[LEVEL_COUNT];			///< Templates of particle systems for projectile exhaust
	const ObjectCreationList* m_fireOCLs[LEVEL_COUNT];									///< Post-loaded lookup of name string for ease and speed
	const ObjectCreationList* m_projectileDetonationOCLs[LEVEL_COUNT];	///< Post-loaded lookup of name string for ease and speed (and subsystem init order)
	const FXList* m_fireFXs[LEVEL_COUNT];															///< weapon is fired fx
	const FXList* m_projectileDetonateFXs[LEVEL_COUNT];								///< if we have a projectile, fx for projectile blowing up
	AudioEventRTS m_fireSound;							///< weapon is fired sound
	UnsignedInt m_fireSoundLoopTime;				///< if nonzero, num frames for looping of fire sound
	WeaponBonusSet* m_extraBonus;						///< optional extra per-weapon bonus
	Int m_clipSize;													///< number of 'shots' in a clip
	Int m_clipReloadTime;										///< when 'clip' is empty, how long it takes to reload (frames)
	Int m_minDelayBetweenShots;							///< min time allowed between firing single shots (frames)
	Int m_maxDelayBetweenShots;							///< max time allowed between firing single shots (frames)
	Int m_continuousFireOneShotsNeeded;			///< How many consecutive shots will give my owner the ContinuousFire Property
	Int m_continuousFireTwoShotsNeeded;			///< How many consecutive shots will give my owner the ContinuousFireTwo Property
	UnsignedInt m_continuousFireCoastFrames;///< How long after we should have shot should we start to wind down from Continuous fire mode
 	UnsignedInt m_autoReloadWhenIdleFrames;	///< How long we have to wait after our last shot to force a reload
	Int m_shotsPerBarrel;										///< If non zero, don't cycle through your launch points every shot, mod the shot by this to get even chucks of firing
	Int m_antiMask;													///< what we can target
	Int m_affectsMask;											///< what we can affect
	Int m_collideMask;											///< what we can collide with (projectiles only)
	Bool m_damageDealtAtSelfPosition;				///< if T, weapon damage is done at source's position, not victim's pos. (useful for suicide weapons.)
	WeaponReloadType m_reloadType;					///< does the weapon auto-reload a clip when empty?
	WeaponPrefireType m_prefireType;				///< The way this weapon handles its prefire delay
	UnsignedInt m_historicBonusTime;				///< if 'count' instances of this weapon do damage within 'time' and 'radius' from each other, fire the historic bonus weapon
	Real m_historicBonusRadius;							///< see above
	Int m_historicBonusCount;								///< see above
	const WeaponTemplate* m_historicBonusWeapon;	///< see above
	Bool m_leechRangeWeapon;								///< once the weapon has fired once at the proper range, the weapon gains unlimited range for the remainder of the attack cycle
	Bool m_capableOfFollowingWaypoint;			///< determines if the weapon is capable of following a waypoint path.
	Bool m_isShowsAmmoPips;									///< shows ammo pips
	Bool m_allowAttackGarrisonedBldgs;			///< allow attacks on garrisoned bldgs, even if estimated damage would be zero
	Bool m_playFXWhenStealthed;					///< Ignores rule about not playing FX when stealthed
	Int m_preAttackDelay;										///< doesn't attack until preAttack delay is finish (triggering detonation, aiming a snipe shot, etc.)
	Real m_continueAttackRange;							///< if nonzero: when you destroy something, look for a similar obj controlled by same player to attack (used mainly for mine-clearing)
	Real m_infantryInaccuracyDist;					///< When this weapon is used against infantry, it can randomly miss by as much as this distance.
	ObjectStatusTypes m_damageStatusType;		///< If our damage is Status damage, the status we apply
	UnsignedInt m_suspendFXDelay;						///< The fx can be suspended for any delay, in frames, then they will execute as normal
	Bool m_dieOnDetonate;

	// Hypersonic @feature Generals Hypersonic combat rules (IRAN_DESIGN 7b, 7c, 10b); defaults keep stock behaviour.
	InterceptorClassType m_interceptorClass;					///< what a hit does to a HitChanceBehavior target (GUN: normal damage, SAM: can be lethal)
	Int m_missileHitsToKill;													///< Hypersonic (D-195) > 0: this many hits destroy a SAMHitIsLethal target (an Iran missile)
	Real m_droneHitChanceScale;												///< Hypersonic (D-195) factor on the hit chance of a HitChanceBehavior target that is not a missile (an Iran drone)
	Real m_missileHitChanceScale;											///< Hypersonic (D-195) factor on the hit chance of a SAMHitIsLethal target (an Iran missile)
	Bool m_scatterRadiusUniform;											///< scatter points are uniform over the disc (sqrt of the random radius fraction)
	ScatterRadiusUpgradeVec m_scatterRadiusUpgrades;	///< player upgrades that replace m_scatterRadius
	AsciiString m_zeroScatterMarkerName;							///< a target carrying this marker (StickyBombUpdate) for the firer or an ally is hit with no scatter
	const ThingTemplate* m_zeroScatterMarkerTmpl;			///< post-loaded m_zeroScatterMarkerName
	LeadInterceptProfile m_leadProfile;								///< SAM lead interception (D-038); not valid for other weapons
	Int m_clusterCount;																///< cluster warhead (D-048): bomblets its detonation spreads (0: a normal blast)
	Real m_clusterRadius;															///< ... evenly over this radius around the detonation point
	UnsignedInt m_clusterDelay;												///< ... landing within this many frames after it
	AsciiString m_clusterWeaponName;									///< ... each dealing this weapon's damage and ProjectileDetonationFX
	const WeaponTemplate* m_clusterWeapon;						///< post-loaded m_clusterWeaponName
	Bool m_ignoresCountermeasures;									///< (D-055) its missiles are never diverted by countermeasure flares
	Bool m_scaleDetonationFX;												///< (D-060) play ProjectileDetonationFX scaled by this blast's damage
	Bool m_fxAtTarget;															///< (D-148) FireFX / detonation FX at the target point (a launcher cook-off fired by its facility)
	Real m_fxScale;																	///< (D-148) fixed size factor of its FireFX and detonation FX (1 = as authored)
	AsciiString m_clusterCenterWeaponName;						///< (D-158) split warhead: the first (centre) warhead uses this weapon, on the aim point
	const WeaponTemplate* m_clusterCenterWeapon;			///< post-loaded m_clusterCenterWeaponName
	UnsignedInt m_clusterReleaseInterval;					///< (D-162) frames between one-by-one releases (0: burst at once)
	Real m_clusterReleaseCarrierSpeed;						///< (D-162) fraction of its top speed the carrier slows to past the apex (1 = none)
	Real m_clusterReleaseFinalSpeed;							///< (D-215) fraction of its top speed (may be > 1) the carrier reaches within 1 s after its last release (0 = none)
	UnsignedInt m_clusterReleaseStart;						///< (D-162) > 0: the first release this many frames after the carrier starts its arc (after climb and pitch-over); 0: past the apex, ClusterReleaseLead before landing
	UnsignedInt m_clusterReleaseLead;							///< (D-162) frames from the last release to the carrier's own impact
	Bool m_clusterReleaseCarrierVanishes;					///< (D-245) after the last release the carrier is removed (no centre blast, no damage)
	const FXList* m_clusterReleaseVanishFX;				///< (D-245) FX at the carrier when it vanishes (nullptr: none)
	UnsignedInt m_clusterReleaseDropTime;					///< (D-261) > 0: frames each released warhead falls, leaving level (0: timed to the carrier's landing, D-162)
	Real m_clusterReleaseConeSpeed;								///< (D-267) > 0 (with DropTime): all warheads leave in one frame and fly a cone at about this mean speed (dist/frame)
	Real m_clusterReleaseConeInherit;							///< (D-267) how much of the carrier's velocity their start keeps
	Real m_hitChance;																///< (D-147) chance that a hit affects an airborne target without its own HitChanceBehavior (1 = always)

	mutable HistoricWeaponDamageList m_historicDamage;
	mutable UnsignedInt m_historicDamageTriggerId;
};

// ---------------------------------------------------------
class Weapon : public MemoryPoolObject,
							 public Snapshot
{
	friend class WeaponStore;

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( Weapon, "Weapon" )

private:

	// only our friend (WeaponStore) is allowed to use our ctor
	// Weapon(); -- nope, no default ctor anymore! (srj)
	Weapon(const Weapon& that);
	Weapon& operator=(const Weapon& that);

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

public:

//~Weapon();

	// return true if we auto-reloaded our clip after firing.
	Bool fireWeapon(const Object *source, Object *target, ObjectID* projectileID = nullptr);

	// return true if we auto-reloaded our clip after firing.
	Bool fireWeapon(const Object *source, const Coord3D* pos, ObjectID* projectileID = nullptr);

	void fireProjectileDetonationWeapon(const Object *source, Object *target, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage = TRUE );

	void fireProjectileDetonationWeapon(const Object *source, const Coord3D* pos, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage = TRUE );

	void preFireWeapon( const Object *source, const Object *victim );

	//Currently, this function was added to allow a script to force fire a weapon,
	//and immediately gain control of the weapon that was fired to give it special orders...
	Object* forceFireWeapon( const Object *source, const Coord3D *pos );

	/**
		return the estimate damage that would be done to the given target, taking bonuses, armor, etc
		into account. (this isn't guaranteed to be 100% accurate; it is intended to be used to
		decide which weapon should be used when a unit has multiple weapons.) Note that it DOES NOT
		take weapon range into account -- it ASSUMES that the victim is within range!
	*/
	Real estimateWeaponDamage(const Object *source, const Object *target)
	{
		return estimateWeaponDamage(source, target, nullptr);
	}

	Real estimateWeaponDamage(const Object *source, const Coord3D* pos)
	{
		return estimateWeaponDamage(source, nullptr, pos);
	}

	void onWeaponBonusChange(const Object *source);///< Our Object's weapon bonus changed, so we need to update to reflect that instead of waiting

	/** return true if the target is within attack range, false otherwise.
	*/
	Bool isWithinAttackRange(const Object *source, const Object *target) const;
	Bool isWithinAttackRange(const Object *source, const Coord3D* pos) const;

	Bool isTooClose(const Object *source, const Object *target) const;
	Bool isTooClose(const Object *source, const Coord3D *pos) const;

	Bool isGoalPosWithinAttackRange(const Object *source, const Coord3D* goalPos, const Object *target, const Coord3D* targetPos) const;

	//Used only by garrison contains that move objects around before doing the range check.
	//If target object is specified, we'll use his position, but if it's nullptr we will use the
	//target position passed in.
	//NOTE: This is not a user friendly function -- use with caution if at all! -- Kris
	Bool isSourceObjectWithGoalPositionWithinAttackRange(const Object *source, const Coord3D *goalPos, const Object *target, const Coord3D *targetPos) const;

	/**
		Compute a target position from which 'source' can target 'target'. Note that this doesn't
		guarantee that source can actually move to the location, it just guarantees that the
		target position is plausibly within attack range. returns TRUE if the source's current pos
		is already close enough (in which case that current pos is stuffed into the return arg).
	*/
	Bool computeApproachTarget(const Object *source, const Object *target, const Coord3D *pos, Real angleOffset, Coord3D& approachTargetPos) const;

	/**
		note that "load" makes the ammo instantly available, while "reload" keeps the weapon in the RELOADING
		state until the clip-reload-time is up. Normally you should use "load" only for newly-created
		units, who presumably come into existence with fully loaded weapons.
	*/
	void loadAmmoNow(const Object *source);
	void reloadAmmo(const Object *source);

	WeaponStatus getStatus() const;

	/// Some AI needs to know when the soonest I can possibly next shoot.  Bonuses have been figured into this number already.
	UnsignedInt getPossibleNextShotFrame() const { return m_whenWeCanFireAgain; }
	UnsignedInt getPreAttackFinishedFrame() const { return m_whenPreAttackFinished; }
	UnsignedInt getLastReloadStartedFrame() const { return m_whenLastReloadStarted; }
	Real getPercentReadyToFire() const;

	// do not ever use this unless you are weaponset.cpp
	void setPossibleNextShotFrame( UnsignedInt frameNum ) { m_whenWeCanFireAgain = frameNum; }
	void setPreAttackFinishedFrame( UnsignedInt frameNum ) { m_whenPreAttackFinished = frameNum; }
	void setLastReloadStartedFrame( UnsignedInt frameNum ) { m_whenLastReloadStarted = frameNum; }

	//Transfer the reload times and status from the passed in weapon.
	void transferNextShotStatsFrom( const Weapon &weapon );

	// we must pass the source object for these (and for ANY FUTURE ADDITIONS)
	// so that we can take the source's weapon bonuses, if any, into account.
	// Also note: you should RARELY need to call getAttackRange. If what you want is to
	// determine if you are within attack range, please call isWithinAttackRange instead.
	Real getAttackRange(const Object *source) const;

	// Returns the max distance between the centerpoints of source & victim	for victim to be in range.
	Real getAttackDistance(const Object *source, const Object *victim, const Coord3D* victimPos) const;

	void newProjectileFired( const Object *sourceObj, const Object *projectile, const Object *victimObj, const Coord3D *victimPos );///<I just made this projectile and may need to keep track of it

	Bool isLaser() const { return m_template->getLaserName().isNotEmpty(); }
	void createLaser( const Object *sourceObj, const Object *victimObj, const Coord3D *victimPos );

	const WeaponTemplate* getTemplate() const { return m_template; }
	WeaponSlotType getWeaponSlot() const { return m_wslot; }
	AsciiString getName() const { return m_template->getName(); }
	UnsignedInt getLastShotFrame() const { return m_lastFireFrame; }						///< frame a shot was last fired on
	// If we are "reloading", then m_ammoInClip is a lie.  It will say full.
	UnsignedInt getRemainingAmmo() const { return (getStatus() == RELOADING_CLIP) ? 0 : m_ammoInClip; }
	WeaponReloadType getReloadType() const { return m_template->getReloadType(); }
	Bool getAutoReloadsClip() const { return m_template->getAutoReloadsClip(); }
	Real getAimDelta() const { return m_template->getAimDelta(); }
	Real getScatterRadius() const { return m_template->getScatterRadius(); }
	Real getScatterTargetScalar() const { return m_template->getScatterTargetScalar(); }
	Int getAntiMask() const { return m_template->getAntiMask(); }
	Bool isCapableOfFollowingWaypoint() const { return m_template->isCapableOfFollowingWaypoint(); }
	Int getContinuousFireOneShotsNeeded() const { return m_template->getContinuousFireOneShotsNeeded(); }
	Int getContinuousFireTwoShotsNeeded() const { return m_template->getContinuousFireTwoShotsNeeded(); }
	UnsignedInt getContinuousFireCoastFrames() const { return m_template->getContinuousFireCoastFrames(); }
 	UnsignedInt getAutoReloadWhenIdleFrames() const { return m_template->getAutoReloadWhenIdleFrames(); }
	const AudioEventRTS& getFireSound() const { return m_template->getFireSound(); }
	UnsignedInt getFireSoundLoopTime() const { return m_template->getFireSoundLoopTime(); }
	DamageType getDamageType() const { return m_template->getDamageType(); }
	DeathType getDeathType() const { return m_template->getDeathType(); }
	Real getContinueAttackRange() const { return m_template->getContinueAttackRange(); }
	Bool isShowsAmmoPips() const { return m_template->isShowsAmmoPips(); }
	Int getClipSize() const { return m_template->getClipSize(); }
	// Contact weapons (like car bombs) need to basically collide with their target.
	Bool isContactWeapon() const { return m_template->isContactWeapon(); }

	Int getClipReloadTime(const Object *source) const;

	Real getPrimaryDamageRadius(const Object *source) const;

	Int getPreAttackDelay( const Object *source, const Object *victim ) const;

	Bool isDamageWeapon() const;

	Bool isPitchLimited() const { return m_pitchLimited; }
	Bool isWithinTargetPitch(const Object *source, const Object *victim) const;

	//Leech range functionality simply means this weapon has unlimited range temporarily. How it works is if the
	//weapon template has the LeechRangeWeapon set, it means that once the unit has closed to standard weapon range
	//it fires the weapon, and will be able to hit the target even if it moves out of range! The unit will simply
	//stand there. This functionality is used by hack attacks.
	void setLeechRangeActive( Bool active ) { m_leechWeaponRangeActive = active; }
	Bool hasLeechRange() const { return m_leechWeaponRangeActive; }

	void setMaxShotCount(Int maxShots) { m_maxShotCount = maxShots; }
	Int getMaxShotCount() const { return m_maxShotCount; }

	Bool isClearFiringLineOfSightTerrain(const Object* source, const Object* victim) const;
	Bool isClearFiringLineOfSightTerrain(const Object* source, const Coord3D& victimPos) const;

	Bool isClearGoalFiringLineOfSightTerrain(const Object* source, const Coord3D& goalPos, const Object* victim) const;
	Bool isClearGoalFiringLineOfSightTerrain(const Object* source, const Coord3D& goalPos, const Coord3D& victimPos) const;

	static void calcProjectileLaunchPosition(
		const Object* launcher,
		WeaponSlotType wslot,
		Int specificBarrelToUse,
		Matrix3D& worldTransform,
		Coord3D& worldPos
	);

	static void positionProjectileForLaunch(
		Object* projectile,
		const Object *launcher,
		WeaponSlotType wslot,
		Int specificBarrelToUse
	);

	/**
		special purpose call for jets in airfields: directly set the ammoinclip to a certain
		percentage full (note that percent=1.0 is full, NOT percent=100.0!). this will end up
		with status as READY_TO_FIRE or OUT_OF_AMMO, but never RELOADING_CLIP!
	*/
	void setClipPercentFull(Real percent, Bool allowReduction);
	UnsignedInt getSuspendFXFrame() const { return m_suspendFXFrame; }

protected:

	Weapon(const WeaponTemplate* tmpl, WeaponSlotType wslot);

	// return true if we auto-reloaded our clip after firing.
	Bool privateFireWeapon(
		const Object *sourceObj,
		Object *victimObj,
		const Coord3D* victimPos,
		Bool isProjectileDetonation,
		Bool ignoreRanges,
		WeaponBonusConditionFlags extraBonusFlags,
		ObjectID* projectileID,
		Bool inflictDamage
	);
	Real estimateWeaponDamage(const Object *sourceObj, const Object *victimObj, const Coord3D* victimPos);
	void reloadWithBonus(const Object *source, const WeaponBonus& bonus, Bool loadInstantly);

	void processRequestAssistance( const Object *requestingObject, Object *victimObject ); ///< Weapons can call for extra attacks from nearby objects

	void getFiringLineOfSightOrigin(const Object* source, Coord3D& origin) const;

	void computeBonus(const Object *source, WeaponBonusConditionFlags extraBonusFlags, WeaponBonus& bonus) const;

	void rebuildScatterTargets();


private:
	const WeaponTemplate*			m_template;									///< the kind of weapon this is
	WeaponSlotType						m_wslot;										///< are we primary, secondary, etc. weapon? (used for projectile placement on reload)
	mutable WeaponStatus			m_status;										///< weapon status
	UnsignedInt								m_ammoInClip;								///< how many shots left in current clip
	UnsignedInt								m_whenWeCanFireAgain;				///< the first frame the weapon can fire again
	UnsignedInt								m_whenPreAttackFinished;		///< the frame the pre attack will complete.
	UnsignedInt								m_whenLastReloadStarted;		///< the frame the current reload/between-shots began. (only valid if status is RELOADING_CLIP or BETWEEN_FIRING_SHOTS)
	UnsignedInt								m_lastFireFrame;						///< frame a shot was last fired on
	UnsignedInt								m_suspendFXFrame;						///< The fireFX can be suspended for any delay, in frames, then they will execute as normal
	ObjectID									m_projectileStreamID;				///< the object that is tracking our stream if we have one.  It can't go away without us.
	Int												m_maxShotCount;							///< used for limiting consecutive firing
	Int												m_curBarrel;								///< current barrel used for firing
	Int												m_numShotsForCurBarrel;			///< how many shots to fire from cur barrel before moving to next barrel
	std::vector<Int>					m_scatterTargetsUnused;			///< A running memory of which targets I've used, so I can shoot them all at random
	Bool											m_pitchLimited;
	Bool											m_leechWeaponRangeActive;		///< This weapon has unlimited range until attack state is aborted!

	// setter function for status that should not be used outside this class
	void setStatus( WeaponStatus status) { m_status = status; }
};

//-------------------------------------------------------------------------------------------------
/**
	The "store" used to hold all the WeaponTemplates in existence. This is usually used when creating
	an Object, but can be used at any time after that. (It is explicitly
	OK to swap an Object's Weapon out at any given time.)
*/
//-------------------------------------------------------------------------------------------------
class WeaponStore : public SubsystemInterface
{
	friend class WeaponTemplate;

public:

	WeaponStore();
	virtual ~WeaponStore() override;

	virtual void init() override { };
	virtual void postProcessLoad() override;
	virtual void reset() override;
	virtual void update() override;

	/**
		Find the WeaponTemplate with the given name. If no such WeaponTemplate exists, return null.
	*/
	const WeaponTemplate *findWeaponTemplate(const AsciiString& name) const;
	const WeaponTemplate *findWeaponTemplate(const char* name) const;
	const WeaponTemplate *findWeaponTemplateByNameKey( NameKeyType key ) const { return findWeaponTemplatePrivate( key ); }

	// this dynamically allocates a new Weapon, which is owned (and must be freed!) by the caller.
	Weapon* allocateNewWeapon(const WeaponTemplate *tmpl, WeaponSlotType wslot) const
	{
		return newInstance(Weapon)(tmpl, wslot);	// my, that was easy
	}

	void createAndFireTempWeapon(const WeaponTemplate* w, const Object *source, const Coord3D* pos);
	void createAndFireTempWeapon(const WeaponTemplate* w, const Object *source, Object *target);

	void handleProjectileDetonation( const WeaponTemplate* w, const Object *source, const Coord3D* pos, WeaponBonusConditionFlags extraBonusFlags, Bool inflictDamage = TRUE );

	static void parseWeaponTemplateDefinition(INI* ini);

protected:

	WeaponTemplate *findWeaponTemplatePrivate( NameKeyType key ) const;

	WeaponTemplate *newWeaponTemplate( AsciiString name );
	WeaponTemplate *newOverride( WeaponTemplate *weaponTemplate );

	void deleteAllDelayedDamage();
	void resetWeaponTemplates();
	void setDelayedDamage(const WeaponTemplate *weapon, const Coord3D* pos, UnsignedInt whichFrame, ObjectID sourceID, ObjectID victimID, const WeaponBonus& bonus, Bool playDetonationFX = FALSE);

private:

	/**
		WeaponDelayedDamageInfo is a utility class used by the WeaponStore to keep track
		of what damage will need to be dealt in the future. It is never used for Projectile
		weaponry, but rather for weapons that do damage in the short-term future without
		instantiating full-fledged Objects as a bullet. (e.g., tank shells do this.)
	*/
	struct WeaponDelayedDamageInfo
	{
	public:
		const WeaponTemplate *m_delayedWeapon;			///< if delayed damage is pending, the weapon to deal the damage
		Coord3D m_delayDamagePos;										///< where to do the delay damage when it's time
		UnsignedInt m_delayDamageFrame;							///< frames we do the damage
		ObjectID m_delaySourceID;										///< who dealt the damage (by ID since it might be dead due to delay)
		ObjectID m_delayIntendedVictimID;						///< who the damage was intended for (or zero if no specific target)
		WeaponBonus m_bonus;												///< the weapon bonus to use
		Bool m_playDetonationFX;										///< Hypersonic @feature (D-048) a cluster bomblet: play the weapon's ProjectileDetonationFX there
	};

	std::vector<WeaponTemplate*> m_weaponTemplateVector;

	// TheSuperHackers @performance IamInnocent 01/01/2026 - Now additionally stores the same weapon templates in a hash map to optimize lookups by name key
	typedef std::hash_map<NameKeyType, WeaponTemplate*, rts::hash<NameKeyType>, rts::equal_to<NameKeyType>/**/> WeaponTemplateMap;
	WeaponTemplateMap m_weaponTemplateHashMap;

	std::list<WeaponDelayedDamageInfo> m_weaponDDI;
};

// EXTERNALS //////////////////////////////////////////////////////////////////////////////////////
extern WeaponStore *TheWeaponStore;
