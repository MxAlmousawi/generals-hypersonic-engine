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

// FILE: CruiseFlightUpdate.cpp /////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Fire-and-forget flight of a launched drone (see CruiseFlightUpdate.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameAudio.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CruiseFlightUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

// terrain samples along the route when the cruise height is planned
static const Real ROUTE_SAMPLE_STEP = 20.0f;
// the climb must clear the terrain ahead by this share of the cruise clearance (at the end of the look-ahead;
// nearer points need proportionally less, so a launch from low ground does not force a steep climb)
static const Real CLIMB_CLEARANCE_SHARE = 0.5f;
// the cruise height only sinks once the terrain ahead is this much lower (no chasing small bumps)
static const Real DESCENT_DEADBAND = 10.0f;
// pitch rate factor while the terrain close ahead needs a steeper climb than the current pitch (a hill right after
// the launcher): pull up harder rather than fly into it
static const Real PULL_UP_RATE_FACTOR = 3.0f;
// a culled or finished engine sound is started again this often while flying
static const UnsignedInt ENGINE_SOUND_RETRY_FRAMES = LOGICFRAMES_PER_SECOND;
static const UnsignedInt ENGINE_SOUND_CROSSFADE_MS = 250;	// Hypersonic @feature (D-186) cruise -> dive sound
// steepest climb the planner asks for
static const Real MAX_CLIMB_ANGLE = PI / 3.0f;
// altitude hold: the drone aims at the cruise height this many frames of flight ahead (smooth level-off)
static const Real LEVEL_OFF_LOOKAHEAD_FRAMES = LOGICFRAMES_PER_SECOND * 3.0f;
// safety: a flight longer than this many times the straight route (plus a margin) ends where the drone is
static const Real MAX_FLIGHT_ROUTE_FACTOR = 3.0f;
static const UnsignedInt MAX_FLIGHT_MARGIN_FRAMES = LOGICFRAMES_PER_SECOND * 30;
// Hypersonic @feature smooth dive (D-090): the early (lead-in) dive start only once the drone is no longer climbing
// (a target close to the launcher keeps the old start: the impact point DiveAngle below the horizon)
static const Real DIVE_LEAD_MAX_CLIMB = PI / 36.0f;	// 5 degrees
// Hypersonic @feature curved dive (D-152): the drone aims at its point on the dive curve this many frames of flight ahead
static const Real DIVE_CURVE_HOLD_FRAMES = 10.0f;
// ... and in the last frames straight at the impact point (where the curve and the line of sight agree)
static const Real DIVE_CURVE_FINAL_FRAMES = 3.0f;
// Hypersonic @bugfix steep dive (D-230): a curve that began late (still climbing up a slope toward the impact point)
// is left for the straight, tightening dive once the impact point lies this far below the horizon (a normal curve
// stays near DiveCurveAngle)
static const Real DIVE_CURVE_MAX_LINE_OF_SIGHT = PI * 40.0f / 180.0f;
// Hypersonic @feature salvos (D-152): a drone that left its launcher this long ago no longer counts for a new salvo member
static const UnsignedInt SALVO_WINDOW_FRAMES = LOGICFRAMES_PER_SECOND * 3;
// ... impact point draws tried for the wanted spacing (the farthest one is kept when none reaches it)
static const Int SALVO_AIM_TRIES = 8;
// ... at most this many earlier members are compared
enum { SALVO_MAX_MEMBERS = 16 };

//-------------------------------------------------------------------------------------------------
static Real clampReal( Real v, Real lo, Real hi )
{
	return v < lo ? lo : (v > hi ? hi : v);
}

