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

// FILE: MissileLaunchSiteUpdate.h //////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Ballistic Missile Facility launch sites (Generals Hypersonic IRAN_DESIGN 3, 7, 7b,
//         D-014, D-031, D-040): a magazine plus a fixed number of launch sites that hold one missile each,
//         hidden inside the facility. Two kinds, both by INI numbers:
//
//         * TEL bays: a launch spawns the TEL (a unit) at its door bone, drives it to its pad bone (ExitTime),
//           erects the missile (PrepareTime, the TEL's turret pitches up), holds a random LaunchHoldMin..Max
//           (D-084), ignites (LaunchTime), fires the TEL's PRIMARY weapon at the target, stays on the pad for
//           PostLaunchHold (D-084) and drives back in (ReturnTime), where the TEL is removed and reloads
//           from the magazine (LoadTime). A TEL killed outside is lost with its missile; the bay gets a new TEL
//           after RegenerateTime.
//         * Silo gates (no PadBone): the gate object appears at the bone while the gate is open (ExitTime), the
//           missile fires after LaunchTime and the gate closes (ReturnTime). The gate object can be shot only
//           then; killing it loses the missile and the silo needs RegenerateTime.
//           Hypersonic @feature (D-199): the gate object shows DOOR_1_OPENING while its gate opens (after any D-189
//           delay) and stays open, DOOR_1_CLOSING while it closes; SiteCoverSubObject = <prefix> names the facility's
//           own closed gate meshes (<prefix>01..), hidden while the gate object is out or the silo regenerates.
//
//         Purchases come from the facility's ProductionUpdate (MunitionMagazineInterface, as the drone magazine),
//         launches from MunitionLaunchPower. LaunchGap is the facility-wide minimum time between two launches of
//         this module. Everything inside is lost with the facility; launchers outside are killed with it.
//         ComputerRestockReserve lets a computer player buy missiles here while it keeps that much money.
//         D-251: PlayerSalvoGap spaces actual human-player lift-offs across this facility's opted-in modules,
//         after normal preparation/holds. Orders remain available; computer players keep their existing salvos.
//
//         Hypersonic @feature cook-off (D-142): Munition = <stored> <launcher> [<cook-off weapon>]. A missile lost on its
//         launcher (TEL or silo gate killed while it still carries the missile, also when the facility dies and takes
//         it along) detonates the cook-off weapon at the launcher with CookOffChance (one logic-random roll per missile).
//         Missiles inside the facility, or a launcher removed silently (sold), never cook off.
//         Hypersonic @feature target reveal (D-143, D-213): every launch order creates TargetRevealObject (an invisible
//         vision object) at its aim point for the facility's player; the missile takes it over when it leaves the
//         launcher and ends it TargetRevealLinger after it lands or is shot down (MissileAIUpdate::setTargetReveal). A
//         launch that never fires (launcher lost, missile brought back) ends it TargetRevealLinger after that.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Module/UpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;
class Thing;
class ThingTemplate;
class WeaponTemplate;

//-------------------------------------------------------------------------------------------------
class MissileLaunchSiteUpdateModuleData : public UpdateModuleData
{
public:

	MissileLaunchSiteUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseMunition( INI *ini, void *instance, void *store, const void *userData );

