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

// FILE: SpyVisionUpdate.cpp /////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, September 2002
// Desc:   Special Power will spy on the vision of all enemy players.
//				Putting a SpecialPower in a behavior takes a big huge amount of code duplication and
//				has no precedent.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpyVisionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature science-levelled reveal (D-066): "RevealLevel = <science> <OCL>".
/*static*/ void SpyVisionUpdateModuleData::parseRevealLevel( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	RevealLevel level;
	level.m_science = SCIENCE_INVALID;
	level.m_ocl = nullptr;
	INI::parseScience( ini, nullptr, &level.m_science, nullptr );
	INI::parseObjectCreationList( ini, nullptr, &level.m_ocl, nullptr );
	((std::vector<RevealLevel>*)store)->push_back( level );
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpyVisionUpdate::SpyVisionUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_deactivateFrame = 0;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	m_currentlyActive = FALSE;
	m_resetTimersNextUpdate = FALSE;
	m_disabledUntilFrame = 0;
	// Hypersonic (D-066): a science-levelled reveal checks its owner's sciences from the start.
	if( !getSpyVisionUpdateModuleData()->m_revealLevels.empty() )
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpyVisionUpdate::~SpyVisionUpdate()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::activateSpyVision( UnsignedInt duration )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( duration == 0 )
		m_deactivateFrame = UINT_MAX;
	else
		m_deactivateFrame = now + duration;

	doActivationWork( getObject()->getControllingPlayer(), TRUE );

	if( duration == 0 )
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	else
		setWakeFrame( getObject(), UPDATE_SLEEP(duration) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::setDisabledUntilFrame( UnsignedInt frame )
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( frame > now )
	{
		//Turn off the spy vision now since we are disabled
		if( m_currentlyActive )
		{
			doActivationWork( getObject()->getControllingPlayer(), FALSE );
		}

		//When should we wake up?
		m_disabledUntilFrame = frame;

		//Mark it so we can turn it on again on next update.
		m_resetTimersNextUpdate = TRUE;

		//Now sleep until the disabled has expired. If it's expired again when we come back due to another
		//sabotage or something else... we'll do the same thing over again.
		setWakeFrame( getObject(), (UpdateSleepTime)(m_disabledUntilFrame - now) );
	}
	else
	{
		// Else it is a wakeup message.  Update does the turning on.
		m_disabledUntilFrame = now;
		m_resetTimersNextUpdate = TRUE;
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onCapture( Player *oldOwner, Player *newOwner )
{
	if( m_currentlyActive )
	{
		// If on, flick the switch real fast to get everything to update for the new owner
		doActivationWork( oldOwner, FALSE );
		doActivationWork( newOwner, TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onDisabledEdge( Bool nowDisabled )
{
	if( nowDisabled )
		setDisabledUntilFrame(UINT_MAX);
	else
		setDisabledUntilFrame(0);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SpyVisionUpdate::update()
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	UnsignedInt now = TheGameLogic->getFrame();

	if( !data->m_revealLevels.empty() )
		return updateReveal();	// Hypersonic (D-066)

	//Once disable has complete and we are waking up from it, then reset the interval so
	//it has to wait before going on again. If it doesn't have an interval, we turn it on
	//immediately, but only for self powered objects.
	if( m_resetTimersNextUpdate )
	{
		m_resetTimersNextUpdate = FALSE;

		if( data->m_selfPowered )
		{
			if( data->m_selfPoweredInterval == 0 )
			{
				//It's an always on self-powered special. So turn it back on now.
				doActivationWork( getObject()->getControllingPlayer(), TRUE );
				return UPDATE_SLEEP( UPDATE_SLEEP_FOREVER );
			}
			else
			{
				//Reset the interval timer via sleeping (so we have to wait a longer time before going on)
				return UPDATE_SLEEP( data->m_selfPoweredInterval );
			}
		}
	}

	if( m_currentlyActive && (m_deactivateFrame <= TheGameLogic->getFrame()) )
	{
		// Turn off SpyVision.
		doActivationWork( getObject()->getControllingPlayer(), FALSE );

		m_deactivateFrame = 0;
	}
	else if( !m_currentlyActive && data->m_selfPowered )
	{
		doActivationWork( getObject()->getControllingPlayer(), TRUE );
		if( data->m_selfPoweredDuration == 0 )
			m_deactivateFrame = UINT_MAX;
		else
			m_deactivateFrame = now + data->m_selfPoweredDuration;
	}

	if( data->m_selfPowered )
	{
		if( m_currentlyActive )
			return UPDATE_SLEEP(data->m_selfPoweredDuration);
		else
			return UPDATE_SLEEP(data->m_selfPoweredInterval);
	}

	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature science-levelled reveal (D-066). The next reveal frame is kept in m_deactivateFrame (saved; the
// spy-vision use of it never runs in this mode), so there is no save-format change. Before a level is bought it checks
// once a second, so the first reveal comes right after the purchase; then every SelfPoweredInterval.
//-------------------------------------------------------------------------------------------------
namespace
{
	struct RevealTargets
	{
		KindOfMaskType kinds;
		std::vector<Coord3D> *positions;
	};
	void collectRevealTarget( Object *obj, void *userData )
	{
		RevealTargets *targets = (RevealTargets *)userData;
		if( obj != nullptr && !obj->isEffectivelyDead() && obj->isAnyKindOf( targets->kinds ) )
			targets->positions->push_back( *obj->getPosition() );
	}

	struct RevealerSearch
	{
		const Object *self;
		Bool foundLower;
	};
	void findLowerRevealer( Object *obj, void *userData )
	{
		RevealerSearch *search = (RevealerSearch *)userData;
		if( obj == nullptr || obj == search->self || search->foundLower )
			return;
		if( obj->getID() < search->self->getID() && obj->getTemplate()->isEquivalentTo( search->self->getTemplate() )
			&& !obj->isEffectivelyDead() && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			&& !obj->testStatus( OBJECT_STATUS_SOLD ) )
			search->foundLower = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Bool SpyVisionUpdate::isPlayersRevealer() const
{
	const Object *obj = getObject();
	const Player *owner = obj->getControllingPlayer();
	if( owner == nullptr )
		return FALSE;
	RevealerSearch search;
	search.self = obj;
	search.foundLower = FALSE;
	owner->iterateObjects( findLowerRevealer, &search );
	return !search.foundLower;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime SpyVisionUpdate::updateReveal()
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	Object *obj = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt checkFrames = LOGICFRAMES_PER_SECOND;
	m_resetTimersNextUpdate = FALSE;

	Player *owner = obj->getControllingPlayer();
	const ObjectCreationList *ocl = nullptr;
	if( owner != nullptr )
	{
		for( std::vector<SpyVisionUpdateModuleData::RevealLevel>::const_iterator it = data->m_revealLevels.begin();
				it != data->m_revealLevels.end(); ++it )
		{
			if( it->m_ocl != nullptr && owner->hasScience( it->m_science ) )
				ocl = it->m_ocl;	// listed lowest level first: the highest one owned wins
		}
	}
	if( ocl == nullptr || ThePlayerList == nullptr )
		return UPDATE_SLEEP( checkFrames );
	if( obj->isEffectivelyDead() || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD )
		|| obj->isDisabled() )
		return UPDATE_SLEEP( checkFrames );
	if( now < m_deactivateFrame )
		return UPDATE_SLEEP( m_deactivateFrame - now );
	const UnsignedInt interval = max( (UnsignedInt)1, data->m_selfPoweredInterval );
	if( !isPlayersRevealer() )
	{
		// another of the player's objects does it: wait a full interval as if this one had revealed, so if that one is
		// lost, this one takes over without revealing early
		m_deactivateFrame = now + interval;
		return UPDATE_SLEEP( interval );
	}

	std::vector<Coord3D> positions;
	RevealTargets targets;
	targets.kinds = data->m_spyOnKindof;
	targets.positions = &positions;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( player == nullptr || player == owner || player->getDefaultTeam() == nullptr )
			continue;
		if( owner->getRelationship( player->getDefaultTeam() ) == ENEMIES )
			player->iterateObjects( collectRevealTarget, &targets );
	}
	for( std::vector<Coord3D>::const_iterator p = positions.begin(); p != positions.end(); ++p )
		ObjectCreationList::create( ocl, obj, &(*p), &(*p), 0.0f );

	m_deactivateFrame = now + interval;
	return UPDATE_SLEEP( interval );
}

//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::doActivationWork( Player *playerToSetFor, Bool setting )
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	if( playerToSetFor == nullptr  ||  ThePlayerList == nullptr )
		return;

	for (Int i=0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *player = ThePlayerList->getNthPlayer(i);
		if( playerToSetFor->getRelationship(player->getDefaultTeam()) == ENEMIES )
		{
			player->setUnitsVisionSpied( setting, data->m_spyOnKindof, playerToSetFor->getPlayerIndex() );
		}
	}

	m_currentlyActive = setting;
}

//-------------------------------------------------------------------------------------------------
void SpyVisionUpdate::onDelete()
{
	// If I was left on at the time of death, then turn me off.
	if( m_currentlyActive )
	{
		doActivationWork( getObject()->getControllingPlayer(), FALSE );
	}
}

// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::upgradeImplementation()
{
	const SpyVisionUpdateModuleData *data = getSpyVisionUpdateModuleData();
	if( data->m_needsUpgrade && !isAlreadyUpgraded() )
	{
		activateSpyVision(data->m_selfPoweredDuration);// If zero, will turn on permanently.  And it does the wake up setting
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// deactivate frame
	xfer->xferUnsignedInt( &m_deactivateFrame );

	xfer->xferBool( &m_currentlyActive );

	if( version >= 2 )
	{
		xfer->xferBool( &m_resetTimersNextUpdate );
		xfer->xferUnsignedInt( &m_disabledUntilFrame );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SpyVisionUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