//-------------------------------------------------------------------------------------------------
// Terrain (or water surface) height at a point.
static Real getSurfaceHeight( Real x, Real y )
{
	Real ground = TheTerrainLogic->getGroundHeight( x, y );
	Real water = 0.0f;
	if( TheTerrainLogic->isUnderwater( x, y, &water ) && water > ground )
		return water;
	return ground;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CruiseFlightUpdateModuleData::CruiseFlightUpdateModuleData()
{
	m_speed = 0.0f;
	m_upgradedSpeed = 0.0f;
	m_cruiseClearance = 100.0f;
	m_cruiseLookAhead = 300.0f;
	m_descentAngle = PI / 18.0f;			// 10 degrees
	m_climbAngle = PI / 9.0f;					// 20 degrees
	m_diveAngle = PI * 35.0f / 180.0f;
	m_pitchRate = PI / 6.0f / LOGICFRAMES_PER_SECOND;	// 30 degrees per second
	m_turnRate = PI / 4.0f / LOGICFRAMES_PER_SECOND;	// 45 degrees per second
	m_diveEaseFrames = 0;
	m_diveCurveAngle = 0.0f;
	m_diveSpeedFactor = 1.0f;
	m_turnEaseFrames = 0;
	m_maxBankAngle = 0.0f;
	m_salvoGapFrames = 0;
	m_salvoRadius = 150.0f;
	m_salvoSpacing = 0.0f;
	m_prepareFrames = 0;
	m_warhead = nullptr;
	m_boostFrames = 0;
	m_boostFX = nullptr;
	m_boosterOCL = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Speed",							INI::parseVelocityReal,					nullptr, offsetof( CruiseFlightUpdateModuleData, m_speed ) },
		{ "UpgradedSpeed",			INI::parseVelocityReal,					nullptr, offsetof( CruiseFlightUpdateModuleData, m_upgradedSpeed ) },
		{ "SpeedUpgrade",				INI::parseAsciiString,					nullptr, offsetof( CruiseFlightUpdateModuleData, m_speedUpgradeName ) },
		{ "CruiseClearance",		INI::parseReal,									nullptr, offsetof( CruiseFlightUpdateModuleData, m_cruiseClearance ) },
		{ "CruiseLookAhead",		INI::parseReal,									nullptr, offsetof( CruiseFlightUpdateModuleData, m_cruiseLookAhead ) },
		{ "DescentAngle",				INI::parseAngleReal,						nullptr, offsetof( CruiseFlightUpdateModuleData, m_descentAngle ) },
		{ "ClimbAngle",					INI::parseAngleReal,						nullptr, offsetof( CruiseFlightUpdateModuleData, m_climbAngle ) },
		{ "DiveAngle",					INI::parseAngleReal,						nullptr, offsetof( CruiseFlightUpdateModuleData, m_diveAngle ) },
		{ "PitchRate",					INI::parseAngularVelocityReal,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_pitchRate ) },
		{ "TurnRate",						INI::parseAngularVelocityReal,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_turnRate ) },
		{ "DiveEaseTime",				INI::parseDurationUnsignedInt,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_diveEaseFrames ) },	// Hypersonic @feature (D-090)
		{ "DiveCurveAngle",			INI::parseAngleReal,						nullptr, offsetof( CruiseFlightUpdateModuleData, m_diveCurveAngle ) },	// Hypersonic @feature (D-152)
		{ "DiveSpeedFactor",		INI::parsePercentToReal,				nullptr, offsetof( CruiseFlightUpdateModuleData, m_diveSpeedFactor ) },	// Hypersonic @feature (D-152)
		{ "TurnEaseTime",				INI::parseDurationUnsignedInt,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_turnEaseFrames ) },	// Hypersonic @feature (D-152)
		{ "MaxBankAngle",				INI::parseAngleReal,						nullptr, offsetof( CruiseFlightUpdateModuleData, m_maxBankAngle ) },		// Hypersonic @feature (D-152)
		{ "SalvoGap",						INI::parseDurationUnsignedInt,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_salvoGapFrames ) },	// Hypersonic @feature (D-152)
		{ "SalvoRadius",				INI::parseReal,									nullptr, offsetof( CruiseFlightUpdateModuleData, m_salvoRadius ) },		// Hypersonic @feature (D-152)
		{ "SalvoSpacing",				INI::parseReal,									nullptr, offsetof( CruiseFlightUpdateModuleData, m_salvoSpacing ) },		// Hypersonic @feature (D-152)
		{ "LaunchPrepareTime",	INI::parseDurationUnsignedInt,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_prepareFrames ) },
		{ "Warhead",						INI::parseWeaponTemplate,				nullptr, offsetof( CruiseFlightUpdateModuleData, m_warhead ) },
		{ "BoostTime",					INI::parseDurationUnsignedInt,	nullptr, offsetof( CruiseFlightUpdateModuleData, m_boostFrames ) },
		{ "BoostFX",						INI::parseFXList,								nullptr, offsetof( CruiseFlightUpdateModuleData, m_boostFX ) },
		{ "BoosterOCL",					INI::parseObjectCreationList,		nullptr, offsetof( CruiseFlightUpdateModuleData, m_boosterOCL ) },
		{ "EngineSound",				INI::parseAudioEventRTS,				nullptr, offsetof( CruiseFlightUpdateModuleData, m_engineSound ) },
		{ "DiveSound",					INI::parseAudioEventRTS,				nullptr, offsetof( CruiseFlightUpdateModuleData, m_diveSound ) },	// Hypersonic @feature (D-161)
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CruiseFlightUpdate::CruiseFlightUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	m_state = FLIGHT_ON_LAUNCHER;
	m_framesLeft = 0;
	m_flightFrames = 0;
	m_maxFlightFrames = 0;
	m_impact.zero();
	m_cruiseZ = 0.0f;
	m_climbAngle = data->m_climbAngle;
	m_yaw = 0.0f;
	m_pitch = 0.0f;
	m_pitchSpeed = 0.0f;
	m_yawSpeed = 0.0f;
	m_diveStartDist = 0.0f;
	m_diveHeight = 0.0f;
	m_leaveFrame = 0;
	m_salvoTarget.zero();
	m_boostFramesLeft = 0;
	m_speedUpgrade = nullptr;
	if( data->m_speedUpgradeName.isNotEmpty() && TheUpgradeCenter != nullptr )
	{
		m_speedUpgrade = TheUpgradeCenter->findUpgrade( data->m_speedUpgradeName );
		DEBUG_ASSERTCRASH( m_speedUpgrade != nullptr, ("CruiseFlightUpdate: unknown SpeedUpgrade '%s'", data->m_speedUpgradeName.str()) );
	}

	// nothing to do until launched
	setWakeFrame( getObject(), UPDATE_SLEEP_FOREVER );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CruiseFlightUpdate::~CruiseFlightUpdate()
{
}

