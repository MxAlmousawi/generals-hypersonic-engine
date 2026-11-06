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

// FILE: MunitionLaunchPower.h //////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Launch one loaded munition of one type from a launch facility (Generals Hypersonic
//         IRAN_DESIGN 3, D-009, D-027, D-039). A special power on the facility, so it works from the facility's
//         command set, from the side-bar shortcut (one button per munition type, fired from whichever of the
//         player's facilities has one ready) and from scripts.
//
//         The power is ready while the facility's magazine module (MunitionMagazineUpdate: drones on typed
//         launchers; MissileLaunchSiteUpdate: TELs and silo gates, D-040) has a Munition of this type ready on a
//         launcher and the launch gap has passed. The gap is a player-wide timer of the LaunchGapTimer special
//         power (its ReloadTime), shared by every launch power that names it; without one, the power's own
//         ReloadTime is the gap (0 for Iran's powers since D-042: no gap); a magazine may add its own gap (the
//         TEL bays' LaunchGap). Firing hands the launch to the magazine module (a drone prepares on its launcher
//         and flies to the ground point by itself, D-042; a TEL starts its sortie; a silo opens its gate) and
//         restarts the gap. The side-bar count is the number of such ready munitions.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/SpecialPowerModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class MunitionMagazineInterface;
class SpecialPowerTemplate;
class Thing;
class ThingTemplate;

//-------------------------------------------------------------------------------------------------
class MunitionLaunchPowerModuleData : public SpecialPowerModuleData
{
public:

	MunitionLaunchPowerModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	AsciiString									m_munitionName;		///< object type this power launches (Munition)
	const SpecialPowerTemplate*	m_launchGapTimer;	///< player-wide gap timer shared with other launch powers (optional)
};

//-------------------------------------------------------------------------------------------------
class MunitionLaunchPower : public SpecialPowerModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MunitionLaunchPower, "MunitionLaunchPower" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MunitionLaunchPower, MunitionLaunchPowerModuleData )

public:

	MunitionLaunchPower( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

	// SpecialPowerModuleInterface
	virtual Bool isReady() const override;
	virtual Real getPercentReady() const override;
	virtual UnsignedInt getReadyFrame() const override;
	virtual Int getShortcutReadyCount() const override;
	virtual Bool isValidTargetLocation( const Coord3D *loc ) const override;
	virtual const ThingTemplate *getMunitionTemplate() const override { return m_munition; }	///< Hypersonic @feature D-110

	virtual void doSpecialPower( UnsignedInt commandOptions ) override { return; }	// needs a target
	virtual void doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions ) override;
	virtual void doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions ) override;

private:

	MunitionMagazineInterface *getMagazine() const;
	const SpecialPowerTemplate *getGapTimer() const;
	UnsignedInt getGapReadyFrame( UnsignedInt *gapFrames ) const;
	void launch( Object *target, const Coord3D *pos );

	const ThingTemplate *m_munition;	///< resolved m_munitionName
};
