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

// FILE: CruiseFlightUpdate.h ///////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Fire-and-forget flight of a launched drone (Generals Hypersonic IRAN_DESIGN 3, 6, D-042).
//         The drone sits on a launcher of its facility (MunitionMagazineUpdate places it there) and is never
//         selectable or commandable. A launch names a ground point: the Warhead weapon's scatter rules pick the
//         impact point once (Guidance upgrades, uniform scatter, designation), the drone waits LaunchPrepareTime
//         on its launcher (the facility turns the launcher toward the target meanwhile), then flies by itself:
//
//           boost    (D-044) a rocket booster burns for BoostTime after it leaves the launcher: BoostFX plays at
//                    ignition (launch sound, backblast), the model shows MODELCONDITION_JETAFTERBURNER (flame and
//                    smoke on the booster); at burn-out BoosterOCL drops the spent booster and the model switches
//                    to MODELCONDITION_JETEXHAUST (the booster sub-object hidden) for the rest of the flight;
//           climb    at ClimbAngle or steeper (steep enough to clear the terrain ahead), levelling off smoothly
//                    at the cruise height;
//           cruise   straight at the impact point, at Speed (UpgradedSpeed with SpeedUpgrade), following the terrain
//                    (D-044): the cruise height is the highest terrain in the next CruiseLookAhead of the route +
//                    CruiseClearance; it rises at once (the climb is limited by the pitch rate and angle) and sinks
//                    at most at DescentAngle, so the flight stays smooth (no bobbing);
//           dive     one steady dive once the impact point lies DiveAngle below the horizon.
//                    Hypersonic @feature smooth dive (D-090): with DiveEaseTime the pitch-over starts early (the
//                    lead-in of a turn at PitchRate plus the easing), so the path curves into the DiveAngle line
//                    instead of bending sharply at it, and the pitch rate builds up and winds down over
//                    DiveEaseTime (no sudden start or stop of the pitch-over).
//
//         Hypersonic @feature D-152 (all opt-in, 0 = the behaviour above):
//           curved dive   DiveCurveAngle: the dive starts level and steepens along a parabola to this angle at the
//                         impact point; it begins 2 x height / tan(DiveCurveAngle) out, so it scales with the height
//                         above the impact point (replaces the DiveAngle start for a drone that is not climbing);
//           dive speed    DiveSpeedFactor: the speed grows over the first half of the dive to this x the cruise speed;
//           banked turns  TurnEaseTime: the turn rate builds up and winds down over this time (no snapping to a new
//                         heading); MaxBankAngle: the drone rolls into the turn, in proportion to its turn rate;
//           salvos        SalvoGap: drones of one player launched at nearly the same point (within SalvoRadius of
//                         each other's target, while the other is preparing or left within the last 3 s) leave their
//                         launchers at least this far apart, and each picks its impact point (same scatter disc, the
//                         same draw) at least SalvoSpacing (at most the scatter radius) from the others' if it can.
//
//         EngineSound loops on the drone while it is in the air (client audio, restarted every second while it is
//         not playing, e.g. culled by distance at launch). Hypersonic @feature dive sound (D-161): DiveSound, if set,
//         replaces it from the start of the dive to the impact (the engine spinning up: a rising-pitch sample).
//
//         Pitch and heading change at most PitchRate / TurnRate (the dive tightens this near the end so it always
//         arrives). At the impact point (or where it hits the ground first) the Warhead detonates like a projectile
//         (damage, FX, the drone's weapon bonuses) and the drone is removed silently. Shot down, it just dies (its
//         own die modules): no warhead. The motion is set directly each logic frame (no locomotor, no physics), so
//         it is deterministic and smooth. While it flies it is an airborne target (anti-air, HitChanceBehavior).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "GameLogic/Module/UpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class FXList;
class ObjectCreationList;
class Thing;
class UpgradeTemplate;
class WeaponTemplate;

//-------------------------------------------------------------------------------------------------
class CruiseFlightUpdateModuleData : public UpdateModuleData
{
public:

	CruiseFlightUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	Real										m_speed;						///< dist per frame (dist/sec in INI)
	Real										m_upgradedSpeed;		///< with SpeedUpgrade (0: same as Speed)
	AsciiString							m_speedUpgradeName;	///< player upgrade that switches to UpgradedSpeed
	Real										m_cruiseClearance;	///< cruise height above the highest terrain ahead
	Real										m_cruiseLookAhead;	///< how far ahead on the route the terrain sets the cruise height
	Real										m_descentAngle;			///< radians; steepest glide down to a lower cruise height
	Real										m_climbAngle;				///< radians; the least climb angle
	Real										m_diveAngle;				///< radians below the horizon where the dive starts
	Real										m_pitchRate;				///< radians per frame (deg/sec in INI)
	Real										m_turnRate;					///< radians per frame (deg/sec in INI)
	UnsignedInt							m_diveEaseFrames;	///< Hypersonic @feature smooth dive (D-090): pitch rate ramp in the dive (0: off)
	Real										m_diveCurveAngle;	///< Hypersonic @feature (D-152) radians at the impact point of the curved dive (0: off)
	Real										m_diveSpeedFactor;	///< Hypersonic @feature (D-152) dive speed / cruise speed (1: off)
	UnsignedInt							m_turnEaseFrames;	///< Hypersonic @feature (D-152) turn rate ramp (0: off)
	Real										m_maxBankAngle;		///< Hypersonic @feature (D-152) radians of roll at the full turn rate (0: off)
	UnsignedInt							m_salvoGapFrames;	///< Hypersonic @feature (D-152) least gap between salvo departures (0: off)
	Real										m_salvoRadius;		///< Hypersonic @feature (D-152) targets this close together form one salvo
	Real										m_salvoSpacing;		///< Hypersonic @feature (D-152) wanted distance between the impact points of a salvo
	UnsignedInt							m_prepareFrames;		///< on the launcher before it leaves (booster ignition)
	const WeaponTemplate*		m_warhead;					///< scatter rules at launch, detonation at the impact point
	UnsignedInt							m_boostFrames;			///< booster burn after leaving the launcher (0: no booster)
	const FXList*						m_boostFX;					///< at booster ignition (launch sound, backblast)
	const ObjectCreationList*	m_boosterOCL;			///< at burn-out: the spent booster drops away
	AudioEventRTS						m_engineSound;			///< loops while the drone is in the air
	AudioEventRTS						m_diveSound;				///< Hypersonic @feature (D-161): replaces m_engineSound in the dive
};