//-------------------------------------------------------------------------------------------------
// The drone is removed (detonated, shot down, or its facility is gone): its engine falls silent at once.
void CruiseFlightUpdate::onDelete()
{
	stopEngineSound();
	UpdateModule::onDelete();
}

//-------------------------------------------------------------------------------------------------
// Engine loop while in the air. Client audio only: nothing in the logic reads it. A sound culled at launch (the
// listener was too far) or cut off is started again by fly() every ENGINE_SOUND_RETRY_FRAMES.
void CruiseFlightUpdate::startEngineSound()
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	// Hypersonic @feature dive sound (D-161): in the dive the DiveSound (if set) takes over from the engine loop
	const AudioEventRTS &wanted = ( m_state == FLIGHT_DIVE && data->m_diveSound.getEventName().isNotEmpty() )
		? data->m_diveSound : data->m_engineSound;
	if( TheAudio == nullptr || wanted.getEventName().isEmpty() )
		return;
	if( m_engineLoop.getEventName().isNotEmpty() )
	{
		if( m_engineLoop.getEventName() == wanted.getEventName() && m_engineLoop.isCurrentlyPlaying() )
			return;
		// Hypersonic @feature (D-186): the cruise loop fades out under the starting dive sound (a cross-fade, no gap)
		if( m_engineLoop.getEventName() != wanted.getEventName() && m_engineLoop.isCurrentlyPlaying() )
			TheAudio->fadeOutAudioEvent( m_engineLoop.getPlayingHandle(), ENGINE_SOUND_CROSSFADE_MS );
		else
			TheAudio->killAudioEventImmediately( m_engineLoop.getPlayingHandle() );
	}
	m_engineLoop = wanted;
	m_engineLoop.setObjectID( getObject()->getID() );
	m_engineLoop.setPlayingHandle( TheAudio->addAudioEvent( &m_engineLoop ) );
}

//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::stopEngineSound()
{
	if( TheAudio != nullptr && m_engineLoop.getEventName().isNotEmpty() )
		TheAudio->killAudioEventImmediately( m_engineLoop.getPlayingHandle() );
	m_engineLoop.setEventName( AsciiString::TheEmptyString );
}

//-------------------------------------------------------------------------------------------------
CruiseFlightUpdate *CruiseFlightUpdate::findFlight( Object *obj )
{
	static const NameKeyType key_CruiseFlightUpdate = NAMEKEY( "CruiseFlightUpdate" );
	return obj != nullptr ? (CruiseFlightUpdate *)obj->findUpdateModule( key_CruiseFlightUpdate ) : nullptr;
}

//-------------------------------------------------------------------------------------------------
const CruiseFlightUpdate *CruiseFlightUpdate::findFlight( const Object *obj )
{
	return findFlight( const_cast<Object *>( obj ) );
}

//-------------------------------------------------------------------------------------------------
// Part of its facility until it flies: the player never selects or commands it (D-042).
void CruiseFlightUpdate::onObjectCreated()
{
	UpdateModule::onObjectCreated();
	getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
}

