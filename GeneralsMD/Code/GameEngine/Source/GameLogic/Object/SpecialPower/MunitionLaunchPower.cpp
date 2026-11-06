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

// FILE: MunitionLaunchPower.cpp ////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Launch one loaded munition from a launch facility (see MunitionLaunchPower.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/MunitionLaunchPower.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Object.h"

// "never" for a source with nothing loaded; below the disabled-source value Player uses (UINT_MAX - 10)
static const UnsignedInt NOTHING_LOADED_FRAME = 0xFFFFFFFF - 20;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionLaunchPowerModuleData::MunitionLaunchPowerModuleData()
{
	m_launchGapTimer = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ void MunitionLaunchPowerModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	SpecialPowerModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Munition",				INI::parseAsciiString,						nullptr, offsetof( MunitionLaunchPowerModuleData, m_munitionName ) },
		{ "LaunchGapTimer",	INI::parseSpecialPowerTemplate,		nullptr, offsetof( MunitionLaunchPowerModuleData, m_launchGapTimer ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionLaunchPower::MunitionLaunchPower( Thing *thing, const ModuleData *moduleData ) : SpecialPowerModule( thing, moduleData )
{
	const MunitionLaunchPowerModuleData *data = getMunitionLaunchPowerModuleData();
	m_munition = TheThingFactory->findTemplate( data->m_munitionName );
	DEBUG_ASSERTCRASH( m_munition != nullptr, ("MunitionLaunchPower: unknown Munition '%s'", data->m_munitionName.str()) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionLaunchPower::~MunitionLaunchPower()
{
}

//-------------------------------------------------------------------------------------------------
MunitionMagazineInterface *MunitionLaunchPower::getMagazine() const
{
	if( m_munition == nullptr )
		return nullptr;

	for( BehaviorModule **m = getObject()->getBehaviorModules(); *m; ++m )
	{
		MunitionMagazineInterface *magazine = (*m)->getMunitionMagazineInterface();
		if( magazine != nullptr && magazine->storesMunition( m_munition ) )
			return magazine;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const SpecialPowerTemplate *MunitionLaunchPower::getGapTimer() const
{
	const SpecialPowerTemplate *gap = getMunitionLaunchPowerModuleData()->m_launchGapTimer;
	return gap != nullptr ? gap : getSpecialPowerTemplate();
}

//-------------------------------------------------------------------------------------------------
// The frame the launch gap ends (0 when none runs): the player-wide gap timer of the launch powers, or the
// facility's own gap between launches (TEL bays, D-040), whichever ends later. Read-only: the client asks too.
UnsignedInt MunitionLaunchPower::getGapReadyFrame( UnsignedInt *gapFrames ) const
{
	UnsignedInt frame = 0;
	UnsignedInt frames = getGapTimer()->getReloadTime();
	const Player *player = getObject()->getControllingPlayer();
	if( player == nullptr || !player->findSpecialPowerReadyFrame( getGapTimer(), &frame ) )
		frame = 0;

	const MunitionMagazineInterface *magazine = getMagazine();
	UnsignedInt launcherFrames = 0;
	UnsignedInt launcherFrame = magazine != nullptr ? magazine->getLaunchGapReadyFrame( &launcherFrames ) : 0;
	if( launcherFrame > frame )
	{
		frame = launcherFrame;
		frames = launcherFrames;
	}

	if( gapFrames )
		*gapFrames = frames;
	return frame;
}

//-------------------------------------------------------------------------------------------------
Int MunitionLaunchPower::getShortcutReadyCount() const
{
	const MunitionMagazineInterface *magazine = getMagazine();
	if( magazine == nullptr || getObject()->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || getObject()->testStatus( OBJECT_STATUS_SOLD ) )
		return 0;
	return magazine->getLoadedCount( m_munition, TRUE );
}

//-------------------------------------------------------------------------------------------------
Bool MunitionLaunchPower::isReady() const
{
	if( m_pausedCount > 0 )
		return FALSE;

	if( getShortcutReadyCount() <= 0 )
		return FALSE;

	return TheGameLogic->getFrame() >= getGapReadyFrame( nullptr );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-040): the cursor refuses a target that no ready launcher of this facility may fire
// at (inside the missile's minimum range).
Bool MunitionLaunchPower::isValidTargetLocation( const Coord3D *loc ) const
{
	const MunitionMagazineInterface *magazine = getMagazine();
	if( magazine == nullptr || loc == nullptr )
		return TRUE;
	if( getShortcutReadyCount() <= 0 )
		return TRUE;	// nothing ready: the button is greyed anyway
	return magazine->canLaunchAt( m_munition, loc );
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MunitionLaunchPower::getReadyFrame() const
{
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt gapReady = getGapReadyFrame( nullptr );

	if( getShortcutReadyCount() > 0 )
		return gapReady <= now ? 0 : gapReady;

	// nothing ready here: when the next load of this type lands on its launcher, if one is under way
	const MunitionMagazineInterface *magazine = getMagazine();
	UnsignedInt loadedFrame = 0;
	if( magazine != nullptr && magazine->getNextLoad( m_munition, &loadedFrame, nullptr ) )
		return loadedFrame > gapReady ? loadedFrame : gapReady;

	return NOTHING_LOADED_FRAME;
}

//-------------------------------------------------------------------------------------------------
Real MunitionLaunchPower::getPercentReady() const
{
	if( isReady() )
		return 1.0f;

	const Real ALMOST_READY = 0.99999f;
	UnsignedInt now = TheGameLogic->getFrame();

	// loaded, waiting for the launch gap
	if( m_pausedCount == 0 && getShortcutReadyCount() > 0 )
	{
		UnsignedInt gapFrames = 0;
		UnsignedInt gapReady = getGapReadyFrame( &gapFrames );
		if( gapFrames == 0 || gapReady <= now )
			return ALMOST_READY;
		Real percent = 1.0f - (Real)(gapReady - now) / (Real)gapFrames;
		return percent < 0.0f ? 0.0f : (percent > ALMOST_READY ? ALMOST_READY : percent);
	}

	// nothing loaded: progress of the next load of this type
	const MunitionMagazineInterface *magazine = getMagazine();
	Real fraction = 0.0f;
	if( magazine != nullptr && magazine->getNextLoad( m_munition, nullptr, &fraction ) )
		return fraction < 0.0f ? 0.0f : (fraction > ALMOST_READY ? ALMOST_READY : fraction);

	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
// Launches one munition at the target (an object, else the position); on success starts the player-wide
// gap and announces the launch like any special power (no EVA of its own).
//-------------------------------------------------------------------------------------------------
void MunitionLaunchPower::launch( Object *target, const Coord3D *pos )
{
	Object *facility = getObject();
	Player *player = facility->getControllingPlayer();
	if( player == nullptr || pos == nullptr || m_pausedCount > 0 || facility->isDisabled() || !isReady() )
		return;

	MunitionMagazineInterface *magazine = getMagazine();
	if( magazine == nullptr || !magazine->canLaunchAt( m_munition, pos ) )
		return;
	if( !magazine->launchMunition( m_munition, target, pos ) )
		return;

	// player-wide gap before the next launch
	const SpecialPowerTemplate *gap = getGapTimer();
	player->expressSpecialPowerReadyFrame( gap, TheGameLogic->getFrame() + gap->getReloadTime() );

	aboutToDoSpecialPower( pos );
}

//-------------------------------------------------------------------------------------------------
void MunitionLaunchPower::doSpecialPowerAtLocation( const Coord3D *loc, Real angle, UnsignedInt commandOptions )
{
	if( loc == nullptr )
		return;

	launch( nullptr, loc );
}

//-------------------------------------------------------------------------------------------------
void MunitionLaunchPower::doSpecialPowerAtObject( Object *obj, UnsignedInt commandOptions )
{
	if( obj == nullptr )
		return;

	launch( obj, obj->getPosition() );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MunitionLaunchPower::crc( Xfer *xfer )
{

	// extend base class
	SpecialPowerModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void MunitionLaunchPower::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SpecialPowerModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MunitionLaunchPower::loadPostProcess()
{

	// extend base class
	SpecialPowerModule::loadPostProcess();

}
