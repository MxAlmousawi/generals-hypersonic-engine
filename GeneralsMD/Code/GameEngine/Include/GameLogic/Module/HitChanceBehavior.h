/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Generals Hypersonic
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

// FILE: HitChanceBehavior.h ////////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Interception hit chance of a drone or missile (Generals Hypersonic IRAN_DESIGN 7c).
//         While the object is airborne (or is a projectile), every weapon shot that would damage it rolls
//         HitChance with the synced logic random; a miss does nothing to it. Once the TriggeredBy upgrade
//         applies (a player upgrade: per owning player), UpgradedHitChance is used instead.
//         SAMHitIsLethal: a hit by an InterceptorClass = SAM weapon destroys the object regardless of its health.
//         TargetableByAntiAir: a BALLISTIC_MISSILE that every anti-air weapon may engage (not only
//         AntiBallisticMissile ones).
//         ImmuneToPointDefense: Point Defense Lasers (PointDefenseLaserUpdate) never pick it as a target (D-053).
//         The only runtime state is the upgrade flag, which UpgradeModule saves and CRCs.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpgradeModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;
class Player;
class Thing;

//-------------------------------------------------------------------------------------------------
class HitChanceBehaviorModuleData : public UpgradeModuleData
{
public:

	HitChanceBehaviorModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	Real m_hitChance;						///< chance (0..1) that a shot connects
	Real m_upgradedHitChance;		///< chance once the TriggeredBy upgrade applies; negative means "same as HitChance"
	Bool m_samHitIsLethal;			///< a SAM-class hit destroys the object outright
	Bool m_targetableByAntiAir;	///< a ballistic missile that every anti-air weapon may target
	Bool m_immuneToPointDefense;	///< Hypersonic @feature (D-053) Point Defense Lasers ignore it
	Real m_laserHitChance;				///< Hypersonic @feature (D-059) chance of a Point Defense Laser (InterceptorClass LASER) shot; negative means "same as HitChance"
	Real m_upgradedLaserHitChance;	///< the laser chance once the TriggeredBy upgrade applies; negative means "same as LaserHitChance"
	AsciiString m_secondUpgrade;			///< Hypersonic @feature (D-196) a second player upgrade (Countermeasures II); empty: none
	Real m_secondUpgradedHitChance;		///< the chance once the owner has SecondUpgrade; negative means "same as the first-upgrade chance"
	Real m_secondUpgradedLaserHitChance;	///< the laser chance once the owner has SecondUpgrade; negative means "same as the first-upgrade laser chance"

	/// Hypersonic @feature (D-196) the chance for a shot (laser or not) given which upgrades the owner has
	Real getChance( Bool pointDefenseLaser, Bool upgraded, Bool secondUpgraded ) const;
	/// Hypersonic @feature (D-196) the owner has completed SecondUpgrade
	Bool hasSecondUpgrade( const Player *player ) const;
};

//-------------------------------------------------------------------------------------------------
class HitChanceBehavior : public UpgradeModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( HitChanceBehavior, "HitChanceBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( HitChanceBehavior, HitChanceBehaviorModuleData );

public:

	HitChanceBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

	static const HitChanceBehavior* findHitChanceBehavior( const Object *obj );

	Bool isActive() const;			///< airborne (or a projectile): shots at it roll the hit chance
	Real getHitChance( Bool pointDefenseLaser = FALSE ) const;	///< the current chance, upgrade included (D-059: a laser shot's own chance)
	Bool rollHit( Bool pointDefenseLaser = FALSE, Real chanceScale = 1.0f ) const;	///< one independent roll with the synced logic random; Hypersonic (D-195) chanceScale: the weapon's DroneHitChanceScale or MissileHitChanceScale
	Bool isSAMHitLethal() const { return getHitChanceBehaviorModuleData()->m_samHitIsLethal; }
	Bool isTargetableByAntiAir() const { return getHitChanceBehaviorModuleData()->m_targetableByAntiAir; }
	Bool isImmuneToPointDefense() const { return getHitChanceBehaviorModuleData()->m_immuneToPointDefense; }

	/// True while any object with a SAMHitIsLethal HitChanceBehavior exists. A cheap early-out for the SAM
	/// lead-intercept target search (D-038); a plain instance count, neither saved nor CRC'd.
	static Bool isAnySAMHitLethalObjectAlive() { return s_samHitLethalCount > 0; }

protected:

	virtual void upgradeImplementation() override { } ///< the upgrade flag (isAlreadyUpgraded) is all the state we need
	virtual Bool isSubObjectsUpgrade() override { return false; }

private:

	static UnsignedInt s_samHitLethalCount;	///< live HitChanceBehavior instances with SAMHitIsLethal

};