//-------------------------------------------------------------------------------------------------
Real CruiseFlightUpdate::getSpeed() const
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	const Player *player = getObject()->getControllingPlayer();
	if( m_speedUpgrade != nullptr && data->m_upgradedSpeed > 0.0f && player != nullptr && player->hasUpgradeComplete( m_speedUpgrade ) )
		return data->m_upgradedSpeed;
	return data->m_speed;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-149) the flight velocity (heading and flight path angle at the cruise speed; the dive's extra
// speed is left out), so a shot-down drone's falling wreck (OCL INHERIT_VELOCITY) carries on along its path.
void CruiseFlightUpdate::getFlightVelocity( Coord3D *vel ) const
{
	vel->zero();
	if( m_state != FLIGHT_CLIMB_CRUISE && m_state != FLIGHT_DIVE )
		return;
	const Real speed = getSpeed();
	const Real cp = Cos( m_pitch );
	vel->x = speed * cp * Cos( m_yaw );
	vel->y = speed * cp * Sin( m_yaw );
	vel->z = speed * Sin( m_pitch );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature dive speed (D-152): in the dive the speed grows evenly over the first half of the dive distance
// to DiveSpeedFactor x the cruise speed and stays there to the impact.
Real CruiseFlightUpdate::getDiveSpeedFactor( Real horizontal ) const
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	if( m_state != FLIGHT_DIVE || data->m_diveSpeedFactor == 1.0f || m_diveStartDist <= 0.0f )
		return 1.0f;
	Real done = 2.0f * ( 1.0f - horizontal / m_diveStartDist );
	return 1.0f + ( data->m_diveSpeedFactor - 1.0f ) * clampReal( done, 0.0f, 1.0f );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature salvos (D-152): still on the launcher preparing, or left within the salvo window.
Bool CruiseFlightUpdate::isInSalvoWindow( UnsignedInt now ) const
{
	if( m_state == FLIGHT_PREPARING )
		return TRUE;
	if( m_state == FLIGHT_CLIMB_CRUISE || m_state == FLIGHT_DIVE )
		return m_leaveFrame + SALVO_WINDOW_FRAMES >= now;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature salvos (D-152): the drones of the launching player that belong to the same salvo.
struct SalvoScan
{
	const Object*	self;
	Coord3D				target;
	Real					radiusSqr;
	UnsignedInt		now;
	UnsignedInt		lastLeave;
	Int						count;
	Coord3D				impacts[SALVO_MAX_MEMBERS];
};

static void salvoScanCallback( Object *obj, void *userData )
{
	SalvoScan *scan = (SalvoScan *)userData;
	if( obj == scan->self || obj->isEffectivelyDead() )
		return;
	const CruiseFlightUpdate *flight = CruiseFlightUpdate::findFlight( obj );
	if( flight == nullptr || !flight->isInSalvoWindow( scan->now ) )
		return;
	Real dx = flight->getSalvoTarget()->x - scan->target.x;
	Real dy = flight->getSalvoTarget()->y - scan->target.y;
	if( dx * dx + dy * dy > scan->radiusSqr )
		return;
	if( flight->getLeaveFrame() > scan->lastLeave )
		scan->lastLeave = flight->getLeaveFrame();
	if( scan->count < SALVO_MAX_MEMBERS )
		scan->impacts[scan->count] = *flight->getImpactPoint();
	++scan->count;
}

//-------------------------------------------------------------------------------------------------
// The launch order: the impact point is decided now (the warhead's scatter disc around the target, or the
// designated object), then the drone waits out its launch preparation on the launcher.
//-------------------------------------------------------------------------------------------------
Bool CruiseFlightUpdate::launch( const Coord3D *target )
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Object *obj = getObject();
	if( target == nullptr || m_state != FLIGHT_ON_LAUNCHER || obj->isEffectivelyDead() || data->m_speed <= 0.0f )
		return FALSE;

	// Hypersonic @feature salvos (D-152): the other drones of this player's salvo (same target area, preparing or just
	// gone), in the deterministic order of the player's objects
	const UnsignedInt now = TheGameLogic->getFrame();
	SalvoScan scan;
	scan.self = obj;
	scan.target = *target;
	scan.radiusSqr = data->m_salvoRadius * data->m_salvoRadius;
	scan.now = now;
	scan.lastLeave = 0;
	scan.count = 0;
	Player *player = obj->getControllingPlayer();
	if( data->m_salvoGapFrames > 0 && player != nullptr )
		player->iterateObjects( salvoScanCallback, &scan );

	if( data->m_warhead != nullptr )
	{
		data->m_warhead->computeStrikeAimPoint( obj, target, &m_impact );

		// Hypersonic @feature salvos (D-152): each member aims at its own point of the same scatter disc: draws are
		// repeated (synced random) until the point is SalvoSpacing (at most the scatter radius) from the others; when
		// no draw gets there the farthest one is kept. The scatter rules themselves are unchanged.
		const Int compared = scan.count < SALVO_MAX_MEMBERS ? scan.count : SALVO_MAX_MEMBERS;
		Real spacing = data->m_salvoSpacing;
		Real radius = data->m_warhead->getScatterRadiusForSource( obj );
		if( spacing > radius )
			spacing = radius;
		if( compared > 0 && spacing > 0.0f )
		{
			Coord3D best = m_impact;
			Real bestSqr = -1.0f;
			for( Int attempt = 0; attempt < SALVO_AIM_TRIES; ++attempt )
			{
				if( attempt > 0 )
				{
					Coord3D previous = m_impact;
					data->m_warhead->computeStrikeAimPoint( obj, target, &m_impact );
					if( m_impact.x == previous.x && m_impact.y == previous.y )
						break;	// no scatter (a designated target): every draw is the same point
				}
				Real nearestSqr = 1.0e30f;
				for( Int i = 0; i < compared; ++i )
				{
					Real ex = m_impact.x - scan.impacts[i].x;
					Real ey = m_impact.y - scan.impacts[i].y;
					Real dSqr = ex * ex + ey * ey;
					if( dSqr < nearestSqr )
						nearestSqr = dSqr;
				}
				if( nearestSqr > bestSqr )
				{
					bestSqr = nearestSqr;
					best = m_impact;
				}
				if( nearestSqr >= spacing * spacing )
					break;
			}
			m_impact = best;
		}
	}
	else
	{
		m_impact = *target;
		m_impact.z = TheTerrainLogic->getGroundHeight( m_impact.x, m_impact.y );
	}

	m_state = FLIGHT_PREPARING;
	m_framesLeft = data->m_prepareFrames;
	// Hypersonic @feature salvos (D-152): leave SalvoGap after the last member of the salvo
	if( scan.count > 0 && scan.lastLeave + data->m_salvoGapFrames > now + m_framesLeft )
		m_framesLeft = scan.lastLeave + data->m_salvoGapFrames - now;
	m_leaveFrame = now + m_framesLeft;
	m_salvoTarget = *target;
	setWakeFrame( obj, UPDATE_SLEEP_NONE );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Leaves the launcher: the booster ignites; fly() plans the cruise height from the terrain ahead each frame.
//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::startFlight()
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Object *obj = getObject();
	const Coord3D start = *obj->getPosition();

	Real dx = m_impact.x - start.x;
	Real dy = m_impact.y - start.y;
	Real dist = sqrtf( dx * dx + dy * dy );

	// followTerrain() raises it to the terrain ahead + CruiseClearance on the first frame
	m_cruiseZ = start.z;
	m_climbAngle = data->m_climbAngle;

	// the direction it sits in on the launcher (the launcher has turned it toward the target)
	const Matrix3D *mtx = obj->getTransformMatrix();
	Vector3 forward = mtx->Get_X_Vector();
	m_yaw = (Real)atan2( forward.Y, forward.X );
	Real horizontal = sqrtf( forward.X * forward.X + forward.Y * forward.Y );
	m_pitch = (Real)atan2( forward.Z, horizontal );
	m_pitchSpeed = 0.0f;
	m_yawSpeed = 0.0f;
	m_diveStartDist = 0.0f;
	m_diveHeight = 0.0f;
	m_leaveFrame = TheGameLogic->getFrame();

	// safety limit on the flight time: the straight route, climbing to the highest terrain on it + the clearance
	// and back down (the terrain-following flight is never longer than that)
	Real highest = getSurfaceHeight( m_impact.x, m_impact.y );
	Int samples = (Int)(dist / ROUTE_SAMPLE_STEP);
	for( Int i = 1; i <= samples; ++i )
	{
		Real t = ROUTE_SAMPLE_STEP * (Real)i / dist;
		Real h = getSurfaceHeight( start.x + dx * t, start.y + dy * t );
		if( h > highest )
			highest = h;
	}
	Real climb = highest + data->m_cruiseClearance - start.z;
	if( climb < 0.0f )
		climb = 0.0f;
	Real speed = getSpeed();
	Real route = dist + 2.0f * climb;
	m_maxFlightFrames = (UnsignedInt)(route * MAX_FLIGHT_ROUTE_FACTOR / (speed > 0.0f ? speed : 1.0f)) + MAX_FLIGHT_MARGIN_FRAMES;
	m_flightFrames = 0;

	m_state = FLIGHT_CLIMB_CRUISE;
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_AIRBORNE_TARGET ) );

	// booster ignition (D-044): launch sound and backblast, flame and smoke on the booster while it burns
	m_boostFramesLeft = data->m_boostFrames;
	if( m_boostFramesLeft > 0 )
		obj->setModelConditionState( MODELCONDITION_JETAFTERBURNER );
	else
		obj->setModelConditionState( MODELCONDITION_JETEXHAUST );
	FXList::doFXObj( data->m_boostFX, obj );
	startEngineSound();
}

