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

// FILE: MunitionMagazineUpdate.h ///////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Launch facility magazine and typed launchers (Generals Hypersonic IRAN_DESIGN 3,
//         7b, 7e, D-008, D-009, D-039, D-042). A munition bought at the facility (ProductionUpdate) does not come
//         out as a unit: it goes into this magazine, in purchase order. The magazine holds Capacity storage units;
//         each Munition takes its size (default 1). The facility has typed launchers (Launchers = <munition>
//         <count> [load time], numbered 01.. in that order): a stored munition loads only onto a free launcher of
//         its type, the oldest purchase first that has one, SimultaneousLoads at a time. A finished load creates the
//         munition object on its launcher seat (SeatBone, on the launcher's LauncherBone pivot), where it can be
//         shot like any unit. It is never selectable: it leaves only by a launch (MunitionLaunchPower), which turns
//         its launcher toward the target at LauncherTurnRate while the munition's CruiseFlightUpdate prepares; then
//         the munition flies off and the launcher turns back to rest and reloads.
//
//         Pad overflow (D-080): a munition may also be bought when the magazine is full but an empty launcher of its
//         type is not yet spoken for (by one stored, waiting or queued); if it does not fit the magazine when
//         complete it waits outside it (overflow) for that launcher and loads first, with the normal load time.
//
//         Everything stored or on the launchers is lost with the facility (killed with it; removed silently when it
//         is sold); a captured facility keeps them for the captor. ComputerRestockReserve lets a computer player
//         buy munitions here to keep its launchers fed while it keeps that much money.
//
//         Other launcher kinds implement MunitionMagazineInterface themselves: the Ballistic Missile Facility's
//         TEL bays and Fattah silo gates are MissileLaunchSiteUpdate (D-040).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "WWMath/matrix3d.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;
class Player;
class Thing;
class ThingTemplate;
class WeaponTemplate;

// Hypersonic @feature (D-122) AI-only purchase probe, using the same kill/value/route rules as launches.
Bool iranComputerMunitionHasTarget( Object *facility, const ThingTemplate *munition, Int prospectiveCount );

//-------------------------------------------------------------------------------------------------
/** What production, the launch power and the UI ask of a launch facility magazine. */
//-------------------------------------------------------------------------------------------------
class MunitionMagazineInterface
{
public:
	virtual Bool storesMunition( const ThingTemplate *munition ) const = 0;		///< is this a munition type of this magazine
	virtual Int getCapacity() const = 0;																			///< magazine room, in storage units
	virtual Int getMunitionSize( const ThingTemplate *munition ) const = 0;		///< storage units one of these takes
	virtual Int getStoredCount( const ThingTemplate *munition ) const = 0;		///< munitions in the magazine (nullptr = every type)
	virtual Int getStoredSize() const = 0;																		///< storage units in use
	virtual void addMunition( const ThingTemplate *munition ) = 0;						///< a purchase has completed
	virtual Int getLauncherCount( const ThingTemplate *munition ) const = 0;	///< launchers that can hold this type (nullptr = all)
	virtual Int getLoadedCount( const ThingTemplate *munition, Bool readyOnly ) const = 0;	///< on launchers (nullptr = every type)
	virtual Bool getNextLoad( const ThingTemplate *munition, UnsignedInt *finishFrame, Real *fractionDone ) const = 0;

	// Hypersonic @feature Launch facilities (chunk 2b, D-040): the launch power works through these, so each
	// launcher kind (drone launchers here, TEL bays and silo gates in MissileLaunchSiteUpdate) launches its own way.
	virtual Bool canLaunchAt( const ThingTemplate *munition, const Coord3D *pos ) const = 0;	///< a ready launcher may fire at pos
	virtual Bool launchMunition( const ThingTemplate *munition, Object *target, const Coord3D *pos ) = 0;	///< TRUE when launched
	virtual UnsignedInt getLaunchGapReadyFrame( UnsignedInt *gapFrames ) const = 0;	///< launcher-side gap end (0: none), and its length