	Int													m_capacity;						///< missiles in the magazine (loaded sites not counted)
	std::vector<AsciiString>		m_munitionNames;			///< stored object types (Munition, first token)
	std::vector<AsciiString>		m_launcherNames;			///< the launcher object for each munition (second token)
	std::vector<AsciiString>		m_cookOffWeaponNames;	///< Hypersonic @feature D-142: optional third token (empty: none)
	Real												m_cookOffChance;			///< Hypersonic @feature D-142: chance a lost missile detonates (0..1)
	AsciiString									m_targetRevealName;		///< Hypersonic @feature D-143: vision object at the aim point (empty: none)
	UnsignedInt									m_targetRevealLingerFrames;	///< Hypersonic @feature D-143: it stays this long after the missile
	Int													m_siteCount;					///< launch sites (TEL bays, silo gates)
	AsciiString									m_exitBone;						///< bone prefix, numbered 01..: TEL door / silo gate
	AsciiString									m_padBone;						///< bone prefix of the TEL pads; empty for silo gates
	UnsignedInt									m_exitFrames;					///< door to pad / gate opening
	UnsignedInt									m_prepareFrames;			///< erect + fuel/align
	UnsignedInt									m_launchFrames;				///< ignition + launch; the missile fires at the end
	UnsignedInt									m_launchHoldMinFrames;	///< Hypersonic @feature D-084: random hold on the pad before ignition, min
	UnsignedInt									m_launchHoldMaxFrames;	///< Hypersonic @feature D-084: ... and max (0/0: no hold)
	UnsignedInt									m_postLaunchFrames;		///< Hypersonic @feature D-084: hold on the pad after the missile fires
	UnsignedInt									m_returnFrames;				///< pad to door / gate closing
	UnsignedInt									m_loadFrames;					///< reload one site from the magazine
	UnsignedInt									m_regenerateFrames;		///< a lost TEL / gate is replaced after this
	UnsignedInt									m_launchGapFrames;		///< facility-wide minimum time between launches
	UnsignedInt m_playerSalvoGapFrames;	///< D-251: human lift-off gap; 0 keeps legacy behavior
	Int													m_computerRestockReserve;	///< computer players buy while money >= cost + this (-1: never)
	AsciiString									m_siteCoverSubObject;	///< Hypersonic @feature D-199: facility sub-object prefix (01..) hidden while a silo's gate object is out
};

//-------------------------------------------------------------------------------------------------
class MissileLaunchSiteUpdate : public UpdateModule, public MunitionMagazineInterface, public DieModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MissileLaunchSiteUpdate, "MissileLaunchSiteUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MissileLaunchSiteUpdate, MissileLaunchSiteUpdateModuleData )

public:

	MissileLaunchSiteUpdate( Thing *thing, const ModuleData* moduleData );
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
	virtual Int getMunitionSize( const ThingTemplate * /*munition*/ ) const override { return 1; }
	virtual Int getStoredCount( const ThingTemplate *munition ) const override;
	virtual Int getStoredSize() const override { return (Int)m_magazine.size(); }
	virtual void addMunition( const ThingTemplate *munition ) override;
	virtual Int getLauncherCount( const ThingTemplate * /*munition*/ ) const override { return (Int)m_sites.size(); }	///< every site takes every type
	virtual Int getLoadedCount( const ThingTemplate *munition, Bool readyOnly ) const override;
	virtual Bool getNextLoad( const ThingTemplate *munition, UnsignedInt *finishFrame, Real *fractionDone ) const override;
	virtual Bool canLaunchAt( const ThingTemplate *munition, const Coord3D *pos ) const override;
	virtual Bool launchMunition( const ThingTemplate *munition, Object *target, const Coord3D *pos ) override;
	virtual UnsignedInt getLaunchGapReadyFrame( UnsignedInt *gapFrames ) const override;
	virtual const WeaponTemplate *getMunitionWeapon( const ThingTemplate *munition ) const override;	///< Hypersonic @feature D-110
	virtual Int getPendingLaunches( ObjectID *targets, Coord3D *positions, const ThingTemplate **munitions, Int max ) const;	///< Hypersonic @feature D-110
	virtual void setNextLaunchDelay( UnsignedInt frames ) override { m_nextLaunchDelay = frames; }	///< Hypersonic @feature D-189