//-------------------------------------------------------------------------------------------------
// Booster burn-out: the spent booster drops away (BoosterOCL, created in the drone's frame) and the model hides it.
//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::dropBooster()
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Object *obj = getObject();
	obj->clearAndSetModelConditionState( MODELCONDITION_JETAFTERBURNER, MODELCONDITION_JETEXHAUST );
	if( data->m_boosterOCL != nullptr )
		ObjectCreationList::create( data->m_boosterOCL, obj, nullptr );
}

//-------------------------------------------------------------------------------------------------
// Terrain following (D-044): the cruise height is the highest terrain (or water) on the next CruiseLookAhead of
// the straight route to the impact point + CruiseClearance. It rises at once when higher ground comes into view
// (the pitch rate and climb angle keep the climb smooth) and sinks at most at DescentAngle, and only once the
// ground ahead is DESCENT_DEADBAND lower, so the drone does not bob over small bumps. Returns the least climb angle
// (radians; may be negative) that passes the terrain ahead with part of the clearance.
//-------------------------------------------------------------------------------------------------
Real CruiseFlightUpdate::followTerrain( const Coord3D *pos, Real dirX, Real dirY, Real horizontal, Real speed )
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Real lookAhead = data->m_cruiseLookAhead > ROUTE_SAMPLE_STEP ? data->m_cruiseLookAhead : ROUTE_SAMPLE_STEP;
	Real window = horizontal < lookAhead ? horizontal : lookAhead;
	Real margin = data->m_cruiseClearance * CLIMB_CLEARANCE_SHARE;

	Real highest = getSurfaceHeight( pos->x, pos->y );
	Real need = -PI / 2.0f;
	Int samples = (Int)( window / ROUTE_SAMPLE_STEP );
	if( ROUTE_SAMPLE_STEP * (Real)samples < window )
		++samples;	// the last sample is the end of the window
	for( Int i = 1; i <= samples; ++i )
	{
		Real d = ROUTE_SAMPLE_STEP * (Real)i;
		if( d > window )
			d = window;
		Real h = getSurfaceHeight( pos->x + dirX * d, pos->y + dirY * d );
		if( h > highest )
			highest = h;
		Real angle = (Real)atan2( h + margin * d / lookAhead - pos->z, d );
		if( angle > need )
			need = angle;
	}

	Real target = highest + data->m_cruiseClearance;
	if( target >= m_cruiseZ )
		m_cruiseZ = target;
	else if( target < m_cruiseZ - DESCENT_DEADBAND )
	{
		Real sink = speed * (Real)tan( data->m_descentAngle );
		m_cruiseZ = ( m_cruiseZ - sink > target ) ? m_cruiseZ - sink : target;
	}
	return need;
}

