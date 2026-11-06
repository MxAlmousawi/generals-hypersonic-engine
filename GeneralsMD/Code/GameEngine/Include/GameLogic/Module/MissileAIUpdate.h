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

// FILE:		MissileAIUpdate.h
// Author:	Michael S. Booth, December 2001
// Desc:		Missile behavior

#pragma once

#include "Common/AudioEventRTS.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Damage.h"	// Hypersonic D-228: DeathType
#include "GameLogic/WeaponBonusConditionFlags.h"
#include "Common/INI.h"
#include "WWMath/matrix3d.h"

enum ParticleSystemID CPP_11(: Int);
class FXList;
class ObjectCreationList;


//-------------------------------------------------------------------------------------------------
class MissileAIUpdateModuleData : public AIUpdateModuleData
{
public:
	Bool						m_tryToFollowTarget;	///< if true, attack object, not pos
	UnsignedInt			m_fuelLifetime;				///< num frames till missile runs out of motive power (0 == inf)
	UnsignedInt			m_ignitionDelay;			///< delay in frames from when missile is 'fired', to when it starts moving		15
	Real						m_initialVel;
	Real						m_initialDist;
	Real						m_diveDistance;				///< If I get this close to my target, start ignoring my preferred height
	const FXList*		m_ignitionFX;					///< FXList to do when missile 'ignites'
	Bool						m_useWeaponSpeed;			///< if true, limit speed of projectile to the Weapon's info
	Bool						m_detonateOnNoFuel;		///< If true, don't just stop thrusting, blow up when out of gas
	Int							m_garrisonHitKillCount;
	KindOfMaskType	m_garrisonHitKillKindof;			///< the kind(s) of units that can be collided with
	KindOfMaskType	m_garrisonHitKillKindofNot;		///< the kind(s) of units that CANNOT be collided with
	const FXList*		m_garrisonHitKillFX;
	Real						m_distanceScatterWhenJammed;	///< How far I scatter when Jammed

	Real						m_lockDistance;				///< If I get this close to my target, guaranteed hit.
	Bool						m_detonateCallsKill;			///< if true, kill() will be called, instead of KILL_SELF state, which calls destroy.
  Int             m_killSelfDelay;      ///< If I have detonated and entered the KILL-SELF state, how ling do I wait before I Kill/destroy self?
	// Hypersonic @feature ballistic arc (Generals Hypersonic D-042): a position shot flies a ballistic arc whose apex
	// is this fraction of the launch-to-target distance, clamped to [min, max] (max 0 = no limit). 0 = stock flight.
	Real						m_ballisticApexRatio;
	Real						m_ballisticApexMin;
	Real						m_ballisticApexMax;
	// Hypersonic @feature vertical launch (Generals Hypersonic D-051): an arc missile fired from a standing launcher first
	// climbs straight up for this long, then pitches over into its arc for about the second time. 0 = the arc starts at once.
	UnsignedInt			m_ballisticClimbFrames;
	UnsignedInt			m_ballisticPitchFrames;
	// Hypersonic @feature realistic flight (Generals Hypersonic D-060): the apex moves this fraction later along the arc
	// (the climb is flatter, the terminal dive steeper); below BallisticLiftOffSpeed the motor's acceleration is scaled by
	// speed / that speed (slow lift-off from the launcher); while diving (past the apex) the missile has the FREEFALL model
	// condition and plays BallisticDiveFX every BallisticDiveFXInterval. All 0 / none = D-042/D-051 behaviour.
	Real						m_ballisticApexShift;
	Real						m_ballisticLiftOffSpeed;
	const FXList*		m_ballisticDiveFX;
	UnsignedInt			m_ballisticDiveFXInterval;
	// Hypersonic @feature booster drop (Generals Hypersonic D-182): created once at the missile when its dive starts
	// (FREEFALL), e.g. the spent booster falling away from the warhead. None = nothing.
	const ObjectCreationList*	m_ballisticDiveOCL;
	// Hypersonic @feature split warhead (Generals Hypersonic D-062): past the apex, once the missile is at most this high
	// above its aim point, it detonates in the air with its weapon aimed at the aim point (a cluster weapon releases its
	// warheads from here). More than the apex height = at the apex. 0 = it detonates on the aim point.
	Real						m_ballisticAirburstHeight;
	// Hypersonic @feature straighter dive (Generals Hypersonic D-197): past the apex the descending half of the arc is a
	// quadratic Bezier, level at the apex, that bows at most this fraction of the drop (vertically) above the straight
	// line from the apex to the aim point. 25% = the D-060 half-parabola (default, every other missile unchanged); less =
	// straighter, steering down soon after the apex (the Bezier's control point lies 2 x this along the way).
	Real						m_ballisticDiveBulge;
	// Hypersonic @feature dive sound (Generals Hypersonic D-063): a sound attached to the missile from the start of its
	// dive (FREEFALL) to its detonation or removal, one per missile; while it is set the drawable's ambient sound (the
	// flight loop) is switched off in the dive. Empty = none.
	AudioEventRTS		m_ballisticDiveSound;
	// Hypersonic @feature smooth dive (Generals Hypersonic D-090): within DistanceToTargetBeforeDiving the missile does
	// not turn straight at the target; its preferred height sinks along a parabola, full height x u x (2 - u) with
	// u = distance / the distance where the dive started (level there, the ground at the target), so the path curves
	// down into the dive until DistanceToTargetForLock homes it in. A missile fired already closer than
	// DistanceToTargetBeforeDiving starts the curve where it is fired. No = stock (straight at the target).
	Bool						m_diveCurve;
	// Hypersonic @feature climb over buildings (Generals Hypersonic D-092): > 0 = the clearance above a building roof.
	// While its preferred height is held (cruise and the D-090 dive curve), the missile looks ahead along its line to
	// the target and raises the preferred height so it passes this far above any structure in the way (not the one it is
	// aimed at); past the building the normal height (or dive curve) takes over again. 0 = stock.
	Real						m_climbOverStructures;
	// Hypersonic @feature retarget (Generals Hypersonic D-228): > 0 = a missile chasing an object whose target dies or is
	// gone, with fuel left, flies at the nearest living enemy vehicle, infantry or structure within this range of itself
	// (not airborne, not a projectile, not unattackable, not undetected stealth); with none it dives onto the dead
	// target's last position. It keeps the fuel it has left. 0 = stock (it runs out of gas at once and is removed).
	Real						m_retargetRange;
	// Hypersonic @feature battery failure (Generals Hypersonic D-228): when the fuel runs out the missile is killed with
	// this death type (its death modules show the failure) instead of going limp or detonating. NONE = stock.
	DeathType				m_noFuelDeathType;
	// Hypersonic @feature terminal steering (Generals Hypersonic D-228): > 1 = within DistanceToTargetBeforeDiving the
	// turn rate rises, up to this factor, to 1.25 x the rate that curves the missile onto its target
	// (2 x speed x sin(angle off the nose) / distance), so a target that dodges inside the turn circle is not circled.
	// 0 or 1 = stock.
	Real						m_terminalTurnBoost;
	MissileAIUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
class MissileAIUpdate : public AIUpdateInterface, public ProjectileUpdateInterface
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MissileAIUpdate, "MissileAIUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MissileAIUpdate, MissileAIUpdateModuleData );

public:
	MissileAIUpdate( Thing *thing, const ModuleData* moduleData );