	/// Hypersonic @feature (D-042) the current turn of launcher 'index' away from its rest heading (the draw module
	/// turns the launcher's bone by it); FALSE for launchers that do not turn.
	virtual Bool getLauncherTurn( Int index, Real *angle ) const { return FALSE; }

	/// Hypersonic @feature Pad overflow (D-080): empty launchers of this type that no munition already stored or
	/// waiting here will take, so a purchase beyond the magazine's room can still go straight onto one; 0 for
	/// launcher kinds without overflow (the Ballistic Missile Facility keeps its capacity rule).
	virtual Int getUnclaimedLauncherCount( const ThingTemplate *munition ) const { return 0; }

	/// Hypersonic @feature Iran AI strike targeting (D-110): the weapon whose blast a munition of this type
	/// delivers (nullptr: unknown), and the launches ordered at a target object that have not fired yet (a TEL or
	/// silo gate out on its sortie): fills up to 'max' target IDs and munition types, returns how many.
	virtual const WeaponTemplate *getMunitionWeapon( const ThingTemplate *munition ) const { return nullptr; }
	virtual Int getPendingLaunches( ObjectID *targets, Coord3D *positions, const ThingTemplate **munitions, Int max ) const { return 0; }

	/// Hypersonic @feature (D-189) Iran AI strike doctrine: the next launchMunition call starts its sortie this many
	/// frames late (the launcher waits at its exit first), so a later missile of a salvo lands after the earlier ones
	/// (a GLA rebuild hole exists only once its building is dead). Used once, then back to 0. Launcher kinds without
	/// sorties ignore it.
	virtual void setNextLaunchDelay( UnsignedInt /*frames*/ ) {}
};

//-------------------------------------------------------------------------------------------------
class MunitionMagazineUpdateModuleData : public UpdateModuleData
{
public:

	MunitionMagazineUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseMunition( INI *ini, void *instance, void *store, const void *userData );
	static void parseLaunchers( INI *ini, void *instance, void *store, const void *userData );

	struct LauncherGroup
	{
		AsciiString		m_munitionName;		///< the one type these launchers take
		Int						m_count;					///< launchers
		UnsignedInt		m_loadFrames;			///< time to load one (0: LoadTime)
	};

	Int													m_capacity;						///< storage units the magazine holds (launchers not counted)
	std::vector<AsciiString>		m_munitionNames;			///< object types this magazine stores (Munition, repeatable)
	std::vector<Int>						m_munitionSizes;			///< storage units each takes (same order)
	std::vector<LauncherGroup>	m_launcherGroups;			///< Launchers, in launcher number order
	AsciiString									m_launcherBone;				///< pivot bone prefix, numbered 01..
	AsciiString									m_seatBone;						///< seat bone prefix, numbered 01..
	Real												m_launcherTurnRate;		///< radians per frame (degrees per second in INI)
	UnsignedInt									m_loadFrames;					///< default time to load one launcher (LoadTime, msec in INI)
	Int													m_simultaneousLoads;	///< launchers loading at once
	Int													m_computerRestockReserve;	///< computer players buy while money >= cost + this (-1: never)
};

//-------------------------------------------------------------------------------------------------
class MunitionMagazineUpdate : public UpdateModule, public MunitionMagazineInterface, public DieModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MunitionMagazineUpdate, "MunitionMagazineUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MunitionMagazineUpdate, MunitionMagazineUpdateModuleData )