//-------------------------------------------------------------------------------------------------
// Position and orientation: nose (+X) along the flight path, wings level.
void CruiseFlightUpdate::setFlightTransform( const Coord3D *pos )
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Real cy = Cos( m_yaw ), sy = Sin( m_yaw );
	Real cp = Cos( m_pitch ), sp = Sin( m_pitch );
	Vector3 x( cp * cy, cp * sy, sp );
	Vector3 y( -sy, cy, 0.0f );
	Vector3 z( -sp * cy, -sp * sy, cp );
	// Hypersonic @feature banked turns (D-152): roll into the turn (left wing down in a left turn) in proportion to the
	// turn rate, up to MaxBankAngle at the full TurnRate
	if( data->m_maxBankAngle > 0.0f && data->m_turnRate > 0.0f && m_yawSpeed != 0.0f )
	{
		Real roll = clampReal( -data->m_maxBankAngle * m_yawSpeed / data->m_turnRate, -data->m_maxBankAngle, data->m_maxBankAngle );
		Real cr = Cos( roll ), sr = Sin( roll );
		Vector3 yr = y * cr + z * sr;
		Vector3 zr = z * cr - y * sr;
		y = yr;
		z = zr;
	}
	Matrix3D mtx( x, y, z, Vector3( pos->x, pos->y, pos->z ) );
	getObject()->setTransformMatrix( &mtx );
}