//-------------------------------------------------------------------------------------------------
class CruiseFlightUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CruiseFlightUpdate, "CruiseFlightUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( CruiseFlightUpdate, CruiseFlightUpdateModuleData )

public:

	enum FlightState
	{
		FLIGHT_ON_LAUNCHER = 0,		///< waiting on its launcher
		FLIGHT_PREPARING,					///< launch ordered, still on the launcher
		FLIGHT_CLIMB_CRUISE,			///< in the air, climbing to / holding the cruise height
		FLIGHT_DIVE								///< in the air, diving onto the impact point
	};

	CruiseFlightUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static CruiseFlightUpdate *findFlight( Object *obj );	///< the module on this object, if any
	static const CruiseFlightUpdate *findFlight( const Object *obj );

	virtual void onObjectCreated() override;
	virtual void onDelete() override;
	virtual UpdateSleepTime update() override;
	virtual DisabledMaskType getDisabledTypesToProcess() const override { return DISABLEDMASK_ALL; }	// nothing stops a drone in the air

	Bool isOnLauncher() const { return m_state == FLIGHT_ON_LAUNCHER || m_state == FLIGHT_PREPARING; }
	Bool isWaitingOnLauncher() const { return m_state == FLIGHT_ON_LAUNCHER; }
	Bool isPreparing() const { return m_state == FLIGHT_PREPARING; }
	const Coord3D *getImpactPoint() const { return &m_impact; }
	// Hypersonic @feature salvos (D-152)
	Bool isInSalvoWindow( UnsignedInt now ) const;	///< preparing, or left its launcher within the salvo window
	UnsignedInt getLeaveFrame() const { return m_leaveFrame; }
	const Coord3D *getSalvoTarget() const { return &m_salvoTarget; }
	/// Hypersonic @feature (D-149) its velocity in the air (dist/frame; zero on its launcher), for wreck objects that inherit it
	void getFlightVelocity( Coord3D *vel ) const;

	/// Launch at a ground point: picks the impact point (Warhead scatter) and starts the launch preparation.
	Bool launch( const Coord3D *target );

private:

	void startFlight();
	void fly();
	Real followTerrain( const Coord3D *pos, Real dirX, Real dirY, Real horizontal, Real speed );
	void dropBooster();
	void detonate( const Coord3D *pos );
	Real getSpeed() const;
	Real getDiveSpeedFactor( Real horizontal ) const;	///< Hypersonic @feature (D-152)
	void setFlightTransform( const Coord3D *pos );
	void startEngineSound();
	void stopEngineSound();

	Int											m_state;					///< FlightState
	UnsignedInt							m_framesLeft;			///< of the launch preparation
	UnsignedInt							m_flightFrames;		///< frames in the air
	UnsignedInt							m_maxFlightFrames;	///< safety: detonate where it is after this long
	Coord3D									m_impact;					///< impact point (scatter applied)
	Real										m_cruiseZ;				///< cruise height (absolute)
	Real										m_climbAngle;			///< steepest climb allowed now (terrain ahead)
	UnsignedInt							m_boostFramesLeft;	///< of the booster burn
	AudioEventRTS						m_engineLoop;			///< client audio only (not saved; restarted while flying)
	Real										m_yaw;						///< heading
	Real										m_pitch;					///< flight path angle, positive up
	Real										m_pitchSpeed;			///< Hypersonic @feature smooth dive (D-090): pitch change of the last frame
	Real										m_yawSpeed;				///< Hypersonic @feature banked turns (D-152): heading change of the last frame
	Real										m_diveStartDist;	///< Hypersonic @feature (D-152) horizontal distance where the dive began (0: not yet)
	Real										m_diveHeight;			///< Hypersonic @feature (D-152) curved dive: height above the impact point at its start (0: straight dive)
	UnsignedInt							m_leaveFrame;			///< Hypersonic @feature salvos (D-152): frame it leaves (left) its launcher
	Coord3D									m_salvoTarget;		///< Hypersonic @feature salvos (D-152): the ordered target point (before scatter)
	const UpgradeTemplate*	m_speedUpgrade;		///< resolved m_speedUpgradeName
};