	enum MissileStateType	 // Stored in save file, don't renumber.
	{
		PRELAUNCH			= 0,
		LAUNCH				= 1, ///< released from launcher, falling
		IGNITION			= 2, ///< engines ignite
		ATTACK_NOTURN	= 3, ///< fly toward victim
		ATTACK				= 4, ///< fly toward victim
		DEAD					= 5,
		KILL					= 6, ///< Hit victim (cheat).
		KILL_SELF			= 7, ///< Destroy self.
	};

	virtual ProjectileUpdateInterface* getProjectileUpdateInterface() override { return this; }
	virtual void projectileFireAtObjectOrPosition( const Object *victim, const Coord3D *victimPos, const WeaponTemplate *detWeap, const ParticleSystemTemplate* exhaustSysOverride ) override;
	virtual void projectileLaunchAtObjectOrPosition(const Object *victim, const Coord3D* victimPos, const Object *launcher, WeaponSlotType wslot, Int specificBarrelToUse, const WeaponTemplate* detWeap, const ParticleSystemTemplate* exhaustSysOverride) override;
	virtual Bool projectileHandleCollision( Object *other ) override;
	virtual Bool projectileIsArmed() const override { return m_isArmed; }
	virtual ObjectID projectileGetLauncherID() const override { return m_launcherID; }
	virtual void setFramesTillCountermeasureDiversionOccurs( UnsignedInt frames ) override; ///< Number of frames till missile diverts to countermeasures.
	virtual void projectileNowJammed() override;///< We lose our Object target and scatter to the ground

	virtual Bool processCollision(PhysicsBehavior *physics, Object *other) override; ///< Returns true if the physics collide should apply the force.  Normally not.  jba.

	virtual UpdateSleepTime update() override;
	virtual void onDelete() override;