//-------------------------------------------------------------------------------------------------
// One frame of flight.
//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::fly()
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Object *obj = getObject();
	Coord3D pos = *obj->getPosition();

	Real dx = m_impact.x - pos.x;
	Real dy = m_impact.y - pos.y;
	Real dz = m_impact.z - pos.z;
	Real horizontal = sqrtf( dx * dx + dy * dy );
	Real dist = sqrtf( horizontal * horizontal + dz * dz );
	const Real speed = getSpeed() * getDiveSpeedFactor( horizontal );	// Hypersonic @feature dive speed (D-152)

	// arrives this frame
	if( dist <= speed )
	{
		detonate( &m_impact );
		return;
	}

	// booster burn-out
	if( m_boostFramesLeft > 0 && --m_boostFramesLeft == 0 )
		dropBooster();

	// safety: never fly on forever
	if( ++m_flightFrames > m_maxFlightFrames )
	{
		pos.z = getSurfaceHeight( pos.x, pos.y );
		detonate( &pos );
		return;
	}

	// the dive starts once the impact point lies DiveAngle below the horizon
	Real lineOfSight = (Real)atan2( -dz, horizontal );	// positive = below
	if( m_state == FLIGHT_CLIMB_CRUISE && lineOfSight >= data->m_diveAngle )
		m_state = FLIGHT_DIVE;

	// Hypersonic @feature curved dive (D-152): with DiveCurveAngle the dive starts level, 2 x height / tan(DiveCurveAngle)
	// from the impact point (height above it now), and follows a parabola that reaches DiveCurveAngle at the impact
	if( m_state == FLIGHT_CLIMB_CRUISE && data->m_diveCurveAngle > 0.0f && -dz > 0.0f && m_pitch < DIVE_LEAD_MAX_CLIMB )
	{
		Real tanEnd = (Real)tan( data->m_diveCurveAngle );
		if( tanEnd > 0.0f && horizontal <= 2.0f * -dz / tanEnd )
		{
			m_state = FLIGHT_DIVE;
			m_diveHeight = -dz;
		}
	}
	// Hypersonic @feature smooth dive (D-090): with DiveEaseTime the pitch-over starts before the point where the
	// DiveAngle line through the impact point meets the flight path: by the lead-in of a turn at PitchRate (radius
	// speed / PitchRate, tangent length radius * tan(DiveAngle / 2)) plus half the distance flown while the pitch rate
	// builds up, so the path curves into the dive line and the drone dives along it at DiveAngle.
	if( m_state == FLIGHT_CLIMB_CRUISE && data->m_diveCurveAngle <= 0.0f && data->m_diveEaseFrames > 0 && data->m_pitchRate > 0.0f
		&& -dz > 0.0f && m_pitch < DIVE_LEAD_MAX_CLIMB )
	{
		Real tanDive = (Real)tan( data->m_diveAngle );
		Real leadIn = speed / data->m_pitchRate * (Real)tan( data->m_diveAngle * 0.5f ) + speed * data->m_diveEaseFrames * 0.5f;
		if( tanDive > 0.0f && horizontal <= -dz / tanDive + leadIn )
			m_state = FLIGHT_DIVE;
	}

	// Hypersonic @feature (D-152): where the dive began (the dive speed and the dive curve are measured from there)
	if( m_state == FLIGHT_DIVE && m_diveStartDist <= 0.0f )
	{
		m_diveStartDist = horizontal > 1.0f ? horizontal : 1.0f;
		startEngineSound();	// Hypersonic @feature dive sound (D-161): the DiveSound takes over now
	}

	Real wantedPitch;
	Real pitchRate = data->m_pitchRate;
	Real turnRate = data->m_turnRate;
	if( m_state == FLIGHT_DIVE )
	{
		// straight at the impact point; close in, the steering tightens so the dive always ends on it
		wantedPitch = -lineOfSight;
		Bool onCurve = FALSE;
		// Hypersonic @bugfix steep dive (D-230): leave a curve that has become too steep to follow (once left, for good)
		if( m_diveHeight > 0.0f && lineOfSight > DIVE_CURVE_MAX_LINE_OF_SIGHT )
			m_diveHeight = 0.0f;
		// Hypersonic @feature curved dive (D-152): follow the parabola height x u x (2 - u), u = distance left / distance
		// at the start (level at the start, DiveCurveAngle at the impact); a height error is closed over
		// DIVE_CURVE_HOLD_FRAMES of flight. The last frames aim straight at the impact point (the curve's end angle).
		if( m_diveHeight > 0.0f && horizontal > speed * DIVE_CURVE_FINAL_FRAMES )
		{
			Real u = clampReal( horizontal / m_diveStartDist, 0.0f, 1.0f );
			Real slope = 2.0f * m_diveHeight / m_diveStartDist * ( 1.0f - u );
			Real curveHeight = m_diveHeight * u * ( 2.0f - u );
			Real error = curveHeight - ( pos.z - m_impact.z );
			wantedPitch = -(Real)atan( slope ) + (Real)atan2( error, speed * DIVE_CURVE_HOLD_FRAMES );
			onCurve = TRUE;
		}
		Real tighten = 2.0f * speed / dist;
		Real pitchError = (Real)fabs( wantedPitch - m_pitch );
		if( !onCurve && pitchError * tighten > pitchRate )
			pitchRate = pitchError * tighten;
		if( horizontal > 1.0f )
		{
			Real yawError = (Real)fabs( normalizeAngle( (Real)atan2( dy, dx ) - m_yaw ) );
			if( yawError * tighten > turnRate )
				turnRate = yawError * tighten;
		}
	}
	else
	{
		// follow the terrain ahead: climb to / hold the cruise height, level off smoothly, glide down gently
		Real need = horizontal > 1.0f ? followTerrain( &pos, dx / horizontal, dy / horizontal, horizontal, speed ) : -PI / 2.0f;
		m_climbAngle = clampReal( need > data->m_climbAngle ? need : data->m_climbAngle, 0.0f, MAX_CLIMB_ANGLE );
		Real lookahead = speed * LEVEL_OFF_LOOKAHEAD_FRAMES;
		wantedPitch = clampReal( (Real)atan2( m_cruiseZ - pos.z, lookahead ), -data->m_descentAngle, m_climbAngle );
		if( need > wantedPitch )
			wantedPitch = need < MAX_CLIMB_ANGLE ? need : MAX_CLIMB_ANGLE;	// terrain close ahead: climb over it now
		if( need > m_pitch )
			pitchRate *= PULL_UP_RATE_FACTOR;	// rising ground close ahead: pull up harder so it clears it
	}

	Real pitchStep = clampReal( wantedPitch - m_pitch, -pitchRate, pitchRate );

	// Hypersonic @feature smooth dive (D-090): in the dive the pitch rate changes by at most PitchRate / DiveEaseTime
	// per frame and slows down before the wanted pitch is reached (no sudden start or stop of the pitch-over). The
	// tightened steering close to the impact point (above PitchRate) is not eased, so the dive still ends on it.
	if( m_state == FLIGHT_DIVE && data->m_diveEaseFrames > 0 && pitchRate <= data->m_pitchRate )
	{
		Real accel = data->m_pitchRate / (Real)data->m_diveEaseFrames;
		Real error = wantedPitch - m_pitch;
		Real wanted = sqrtf( 2.0f * accel * (Real)fabs( error ) );
		if( wanted > pitchRate )
			wanted = pitchRate;
		if( error < 0.0f )
			wanted = -wanted;
		pitchStep = m_pitchSpeed + clampReal( wanted - m_pitchSpeed, -accel, accel );
		if( (Real)fabs( error ) < (Real)fabs( pitchStep ) && (Real)fabs( error ) <= accel )
			pitchStep = error;	// arrived
	}
	m_pitchSpeed = pitchStep;
	m_pitch += pitchStep;
	// Hypersonic @bugfix steep dive (D-230): never pitch past straight down (it would fly backwards, then turn round)
	if( m_pitch < -PI / 2.0f )
	{
		m_pitch = -PI / 2.0f;
		m_pitchSpeed = 0.0f;
	}
	if( horizontal > 1.0f )
	{
		Real yawError = normalizeAngle( (Real)atan2( dy, dx ) - m_yaw );
		Real yawStep = clampReal( yawError, -turnRate, turnRate );
		// Hypersonic @feature banked turns (D-152): the turn rate changes by at most TurnRate / TurnEaseTime per frame and
		// slows down before the heading is reached (no snapping onto a new heading). The tightened steering close to the
		// impact point (above TurnRate) is not eased, so the dive still ends on it.
		if( data->m_turnEaseFrames > 0 && turnRate <= data->m_turnRate )
		{
			Real accel = data->m_turnRate / (Real)data->m_turnEaseFrames;
			Real wanted = sqrtf( 2.0f * accel * (Real)fabs( yawError ) );
			if( wanted > turnRate )
				wanted = turnRate;
			if( yawError < 0.0f )
				wanted = -wanted;
			yawStep = m_yawSpeed + clampReal( wanted - m_yawSpeed, -accel, accel );
			if( (Real)fabs( yawError ) < (Real)fabs( yawStep ) && (Real)fabs( yawError ) <= accel )
				yawStep = yawError;	// arrived
		}
		m_yawSpeed = yawStep;
		m_yaw = normalizeAngle( m_yaw + yawStep );
	}
	else
	{
		m_yawSpeed = 0.0f;
	}

	Real cp = Cos( m_pitch );
	pos.x += speed * cp * Cos( m_yaw );
	pos.y += speed * cp * Sin( m_yaw );
	pos.z += speed * Sin( m_pitch );

	// flew into the ground (a hill on the dive line): the warhead goes off there
	Real surface = getSurfaceHeight( pos.x, pos.y );
	if( pos.z <= surface )
	{
		pos.z = surface;
		detonate( &pos );
		return;
	}

	setFlightTransform( &pos );

	// the engine sound, if it was culled (too far from the listener at launch) or cut off
	if( m_flightFrames % ENGINE_SOUND_RETRY_FRAMES == 0 )
		startEngineSound();
}

