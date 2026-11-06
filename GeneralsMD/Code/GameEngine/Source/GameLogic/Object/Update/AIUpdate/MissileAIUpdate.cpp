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

// FILE: MissileAIUpdate.cpp
// Author: Michael S. Booth, December 2001
// Desc:   Implementation of missile behavior

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_DEATH_NAMES	// Hypersonic D-228: NoFuelDeathType

#include "Common/GameAudio.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/RandomValue.h"
#include "Common/BitFlagsIO.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/ParticleSys.h"

const Real BIGNUM = 99999.0f;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Hypersonic @feature realistic flight (D-060): the horizontal fraction of the arc at which it peaks. A single parabola
// through the start and the end peaks at a / (a + b) (a, b = square roots of the apex height above start and end, D-042);
// BallisticApexShift moves that peak later by the given fraction of itself, at most to 90% of the way (a missile that
// starts at its apex, the Fattah Strike, still only descends).
//-----------------------------------------------------------------------------
static Real ballisticApexFraction(Real a, Real b, Real shift)
{
	if (a + b <= 0.001f)
		return 0.5f;
	const Real u = a / (a + b);
	return min(max(u, 0.9f), u * (1.0f + shift));
}

//-----------------------------------------------------------------------------
MissileAIUpdateModuleData::MissileAIUpdateModuleData()
{
	m_tryToFollowTarget = true;
	m_fuelLifetime = 0;
	m_ignitionDelay = 0;
	m_initialVel = 0;
	m_initialDist = 0.0f;
	m_diveDistance = 0.0f;
	m_ignitionFX = nullptr;
	m_useWeaponSpeed = false;
	m_detonateOnNoFuel = FALSE;
	m_garrisonHitKillCount = 0;
	m_garrisonHitKillFX = nullptr;
	m_lockDistance = 75.0f;
	m_distanceScatterWhenJammed = 75.0f;
    m_detonateCallsKill = FALSE;
    m_killSelfDelay   = 3; // just long enough for the contrail to catch up to me
	m_ballisticApexRatio = 0.0f;
	m_ballisticApexMin = 0.0f;
	m_ballisticApexMax = 0.0f;
	m_ballisticClimbFrames = 0;
	m_ballisticPitchFrames = 0;
	m_ballisticApexShift = 0.0f;
	m_ballisticLiftOffSpeed = 0.0f;
	m_ballisticDiveFX = nullptr;
	m_ballisticDiveFXInterval = 0;
	m_ballisticDiveOCL = nullptr;
	m_ballisticAirburstHeight = 0.0f;
	m_ballisticDiveBulge = 0.25f;	// D-197: 25% = the D-060 half-parabola
	m_diveCurve = FALSE;
	m_climbOverStructures = 0.0f;
	m_retargetRange = 0.0f;					// Hypersonic D-228: stock
	m_noFuelDeathType = DEATH_NONE;	// Hypersonic D-228: stock
	m_terminalTurnBoost = 0.0f;			// Hypersonic D-228: stock
}

//-----------------------------------------------------------------------------
void MissileAIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  AIUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "TryToFollowTarget",			INI::parseBool,		nullptr, offsetof( MissileAIUpdateModuleData, m_tryToFollowTarget ) },
		{ "FuelLifetime",						INI::parseDurationUnsignedInt,		nullptr, offsetof( MissileAIUpdateModuleData, m_fuelLifetime ) },
		{ "IgnitionDelay",					INI::parseDurationUnsignedInt,		nullptr, offsetof( MissileAIUpdateModuleData, m_ignitionDelay ) },
		{ "InitialVelocity",				INI::parseVelocityReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_initialVel) },
		{ "DistanceToTravelBeforeTurning",	INI::parseReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_initialDist ) },
		{ "DistanceToTargetBeforeDiving",		INI::parseReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_diveDistance ) },
		{ "DistanceToTargetForLock",INI::parseReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_lockDistance ) },
		{ "IgnitionFX",							INI::parseFXList,		nullptr, offsetof( MissileAIUpdateModuleData, m_ignitionFX ) },
		{ "UseWeaponSpeed",				  INI::parseBool,			nullptr, offsetof( MissileAIUpdateModuleData, m_useWeaponSpeed ) },
		{ "DetonateOnNoFuel",			  INI::parseBool,			nullptr, offsetof( MissileAIUpdateModuleData, m_detonateOnNoFuel ) },
		{ "DistanceScatterWhenJammed",INI::parseReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_distanceScatterWhenJammed ) },

		{ "GarrisonHitKillRequiredKindOf", KindOfMaskType::parseFromINI, nullptr, offsetof( MissileAIUpdateModuleData, m_garrisonHitKillKindof ) },
		{ "GarrisonHitKillForbiddenKindOf", KindOfMaskType::parseFromINI, nullptr, offsetof( MissileAIUpdateModuleData, m_garrisonHitKillKindofNot ) },
		{ "GarrisonHitKillCount", INI::parseUnsignedInt, nullptr, offsetof( MissileAIUpdateModuleData, m_garrisonHitKillCount ) },
		{ "GarrisonHitKillFX", INI::parseFXList, nullptr, offsetof( MissileAIUpdateModuleData, m_garrisonHitKillFX ) },
    { "DetonateCallsKill", INI::parseBool,   nullptr, offsetof( MissileAIUpdateModuleData, m_detonateCallsKill ) },
    { "KillSelfDelay",     INI::parseDurationUnsignedInt, nullptr, offsetof( MissileAIUpdateModuleData, m_killSelfDelay ) },
		// Hypersonic @feature ballistic arc (Generals Hypersonic D-042)
		{ "BallisticApexRatio",			INI::parsePercentToReal,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticApexRatio ) },
		{ "BallisticApexMinHeight",	INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticApexMin ) },
		{ "BallisticApexMaxHeight",	INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticApexMax ) },
		// Hypersonic @feature vertical launch (Generals Hypersonic D-051)
		{ "BallisticVerticalClimbTime",	INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticClimbFrames ) },
		{ "BallisticPitchOverTime",			INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticPitchFrames ) },
		// Hypersonic @feature realistic flight (Generals Hypersonic D-060)
		{ "BallisticApexShift",				INI::parsePercentToReal,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticApexShift ) },
		{ "BallisticLiftOffSpeed",		INI::parseVelocityReal,		nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticLiftOffSpeed ) },
		{ "BallisticDiveFX",					INI::parseFXList,					nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticDiveFX ) },
		{ "BallisticDiveFXInterval",	INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticDiveFXInterval ) },
		// Hypersonic @feature booster drop (Generals Hypersonic D-182)
		{ "BallisticDiveOCL",				INI::parseObjectCreationList,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticDiveOCL ) },
		// Hypersonic @feature split warhead (Generals Hypersonic D-062)
		{ "BallisticAirburstHeight",	INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticAirburstHeight ) },
		// Hypersonic @feature straighter dive (Generals Hypersonic D-197)
		{ "BallisticDiveBulge",				INI::parsePercentToReal,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticDiveBulge ) },
		// Hypersonic @feature dive sound (Generals Hypersonic D-063)
		{ "BallisticDiveSound",				INI::parseAudioEventRTS,	nullptr, offsetof( MissileAIUpdateModuleData, m_ballisticDiveSound ) },
		// Hypersonic @feature smooth dive (Generals Hypersonic D-090)
		{ "DiveCurve",								INI::parseBool,						nullptr, offsetof( MissileAIUpdateModuleData, m_diveCurve ) },
		// Hypersonic @feature climb over buildings (Generals Hypersonic D-092)
		{ "ClimbOverStructures",			INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_climbOverStructures ) },
		// Hypersonic @feature retarget, battery failure, terminal steering (Generals Hypersonic D-228)
		{ "RetargetRange",						INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_retargetRange ) },
		{ "NoFuelDeathType",					INI::parseIndexList,			TheDeathNames, offsetof( MissileAIUpdateModuleData, m_noFuelDeathType ) },
		{ "TerminalTurnBoost",				INI::parseReal,						nullptr, offsetof( MissileAIUpdateModuleData, m_terminalTurnBoost ) },
    { nullptr, nullptr, nullptr, 0 }
	};

  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
MissileAIUpdate::MissileAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();

	m_state = PRELAUNCH;
	m_stateTimestamp = TheGameLogic->getFrame();
	m_nextTargetTrackTime = 0x7fffffff;	// so that we never recalc target pos, by default
	m_launcherID = INVALID_ID;
	m_victimID = INVALID_ID;
	m_isArmed = false;
	m_fuelExpirationDate = 0;
	m_noTurnDistLeft = d->m_initialDist;
	m_prevPos = *getObject()->getPosition();
	m_maxAccel = BIGNUM;
	m_detonationWeaponTmpl = nullptr;
	m_exhaustSysTmpl = nullptr;
	m_isTrackingTarget = FALSE;
	m_exhaustID = INVALID_PARTICLE_SYSTEM_ID;
	m_extraBonusFlags = 0;
	m_originalTargetPos.zero();
	m_framesTillDecoyed = 0;
	m_noDamage = FALSE;
	m_isJammed = FALSE;
	m_isLeadIntercept = FALSE;
	m_isBallistic = FALSE;
	m_diveCurveStart = 0.0f;
	m_arcStart.zero();
	m_arcEnd.zero();
	m_arcApexZ = 0.0f;
	m_arcProgress = 0.0f;
	m_arcSpeed = 0.0f;
	m_arcSegment = BALLISTIC_ARC;
	m_climbStart.zero();
	m_climbHeight = 0.0f;
	m_pitchRadius = 0.0f;
	m_pitchLength = 0.0f;
	m_targetRevealID = INVALID_ID;
	m_targetRevealLinger = 0;
	m_releaseCount = -1;
	m_releaseStartFrame = 0;
	m_releaseImpactFrame = 0;
	m_releaseInterval = 1.0f;
	m_releaseTurn = 0.0f;
	m_releaseSpeedFactor = 1.0f;
	m_releaseFinalFactor = 0.0f;
	m_releaseFinalIn = -1;
	m_diveOCLDone = FALSE;
	m_lastTargetPos.zero();
}