	// Hypersonic @feature ballistic arc (D-042): lets the SAM lead solver (WeaponTemplate::solveLeadIntercept) predict
	// the missile along its arc with the same steps the missile flies.
	virtual const MissileAIUpdate* getMissileAIUpdate() const override { return this; }
	Bool isOnBallisticArc() const { return m_isBallistic && m_state >= LAUNCH && m_state <= ATTACK; }
	// Hypersonic @feature vertical launch (D-051): the flight path is a vertical climb, a pitch-over curve, then the arc.
	enum BallisticSegment	// Stored in save file, don't renumber.
	{
		BALLISTIC_CLIMB = 0,	///< param = height climbed above the launch point
		BALLISTIC_PITCH = 1,	///< param = distance flown along the pitch-over circle
		BALLISTIC_ARC		= 2		///< param = horizontal fraction of the arc flown, 0..1
	};
	struct BallisticFlight
	{
		Int		segment;
		Real	param;
		Real	speed;	///< dist/frame
		// Hypersonic @feature one-by-one release (D-162, D-215): top speed factor of a releasing missile (1 = none) and,
		// with finalIn >= 0, the factor it switches to after finalIn more frames (finalIn < 0: never)
		Real	speedFactor;
		Real	finalFactor;
		Int		finalIn;
	};
	void getBallisticFlight( BallisticFlight *f ) const { f->segment = m_arcSegment; f->param = m_arcProgress; f->speed = m_arcSpeed; f->speedFactor = m_releaseSpeedFactor; f->finalFactor = m_releaseFinalFactor; f->finalIn = m_releaseFinalIn; }
	Bool stepBallisticFlight( BallisticFlight *f, Coord3D *pos ) const;	///< one logic frame on; FALSE once it has arrived
	// Hypersonic @fix (D-048): the missile is placed on its arc directly, so its physics velocity is zero; this is its
	// true motion (the step it flies in the coming frame, per frame) for the SAM lead solver and the proximity fuse.
	void getBallisticVelocity( Coord3D *vel ) const;
	const Coord3D *getOriginalTargetPos() const { return &m_originalTargetPos; }	///< Hypersonic @feature (D-110) where it was fired (scatter applied)
	// Hypersonic @feature missile target reveal (Generals Hypersonic D-143): the launch system's reveal object at the aim
	// point; when this missile is gone (landed, shot down or removed) the reveal object lives 'lingerFrames' longer.
	void setTargetReveal( ObjectID revealID, UnsignedInt lingerFrames ) { m_targetRevealID = revealID; m_targetRevealLinger = lingerFrames; }


protected:

	void detonate();
	void detonateAt(const Coord3D *pos);	///< Hypersonic @feature (D-062) the weapon detonates at 'pos', the missile stays where it is

private:

	MissileStateType			m_state;									///< the behavior state of the missile
	UnsignedInt						m_stateTimestamp;					///< time of state change
	UnsignedInt						m_nextTargetTrackTime;		///< if nonzero, how often we update our target pos
	ObjectID							m_launcherID;							///< ID of object that launched us (INVALID_ID if not yet launched)
	ObjectID							m_victimID;								///< ID of object that I am rocketing towards (INVALID_ID if not yet launched)
	UnsignedInt						m_fuelExpirationDate;			///< how long 'til we run out of fuel
	Real									m_noTurnDistLeft;					///< when zero, ok to start turning
	Real									m_maxAccel;
	Coord3D								m_originalTargetPos;			///< When firing uphill, we aim high to clear the brow of the hill.  jba.
	Coord3D								m_prevPos;
	WeaponBonusConditionFlags		m_extraBonusFlags;
	const WeaponTemplate*	m_detonationWeaponTmpl;		///< weapon to fire at end (or null)
	const ParticleSystemTemplate* m_exhaustSysTmpl;
	ParticleSystemID			m_exhaustID;								///< our exhaust particle system (if any)
	UnsignedInt						m_framesTillDecoyed;			///< Number of frames before missile will get distracted by decoy countermeasures.
	Bool									m_isTrackingTarget;				///< Was I originally shot at a moving object?
	Bool									m_isArmed;								///< if true, missile will explode on contact
	Bool									m_noDamage;								///< if true, missile will not cause damage when it detonates. (Used for flares).
	Bool									m_isJammed;								///< No target, just shooting at a scattered position
	Bool									m_isLeadIntercept;				///< Hypersonic @feature SAM lead pursuit of m_victimID (D-038)
	Bool									m_isBallistic;						///< Hypersonic @feature flying a ballistic arc (D-042)
	Real									m_diveCurveStart;					///< Hypersonic @feature smooth dive (D-090): distance where the curve began (0 = not yet)
	Coord3D								m_arcStart;								///< arc: launch point
	Coord3D								m_arcEnd;									///< arc: aim point
	Real									m_arcApexZ;								///< arc: apex height (world z)
	Real									m_arcProgress;						///< arc: horizontal fraction flown, 0..1
	Real									m_arcSpeed;								///< arc: current speed (dist/frame)
	Int										m_arcSegment;							///< Hypersonic @feature vertical launch (D-051): BallisticSegment
	Coord3D								m_climbStart;							///< climb: launch point (the arc starts where the pitch-over ends)
	Real									m_climbHeight;						///< climb: height of the vertical segment
	Real									m_pitchRadius;						///< pitch-over: radius of its circle (in the arc's vertical plane)
	Real									m_pitchLength;						///< pitch-over: length along the circle (0 = none)
	AudioEventRTS					m_diveSound;							///< Hypersonic @feature (D-063) client audio only, not saved (restarted while diving)
	ObjectID							m_targetRevealID;					///< Hypersonic @feature (D-143) reveal object at the aim point (INVALID_ID: none)
	UnsignedInt						m_targetRevealLinger;			///< Hypersonic @feature (D-143) frames it stays after this missile is gone
	// Hypersonic @feature one-by-one release (D-162), a weapon with ClusterReleaseInterval: see updateClusterRelease
	Int										m_releaseCount;						///< warheads released so far (-1: not planned yet)
	UnsignedInt						m_releaseStartFrame;			///< frame of the first release
	UnsignedInt						m_releaseImpactFrame;			///< frame this missile lands on its aim point; (D-261) ClusterReleaseDropTime: raised to its last warhead's landing
	Real									m_releaseInterval;				///< frames between releases
	Real									m_releaseSpeedFactor;			///< top speed factor while releasing (1 = none)
	Real									m_releaseFinalFactor;			///< (D-215) top speed factor once m_releaseFinalIn runs out
	Int										m_releaseFinalIn;					///< (D-215) frames of flight until then (-1: never)
	Real									m_releaseTurn;						///< random turn of the warheads' ring
	Bool									m_diveOCLDone;						///< Hypersonic @feature (D-182) BallisticDiveOCL already created
	Coord3D								m_lastTargetPos;					///< Hypersonic @feature retarget (D-228): last position of the living chased target