//-------------------------------------------------------------------------------------------------
// The warhead goes off like a projectile's (damage, FX, the drone's weapon bonuses such as Improved Warheads);
// the drone itself leaves silently (no death effects, no kill).
//-------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::detonate( const Coord3D *pos )
{
	const CruiseFlightUpdateModuleData *data = getCruiseFlightUpdateModuleData();
	Object *obj = getObject();

	setFlightTransform( pos );
	stopEngineSound();
	// Hypersonic @bugfix cook-off (D-142): spent before its blast is dealt, so a chain reaction that kills it inside
	// the blast (before the silent removal below) is not a shot-down death (INI death modules exempt KILLING_SELF)
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_MISSILE_KILLING_SELF ) );
	if( data->m_warhead != nullptr && TheWeaponStore != nullptr )
		TheWeaponStore->handleProjectileDetonation( data->m_warhead, obj, pos, 0 );

	m_state = FLIGHT_ON_LAUNCHER;	// done: nothing more happens here
	TheGameLogic->destroyObject( obj );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime CruiseFlightUpdate::update()
{
	Object *obj = getObject();
	if( obj->isEffectivelyDead() )
		return UPDATE_SLEEP_FOREVER;

	switch( m_state )
	{
		case FLIGHT_ON_LAUNCHER:
			return UPDATE_SLEEP_FOREVER;	// launch() wakes us

		case FLIGHT_PREPARING:
			if( m_framesLeft > 0 )
			{
				--m_framesLeft;
				return UPDATE_SLEEP_NONE;
			}
			startFlight();
			fly();
			return UPDATE_SLEEP_NONE;

		default:
			fly();
			return UPDATE_SLEEP_NONE;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

	xfer->xferInt( &m_state );
	xfer->xferUnsignedInt( &m_framesLeft );
	xfer->xferUnsignedInt( &m_flightFrames );
	xfer->xferCoord3D( &m_impact );
	xfer->xferReal( &m_cruiseZ );
	xfer->xferUnsignedInt( &m_boostFramesLeft );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: booster burn (D-044)
	* 3: Hypersonic @feature smooth dive (D-090): pitch speed
	* 4: Hypersonic @feature (D-152): turn speed, dive start and height, salvo leave frame and target */
// ------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 4;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	xfer->xferInt( &m_state );
	xfer->xferUnsignedInt( &m_framesLeft );
	xfer->xferUnsignedInt( &m_flightFrames );
	xfer->xferUnsignedInt( &m_maxFlightFrames );
	xfer->xferCoord3D( &m_impact );
	xfer->xferReal( &m_cruiseZ );
	xfer->xferReal( &m_climbAngle );
	xfer->xferReal( &m_yaw );
	xfer->xferReal( &m_pitch );
	if( version >= 2 )
		xfer->xferUnsignedInt( &m_boostFramesLeft );
	if( version >= 3 )
		xfer->xferReal( &m_pitchSpeed );
	if( version >= 4 )
	{
		xfer->xferReal( &m_yawSpeed );
		xfer->xferReal( &m_diveStartDist );
		xfer->xferReal( &m_diveHeight );
		xfer->xferUnsignedInt( &m_leaveFrame );
		xfer->xferCoord3D( &m_salvoTarget );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CruiseFlightUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

	// the engine sound is client state (not saved): fly() starts it again within a second while in the air

}