private:

	enum SiteState
	{
		SITE_EMPTY = 0,			///< inside, no missile
		SITE_LOADING,				///< inside, a missile is being loaded from the magazine
		SITE_LOADED,				///< inside, ready to launch
		SITE_SORTIE,				///< the launcher is out (TEL on its way / gate open)
		SITE_REGENERATING		///< the launcher was lost; a new one is on its way
	};

	enum SortiePhase
	{
		PHASE_EXIT = 0,			///< door to pad / gate opening
		PHASE_PREPARE,			///< erect + fuel/align
		PHASE_LAUNCH,				///< ignition; fires at the end
		PHASE_RETURN,				///< pad to door / gate closing
		PHASE_HOLD,					///< Hypersonic @feature D-084: erected, random wait before ignition (appended: saved values stay valid)
		PHASE_POST_LAUNCH		///< Hypersonic @feature D-084: fired, still erected on the pad before the return
	};

	struct Site
	{
		Int										m_state;				///< SiteState
		Int										m_phase;				///< SortiePhase while SITE_SORTIE
		const ThingTemplate*	m_munition;			///< loading / loaded / carried; nullptr once fired
		UnsignedInt						m_framesLeft;		///< of the load, regeneration or sortie phase
		ObjectID							m_launcherID;		///< the TEL / gate object while out
		ObjectID							m_targetID;			///< target object, if the launch named one
		Coord3D								m_targetPos;		///< target position at the launch order
		ObjectID							m_revealID;			///< Hypersonic @feature D-213: target reveal from the order until the missile takes it
	};

	Int findMunitionIndex( const ThingTemplate *munition ) const;
	const WeaponTemplate *getLaunchWeapon( Int munitionIndex ) const;
	Bool getSitePositions( Int site, Coord3D *exitPos, Coord3D *padPos, Real *angle ) const;
	Bool isSiteInRange( Int site, Int munitionIndex, const Coord3D *pos ) const;
	void updateSortie( Int site );
	void startPhase( Site &site, Int phase );
	void fireMissile( Site &site, Object *launcher );
	UnsignedInt getPlayerSalvoReadyFrame() const;	///< D-251: latest lift-off gap end across opted-in sibling modules
	void placeLauncher( Object *launcher, Int site, Real fraction ) const;
	void restockForComputer();
	void killOutsideLaunchers( Bool silently );
	Real getLauncherFraction( const Site &site ) const;	///< where placeLauncher puts it this phase (exit 0 .. pad 1)
	void cookOff( const ThingTemplate *munition, const Coord3D *pos );	///< Hypersonic @feature D-142
	ObjectID createTargetReveal( const Coord3D *pos, const ThingTemplate *munition );	///< Hypersonic @feature D-143, D-213
	void handTargetReveal( Site &site, ObjectID projectileID, const Coord3D *aim );	///< Hypersonic @feature D-213
	void endSiteReveal( Site &site );	///< Hypersonic @feature D-213
	void updateGateLook( const Site &site, Object *launcher ) const;	///< Hypersonic @feature D-199
	void updateSiteCovers();	///< Hypersonic @feature D-199
	void xferState( Xfer *xfer, XferVersion version );

	std::vector<const ThingTemplate*>	m_munitions;	///< resolved m_munitionNames
	std::vector<const ThingTemplate*>	m_launchers;	///< resolved m_launcherNames, same order
	std::vector<const WeaponTemplate*>	m_cookOffWeapons;	///< Hypersonic @feature D-142: resolved m_cookOffWeaponNames, same order
	std::vector<const ThingTemplate*>	m_magazine;		///< stored missiles, oldest purchase first
	std::vector<Site>									m_sites;
	UnsignedInt												m_nextLaunchFrame;	///< the facility-wide launch gap ends here
	UnsignedInt												m_nextRestockFrame;	///< computer restock: next check
	Int																m_restockIndex;			///< computer restock: next munition to buy
	UnsignedInt												m_nextLaunchDelay;	///< Hypersonic @feature D-189: extra exit wait of the next launch (transient: set and used in one call)
	UnsignedInt m_nextPlayerSalvoFrame;	///< D-251: gap after this module's last human lift-off (saved)
	std::vector<Int>									m_siteCoverHidden;	///< Hypersonic @feature D-199: cover shown (0) / hidden (1) / not applied yet (-1); drawing only, not saved
	std::vector<UnsignedInt>					m_siteSortieFrame;	///< Hypersonic @feature D-199: frame each site's gate object was created (drawing only, not saved; 0 after a load)
};