	void endTargetReveal();												///< Hypersonic @feature (D-143)
	void updateClusterRelease();									///< Hypersonic @feature one-by-one release (D-162)
	void releaseClusterWarhead();									///< Hypersonic @feature (D-162) the next warhead
	void vanishAfterRelease();										///< Hypersonic @feature (D-245) carrier gone after its last release

	void doPrelaunchState();
	void doLaunchState();
	void doIgnitionState();
	void doAttackState(Bool turnOK);
	void doKillState();
	void doKillSelfState();
	void doDeadState();
	void doLeadInterceptState(Bool turnOK);				///< Hypersonic @feature SAM lead pursuit (D-038)
	Bool isLeadInterceptFuseTriggered(const Object *victim) const;
	void moveToLeadInterceptPoint(const Coord3D *aim, Bool newMove);
	void startBallisticArc(const Coord3D *target);	///< Hypersonic @feature ballistic arc (D-042)
	void doBallisticFlightState(Bool advance);
	void getBallisticArcPoint(Real progress, Coord3D *pos, Vector3 *tangent) const;
	Real advanceBallisticArc(Real progress, Real dist) const;
	void getBallisticArcShape(Real *kUp, Real *kDown, Real *uApex) const;
	Real getBallisticArcSlope(Real progress, Real kUp, Real kDown, Real uApex) const;	///< dz/du of the arc (D-197)
	void updateBallisticDive();										///< Hypersonic @feature realistic flight (D-060)
	void keepDiveSoundPlaying();										///< Hypersonic @feature dive sound (D-063, D-161)
	void stopBallisticDiveSound();										///< Hypersonic @feature dive sound (D-063)
	Real structureClimbHeight();								///< Hypersonic @feature climb over buildings (D-092)
	Real nextBallisticSpeed(Real speed, Real factor, Real fromFactor) const;
	void getPitchOverPoint(Real dist, Coord3D *pos, Vector3 *tangent) const;
	void getBallisticFlightPoint(const BallisticFlight &f, Coord3D *pos, Vector3 *tangent) const;
	void orientBallistic(Vector3 *dir) const;

	void airborneTargetGone();											///< My airborne target has died, so I have to do something cool to make up for that
	Bool isTrackedTargetGone() const;								///< Hypersonic @feature retarget (D-228): the chased object is gone (or, with RetargetRange, dead)
	Object *findRetarget() const;										///< Hypersonic @feature retarget (D-228): nearest valid enemy within RetargetRange
	void updateLastTargetPos();											///< Hypersonic @feature retarget (D-228)
	Bool dieOfNoFuel();															///< Hypersonic @feature battery failure (D-228): TRUE = killed with NoFuelDeathType
	void applyTerminalTurnBoost(Bool turnOK);				///< Hypersonic @feature terminal steering (D-228)

	void tossExhaust();
	void switchToState(MissileStateType s);


};