//-------------------------------------------------------------------------------------------------
MissileAIUpdate::~MissileAIUpdate()
{
	tossExhaust();
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::onDelete()
{
	//
	// there is no need to destroy the attached particle systems here because the particle
	// systems themselves detect that their "parent" object is gone and destroy themselves.
	// but to be clean, for the exhaust particle system, we will invalidate the ID so that
	// we can't do anything else with it since we're being deleted
	//
	m_exhaustID = INVALID_PARTICLE_SYSTEM_ID;
	stopBallisticDiveSound();	// Hypersonic @feature (D-063): shot down or removed
	endTargetReveal();	// Hypersonic @feature (D-143): every end of the missile runs through here
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature missile target reveal (Generals Hypersonic D-143): the missile is gone (detonated, shot down or
// removed), so the reveal object at its aim point ends 'm_targetRevealLinger' frames from now. Its own DeletionUpdate
// carries the timer (and a safety lifetime if the missile is never deleted normally); logic side, so it stays in sync.
// D-245 Update 8: a vanished cluster carrier leaves submunitions in flight: linger from the planned impact instead.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::endTargetReveal()
{
	if( m_targetRevealID == INVALID_ID )
		return;
	Object *reveal = TheGameLogic->findObjectByID( m_targetRevealID );
	m_targetRevealID = INVALID_ID;
	if( reveal == nullptr || reveal->isDestroyed() )
		return;
	static const NameKeyType key_DeletionUpdate = NAMEKEY( "DeletionUpdate" );
	DeletionUpdate *deletion = (DeletionUpdate *)reveal->findUpdateModule( key_DeletionUpdate );
	UnsignedInt linger = m_targetRevealLinger;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( getObject()->testStatus( OBJECT_STATUS_MASKED ) && m_detonationWeaponTmpl != nullptr
		&& m_detonationWeaponTmpl->getClusterReleaseCarrierVanishes() && m_releaseImpactFrame > now )
		linger += m_releaseImpactFrame - now;
	if( deletion != nullptr && linger > 0 )
		deletion->setLifetimeRange( linger, linger );
	else
		TheGameLogic->destroyObject( reveal );
}


//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::tossExhaust()
{
	if (m_exhaustID != INVALID_PARTICLE_SYSTEM_ID)
	{
		TheParticleSystemManager->destroyParticleSystemByID(m_exhaustID);
		m_exhaustID = INVALID_PARTICLE_SYSTEM_ID;
	}
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::switchToState(MissileStateType s)
{
	if (m_state != s)
	{
		m_state = s;
		m_stateTimestamp = TheGameLogic->getFrame();
	}
}

//-------------------------------------------------------------------------------------------------
// Prepares the missile for launch via proper weapon-system channels.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::projectileLaunchAtObjectOrPosition(
	const Object *victim,
	const Coord3D* victimPos,
	const Object *launcher,
	WeaponSlotType wslot,
	Int specificBarrelToUse,
	const WeaponTemplate* detWeap,
	const ParticleSystemTemplate* exhaustSysOverride
)
{
	DEBUG_ASSERTCRASH(specificBarrelToUse>=0, ("specificBarrelToUse must now be explicit"));

	m_launcherID = launcher ? launcher->getID() : INVALID_ID;
	m_detonationWeaponTmpl = detWeap;
	m_extraBonusFlags = launcher ? launcher->getWeaponBonusCondition() : 0;

	Weapon::positionProjectileForLaunch(getObject(), launcher, wslot, specificBarrelToUse);

	projectileFireAtObjectOrPosition( victim, victimPos, detWeap, exhaustSysOverride );

}

#define APPROACH_HEIGHT 10.0f

//-------------------------------------------------------------------------------------------------
// The actual firing of the missile once setup.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::projectileFireAtObjectOrPosition( const Object *victim, const Coord3D *victimPos, const WeaponTemplate *detWeap, const ParticleSystemTemplate* exhaustSysOverride )
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	Object *obj = getObject();

	m_exhaustSysTmpl = exhaustSysOverride;
	m_detonationWeaponTmpl = detWeap;
	Real initialVelToUse = d->m_initialVel;
	if (d->m_useWeaponSpeed)
	{
		Real weaponSpeed = detWeap->getWeaponSpeed();
		initialVelToUse = weaponSpeed;

		Locomotor* curLoco = getCurLocomotor();
		if (curLoco)
		{
			m_maxAccel = weaponSpeed;
			curLoco->setMaxSpeed(weaponSpeed);
			curLoco->setMaxAcceleration(m_maxAccel);
		}
	}

	Real deltaZ = victimPos->z - obj->getPosition()->z;
	Real dx = victimPos->x - obj->getPosition()->x;
	Real dy = victimPos->y - obj->getPosition()->y;
	Real xyDist = sqrt(sqr(dx)+sqr(dy));
	if (xyDist<1) xyDist = 1;
	Real zFactor = 0;
	if (deltaZ>0) {
		zFactor = deltaZ/xyDist;
	}


	Vector3 dir = getObject()->getTransformMatrix()->Get_X_Vector();
	dir.Normalize();
	dir.Z += 2*zFactor;
	dir.Normalize();
	PhysicsBehavior* physics = getObject()->getPhysics();

	// Hypersonic @feature ballistic arc (Generals Hypersonic D-042): a position shot of a missile with a
	// BallisticApexRatio flies an arc that this module moves it along (startBallisticArc / doBallisticFlightState).
	m_isBallistic = FALSE;
	const Bool ballistic = d->m_ballisticApexRatio > 0.0f && !(victim && d->m_tryToFollowTarget) && xyDist >= 1.0f;
	if (ballistic)
		initialVelToUse = 0.0f;	// the arc sets the velocity itself; InitialVelocity is its starting speed

	if (physics && initialVelToUse > 0)
	{
		Real forceMag = physics->getMass() * initialVelToUse;

		Coord3D force;
		force.x = forceMag * dir.X;
		force.y = forceMag * dir.Y;
		force.z = forceMag * dir.Z;

		physics->applyMotiveForce( &force );
	}

	Vector3 objPos(obj->getPosition()->x, obj->getPosition()->y, obj->getPosition()->z);

	Matrix3D newXform;
	newXform.buildTransformMatrix( objPos, dir );
	obj->setTransformMatrix( &newXform );

	switchToState(LAUNCH);
	m_isTrackingTarget = false;
	m_isLeadIntercept = FALSE;
	// Missiles do their thing by colliding with something and exploding.  So they Move to the target
	// instead of Attacking the target.
	if (victim && d->m_tryToFollowTarget && detWeap && detWeap->isLeadInterceptEngagement(victim))
	{
		// Hypersonic @feature SAM lead interception (Generals Hypersonic D-038): a SAM interceptor fired at an Iranian
		// missile flies to the predicted intercept point instead of chasing the missile, and re-solves it every frame
		// (doLeadInterceptState). The point is a position goal, so the stock chase (AIMoveToState) is not used.
		m_isLeadIntercept = TRUE;
		m_victimID = victim->getID();
		Coord3D aim;
		if (!detWeap->solveLeadIntercept(obj->getPosition(), initialVelToUse, d->m_ignitionDelay, detWeap->getLeadInterceptMaxFrames(), victim, &aim))
			victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), aim);
		m_originalTargetPos = aim;
		moveToLeadInterceptPoint(&aim, TRUE);
	}
	else if (victim && d->m_tryToFollowTarget)
	{
		getStateMachine()->setGoalPosition(victim->getPosition());
		// ick. const-cast is evil. fix. (srj)
 		aiMoveToObject(const_cast<Object*>(victim), CMD_FROM_AI );
		m_originalTargetPos = *victim->getPosition();
		m_isTrackingTarget = TRUE;// Remember that I was originally shot at a moving object, so if the
		// target dies I can do something cool.
		m_victimID = victim->getID();
	}
	else if (ballistic)
	{
		m_originalTargetPos = *victimPos;
		m_victimID = INVALID_ID;
		startBallisticArc(victimPos);
	}
	else
	{
		// Otherwise, we are just a Coord shot.
		Coord3D initialPos = *victimPos;
		m_originalTargetPos = *victimPos;
		if (d->m_lockDistance>0.0f) {
			initialPos.z += APPROACH_HEIGHT;
		}
		aiMoveToPosition(&initialPos, CMD_FROM_AI );
		m_victimID = INVALID_ID;
	}

  setCurrentVictim( victim );/// extending access to the victim via the parent class
	m_prevPos = *getObject()->getPosition();
	m_lastTargetPos = m_originalTargetPos;	// Hypersonic @feature retarget (D-228)
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::projectileHandleCollision( Object *other )
{
	Object* obj = getObject();
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();

	// check if our warhead is "armed" - if not, we are inert
	if (projectileIsArmed() == false)
		return true;

	if (other==nullptr && !m_isBallistic) {	// Hypersonic: an arc that touches the ground (a ridge) detonates there
		// we hit the ground.  Check to see if we hit something unexpected.
		Coord3D goal = *getGoalPosition();
		Coord3D pos = *obj->getPosition();
		Coord3D delta;
		delta.x = pos.x-goal.x;
		delta.y = pos.y-goal.y;
		delta.z = pos.z-goal.z;
		if (delta.z > PATHFIND_CELL_SIZE_F) {
			// we're above our target goal.
			if (delta.length() > 3*PATHFIND_CELL_SIZE_F) {
				// we're somewhere else.
				return true;
			}
		}
	}

	if (other != nullptr)
	{
 		Object *projectileLauncher = TheGameLogic->findObjectByID( projectileGetLauncherID() );

 		// if it's not the specific thing we were targeting, see if we should incidentally collide...
 		if (!m_detonationWeaponTmpl->shouldProjectileCollideWith(projectileLauncher, obj, other, m_victimID))
		{
			//DEBUG_LOG(("ignoring projectile collision with %s at frame %d",other->getTemplate()->getName().str(),TheGameLogic->getFrame()));
			return true;
		}

		if (d->m_garrisonHitKillCount > 0)
		{
			ContainModuleInterface* contain = other->getContain();
			if( contain && contain->getContainCount() > 0 && contain->isGarrisonable() && !contain->isImmuneToClearBuildingAttacks() )
			{
				Int numKilled = 0;

				// garrisonable buildings subvert the normal process here.
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end() && numKilled < d->m_garrisonHitKillCount; )
					{
						Object* thingToKill = *it++;
						if (!thingToKill->isEffectivelyDead() && thingToKill->isKindOfMulti(d->m_garrisonHitKillKindof, d->m_garrisonHitKillKindofNot))
						{
							//DEBUG_LOG(("Killed a garrisoned unit (%08lx %s) via Flash-Bang!",thingToKill,thingToKill->getTemplate()->getName().str()));
							if (projectileLauncher)
								projectileLauncher->scoreTheKill( thingToKill );
							thingToKill->kill();
							++numKilled;
						}
					}
				}

				if (numKilled > 0)
				{
					// note, fx is played at center of building, not at grenade's location
					FXList::doFXObj(d->m_garrisonHitKillFX, other, nullptr);

					// don't do the normal explosion; just destroy ourselves & return
					TheGameLogic->destroyObject(obj);

					return true;
				}
			}
		}
	}

	// collided with something... blow'd up!
	detonate();

	// mark ourself as "no collisions" (since we might still exist in slow death mode)
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_COLLISIONS ) );
	return true;
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::detonate()
{
	detonateAt(getObject()->getPosition());
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature split warhead (D-062): detonate() with the weapon aimed at 'pos' (an air burst over the aim point
// passes the aim point; the detonation FX still plays at the missile).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::detonateAt(const Coord3D *pos)
{
	Object* obj = getObject();
	stopBallisticDiveSound();	// Hypersonic @feature (D-063): the dive sound ends at the detonation

	// Hypersonic @bugfix cook-off (D-142): mark the missile spent BEFORE its blast is dealt. It stays alive in KILL_SELF
	// for a few frames; a neighbour's blast killing it then must not count as a shot-down death (INI death modules
	// exempt KILLING_SELF). Stock set this status only at the end of this function.
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MISSILE_KILLING_SELF ) );

	if (m_detonationWeaponTmpl)
	{
		// Hypersonic @feature one-by-one release (D-162): any warheads not released yet leave now (an arc too short to
		// plan them), then the missile itself is the centre warhead: ClusterCenterWeapon (else one ClusterWeapon blast)
		const WeaponTemplate* detWeapon = m_detonationWeaponTmpl;
		if (detWeapon->isClusterReleaseWeapon())
		{
			if (m_releaseCount < 0)
			{
				m_releaseCount = 0;
				m_releaseTurn = GameLogicRandomValueReal(0.0f, 2.0f * PI);
				m_releaseInterval = 1.0f;
			}
			m_releaseImpactFrame = TheGameLogic->getFrame() + 3;
			while (m_releaseCount < detWeapon->getClusterCount())
				releaseClusterWarhead();
			// Hypersonic @feature terminal cluster release (D-245): a carrier that vanishes after its releases has no
			// centre warhead: only its ClusterReleaseVanishFX here, no blast
			if (detWeapon->getClusterReleaseCarrierVanishes())
			{
				vanishAfterRelease();
				return;
			}
			else
				detWeapon = detWeapon->getClusterCenterWeapon() != nullptr ? detWeapon->getClusterCenterWeapon() : detWeapon->getClusterWeapon();
		}

		if (detWeapon != nullptr)
			TheWeaponStore->handleProjectileDetonation(detWeapon, obj, pos, m_extraBonusFlags, !m_noDamage );

		if( m_detonationWeaponTmpl->getDieOnDetonate() )
		{
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
			damageInfo.in.m_deathType = DEATH_DETONATED;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
			obj->attemptDamage( &damageInfo );
		}
	}
	else if( !m_noDamage )
	{
		DamageInfo damageInfo;
		damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
		damageInfo.in.m_deathType = DEATH_DETONATED;
		damageInfo.in.m_sourceID = INVALID_ID;
		damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
		obj->attemptDamage( &damageInfo );
	}
	if (obj->getDrawable())
		obj->getDrawable()->setDrawableHidden(true);
	// Delay destroying the object two frames to let the contrail catch up. jba.

    switchToState(KILL_SELF);

	  obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MISSILE_KILLING_SELF ) );

}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doPrelaunchState()
{
	Locomotor* curLoco = getCurLocomotor();
	if (curLoco)
	{
		curLoco->setMaxAcceleration(0);
		curLoco->setMaxTurnRate(0);
	}
}
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doKillSelfState()
{
  const MissileAIUpdateModuleData *modData = getMissileAIUpdateModuleData();

	if (m_stateTimestamp > TheGameLogic->getFrame() - modData->m_killSelfDelay )
  {
		// Hold in this state [modData->m_killSelfDelay] frames to let the contrail catch up. jba.
		return;
	}
	Object* obj = getObject();
	if (m_detonationWeaponTmpl)
  {
    if ( modData->m_detonateCallsKill )
      obj->kill(); // kill it (vs destroying it) so that its Die modules are called
    else
		  TheGameLogic->destroyObject( obj );
	}
	switchToState(DEAD);
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doLaunchState()
{
	Locomotor* curLoco = getCurLocomotor();
	if (curLoco)
	{
		curLoco->setMaxAcceleration(0);
		curLoco->setMaxTurnRate(0);
	}

	UnsignedInt delay = getMissileAIUpdateModuleData()->m_ignitionDelay;
	if (TheGameLogic->getFrame() - m_stateTimestamp >= delay)
	{
		switchToState(IGNITION);
	}
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doIgnitionState()
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();

	Locomotor* curLoco = getCurLocomotor();
	if (curLoco)
	{
		curLoco->setMaxAcceleration(m_maxAccel);
		curLoco->setMaxTurnRate(0);
	}

	FXList::doFXObj(d->m_ignitionFX, getObject());
	if (m_exhaustSysTmpl != nullptr)
	{
		m_exhaustID = TheParticleSystemManager->createAttachedParticleSystemID(m_exhaustSysTmpl, getObject());
	}

	// arm the missile's "warhead"
	m_isArmed = true;
	UnsignedInt now = TheGameLogic->getFrame();
	m_fuelExpirationDate = d->m_fuelLifetime ? (now + d->m_fuelLifetime) : 0x7fffffff;

	switchToState(ATTACK_NOTURN);
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doAttackState(Bool turnOK)
{
	Locomotor* curLoco = getCurLocomotor();

	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	if (TheGameLogic->getFrame() >= m_fuelExpirationDate)
	{
		if( d->m_detonateOnNoFuel )
		{
			detonate();
			return;
		}

		if( dieOfNoFuel() )	// Hypersonic @feature battery failure (D-228)
			return;

		if (curLoco)
		{
			curLoco->setMaxAcceleration(0);
			curLoco->setMaxTurnRate(0);
		}
		tossExhaust();
	}
	else
	{
		if (curLoco)
		{
			curLoco->setMaxAcceleration(m_maxAccel);
			curLoco->setMaxTurnRate(turnOK ? BIGNUM : 0);
		}
	}

	// Hypersonic @feature retarget (D-228): the chased object died, so fly at a new one (or its last spot) before the
	// lock and dive checks below look at the goal
	if( d->m_retargetRange > 0.0f && m_isTrackingTarget && isTrackedTargetGone() )
	{
		airborneTargetGone();
		if( m_state != ATTACK_NOTURN && m_state != ATTACK )
			return;
	}
	updateLastTargetPos();
	applyTerminalTurnBoost( turnOK );

	if (d->m_lockDistance > 0)
	{
		Real lockDistanceSquared = d->m_lockDistance;
		Real distanceToTargetSquared;
		if (m_isTrackingTarget && (getGoalObject() != nullptr)) {
			distanceToTargetSquared = ThePartitionManager->getDistanceSquared( getObject(), getGoalObject(), FROM_CENTER_2D);
		}	else {
			distanceToTargetSquared = ThePartitionManager->getDistanceSquared( getObject(), getGoalPosition(), FROM_CENTER_2D );
		}
		if (lockDistanceSquared>0) {
			if (!m_isTrackingTarget) {
				// Immobile or ground target.  Halve the lock distance.
				lockDistanceSquared *= 0.5f;
			}
			lockDistanceSquared *= lockDistanceSquared;
			if (distanceToTargetSquared < lockDistanceSquared) {
				if (!m_isTrackingTarget) {
					// Ground pos.  Change to original goal.
					aiMoveToPosition(&m_originalTargetPos, CMD_FROM_AI );
				}
				switchToState(KILL);
				return;
			}
		}
	}

	// Hypersonic @feature smooth dive (D-090): the parabola below is worked out from the locomotor's full preferred
	// height each frame (a target that moves away out of the dive distance gets the full cruise height again); the same
	// for a D-092 climb over a building once it is passed
	if ((d->m_diveCurve || d->m_climbOverStructures > 0.0f) && curLoco && d->m_diveDistance > 0.0f)
		curLoco->restorePreferredHeightFromTemplate();

	if(curLoco && curLoco->getPreferredHeight() > 0)
	{
		// Am I close enough to the target to ignore my preferred height setting?
#if RETAIL_COMPATIBLE_CRC
		Real distanceToTargetSquared = ThePartitionManager->getDistanceSquared(getObject(), getGoalPosition(), FROM_CENTER_2D);
#else
		// TheSuperHackers @bugfix Stubbjax 23/08/2026 Diving missiles now use their target's position to determine distance
		// when applicable rather than the goal position. This allows them to properly determine when to dive on moving targets.
		Real distanceToTargetSquared;
		if (m_isTrackingTarget && (getGoalObject() != nullptr)) {
			distanceToTargetSquared = ThePartitionManager->getDistanceSquared(getObject(), getGoalObject(), FROM_CENTER_2D);
		}
		else {
			distanceToTargetSquared = ThePartitionManager->getDistanceSquared(getObject(), getGoalPosition(), FROM_CENTER_2D);
		}
#endif

		Real diveDistanceSquared = d->m_diveDistance;
		if (curLoco && curLoco->getPreferredHeight()) {
				diveDistanceSquared *= diveDistanceSquared;
			if( distanceToTargetSquared < diveDistanceSquared )
			{
				// Hypersonic @feature smooth dive (D-090): the preferred height sinks along a parabola that is level where
				// the dive starts (u = 1) and reaches the ground at the target (u = 0), so the missile curves down into
				// its dive; DistanceToTargetForLock homes it in at the end
				if( d->m_diveCurve )
				{
					Real dist = (Real)sqrtf( distanceToTargetSquared );
					if( m_diveCurveStart <= 0.0f )
						m_diveCurveStart = dist > 1.0f ? dist : 1.0f;
					Real u = dist / m_diveCurveStart;
					if( u > 1.0f )
						u = 1.0f;
					curLoco->setPreferredHeight( curLoco->getPreferredHeight() * u * (2.0f - u) );
					keepDiveSoundPlaying();	// Hypersonic @feature (D-161): the drone's engine spins up in its dive
				}
				else
					curLoco->setUsePreciseZPos( true );
			}
		}

		// Hypersonic @feature climb over buildings (D-092): never lower than needed to clear a building ahead
		if( d->m_climbOverStructures > 0.0f )
		{
			Real climb = structureClimbHeight();
			if( climb > curLoco->getPreferredHeight() )
				curLoco->setPreferredHeight( climb );
		}
	}

	if (m_noTurnDistLeft <= 0.0f)
	{
		switchToState(ATTACK);
	}

	// If I was fired at a flyer and have lost target (most likely they died), then I need to do something better
	// than cloverleaf around their last spot.
	if( m_isTrackingTarget && isTrackedTargetGone() )	// Hypersonic (D-228): = goal object gone for stock modules
		airborneTargetGone();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature climb over buildings (D-092): the preferred height (above the terrain under the missile) that
// takes it ClimbOverStructures above the roof of every structure near its line to the target, from just behind it to
// ClimbLookAhead ahead (or to the target), except the object it is aimed at. 0 = nothing in the way.
//-------------------------------------------------------------------------------------------------
Real MissileAIUpdate::structureClimbHeight()
{
	static const Real ClimbLookAhead = 150.0f;	// 1.25 s at the FPV drone's 120: enough to climb a Command Center
	static const Real ClimbLookBehind = 20.0f;	// keep the height until the tail is past the roof edge
	static const Real ClimbSideMargin = 10.0f;	// the corridor's half width beyond the building's footprint

	const MissileAIUpdateModuleData *d = getMissileAIUpdateModuleData();
	const Object *me = getObject();
	const Coord3D *pos = me->getPosition();
	const Object *goalObj = getGoalObject();
	const Coord3D *goal = goalObj ? goalObj->getPosition() : getGoalPosition();

	Real dx = goal->x - pos->x;
	Real dy = goal->y - pos->y;
	Real dist = (Real)sqrtf( dx * dx + dy * dy );
	if( dist < 1.0f )
		return 0.0f;
	dx /= dist;
	dy /= dist;
	Real ahead = dist < ClimbLookAhead ? dist : ClimbLookAhead;

	// search a circle around the middle of the corridor
	Coord3D mid;
	mid.x = pos->x + dx * ( ahead - ClimbLookBehind ) * 0.5f;
	mid.y = pos->y + dy * ( ahead - ClimbLookBehind ) * 0.5f;
	mid.z = pos->z;
	Real searchRadius = ( ahead + ClimbLookBehind ) * 0.5f + ClimbSideMargin;

	PartitionFilterAcceptByKindOf filterStructures( MAKE_KINDOF_MASK( KINDOF_STRUCTURE ), KINDOFMASK_NONE );
	PartitionFilter *filters[] = { &filterStructures, nullptr };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( &mid, searchRadius, FROM_BOUNDINGSPHERE_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Real highestRoof = 0.0f;
	Bool found = FALSE;
	for( Object *other = iter->first(); other; other = iter->next() )
	{
		if( other == goalObj || other->getID() == m_victimID || other->isEffectivelyDead() )
			continue;
		const Coord3D *opos = other->getPosition();
		// where the building's centre lies along the line (from just behind to the end of the look-ahead) and off it
		Real ox = opos->x - pos->x;
		Real oy = opos->y - pos->y;
		Real along = ox * dx + oy * dy;
		Real radius = other->getGeometryInfo().getBoundingCircleRadius();
		if( along < -ClimbLookBehind - radius || along > ahead + radius )
			continue;
		Real side = (Real)fabs( ox * dy - oy * dx );
		if( side > radius + ClimbSideMargin )
			continue;
		Real roof = opos->z + other->getGeometryInfo().getMaxHeightAbovePosition();
		if( !found || roof > highestRoof )
		{
			highestRoof = roof;
			found = TRUE;
		}
	}
	if( !found )
		return 0.0f;

	Real climb = highestRoof + d->m_climbOverStructures - TheTerrainLogic->getGroundHeight( pos->x, pos->y );
	return climb > 0.0f ? climb : 0.0f;
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doKillState()
{
	if (TheGameLogic->getFrame() >= m_fuelExpirationDate)
	{
		if( getMissileAIUpdateModuleData()->m_detonateOnNoFuel )
		{
			detonate();
			return;
		}

		if( dieOfNoFuel() )	// Hypersonic @feature battery failure (D-228)
			return;

		// srj sez: killstate missiles should not be allowed to live forever.
		airborneTargetGone();
		return;
	}

	// Hypersonic @feature retarget (D-228): the chased object died during the final homing; a new target goes back to
	// ATTACK (return, so BRAKING is not set again), its last spot stays in KILL as a position shot
	if( getMissileAIUpdateModuleData()->m_retargetRange > 0.0f && m_isTrackingTarget && isTrackedTargetGone() )
	{
		airborneTargetGone();
		if( m_state != KILL )
			return;
	}
	updateLastTargetPos();

	/* Kill state uses the OBJECT_STATUS_BRAKING, which is a flag to the locomotor to
		"Cheat" and move directly towards the target.  This ensures that the missile will hit.
		jba. */
	Locomotor* curLoco = getCurLocomotor();
	Object *obj = getObject();
	// Objects that are braking don't follow the normal physics, so they end up at their destination exactly.
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );

	if (curLoco)
	{
		curLoco->setMaxAcceleration(m_maxAccel);
		curLoco->setMaxTurnRate(BIGNUM);
	}
	if (isIdle()) {
		// we finished the move
		if (getGoalObject()!=nullptr) {
			Locomotor* curLoco = getCurLocomotor();
			Real closeEnough = 1.0f;
			if (curLoco)
			{
				closeEnough = curLoco->getMaxSpeedForCondition(BODY_PRISTINE);
			}
			Real distanceToTargetSq = ThePartitionManager->getDistanceSquared( getObject(), getGoalObject(), FROM_BOUNDINGSPHERE_3D);
			//DEBUG_LOG(("Distance to target %f, closeEnough %f", sqrt(distanceToTargetSq), closeEnough));
			if (distanceToTargetSq < closeEnough*closeEnough) {
				Coord3D pos = *getGoalObject()->getPosition();
				getObject()->setPosition(&pos);
				detonate();
			}	else{
				aiMoveToObject(getGoalObject(), CMD_FROM_AI );
			}
		} else {
			detonate();
		}
	}
	// If I was fired at a flyer and have lost target (most likely they died), then I need to do something better
	// than cloverleaf around their last spot.
	if( m_isTrackingTarget && isTrackedTargetGone() )	// Hypersonic (D-228): = goal object gone for stock modules
		airborneTargetGone();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature SAM lead pursuit (Generals Hypersonic D-038). Replaces doAttackState for a SAM interceptor fired
// at an Iranian missile: every frame, re-solve the intercept point from the missile's current velocity and steer at
// it; when the missile passes within the proximity fuse (DistanceToTargetForLock), detonate on it, so the weapon's
// damage and the D-018 hit roll land on the missile.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doLeadInterceptState(Bool turnOK)
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	Locomotor* curLoco = getCurLocomotor();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool outOfFuel = now >= m_fuelExpirationDate;

	// fuel and steering, as doAttackState
	if (outOfFuel)
	{
		if( d->m_detonateOnNoFuel )
		{
			detonate();
			return;
		}

		if( dieOfNoFuel() )	// Hypersonic @feature battery failure (D-228)
			return;

		if (curLoco)
		{
			curLoco->setMaxAcceleration(0);
			curLoco->setMaxTurnRate(0);
		}
		tossExhaust();
	}
	else
	{
		if (curLoco)
		{
			curLoco->setMaxAcceleration(m_maxAccel);
			curLoco->setMaxTurnRate(turnOK ? BIGNUM : 0);
		}
	}

	// D-245 Update 8: a vanished carrier is gone even while its hidden KILL_SELF object still exists.
	// Ordinary dying missiles keep the stock pursuit behavior (they are not MASKED).
	Object* victim = TheGameLogic->findObjectByID(m_victimID);
	if (victim == nullptr || m_detonationWeaponTmpl == nullptr
		|| (victim->testStatus(OBJECT_STATUS_MISSILE_KILLING_SELF) && victim->testStatus(OBJECT_STATUS_MASKED)))
	{
		airborneTargetGone();
		return;
	}

	if (isLeadInterceptFuseTriggered(victim))
	{
		Coord3D pos;
		victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), pos);
		getObject()->setPosition(&pos);
		detonate();
		return;
	}

	// Re-solve the intercept point with our current speed and the powered flight we have left. Once no intercept is
	// possible (out of fuel, or the missile outruns us), fly at the missile itself.
	Coord3D aim;
	Bool solved = FALSE;
	if (!outOfFuel)
	{
		UnsignedInt framesLeft = m_fuelExpirationDate - now;
		if (framesLeft > m_detonationWeaponTmpl->getLeadInterceptMaxFrames())
			framesLeft = m_detonationWeaponTmpl->getLeadInterceptMaxFrames();
		const PhysicsBehavior* physics = getObject()->getPhysics();
		const Real speed = physics ? physics->getVelocityMagnitude() : 0.0f;
		solved = m_detonationWeaponTmpl->solveLeadIntercept(getObject()->getPosition(), speed, 0, framesLeft, victim, &aim);
	}
	if (!solved)
		victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), aim);

	m_originalTargetPos = aim;
	moveToLeadInterceptPoint(&aim, FALSE);

	if (m_noTurnDistLeft <= 0.0f)
	{
		switchToState(ATTACK);
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature proximity fuse of a lead-pursuit interceptor (D-038): true when the missile is, or during the
// coming frame will be, within the fuse radius. The closest approach over the frame is used, because at the closing
// speeds involved (up to ~70 units per frame) a check at frame boundaries alone would skip past the missile.
//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::isLeadInterceptFuseTriggered(const Object *victim) const
{
	if (!m_isArmed || m_detonationWeaponTmpl == nullptr)
		return FALSE;

	const Real fuse = m_detonationWeaponTmpl->getLeadInterceptFuseRadius();

	Coord3D victimPos;
	victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), victimPos);
	const Coord3D* myPos = getObject()->getPosition();
	const Real rx = victimPos.x - myPos->x;
	const Real ry = victimPos.y - myPos->y;
	const Real rz = victimPos.z - myPos->z;

	Coord3D victimVel;
	Coord3D myVel;
	WeaponTemplate::getLeadTargetVelocity(victim, &victimVel);
	WeaponTemplate::getLeadTargetVelocity(getObject(), &myVel);
	const Real wx = victimVel.x - myVel.x;
	const Real wy = victimVel.y - myVel.y;
	const Real wz = victimVel.z - myVel.z;

	Real s = 0.0f;
	const Real ww = wx*wx + wy*wy + wz*wz;
	if (ww > 0.0001f)
		s = clamp(0.0f, -(rx*wx + ry*wy + rz*wz) / ww, 1.0f);

	const Real cx = rx + wx * s;
	const Real cy = ry + wy * s;
	const Real cz = rz + wz * s;
	return (cx*cx + cy*cy + cz*cz) <= fuse * fuse;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature steer a lead-pursuit interceptor at the intercept point (D-038). AIMoveToState aims a missile
// at twice the height difference of a goal above it (to clear hills), so the climb is pre-halved to steer at the
// point itself. The goal is updated in place while the move runs; a new move is issued only when needed.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::moveToLeadInterceptPoint(const Coord3D *aim, Bool newMove)
{
	Coord3D goal = *aim;
	const Real zDelta = goal.z - getObject()->getPosition()->z;
	if (zDelta > 0.0f)
		goal.z -= 0.5f * zDelta;

	if (newMove || isIdle())
		aiMoveToPosition(&goal, CMD_FROM_AI);
	else
		getStateMachine()->setGoalPosition(&goal);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature ballistic arc (Generals Hypersonic D-042). The arc lies in the vertical plane through the launch
// point and the aim point E: horizontally the missile goes straight to E (progress u = horizontal fraction of the arc),
// its height is the parabola z(u) = apex - k (u - uApex)^2 through the arc's start and E whose top is the apex. The
// apex is the launch-to-target distance x BallisticApexRatio (clamped) above the higher of the ground at the launcher
// and the target; a missile released above that (the Fattah Strike carrier) starts at its apex and only descends.
// Hypersonic @feature vertical launch (D-051): from a standing launcher (TEL, silo) with a BallisticVerticalClimbTime
// the path is: a vertical climb from the launch point L (what the missile flies in that time, at most half the way to
// the apex) to S, a pitch-over along a circle in the same vertical plane from S (vertical there) to Q, turning toward
// the target at a constant rate per distance (what it flies in BallisticPitchOverTime, shortened if needed to stay
// below half the remaining way to the apex and half the distance), then the D-042 arc from Q to E. The turn angle is
// solved so that the circle ends along the arc's starting direction (fixed-point iteration, converges in a few steps).
// The path is one BallisticFlight state (segment, param, speed) that the missile and the SAM lead solver (D-038) both
// step with stepBallisticFlight.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::startBallisticArc(const Coord3D *target)
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	Object* obj = getObject();

	m_climbStart = *obj->getPosition();
	m_arcStart = m_climbStart;
	m_arcEnd = *target;
	const Real groundAtEnd = TheTerrainLogic->getGroundHeight(m_arcEnd.x, m_arcEnd.y);
	if (m_arcEnd.z < groundAtEnd)
		m_arcEnd.z = groundAtEnd;

	const Real dist = sqrtf(sqr(m_arcEnd.x - m_arcStart.x) + sqr(m_arcEnd.y - m_arcStart.y));
	Real height = dist * d->m_ballisticApexRatio;
	if (height < d->m_ballisticApexMin)
		height = d->m_ballisticApexMin;
	if (d->m_ballisticApexMax > 0.0f && height > d->m_ballisticApexMax)
		height = d->m_ballisticApexMax;

	const Real base = max(TheTerrainLogic->getGroundHeight(m_arcStart.x, m_arcStart.y), m_arcEnd.z);
	m_arcApexZ = max(max(m_arcStart.z, m_arcEnd.z), base + height);
	m_arcSegment = BALLISTIC_ARC;
	m_arcProgress = 0.0f;
	m_arcSpeed = max(0.0f, d->m_initialVel);
	m_climbHeight = 0.0f;
	m_pitchRadius = 0.0f;
	m_pitchLength = 0.0f;
	// Hypersonic @feature (D-045): InitialVelocity is the motor's start speed from a standing launcher; a missile
	// released by a moving carrier (the Fattah Strike) keeps the carrier's speed instead of restarting its boost
	const Object* launcher = TheGameLogic->findObjectByID(obj->getProducerID());
	Real launcherSpeed = 0.0f;
	if (launcher != nullptr && launcher->getPhysics() != nullptr)
		launcherSpeed = launcher->getPhysics()->getVelocityMagnitude();
	if (launcherSpeed > m_arcSpeed)
		m_arcSpeed = launcherSpeed;
	m_isBallistic = TRUE;

	// Hypersonic @feature vertical launch (D-051): only from a standing launcher on the ground (not the Fattah Strike
	// carrier, which is airborne and moving), and only below the apex. Climb and pitch-over lengths are what the
	// missile flies in those times with the same speed steps as the flight.
	const Bool standing = launcher == nullptr || (launcherSpeed < 0.01f && !launcher->isSignificantlyAboveTerrain());
	const Real room = m_arcApexZ - m_climbStart.z;
	if (standing && d->m_ballisticClimbFrames > 0 && room > 2.0f && dist > 2.0f)
	{
		Real speed = m_arcSpeed;
		Real climb = 0.0f;
		for (UnsignedInt i = 0; i < d->m_ballisticClimbFrames; ++i)
		{
			speed = nextBallisticSpeed(speed, m_releaseSpeedFactor, m_releaseSpeedFactor);
			climb += speed;
		}
		Real pitchDist = 0.0f;
		for (UnsignedInt j = 0; j < d->m_ballisticPitchFrames; ++j)
		{
			speed = nextBallisticSpeed(speed, m_releaseSpeedFactor, m_releaseSpeedFactor);
			pitchDist += speed;
		}
		m_climbHeight = min(climb, 0.5f * room);
		if (m_climbHeight > 0.0f)
		{
			m_arcSegment = BALLISTIC_CLIMB;
			m_arcProgress = 0.0f;
			const Real topZ = m_climbStart.z + m_climbHeight;
			const Real maxRise = 0.5f * (m_arcApexZ - topZ);	// the pitch-over stays below half the way to the apex ...
			const Real maxRun = 0.5f * dist;										// ... and half the distance
			const Real b = sqrtf(max(0.0f, m_arcApexZ - m_arcEnd.z));

			// turn angle 'turn' (from vertical): the circle of length len turning by it ends at run/rise from S; the arc
			// from there starts at the angle (from vertical) asin(D / |(D, dz/du)|); iterate until they agree
			Real turn = PI / 4.0f;
			Real radius = 0.0f;
			Real len = 0.0f;
			for (Int it = 0; it < 8 && pitchDist > 0.0f; ++it)
			{
				radius = pitchDist / turn;
				len = pitchDist;
				Real run = radius * (1.0f - Cos(turn));
				Real rise = radius * Sin(turn);
				Real scale = 1.0f;
				if (rise > maxRise && rise > 0.0f)
					scale = min(scale, maxRise / rise);
				if (run > maxRun && run > 0.0f)
					scale = min(scale, maxRun / run);
				radius *= scale;
				len *= scale;
				run *= scale;
				rise *= scale;

				const Real a = sqrtf(max(0.0f, m_arcApexZ - (topZ + rise)));
				const Real horiz = max(0.001f, dist - run);
				const Real uA = ballisticApexFraction(a, b, d->m_ballisticApexShift);
				const Real slope = (uA > 0.0001f) ? 2.0f * a * a / uA : 0.0f;	// dz/du of the arc at its start (D-060 apex shift)
				const Real s = horiz / sqrtf(sqr(horiz) + sqr(slope));
				turn = ASin(min(1.0f, s));
				if (turn < 0.01f)
					break;
			}
			if (pitchDist > 0.0f && turn >= 0.01f)
			{
				radius = pitchDist / turn;
				len = pitchDist;
				const Real run = radius * (1.0f - Cos(turn));
				const Real rise = radius * Sin(turn);
				Real scale = 1.0f;
				if (rise > maxRise && rise > 0.0f)
					scale = min(scale, maxRise / rise);
				if (run > maxRun && run > 0.0f)
					scale = min(scale, maxRun / run);
				m_pitchRadius = radius * scale;
				m_pitchLength = len * scale;
			}

			// the arc starts where the pitch-over ends (the top of the climb without one)
			Vector3 unusedDir;
			getPitchOverPoint(m_pitchLength, &m_arcStart, &unusedDir);
		}
		else
		{
			m_climbHeight = 0.0f;
		}
	}

	// the AI has no move to make; the goal is only for code that asks where the missile is going
	getStateMachine()->setGoalPosition(&m_arcEnd);

	// point along the path from the start (straight up on a vertical launch)
	BallisticFlight f;
	getBallisticFlight(&f);
	Coord3D pos;
	Vector3 tangent;
	getBallisticFlightPoint(f, &pos, &tangent);
	orientBallistic(&tangent);
	Matrix3D xform;
	xform.buildTransformMatrix(Vector3(pos.x, pos.y, pos.z), tangent);
	obj->setTransformMatrix(&xform);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature realistic flight (D-060): while the missile descends on its arc (past the apex) it shows the
// FREEFALL model condition (its draw module switches to the re-entry glow) and plays BallisticDiveFX every
// BallisticDiveFXInterval (a light pulse lighting the ground under the dive). Client-only effects; nothing is saved
// (the condition is recomputed each frame, the FX follows the game frame number).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::updateBallisticDive()
{
	Object* obj = getObject();
	Bool diving = FALSE;
	if (m_arcSegment == BALLISTIC_ARC)
	{
		Real kUp, kDown, uApex;
		getBallisticArcShape(&kUp, &kDown, &uApex);
		diving = m_arcProgress > uApex;
	}
	const Drawable* draw = obj->getDrawable();
	const Bool shown = draw != nullptr && draw->getModelConditionFlags().test(MODELCONDITION_FREEFALL);
	if (diving != shown)
	{
		if (diving)
			obj->setModelConditionState(MODELCONDITION_FREEFALL);
		else
			obj->clearModelConditionState(MODELCONDITION_FREEFALL);
	}

	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();

	// Hypersonic @feature dive sound (D-063): each diving missile carries its own BallisticDiveSound, attached to it, and
	// its flight loop (the drawable's ambient sound) falls silent. A sound culled because the listener was too far is
	// started again every half second while the dive lasts. Client audio only: nothing in the logic reads it.
	if (diving)
		keepDiveSoundPlaying();

	// Hypersonic @feature booster drop (D-182): once, when the dive starts. Logic objects (debris), so the trigger is the
	// logic arc progress above, not the drawable, and the flag is saved.
	if (diving && !m_diveOCLDone)
	{
		m_diveOCLDone = TRUE;
		if (d->m_ballisticDiveOCL != nullptr)
			ObjectCreationList::create(d->m_ballisticDiveOCL, obj, nullptr);
	}

	if (diving && d->m_ballisticDiveFX != nullptr)
	{
		const UnsignedInt interval = max((UnsignedInt)1, d->m_ballisticDiveFXInterval);
		if (TheGameLogic->getFrame() % interval == 0)
			FXList::doFXObj(d->m_ballisticDiveFX, obj);
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature dive sound (D-063; D-161 also for a DiveCurve dive): start this missile's own BallisticDiveSound
// (its drawable's ambient sound falls silent) or start it again if it was culled. Owned by the missile, so
// stopBallisticDiveSound() kills it at once at the detonation or when the missile is shot down / removed.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::keepDiveSoundPlaying()
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	if (TheAudio == nullptr || d->m_ballisticDiveSound.getEventName().isEmpty())
		return;
	Object* obj = getObject();
	Drawable* ambientDraw = obj->getDrawable();
	if (ambientDraw != nullptr && ambientDraw->getAmbientSoundEnabled())
		ambientDraw->enableAmbientSound(FALSE);
	const Bool started = m_diveSound.getEventName().isNotEmpty();
	if (!started || (TheGameLogic->getFrame() % (LOGICFRAMES_PER_SECOND / 2) == 0 && !m_diveSound.isCurrentlyPlaying()))
	{
		m_diveSound = d->m_ballisticDiveSound;
		m_diveSound.setObjectID(obj->getID());
		m_diveSound.setPlayingHandle(TheAudio->addAudioEvent(&m_diveSound));
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature dive sound (D-063): stop this missile's dive sound (detonation, removal).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::stopBallisticDiveSound()
{
	if (TheAudio != nullptr && m_diveSound.getEventName().isNotEmpty())
		TheAudio->killAudioEventImmediately(m_diveSound.getPlayingHandle());
	m_diveSound.setEventName(AsciiString::TheEmptyString);
}

//-------------------------------------------------------------------------------------------------
// The arc z(u) = apex - k (u - uApex)^2 through m_arcStart (u = 0) and m_arcEnd (u = 1): one parabola (D-042), or with
// a BallisticApexShift (D-060) two half-parabolas meeting level at an apex moved later along the arc, kUp before it and
// kDown after it (flatter climb, steeper terminal dive; the same curve as D-042 when the shift is 0).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::getBallisticArcShape(Real *kUp, Real *kDown, Real *uApex) const
{
	const Real a = sqrtf(max(0.0f, m_arcApexZ - m_arcStart.z));
	const Real b = sqrtf(max(0.0f, m_arcApexZ - m_arcEnd.z));
	*kUp = 0.0f;
	*kDown = 0.0f;
	*uApex = 0.5f;
	if (a + b > 0.001f)
	{
		const Real u = ballisticApexFraction(a, b, getMissileAIUpdateModuleData()->m_ballisticApexShift);
		*uApex = u;
		if (u > 0.0001f)
			*kUp = sqr(a / u);
		if (u < 0.9999f)
			*kDown = sqr(b / (1.0f - u));
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature straighter dive (D-197): past the apex, with t = (u - uApex) / (1 - uApex) and the drop
// H = apex - end height, the dive is the quadratic Bezier (t, f)(s) = (2c s (1 - s) + s^2, s^2) from the apex (level,
// control point (c, 0)) to the aim point (1, 1), z = apex - H f. It bows at most c / 2 x H (vertically) above the
// straight line from the apex to the aim point; c = 0.5 is exactly the D-060 half-parabola f = t^2 (kept on the old formula, bit for bit). s(t) is the
// root of (1 - 2c) s^2 + 2c s - t = 0 in the cancellation-free form t / (c + sqrt(c^2 + (1 - 2c) t)); df/dt =
// s / (c + (1 - 2c) s). Only sqrt (IEEE exact), no pow: the same on every machine.
//-------------------------------------------------------------------------------------------------
static Bool ballisticDiveBezier(Real bulge, Real *c)
{
	*c = min(0.98f, max(0.02f, 2.0f * bulge));
	return bulge != 0.25f;
}

static Real ballisticDiveBezierS(Real t, Real c)
{
	t = min(1.0f, max(0.0f, t));
	return t / (c + sqrtf(max(0.0f, c * c + (1.0f - 2.0f * c) * t)));
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::getBallisticArcPoint(Real progress, Coord3D *pos, Vector3 *tangent) const
{
	Real kUp, kDown, uApex;
	getBallisticArcShape(&kUp, &kDown, &uApex);
	const Real k = (progress < uApex) ? kUp : kDown;

	const Real dx = m_arcEnd.x - m_arcStart.x;
	const Real dy = m_arcEnd.y - m_arcStart.y;
	pos->x = m_arcStart.x + dx * progress;
	pos->y = m_arcStart.y + dy * progress;
	pos->z = m_arcApexZ - k * sqr(progress - uApex);
	Real c;
	if (progress >= uApex && uApex < 0.9999f && ballisticDiveBezier(getMissileAIUpdateModuleData()->m_ballisticDiveBulge, &c))
	{
		const Real s = ballisticDiveBezierS((progress - uApex) / (1.0f - uApex), c);
		pos->z = m_arcApexZ - max(0.0f, m_arcApexZ - m_arcEnd.z) * s * s;
	}
	if (tangent)
	{
		tangent->Set(dx, dy, getBallisticArcSlope(progress, kUp, kDown, uApex));
		tangent->Normalize();
	}
}

//-------------------------------------------------------------------------------------------------
// dz/du of the arc at 'progress' (D-042 parabola, D-060 halves, D-197 Bezier dive).
//-------------------------------------------------------------------------------------------------
Real MissileAIUpdate::getBallisticArcSlope(Real progress, Real kUp, Real kDown, Real uApex) const
{
	Real c;
	if (progress >= uApex && uApex < 0.9999f && ballisticDiveBezier(getMissileAIUpdateModuleData()->m_ballisticDiveBulge, &c))
	{
		const Real s = ballisticDiveBezierS((progress - uApex) / (1.0f - uApex), c);
		return -max(0.0f, m_arcApexZ - m_arcEnd.z) * s / ((c + (1.0f - 2.0f * c) * s) * (1.0f - uApex));
	}
	return -2.0f * ((progress < uApex) ? kUp : kDown) * (progress - uApex);
}

//-------------------------------------------------------------------------------------------------
// The progress after flying 'dist' along the arc from 'progress' (arc length per unit progress, midpoint rule).
//-------------------------------------------------------------------------------------------------
Real MissileAIUpdate::advanceBallisticArc(Real progress, Real dist) const
{
	Real kUp, kDown, uApex;
	getBallisticArcShape(&kUp, &kDown, &uApex);
	const Real horizSqr = max(1.0f, sqr(m_arcEnd.x - m_arcStart.x) + sqr(m_arcEnd.y - m_arcStart.y));

	Real rate = sqrtf(horizSqr + sqr(getBallisticArcSlope(progress, kUp, kDown, uApex)));
	Real step = dist / rate;
	const Real mid = progress + 0.5f * step;
	rate = sqrtf(horizSqr + sqr(getBallisticArcSlope(mid, kUp, kDown, uApex)));
	step = dist / rate;

	Real next = progress + step;
	if (next > 1.0f)
		next = 1.0f;
	return next;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature vertical launch (D-051): the point 'dist' along the pitch-over circle (0 = the top of the climb,
// heading straight up) and its unit direction; the circle turns toward the target in the arc's vertical plane.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::getPitchOverPoint(Real dist, Coord3D *pos, Vector3 *tangent) const
{
	Real hx = m_arcEnd.x - m_climbStart.x;
	Real hy = m_arcEnd.y - m_climbStart.y;
	const Real hl = sqrtf(sqr(hx) + sqr(hy));
	if (hl > 0.001f)
	{
		hx /= hl;
		hy /= hl;
	}
	else
	{
		hx = 1.0f;
		hy = 0.0f;
	}

	*pos = m_climbStart;
	pos->z += m_climbHeight;
	Real sinA = 0.0f;
	Real cosA = 1.0f;
	if (m_pitchRadius > 0.0f && dist > 0.0f)
	{
		const Real angle = dist / m_pitchRadius;
		sinA = Sin(angle);
		cosA = Cos(angle);
		const Real run = m_pitchRadius * (1.0f - cosA);
		pos->x += hx * run;
		pos->y += hy * run;
		pos->z += m_pitchRadius * sinA;
	}
	if (tangent)
		tangent->Set(hx * sinA, hy * sinA, cosA);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature vertical launch (D-051): the point (and direction) of a flight state.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::getBallisticFlightPoint(const BallisticFlight &f, Coord3D *pos, Vector3 *tangent) const
{
	if (f.segment == BALLISTIC_CLIMB)
	{
		*pos = m_climbStart;
		pos->z += f.param;
		if (tangent)
			tangent->Set(0.0f, 0.0f, 1.0f);
	}
	else if (f.segment == BALLISTIC_PITCH)
	{
		getPitchOverPoint(f.param, pos, tangent);
	}
	else
	{
		getBallisticArcPoint(f.param, pos, tangent);
	}
}

//-------------------------------------------------------------------------------------------------
// Normalize a flight direction for buildTransformMatrix; a (nearly) vertical one keeps the heading to the target
// (buildTransformMatrix alone would face it east).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::orientBallistic(Vector3 *dir) const
{
	const Real horiz = sqr(dir->X) + sqr(dir->Y);
	if (horiz <= 0.000001f * sqr(dir->Z))
	{
		Real hx = m_arcEnd.x - m_climbStart.x;
		Real hy = m_arcEnd.y - m_climbStart.y;
		const Real hl = sqrtf(sqr(hx) + sqr(hy));
		if (hl > 0.001f)
		{
			hx /= hl;
			hy /= hl;
		}
		else
		{
			hx = 1.0f;
			hy = 0.0f;
		}
		dir->Set(0.001f * hx, 0.001f * hy, dir->Z >= 0.0f ? 1.0f : -1.0f);
	}
	dir->Normalize();
}

//-------------------------------------------------------------------------------------------------
// The speed after one more frame: it grows by the locomotor's acceleration up to its speed (Engine Improvement
// switches the locomotor), times 'factor' for a releasing missile (D-162, D-215; 'fromFactor': its factor while
// releasing).
//-------------------------------------------------------------------------------------------------
Real MissileAIUpdate::nextBallisticSpeed(Real speed, Real factor, Real fromFactor) const
{
	const Locomotor* loco = getCurLocomotor();
	if (loco)
	{
		const BodyDamageType state = getObject()->getBodyModule()->getDamageState();
		Real maxSpeed = loco->getMaxSpeedForCondition(state);
		const Real accel = loco->getMaxAcceleration(state);
		// Hypersonic @feature one-by-one release (D-162): a releasing missile slows to a fraction of its top speed,
		// shedding the difference evenly within one second
		if (factor < 1.0f)
		{
			const Real slow = maxSpeed * factor;
			if (speed > slow)
				return max(slow, speed - (maxSpeed - slow) / (Real)LOGICFRAMES_PER_SECOND);
			maxSpeed = slow;
		}
		// Hypersonic @feature final speed (D-215): after its last release the missile speeds up beyond its top speed,
		// gaining the difference from its speed while releasing evenly within one second (at least its acceleration)
		else if (factor > 1.0f)
		{
			const Real fast = maxSpeed * factor;
			if (speed < fast)
				speed = min(fast, speed + max(accel, (fast - maxSpeed * min(1.0f, fromFactor)) / (Real)LOGICFRAMES_PER_SECOND));
			return speed;
		}
		if (speed < maxSpeed)
		{
			Real a = accel;
			// Hypersonic @feature slow lift-off (D-060): below BallisticLiftOffSpeed the acceleration grows with the
			// speed, so the missile rises slowly off its launcher, then speeds up (at least 2%, so it never stalls)
			const Real liftOff = getMissileAIUpdateModuleData()->m_ballisticLiftOffSpeed;
			if (liftOff > 0.0f && speed < liftOff)
				a *= max(0.02f, speed / liftOff);
			speed = (a > 0.0f) ? min(maxSpeed, speed + a) : maxSpeed;
		}
	}
	return speed;
}

//-------------------------------------------------------------------------------------------------
// One logic frame of flight: the speed grows (nextBallisticSpeed), then the missile moves that far along its path,
// from the climb into the pitch-over into the arc (D-051). FALSE once it has arrived at the end of the arc.
//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::stepBallisticFlight(BallisticFlight *f, Coord3D *pos) const
{
	if (f->segment == BALLISTIC_ARC && f->param >= 1.0f)
		return FALSE;

	// Hypersonic @feature final speed (D-215): the factor switches to finalFactor after finalIn more frames
	Real factor = f->speedFactor;
	if (f->finalIn > 0)
		--f->finalIn;
	else if (f->finalIn == 0)
		factor = f->finalFactor;
	f->speed = nextBallisticSpeed(f->speed, factor, f->speedFactor);
	Real dist = f->speed;
	for (Int guard = 0; guard < 3 && dist > 0.0f; ++guard)
	{
		if (f->segment == BALLISTIC_CLIMB || f->segment == BALLISTIC_PITCH)
		{
			const Real length = (f->segment == BALLISTIC_CLIMB) ? m_climbHeight : m_pitchLength;
			const Real left = length - f->param;
			if (dist < left)
			{
				f->param += dist;
				dist = 0.0f;
			}
			else
			{
				dist -= max(0.0f, left);
				f->segment = (f->segment == BALLISTIC_CLIMB && m_pitchLength > 0.0f) ? BALLISTIC_PITCH : BALLISTIC_ARC;
				f->param = 0.0f;
			}
		}
		else
		{
			f->param = advanceBallisticArc(f->param, dist);
			dist = 0.0f;
		}
	}
	getBallisticFlightPoint(*f, pos, nullptr);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Move the missile one frame along its path: place it directly on the next point, facing along its step. Its
// physics must not move it again (Hypersonic @fix D-048: the old code left it on the previous point and let the physics
// integrate the step as velocity, so the drawn position lagged the arc progress by a frame), so the physics velocity
// is zeroed and gravity cancelled; getBallisticVelocity() gives readers (SAM lead, the fuse) the true motion.
// 'advance' FALSE holds it at its point (waiting for ignition). At the end of the arc it detonates on the aim point.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doBallisticFlightState(Bool advance)
{
	Object* obj = getObject();
	if (advance && m_arcSegment == BALLISTIC_ARC && m_arcProgress >= 1.0f)
	{
		obj->setPosition(&m_arcEnd);
		detonate();
		return;
	}

	BallisticFlight f;
	getBallisticFlight(&f);
	Coord3D here;
	Vector3 dir;
	getBallisticFlightPoint(f, &here, &dir);
	Coord3D next = here;
	if (advance)
	{
		stepBallisticFlight(&f, &next);
		m_arcSegment = f.segment;
		m_arcProgress = f.param;
		m_arcSpeed = f.speed;
		m_releaseFinalIn = f.finalIn;	// Hypersonic @feature final speed (D-215)
	}

	Vector3 step(next.x - here.x, next.y - here.y, next.z - here.z);
	if (step.Length2() > 0.0001f)
		dir = step;
	orientBallistic(&dir);
	Matrix3D xform;
	xform.buildTransformMatrix(Vector3(next.x, next.y, next.z), dir);
	obj->setTransformMatrix(&xform);
	updateBallisticDive();
	if (advance)
		updateClusterRelease();	// Hypersonic @feature one-by-one release (D-162)

	// Hypersonic @feature split warhead (D-062): past the apex, BallisticAirburstHeight above the aim point, the missile
	// bursts in the air with its weapon aimed at the aim point (a cluster weapon releases its warheads from here).
	// Not with a one-by-one release (D-162): that missile flies on to the aim point.
	const Real airburst = getMissileAIUpdateModuleData()->m_ballisticAirburstHeight;
	const Bool releasing = m_detonationWeaponTmpl != nullptr && m_detonationWeaponTmpl->isClusterReleaseWeapon();
	if (advance && airburst > 0.0f && !releasing && m_arcSegment == BALLISTIC_ARC && next.z - m_arcEnd.z <= airburst)
	{
		Real kUp, kDown, uApex;
		getBallisticArcShape(&kUp, &kDown, &uApex);
		if (m_arcProgress > uApex)
		{
			detonateAt(&m_arcEnd);
			return;
		}
	}

	PhysicsBehavior* physics = obj->getPhysics();
	if (physics == nullptr)
		return;
	Coord3D stop = *physics->getVelocity();
	stop.x = -stop.x;
	stop.y = -stop.y;
	stop.z = -stop.z;
	physics->addVelocityTo(&stop);
	physics->clearAcceleration();
	// cancel gravity; as a motive force it also keeps the ground friction's forward drag off near the ground
	Coord3D lift;
	lift.x = 0.0f;
	lift.y = 0.0f;
	lift.z = -TheGlobalData->m_gravity * physics->getMass();
	physics->applyMotiveForce(&lift);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature one-by-one release (Generals Hypersonic D-162): a missile whose weapon has ClusterReleaseInterval
// (Weapon.h isClusterReleaseWeapon) does not burst. On the first frame past its apex it plans the releases: it lands on
// its aim point in 'left' frames (its own arc, stepped as it flies); the ClusterCount warheads leave
// ClusterReleaseInterval apart, the last ClusterReleaseLead before that landing; when the descent is shorter than that
// (a short shot), interval and lead shrink in proportion and the first leaves at once. Each warhead leaves from where the
// missile is then, with its vertical speed, and flies its own ballistic drop (DumbProjectileBehavior BallisticDrop) to
// its own point; the missile flies on and detonates on the aim point with ClusterCenterWeapon (detonateAt): it is the
// centre warhead, landing last.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::updateClusterRelease()
{
	const WeaponTemplate* w = m_detonationWeaponTmpl;
	if (w == nullptr || !w->isClusterReleaseWeapon() || m_arcSegment != BALLISTIC_ARC)
		return;
	const Int count = w->getClusterCount();
	if (m_releaseCount >= count)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (m_releaseCount < 0)
	{
		// D-162 owner ("separation starts" right after launch): with ClusterReleaseStart the releases are planned on the
		// first frame of the arc (after the climb and pitch-over), else on the first frame past the apex
		const UnsignedInt early = w->getClusterReleaseStart();
		Real kUp, kDown, uApex;
		getBallisticArcShape(&kUp, &kDown, &uApex);
		if (early == 0 && m_arcProgress <= uApex)
			return;
		// owner (D-162): the missile slows down past the apex (nextBallisticSpeed eases it to this fraction of its top
		// speed within 1 s), so the faster warheads all land before it; the plan below steps that slower flight
		m_releaseSpeedFactor = min(1.0f, max(0.1f, w->getClusterReleaseCarrierSpeed()));
		// Hypersonic @feature final speed (D-215 owner): after the last release the missile, now the centre warhead,
		// speeds up to ClusterReleaseFinalSpeed. The switch is a countdown in its flight state (m_releaseFinalIn), so the
		// plan steps exactly the flight it will fly. The last release depends on the flight's length (early releases
		// are squeezed into its first half), which depends on when the speed-up starts: plan without the speed-up, then
		// again with it starting after the planned last release, until that frame stays the same (at most 8 passes).
		const Real finalFactor = w->getClusterReleaseFinalSpeed();
		m_releaseFinalFactor = (finalFactor > 0.0f) ? max(0.1f, finalFactor) : 0.0f;
		// Hypersonic @feature cone release (D-267): with ClusterReleaseConeSpeed all warheads leave in the same frame
		const Bool together = w->getClusterReleaseConeSpeed() > 0.0f;
		const Real interval = together ? 0.0f : (Real)w->getClusterReleaseInterval();
		Int finalIn = -1;
		UnsignedInt left = 1;
		Real start = 0.0f;
		for (Int pass = 0; ; ++pass)
		{
			// frames to the landing: it steps once per frame until it is at the end, and detonates on the frame after
			BallisticFlight f;
			getBallisticFlight(&f);
			f.finalIn = finalIn;
			Coord3D p;
			left = 1;
			while (left < 30000 && stepBallisticFlight(&f, &p))
				++left;
			if (early > 0)
			{
				// early: the first release 'early' into the arc, all within the first half of the remaining flight
				const Real half = 0.5f * (Real)left;
				start = min((Real)early, half);
				m_releaseInterval = interval;
				if (count > 1 && start + (Real)(count - 1) * interval > half)
					m_releaseInterval = max(1.0f, (half - start) / (Real)(count - 1));
			}
			else
			{
				const Real lead = (Real)w->getClusterReleaseLead();
				const Real need = (Real)(count - 1) * interval + lead;
				Real scale = 1.0f;
				if (need > (Real)left && need > 0.0f)
					scale = (Real)left / need;
				m_releaseInterval = together ? 0.0f : max(1.0f, interval * scale);
				start = max(0.0f, (Real)left - lead * scale - (Real)(count - 1) * m_releaseInterval);
			}
			if (m_releaseFinalFactor <= 0.0f || pass >= 7)
				break;
			// the last release leaves after the step 'lastAt' frames on; the speed-up starts with the step after it
			const Int lastAt = REAL_TO_INT_FLOOR(start + 0.5f) + REAL_TO_INT_FLOOR((Real)(count - 1) * m_releaseInterval + 0.5f);
			if (lastAt == finalIn)
				break;
			finalIn = lastAt;
		}
		m_releaseFinalIn = finalIn;
		m_releaseStartFrame = now + (UnsignedInt)REAL_TO_INT_FLOOR(start + 0.5f);
		m_releaseImpactFrame = now + left;
		m_releaseTurn = GameLogicRandomValueReal(0.0f, 2.0f * PI);
		m_releaseCount = 0;
	}
	while (m_releaseCount < count
		&& now >= m_releaseStartFrame + (UnsignedInt)REAL_TO_INT_FLOOR((Real)m_releaseCount * m_releaseInterval + 0.5f))
	{
		releaseClusterWarhead();
	}
	// Hypersonic @feature terminal cluster release (D-245): with ClusterReleaseCarrierVanishes the carrier is gone once
	// its last warhead has left (no centre warhead)
	if (m_releaseCount >= count && w->getClusterReleaseCarrierVanishes())
		vanishAfterRelease();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature terminal cluster release (Generals Hypersonic D-245, Qadr): the carrier has released all its
// warheads in the dive (ClusterReleaseStart 0: past the apex, the last ClusterReleaseLead before its own landing) and
// is removed at once without a blast or damage: its dive sound stops, ClusterReleaseVanishFX plays where it is, it is
// hidden and leaves through KILL_SELF like a detonated missile. D-245 Update 8: MASKED prevents new targeting,
// NO_COLLISIONS prevents contacts; already-fired weapons treat the spent, masked carrier as gone.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::vanishAfterRelease()
{
	m_isArmed = FALSE; // D-245: the vanished carrier must never detonate
	Object* obj = getObject();
	stopBallisticDiveSound();
	obj->setStatus( MAKE_OBJECT_STATUS_MASK3( OBJECT_STATUS_MISSILE_KILLING_SELF, OBJECT_STATUS_MASKED, OBJECT_STATUS_NO_COLLISIONS ) );
	const FXList* fx = m_detonationWeaponTmpl != nullptr ? m_detonationWeaponTmpl->getClusterReleaseVanishFX() : nullptr;
	if (fx != nullptr)
		FXList::doFXPos(fx, obj->getPosition());
	if (obj->getDrawable())
		obj->getDrawable()->setDrawableHidden(true);
	switchToState(KILL_SELF);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-162) release warhead number m_releaseCount. Its point: one of ClusterCount slots evenly round a
// ring at 85-100% of ClusterRadius about the aim point (random turn, small jitter), plus its own miss within the
// warhead weapon's ScatterRadius (sideways at most half the slot spacing), as launchClusterWarheads (D-062); the slots
// are taken farthest along the flight first, so the early warheads, released high and far back, get the long flights
// and the late ones, released low near the target, the short ones. Its landing: the warheads land one after another,
// 75% of the release interval apart, the last that gap before the missile itself, with a horizontal speed of 50-160% of
// the warhead weapon's speed (not for early releases, ClusterReleaseStart: they fly exactly the planned time); never
// after the missile.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::releaseClusterWarhead()
{
	const WeaponTemplate* w = m_detonationWeaponTmpl;
	const Int count = w->getClusterCount();
	const Int k = m_releaseCount++;
	const WeaponTemplate* sub = w->getClusterWeapon();
	if (sub == nullptr || count <= 0)
		return;
	Object* obj = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();

	// the k-th slot farthest along the flight (insertion sort by the slot's reach along the heading: deterministic)
	enum { MAX_SLOTS = 64 };
	const Int slots = min(count, (Int)MAX_SLOTS);
	const Real heading = (Real)atan2(m_arcEnd.y - m_arcStart.y, m_arcEnd.x - m_arcStart.x);
	Int order[MAX_SLOTS];
	Real reach[MAX_SLOTS];
	Int i;
	for (i = 0; i < slots; ++i)
	{
		order[i] = i;
		reach[i] = Cos(m_releaseTurn + 2.0f * PI * (Real)i / (Real)count - heading);
	}
	for (i = 1; i < slots; ++i)
	{
		const Int t = order[i];
		Int j = i - 1;
		while (j >= 0 && reach[order[j]] < reach[t])
		{
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = t;
	}
	const Int slot = order[min(k, slots - 1)];

	const Real radius = w->getClusterRadius();
	const Real r = radius * GameLogicRandomValueReal(0.85f, 1.0f);
	const Real a = m_releaseTurn + 2.0f * PI * (Real)slot / (Real)count + GameLogicRandomValueReal(-0.04f, 0.04f);
	Real dr = 0.0f;
	Real dt = 0.0f;
	const Real scatter = sub->getScatterRadius();
	if (scatter > 0.0f)
	{
		const Real sr = scatter * sqrtf(GameLogicRandomValueReal(0.0f, 1.0f));
		const Real sa = GameLogicRandomValueReal(0.0f, 2.0f * PI);
		dr = sr * Cos(sa);
		dt = sr * Sin(sa);
		const Real limit = r * (Real)fabs(Sin(PI / (Real)count));
		if (dt > limit)
			dt = limit;
		else if (dt < -limit)
			dt = -limit;
	}
	Coord3D point;
	point.x = m_arcEnd.x + (r + dr) * Cos(a) - dt * Sin(a);
	point.y = m_arcEnd.y + (r + dr) * Sin(a) + dt * Cos(a);
	point.z = TheTerrainLogic->getGroundHeight(point.x, point.y);

	const Coord3D* here = obj->getPosition();
	const Real dist = sqrtf(sqr(point.x - here->x) + sqr(point.y - here->y));
	const Real speed = max(0.01f, sub->getWeaponSpeed());
	const Real gap = max(1.0f, 0.75f * m_releaseInterval);
	// owner (D-162): a warhead speeds up as it separates: at least 1.4x the missile's own horizontal speed then
	Coord3D vel;
	getBallisticVelocity(&vel);
	const Real carrier = sqrtf(sqr(vel.x) + sqr(vel.y));
	const Real slowest = min(1.6f * speed, max(0.5f * speed, 1.4f * carrier));
	Real frames = (Real)m_releaseImpactFrame - gap * (Real)(count - k) - (Real)now;
	if (w->getClusterReleaseStart() == 0)	// early releases (D-162) fly exactly their planned time: landings in order
	{
		frames = max(frames, dist / (1.6f * speed));
		frames = min(frames, dist / slowest);
	}
	if ((Real)now + frames >= (Real)m_releaseImpactFrame)
		frames = (Real)m_releaseImpactFrame - (Real)now - 1.0f;
	UnsignedInt flightFrames = (UnsignedInt)max((Int)2, (Int)REAL_TO_INT_CEIL(frames));
	Real startVZ = vel.z;

	// Hypersonic @feature drop time (Generals Hypersonic D-261, Qadr, owner: "splitting and falling", not boosting down):
	// with ClusterReleaseDropTime each warhead leaves level (start vertical speed 0: the D-062 drop arc, steepest at the
	// ground) and falls exactly that long, wherever its spot is; it may land after the missile would have. Only a
	// vanishing carrier (ClusterReleaseCarrierVanishes) uses it, so nothing waits on the missile's own landing: the
	// impact frame becomes the last warhead's landing (the aim-point reveal lingers until then, endTargetReveal).
	// Not for leftovers released on the ground (tossExhaust sets the impact 3 frames ahead): they keep the short timing.
	const UnsignedInt dropFrames = w->getClusterReleaseDropTime();
	Coord3D startVel;
	Bool coneRelease = FALSE;
	if (dropFrames > 0 && m_releaseImpactFrame > now + 3)
	{
		flightFrames = max((UnsignedInt)2, dropFrames);
		startVZ = 0.0f;

		// Hypersonic @feature cone release (Generals Hypersonic D-267, Qadr, owner: "just try to make it realistic"): a real
		// cluster warhead opens at speed and its submunitions carry on along the flight in a widening cone, slowed a little by
		// drag and bent down by gravity. Each warhead flies from the carrier to its spot at about ClusterReleaseConeSpeed (the
		// farther the spot, the later it lands; a fixed pattern of +-10% so they do not move as one, no random numbers: the
		// logic RNG stream stays). Its start blends the carrier's velocity with the straight line to its spot, and starts
		// 15% shallower so that the path droops. Same spots; only the path and the landing frame change.
		const Real coneSpeed = w->getClusterReleaseConeSpeed();
		if (coneSpeed > 0.0f && count > 1)
		{
			const Real step = (Real)((k * 7) % count) / (Real)(count - 1);	// 0..1, mixed along the slots
			Coord3D to;
			to.x = point.x - here->x;
			to.y = point.y - here->y;
			to.z = point.z - here->z;
			const Real frames3D = to.length() / (coneSpeed * (0.9f + 0.2f * step));
			flightFrames = (UnsignedInt)max((Int)2, (Int)REAL_TO_INT_CEIL(frames3D));
			const Real inherit = w->getClusterReleaseConeInherit();
			const Real straight = 1.0f / (Real)flightFrames;
			startVel.x = inherit * vel.x + (1.0f - inherit) * to.x * straight;
			startVel.y = inherit * vel.y + (1.0f - inherit) * to.y * straight;
			startVel.z = 0.85f * (inherit * vel.z + (1.0f - inherit) * to.z * straight);
			startVZ = startVel.z;
			coneRelease = TRUE;
		}
		m_releaseImpactFrame = max(m_releaseImpactFrame, now + flightFrames);
	}

	if (coneRelease)
		w->releaseClusterWarhead(obj, m_launcherID, &point, flightFrames, startVZ, &startVel);
	else
		w->releaseClusterWarhead(obj, m_launcherID, &point, flightFrames, startVZ);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @fix (D-048): the arc missile's motion per frame, the step it flies next (zero while it waits on the
// launcher). The missile stands on the point of its flight state (doBallisticFlightState), climb and pitch-over
// included (D-051).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::getBallisticVelocity(Coord3D *vel) const
{
	vel->zero();
	if (!isOnBallisticArc() || m_state < ATTACK_NOTURN)
		return;
	BallisticFlight f;
	getBallisticFlight(&f);
	Coord3D here;
	Coord3D next;
	getBallisticFlightPoint(f, &here, nullptr);
	if (!stepBallisticFlight(&f, &next))
		return;
	vel->x = next.x - here.x;
	vel->y = next.y - here.y;
	vel->z = next.z - here.z;
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::doDeadState()
{
	Locomotor* curLoco = getCurLocomotor();
	if (curLoco)
	{
		curLoco->setMaxAcceleration(0);
		curLoco->setMaxTurnRate(0);
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Simulate one frame of a missile's behavior
 */
UpdateSleepTime MissileAIUpdate::update()
{
	// Hypersonic @feature terminal steering (D-228): the boost is worked out anew each frame (applyTerminalTurnBoost)
	if( getMissileAIUpdateModuleData()->m_terminalTurnBoost > 1.0f && getCurLocomotor() )
		getCurLocomotor()->setTurnRateBoost( 1.0f );

	Coord3D newPos = *getObject()->getPosition();
	if (m_noTurnDistLeft > 0.0f && m_state >= IGNITION)
	{
		Real distThisTurn = sqrtf(sqr(newPos.x-m_prevPos.x) + sqr(newPos.y-m_prevPos.y) + sqr(newPos.z-m_prevPos.z));
		m_noTurnDistLeft -= distThisTurn;
		m_prevPos = newPos;
	}

	//If this missile has been marked to divert to countermeasures, check when
	//that will occur, then do it when the timer expires.
	if( m_framesTillDecoyed && m_framesTillDecoyed <= TheGameLogic->getFrame() )
	{
		Object *missile = getObject();
		m_framesTillDecoyed = 0;
		m_noDamage = TRUE;
		Object *victim = TheGameLogic->findObjectByID( m_victimID );
		if( victim )
		{
			ObjectID targetID = missile->calculateCountermeasureToDivertTo( *victim );
			if( targetID != INVALID_ID )
			{
				victim = TheGameLogic->findObjectByID( targetID );
				getStateMachine()->setGoalPosition(victim->getPosition());
				// ick. const-cast is evil. fix. (srj)
 				aiMoveToObject(const_cast<Object*>(victim), CMD_FROM_AI );
				m_originalTargetPos = *victim->getPosition();
				m_isTrackingTarget = TRUE;// Remember that I was originally shot at a moving object, so if the
				// target dies I can do something cool.
				m_victimID = victim->getID();
				m_isLeadIntercept = FALSE; // Hypersonic: decoyed, chase the decoy the stock way
				m_lastTargetPos = m_originalTargetPos;	// Hypersonic @feature retarget (D-228)
			}
		}
	}

	if (newPos.z < 0)
	{
		// we ended up under the world.  go away.
		TheGameLogic->destroyObject(getObject());
		return UPDATE_SLEEP_FOREVER;
	}
	switch( m_state )
	{
		case PRELAUNCH:
			doPrelaunchState();
			break;

		case LAUNCH:
			doLaunchState();
			// special case!
			if (m_state == IGNITION)
			{
				// just fall thru
			}
			else
			{
				break;
			}
			FALLTHROUGH;

		case IGNITION:
			doIgnitionState();
			if (m_isBallistic && m_state == ATTACK_NOTURN)
				doBallisticFlightState(TRUE);	// Hypersonic @feature ballistic arc (D-042): away at once
			break;

		case ATTACK_NOTURN:
			if (m_isBallistic)
				doBallisticFlightState(TRUE);	// Hypersonic @feature ballistic arc (D-042)
			else if (m_isLeadIntercept)
				doLeadInterceptState(false);	// Hypersonic @feature SAM lead pursuit (D-038)
			else
				doAttackState(false);
			break;

		case ATTACK:
			if (m_isBallistic)
				doBallisticFlightState(TRUE);	// Hypersonic @feature ballistic arc (D-042)
			else if (m_isLeadIntercept)
				doLeadInterceptState(true);	// Hypersonic @feature SAM lead pursuit (D-038)
			else
				doAttackState(true);
			break;

		case KILL:
			doKillState();
			break;

		case KILL_SELF:
			doKillSelfState();
			break;

		case DEAD:
			doDeadState();
			break;
	}

	// Hypersonic @feature ballistic arc (D-042): on its arc the missile is moved by doBallisticFlightState alone; the AI
	// has no move, and its locomotor must not push it (idle, it would try to hold position).
	if (m_isBallistic && m_state == LAUNCH)
		doBallisticFlightState(FALSE);
	if (!isOnBallisticArc())
		/*UpdateSleepTime ret =*/ AIUpdateInterface::update();

#if 1
	// srj sez: doh, why was this never in place? I guess 'cuz so few "smart" missiles ever go in
	// a downward-towards-ground attitude. however, comanche rocket pod missiles do, so this fix
	// is needed for them to target bridges correctly.

	// note that we want to use getHighestLayerForDestination() here, so that anything even slightly
	// below the bridge translates into GROUND. (getLayerForDestination just does a "closest" check)
	PathfindLayerEnum oldLayer = getObject()->getLayer();
	PathfindLayerEnum newLayer = TheTerrainLogic->getHighestLayerForDestination(getObject()->getPosition());
	getObject()->setLayer(newLayer);

	if (projectileIsArmed() && oldLayer != LAYER_GROUND && newLayer == LAYER_GROUND)
	{
		// see if we' still in the bridge's xy area
		Coord3D tmp = *getObject()->getPosition();
		tmp.z = 9999.0f;
		PathfindLayerEnum testLayer = TheTerrainLogic->getHighestLayerForDestination(&tmp);
		if (testLayer == oldLayer)
		{
			// ensure we are slightly above the bridge, to account for fudge & sloppy art
			const Real FUDGE = 2.0f;
			tmp.z = TheTerrainLogic->getLayerHeight(tmp.x, tmp.y, testLayer) + FUDGE;
			getObject()->setPosition(&tmp);
			// blow'd up!
			detonate();
			return UPDATE_SLEEP_NONE;
		}
	}
#endif

	//return (mine < ret) ? mine : ret;
	/// @todo srj -- someday, make sleepy. for now, must not sleep.
	return UPDATE_SLEEP_NONE;
}


//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::processCollision(PhysicsBehavior *physics, Object *other)
/* Returns true if the physics collide should apply the force.  Normally not.
Also determines whether objects are blocked, and if so, if they are stuck.  jba.*/
{
	// Missiles dont' get blocked by other objects.
	return false;
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::airborneTargetGone()
{
	// Hypersonic @feature retarget (Generals Hypersonic D-228): a chasing missile with RetargetRange and fuel left flies
	// at the nearest valid enemy instead (it keeps the fuel it has left); with none it becomes a position shot at the
	// dead target's last spot (it detonates at the lock or on the ground there). Not for a SAM lead pursuit, an arc or
	// a decoyed (no damage) missile, and not once it has detonated (KILL_SELF / DEAD).
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	if( d->m_retargetRange > 0.0f && m_isTrackingTarget && !m_isLeadIntercept && !m_isBallistic && !m_noDamage
		&& TheGameLogic->getFrame() < m_fuelExpirationDate
		&& (m_state == ATTACK_NOTURN || m_state == ATTACK || m_state == KILL) )
	{
		Object *target = findRetarget();
		if( target != nullptr )
		{
			getStateMachine()->setGoalPosition( target->getPosition() );
			aiMoveToObject( target, CMD_FROM_AI );
			m_victimID = target->getID();
			m_isTrackingTarget = TRUE;
			m_originalTargetPos = *target->getPosition();
			m_lastTargetPos = m_originalTargetPos;
			m_diveCurveStart = 0.0f;	// the D-090 dive curve starts anew toward the new target
			getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
			if( m_state == KILL )
				switchToState( ATTACK );
			setCurrentVictim( target );
		}
		else
		{
			// as projectileNowJammed: drop the goal object and move to a position
			getStateMachine()->setGoalObject( nullptr );
			aiMoveToPosition( &m_lastTargetPos, CMD_FROM_AI );
			m_isTrackingTarget = FALSE;
			m_victimID = INVALID_ID;
			m_originalTargetPos = m_lastTargetPos;
			setCurrentVictim( nullptr );
		}
		return;
	}

	// I would really love it if this could retarget, but all of the targeting and legality is done
	// by the Weapon that fired me.  The safest thing for me to do in this state is to just run out of gas.
	m_fuelExpirationDate = TheGameLogic->getFrame();
	switchToState(KILL_SELF);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature retarget (D-228): the chased object no longer exists or, with RetargetRange, is dead (still in
// its death animation). Without RetargetRange this is the stock test (goal object gone).
//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::isTrackedTargetGone() const
{
	const Object *goal = getStateMachine()->getGoalObject();
	if( goal == nullptr )
		return TRUE;
	return getMissileAIUpdateModuleData()->m_retargetRange > 0.0f && goal->isEffectivelyDead();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature retarget (D-228): the nearest living enemy vehicle, infantry or structure within RetargetRange of
// the missile (2D), not airborne, not a projectile, not unattackable, not inside a container, not undetected stealth.
// Fog and shroud are not checked (owner rule: the nearest target). nullptr = none.
//-------------------------------------------------------------------------------------------------
Object *MissileAIUpdate::findRetarget() const
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	const Object *me = getObject();

	PartitionFilterRelationship filterEnemies( me, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive filterAlive;
	PartitionFilterSameMapStatus filterMapStatus( me );
	PartitionFilterStealthedAndUndetected filterStealth( me, FALSE );
	PartitionFilter *filters[] = { &filterEnemies, &filterAlive, &filterMapStatus, &filterStealth, nullptr };
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( me, d->m_retargetRange, FROM_BOUNDINGSPHERE_2D, filters, ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold( iter );

	for( Object *other = iter->first(); other; other = iter->next() )
	{
		if( !other->isKindOf( KINDOF_VEHICLE ) && !other->isKindOf( KINDOF_INFANTRY ) && !other->isKindOf( KINDOF_STRUCTURE ) )
			continue;
		if( other->isKindOf( KINDOF_PROJECTILE ) || other->isKindOf( KINDOF_UNATTACKABLE ) )
			continue;
		if( other->isAirborneTarget() || other->isContained() )
			continue;
		return other;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature retarget (D-228): remember where the living chased object is (the spot to dive onto if it dies
// and nothing else is in range).
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::updateLastTargetPos()
{
	if( getMissileAIUpdateModuleData()->m_retargetRange <= 0.0f || !m_isTrackingTarget )
		return;
	const Object *goal = getGoalObject();
	if( goal != nullptr && !goal->isEffectivelyDead() )
		m_lastTargetPos = *goal->getPosition();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature battery failure (D-228): out of fuel with a NoFuelDeathType, the missile is killed with it (its
// death modules show the failure, e.g. an intact falling model) instead of going limp. FALSE = stock (no death type).
//-------------------------------------------------------------------------------------------------
Bool MissileAIUpdate::dieOfNoFuel()
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	if( d->m_noFuelDeathType == DEATH_NONE )
		return FALSE;

	Object *obj = getObject();
	m_isArmed = FALSE;	// a contact later this frame must not detonate the dead drone
	stopBallisticDiveSound();
	tossExhaust();
	switchToState( DEAD );
	if( !obj->isEffectivelyDead() )
		obj->kill( DAMAGE_UNRESISTABLE, d->m_noFuelDeathType );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature terminal steering (D-228): within DistanceToTargetBeforeDiving, raise the locomotor's turn rate
// (up to TerminalTurnBoost x) to 1.25 x the rate that curves the missile onto its aim point: a circle through the
// missile, tangent to its nose, reaches a point at distance 'dist' and angle 'a' off the nose with the turn rate
// 2 x speed x sin(a) / dist (a point behind it counts as sin = 1). A target that dodges inside the normal turn circle
// is so curved onto instead of circled. The factor is reset to 1 each frame in update().
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::applyTerminalTurnBoost(Bool turnOK)
{
	const MissileAIUpdateModuleData* d = getMissileAIUpdateModuleData();
	Locomotor* curLoco = getCurLocomotor();
	if( d->m_terminalTurnBoost <= 1.0f || curLoco == nullptr || !turnOK || d->m_diveDistance <= 0.0f )
		return;

	Object *obj = getObject();
	Coord3D aim;
	const Object *goalObj = m_isTrackingTarget ? getGoalObject() : nullptr;
	if( goalObj != nullptr )
		goalObj->getGeometryInfo().getCenterPosition( *goalObj->getPosition(), aim );
	else
		aim = *getGoalPosition();

	const Coord3D *pos = obj->getPosition();
	const Real dx = aim.x - pos->x;
	const Real dy = aim.y - pos->y;
	const Real dz = aim.z - pos->z;
	if( dx * dx + dy * dy > d->m_diveDistance * d->m_diveDistance )
		return;
	const Real dist = sqrtf( dx * dx + dy * dy + dz * dz );

	const PhysicsBehavior *physics = obj->getPhysics();
	const Real speed = physics ? physics->getVelocityMagnitude() : 0.0f;	// per frame
	const Real base = curLoco->getMaxTurnRate( obj->getBodyModule()->getDamageState() );	// boost is 1 here
	if( dist < 1.0f || speed <= 0.0f || base <= 0.0f )
		return;

	Vector3 nose = obj->getTransformMatrix()->Get_X_Vector();
	nose.Normalize();
	const Real cosA = ( nose.X * dx + nose.Y * dy + nose.Z * dz ) / dist;
	const Real sinA = cosA <= 0.0f ? 1.0f : sqrtf( max( 0.0f, 1.0f - cosA * cosA ) );

	const Real needed = 1.25f * 2.0f * speed * sinA / dist;
	Real factor = needed / base;
	if( factor > d->m_terminalTurnBoost )
		factor = d->m_terminalTurnBoost;
	if( factor > 1.0f )
		curLoco->setTurnRateBoost( factor );
}

//-------------------------------------------------------------------------------------------------
// Set number of frames till missile diverts to countermeasures.
//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::setFramesTillCountermeasureDiversionOccurs( UnsignedInt frames )
{
	UnsignedInt now = TheGameLogic->getFrame();
	m_framesTillDecoyed = now + frames;
}

//-------------------------------------------------------------------------------------------------
void MissileAIUpdate::projectileNowJammed()
{
	if( m_isJammed )
		return; // Already jammed

	const MissileAIUpdateModuleData *data = getMissileAIUpdateModuleData();

	getObject()->setModelConditionState(MODELCONDITION_JAMMED);

	Coord3D targetPosition;
	// Hypersonic @feature a lead-pursuit interceptor (D-038) moves to a position, not to its victim; scatter around the
	// victim's current position, as a stock tracking missile does, not around the predicted intercept point.
	const Object *leadVictim = m_isLeadIntercept ? TheGameLogic->findObjectByID( m_victimID ) : nullptr;
	if( leadVictim != nullptr )
		targetPosition = *leadVictim->getPosition();
	else if( m_isTrackingTarget && getGoalObject() )
		targetPosition = *getGoalObject()->getPosition();
	else
		targetPosition = *getGoalPosition();

	Real scatter = data->m_distanceScatterWhenJammed;
	targetPosition.x += GameLogicRandomValue(-scatter, scatter);
	targetPosition.y += GameLogicRandomValue(-scatter, scatter);
	targetPosition.z = TheTerrainLogic->getLayerHeight(	targetPosition.x,
																											targetPosition.y,
																											TheTerrainLogic->getHighestLayerForDestination(&targetPosition) );

	getStateMachine()->setGoalObject(nullptr);
	// Projectiles are expressly forbidden from getting AIIdle.  Who am I to argue.
	// I need to do something though, because I can no longer give a new move command
	// while moving because of the big state machine crash fix.
	//
	aiMoveToPosition( &targetPosition, CMD_FROM_AI );

	m_isTrackingTarget = FALSE;
	m_isLeadIntercept = FALSE; // Hypersonic: jammed, no more lead pursuit
	m_isBallistic = FALSE; // Hypersonic: jammed, leave the arc; the locomotor flies it to the scattered point
	m_originalTargetPos = targetPosition;
	m_victimID = INVALID_ID;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MissileAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 7: Hypersonic @feature m_isLeadIntercept (SAM lead pursuit, D-038)
	* 8: Hypersonic @feature ballistic arc (D-042)
	* 9: Hypersonic @feature vertical launch (D-051)
	* 10: Hypersonic @feature smooth dive (D-090)
	* 11: Hypersonic @feature missile target reveal (D-143)
	* 12: Hypersonic @feature one-by-one release (D-162)
	* 13: Hypersonic @feature dive OCL (D-182)
	* 14: Hypersonic @feature final speed (D-215)
	* 15: Hypersonic @feature retarget (D-228) */
// ------------------------------------------------------------------------------------------------
void MissileAIUpdate::xfer( Xfer *xfer )
{
  // version
  const XferVersion currentVersion = 15;
  XferVersion version = currentVersion;
	// Hypersonic: the CRC (AIUpdateInterface::crc runs this xfer) keeps the stock version 6 layout while lead pursuit
	// and the ballistic arc (D-042) are off, so games and replays without them CRC exactly as before; saves always use
	// the current version.
	if( xfer->getXferMode() == XFER_CRC && !m_isLeadIntercept && !m_isBallistic )
		version = 6;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);

	if (version>=2) {
		xfer->xferCoord3D(&m_originalTargetPos);
	}

	xfer->xferUser(&m_state, sizeof(m_state));
	xfer->xferUnsignedInt(&m_stateTimestamp);
	xfer->xferUnsignedInt(&m_nextTargetTrackTime);
	xfer->xferObjectID(&m_launcherID);
	xfer->xferObjectID(&m_victimID);
	xfer->xferBool(&m_isArmed);
	xfer->xferUnsignedInt(&m_fuelExpirationDate);
	xfer->xferReal(&m_noTurnDistLeft);
	xfer->xferReal(&m_maxAccel);

	AsciiString weaponName;
	if (m_detonationWeaponTmpl)
	{
		weaponName = m_detonationWeaponTmpl->getName();
	}
	xfer->xferAsciiString(&weaponName);
	if (weaponName.isNotEmpty() && m_detonationWeaponTmpl == nullptr)
	{
		m_detonationWeaponTmpl = TheWeaponStore->findWeaponTemplate(weaponName);
	}

	AsciiString exhaustName;
	if (m_exhaustSysTmpl)
	{
		exhaustName = m_exhaustSysTmpl->getName();
	}
	xfer->xferAsciiString(&exhaustName);
	if (exhaustName.isNotEmpty() && m_exhaustSysTmpl == nullptr)
	{
		m_exhaustSysTmpl = TheParticleSystemManager->findTemplate(exhaustName);
	}

	xfer->xferBool(&m_isTrackingTarget);

	if (version >= 3)
	{
		xfer->xferCoord3D(&m_prevPos);
	}

	if (version >= 4)
	{
		xfer->xferUnsignedInt(&m_extraBonusFlags);
		xfer->xferUser( &m_exhaustID, sizeof( m_exhaustID ) );
	}

	if( version >= 5 )
	{
		xfer->xferUnsignedInt( &m_framesTillDecoyed );
		xfer->xferBool( &m_noDamage );
	}

	if( version>= 6 )
		xfer->xferBool( &m_isJammed );

	if( version >= 7 )
		xfer->xferBool( &m_isLeadIntercept );

	if( version >= 8 )
	{
		xfer->xferBool( &m_isBallistic );
		xfer->xferCoord3D( &m_arcStart );
		xfer->xferCoord3D( &m_arcEnd );
		xfer->xferReal( &m_arcApexZ );
		xfer->xferReal( &m_arcProgress );
		xfer->xferReal( &m_arcSpeed );
	}

	if( version >= 9 )
	{
		xfer->xferInt( &m_arcSegment );
		xfer->xferCoord3D( &m_climbStart );
		xfer->xferReal( &m_climbHeight );
		xfer->xferReal( &m_pitchRadius );
		xfer->xferReal( &m_pitchLength );
	}
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		// an older save: the arc without a climb
		m_arcSegment = BALLISTIC_ARC;
		m_climbStart = m_arcStart;
		m_climbHeight = 0.0f;
		m_pitchRadius = 0.0f;
		m_pitchLength = 0.0f;
	}

	if( version >= 10 )
		xfer->xferReal( &m_diveCurveStart );

	if( version >= 11 )
	{
		xfer->xferObjectID( &m_targetRevealID );
		xfer->xferUnsignedInt( &m_targetRevealLinger );
	}

	if( version >= 12 )
	{
		xfer->xferInt( &m_releaseCount );
		xfer->xferUnsignedInt( &m_releaseStartFrame );
		xfer->xferUnsignedInt( &m_releaseImpactFrame );
		xfer->xferReal( &m_releaseInterval );
		xfer->xferReal( &m_releaseTurn );
		xfer->xferReal( &m_releaseSpeedFactor );
	}

	if( version >= 13 )
		xfer->xferBool( &m_diveOCLDone );

	if( version >= 14 )
	{
		xfer->xferReal( &m_releaseFinalFactor );	// Hypersonic @feature final speed (D-215)
		xfer->xferInt( &m_releaseFinalIn );
	}

	if( version >= 15 )
		xfer->xferCoord3D( &m_lastTargetPos );	// Hypersonic @feature retarget (D-228)
	else if( xfer->getXferMode() == XFER_LOAD )
		m_lastTargetPos = m_originalTargetPos;

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MissileAIUpdate::loadPostProcess()
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}