public:

	MunitionMagazineUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | (MODULEINTERFACE_DIE); }

	// BehaviorModule
	virtual DieModuleInterface* getDie() override { return this; }
	virtual MunitionMagazineInterface* getMunitionMagazineInterface() override { return this; }

	virtual UpdateSleepTime update() override;
	virtual void onDie( const DamageInfo *damageInfo ) override;
	virtual void onDelete() override;
	virtual void onCapture( Player *oldOwner, Player *newOwner ) override;

	// MunitionMagazineInterface
	virtual Bool storesMunition( const ThingTemplate *munition ) const override;
	virtual Int getCapacity() const override;
	virtual Int getMunitionSize( const ThingTemplate *munition ) const override;
	virtual Int getStoredCount( const ThingTemplate *munition ) const override;
	virtual Int getStoredSize() const override;
	virtual void addMunition( const ThingTemplate *munition ) override;
	virtual Int getLauncherCount( const ThingTemplate *munition ) const override;
	virtual Int getLoadedCount( const ThingTemplate *munition, Bool readyOnly ) const override;
	virtual Bool getNextLoad( const ThingTemplate *munition, UnsignedInt *finishFrame, Real *fractionDone ) const override;
	virtual Bool canLaunchAt( const ThingTemplate *munition, const Coord3D *pos ) const override;
	virtual Bool launchMunition( const ThingTemplate *munition, Object *target, const Coord3D *pos ) override;
	virtual UnsignedInt getLaunchGapReadyFrame( UnsignedInt *gapFrames ) const override { if( gapFrames ) *gapFrames = 0; return 0; }
	virtual Bool getLauncherTurn( Int index, Real *angle ) const override;
	virtual Int getUnclaimedLauncherCount( const ThingTemplate *munition ) const override;
	virtual const WeaponTemplate *getMunitionWeapon( const ThingTemplate *munition ) const override;	///< Hypersonic @feature D-110

private:

	enum LauncherState
	{
		LAUNCHER_EMPTY = 0,		///< nothing on it
		LAUNCHER_LOADING,			///< a munition from the magazine is being loaded
		LAUNCHER_OCCUPIED			///< the munition object sits on it (waiting, or preparing to launch)
	};

	struct Launcher
	{
		const ThingTemplate*	m_munition;			///< the type it takes (from INI)
		UnsignedInt						m_loadFrames;		///< time to load it (from INI)
		Int										m_state;				///< LauncherState
		UnsignedInt						m_framesLeft;		///< of the load
		ObjectID							m_occupantID;		///< the munition on it
		Real									m_angle;				///< current turn away from the rest heading
		Real									m_wantedAngle;	///< turn toward the launch target while preparing
	};

	struct LauncherGeometry
	{
		Bool			m_known;			///< read from the facility's bones
		Coord3D		m_pivot;			///< launcher pivot, world
		Matrix3D	m_seat;				///< seat at rest, world
		Real			m_restYaw;		///< heading of the seat at rest
	};

	Int findMunitionIndex( const ThingTemplate *munition ) const;
	Int findEmptyLauncher( const ThingTemplate *munition ) const;	///< -1: none
	Object *getOccupant( const Launcher &launcher ) const;
	Bool isReadyToLaunch( const Launcher &launcher ) const;	///< a munition waits on it, free to launch now
	const LauncherGeometry &getGeometry( Int index ) const;
	void placeOccupant( Int index, Object *munition ) const;
	void deliverToLauncher( Int index );
	void restockForComputer();
	void removeOccupants( Bool kill );
	void xferState( Xfer *xfer, Int version );

	std::vector<const ThingTemplate*>	m_munitions;	///< resolved m_munitionNames
	std::vector<const ThingTemplate*>	m_magazine;		///< stored munitions, oldest purchase first
	std::vector<const ThingTemplate*>	m_overflow;		///< Hypersonic @feature (D-080) bought beyond the magazine's room, waiting for their launcher, oldest first
	std::vector<Launcher>							m_launchers;
	mutable std::vector<LauncherGeometry>	m_geometry;	///< bone positions, read on first use (not saved)
	UnsignedInt												m_nextRestockFrame;	///< computer restock: next check
	Int																m_restockIndex;			///< computer restock: next munition to buy
};
