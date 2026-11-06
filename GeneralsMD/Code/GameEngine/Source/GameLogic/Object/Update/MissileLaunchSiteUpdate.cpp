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

// FILE: MissileLaunchSiteUpdate.cpp ////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Ballistic Missile Facility launch sites (see MissileLaunchSiteUpdate.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/HypersonicScenario.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DeletionUpdate.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Module/MissileLaunchSiteUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

// how often a computer player's facility considers buying a missile
static const UnsignedInt RESTOCK_CHECK_FRAMES = LOGICFRAMES_PER_SECOND * 2;
// the erecting TEL's turret aims at a point this far ahead of it (inside the minimum range, so it never fires by itself)
static const Real ERECT_AIM_DISTANCE = 30.0f;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MissileLaunchSiteUpdateModuleData::MissileLaunchSiteUpdateModuleData()
{
	m_capacity = 0;
	m_siteCount = 0;
	m_exitFrames = 0;
	m_prepareFrames = 0;
	m_launchFrames = 0;
	m_launchHoldMinFrames = 0;
	m_launchHoldMaxFrames = 0;
	m_postLaunchFrames = 0;
	m_returnFrames = 0;
	m_loadFrames = 0;
	m_regenerateFrames = 0;
	m_launchGapFrames = 0;
	m_playerSalvoGapFrames = 0;
	m_computerRestockReserve = -1;
	m_cookOffChance = 0.0f;
	m_targetRevealLingerFrames = 0;
}

//-------------------------------------------------------------------------------------------------
// Munition = <stored object> <launcher object> [<cook-off weapon>]	(Hypersonic @feature D-142: the third token)
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdateModuleData::parseMunition( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	MissileLaunchSiteUpdateModuleData *self = (MissileLaunchSiteUpdateModuleData *)instance;
	const char *munition = ini->getNextToken();
	const char *launcher = ini->getNextToken();
	const char *cookOff = ini->getNextTokenOrNull();
	self->m_munitionNames.push_back( AsciiString( munition ) );
	self->m_launcherNames.push_back( AsciiString( launcher ) );
	self->m_cookOffWeaponNames.push_back( AsciiString( cookOff != nullptr ? cookOff : "" ) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Capacity",								INI::parseInt,									nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_capacity ) },
		{ "Munition",								parseMunition,									nullptr, 0 },
		{ "LaunchSites",						INI::parseInt,									nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_siteCount ) },
		{ "ExitBone",								INI::parseAsciiString,					nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_exitBone ) },
		{ "PadBone",								INI::parseAsciiString,					nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_padBone ) },
		{ "ExitTime",								INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_exitFrames ) },
		{ "PrepareTime",						INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_prepareFrames ) },
		{ "LaunchTime",							INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_launchFrames ) },
		{ "LaunchHoldMin",					INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_launchHoldMinFrames ) },	// Hypersonic @feature D-084
		{ "LaunchHoldMax",					INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_launchHoldMaxFrames ) },	// Hypersonic @feature D-084
		{ "PostLaunchHold",					INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_postLaunchFrames ) },		// Hypersonic @feature D-084
		{ "ReturnTime",							INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_returnFrames ) },
		{ "LoadTime",								INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_loadFrames ) },
		{ "RegenerateTime",					INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_regenerateFrames ) },
		{ "PlayerSalvoGap", INI::parseDurationUnsignedInt, nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_playerSalvoGapFrames ) },
		{ "LaunchGap",							INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_launchGapFrames ) },
		{ "ComputerRestockReserve",	INI::parseInt,									nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_computerRestockReserve ) },
		{ "CookOffChance",					INI::parsePercentToReal,				nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_cookOffChance ) },					// Hypersonic @feature D-142
		{ "TargetRevealObject",			INI::parseAsciiString,					nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_targetRevealName ) },				// Hypersonic @feature D-143
		{ "TargetRevealLinger",			INI::parseDurationUnsignedInt,	nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_targetRevealLingerFrames ) },	// Hypersonic @feature D-143
		{ "SiteCoverSubObject",			INI::parseAsciiString,					nullptr, offsetof( MissileLaunchSiteUpdateModuleData, m_siteCoverSubObject ) },			// Hypersonic @feature D-199
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MissileLaunchSiteUpdate::MissileLaunchSiteUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	for( size_t i = 0; i < data->m_munitionNames.size(); ++i )
	{
		const ThingTemplate *munition = TheThingFactory->findTemplate( data->m_munitionNames[i] );
		const ThingTemplate *launcher = TheThingFactory->findTemplate( data->m_launcherNames[i] );
		DEBUG_ASSERTCRASH( munition != nullptr && launcher != nullptr, ("MissileLaunchSiteUpdate: unknown Munition '%s %s'",
			data->m_munitionNames[i].str(), data->m_launcherNames[i].str()) );
		if( munition != nullptr && launcher != nullptr )
		{
			m_munitions.push_back( munition );
			m_launchers.push_back( launcher );
			// Hypersonic @feature D-142: the cook-off weapon (none: a lost missile never explodes)
			const AsciiString &cookOffName = data->m_cookOffWeaponNames[i];
			m_cookOffWeapons.push_back( cookOffName.isNotEmpty() ? TheWeaponStore->findWeaponTemplate( cookOffName ) : nullptr );
		}
	}

	Site empty;
	empty.m_state = SITE_EMPTY;
	empty.m_phase = PHASE_EXIT;
	empty.m_munition = nullptr;
	empty.m_framesLeft = 0;
	empty.m_launcherID = INVALID_ID;
	empty.m_targetID = INVALID_ID;
	empty.m_targetPos.zero();
	empty.m_revealID = INVALID_ID;	// Hypersonic @feature D-213
	for( Int s = 0; s < data->m_siteCount; ++s )
		m_sites.push_back( empty );
	m_siteCoverHidden.assign( m_sites.size(), -1 );	// Hypersonic @feature D-199
	m_siteSortieFrame.assign( m_sites.size(), 0 );

	m_nextLaunchFrame = 0;
	m_nextPlayerSalvoFrame = 0;
	m_nextRestockFrame = 0;
	m_restockIndex = 0;
	m_nextLaunchDelay = 0;	// Hypersonic @feature D-189

	// nothing to do until a purchase completes (addMunition wakes us); a computer owner restocks from update
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MissileLaunchSiteUpdate::~MissileLaunchSiteUpdate()
{
}

//-------------------------------------------------------------------------------------------------
Int MissileLaunchSiteUpdate::findMunitionIndex( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return -1;
	for( size_t i = 0; i < m_munitions.size(); ++i )
	{
		if( munition->isEquivalentTo( m_munitions[i] ) )
			return (Int)i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
// The weapon the launcher object fires: the PRIMARY weapon of its first weapon set.
//-------------------------------------------------------------------------------------------------
const WeaponTemplate *MissileLaunchSiteUpdate::getLaunchWeapon( Int munitionIndex ) const
{
	if( munitionIndex < 0 || munitionIndex >= (Int)m_launchers.size() )
		return nullptr;
	const WeaponTemplateSetVector &sets = m_launchers[munitionIndex]->getWeaponTemplateSets();
	return sets.empty() ? nullptr : sets[0].getNth( PRIMARY_WEAPON );
}

//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::storesMunition( const ThingTemplate *munition ) const
{
	return findMunitionIndex( munition ) >= 0;
}

//-------------------------------------------------------------------------------------------------
Int MissileLaunchSiteUpdate::getCapacity() const
{
	return getMissileLaunchSiteUpdateModuleData()->m_capacity;
}

//-------------------------------------------------------------------------------------------------
Int MissileLaunchSiteUpdate::getStoredCount( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return (Int)m_magazine.size();

	Int count = 0;
	for( std::vector<const ThingTemplate*>::const_iterator it = m_magazine.begin(); it != m_magazine.end(); ++it )
	{
		if( munition->isEquivalentTo( *it ) )
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::addMunition( const ThingTemplate *munition )
{
	if( munition == nullptr )
		return;

	m_magazine.push_back( munition );
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
// Loaded sites wait inside, so every loaded site is ready (readyOnly makes no difference here).
//-------------------------------------------------------------------------------------------------
Int MissileLaunchSiteUpdate::getLoadedCount( const ThingTemplate *munition, Bool /*readyOnly*/ ) const
{
	Int count = 0;
	for( std::vector<Site>::const_iterator it = m_sites.begin(); it != m_sites.end(); ++it )
	{
		if( it->m_state != SITE_LOADED || it->m_munition == nullptr )
			continue;
		if( munition != nullptr && !munition->isEquivalentTo( it->m_munition ) )
			continue;
		++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::getNextLoad( const ThingTemplate *munition, UnsignedInt *finishFrame, Real *fractionDone ) const
{
	const UnsignedInt loadFrames = getMissileLaunchSiteUpdateModuleData()->m_loadFrames;
	const Site *best = nullptr;
	for( std::vector<Site>::const_iterator it = m_sites.begin(); it != m_sites.end(); ++it )
	{
		if( it->m_state != SITE_LOADING || it->m_munition == nullptr )
			continue;
		if( munition != nullptr && !munition->isEquivalentTo( it->m_munition ) )
			continue;
		if( best == nullptr || it->m_framesLeft < best->m_framesLeft )
			best = &(*it);
	}
	if( best == nullptr )
		return FALSE;

	if( finishFrame )
		*finishFrame = TheGameLogic->getFrame() + best->m_framesLeft;
	if( fractionDone )
		*fractionDone = loadFrames > 0 ? 1.0f - (Real)best->m_framesLeft / (Real)loadFrames : 1.0f;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MissileLaunchSiteUpdate::getLaunchGapReadyFrame( UnsignedInt *gapFrames ) const
{
	if( gapFrames )
		*gapFrames = getMissileLaunchSiteUpdateModuleData()->m_launchGapFrames;
	return m_nextLaunchFrame > TheGameLogic->getFrame() ? m_nextLaunchFrame : 0;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Iran AI strike targeting (D-110): the launcher's weapon delivers the missile's blast.
//-------------------------------------------------------------------------------------------------
const WeaponTemplate *MissileLaunchSiteUpdate::getMunitionWeapon( const ThingTemplate *munition ) const
{
	return getLaunchWeapon( findMunitionIndex( munition ) );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Iran AI strike targeting (D-110): sorties ordered at an object whose missile has not left.
//-------------------------------------------------------------------------------------------------
Int MissileLaunchSiteUpdate::getPendingLaunches( ObjectID *targets, Coord3D *positions, const ThingTemplate **munitions, Int max ) const
{
	Int count = 0;
	for( std::vector<Site>::const_iterator it = m_sites.begin(); it != m_sites.end() && count < max; ++it )
	{
		if( it->m_state != SITE_SORTIE || it->m_munition == 0 )
			continue;
		targets[count] = it->m_targetID;
		positions[count] = it->m_targetPos; // Hypersonic @feature (D-122) includes point-aimed cluster sorties
		munitions[count] = it->m_munition;
		++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
// World positions of a site: the exit (TEL door / silo gate) and the pad (the exit for silo gates), and the
// heading the launcher faces (outward). Missing bones fall back to the facility's own position.
//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::getSitePositions( Int site, Coord3D *exitPos, Coord3D *padPos, Real *angle ) const
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	const Object *facility = getObject();
	Coord3D exitAt = *facility->getPosition();
	Real heading = facility->getOrientation();
	Bool found = FALSE;

	AsciiString name;
	Matrix3D mtx;
	if( data->m_exitBone.isNotEmpty() )
	{
		name.format( "%s%02d", data->m_exitBone.str(), site + 1 );
		if( facility->getSingleLogicalBonePosition( name.str(), &exitAt, &mtx ) )
		{
			heading = mtx.Get_Z_Rotation();
			found = TRUE;
		}
	}

	Coord3D padAt = exitAt;
	if( data->m_padBone.isNotEmpty() )
	{
		name.format( "%s%02d", data->m_padBone.str(), site + 1 );
		if( facility->getSingleLogicalBonePosition( name.str(), &padAt, &mtx ) )
			heading = mtx.Get_Z_Rotation();
	}

	if( exitPos )
		*exitPos = exitAt;
	if( padPos )
		*padPos = padAt;
	if( angle )
		*angle = heading;
	return found;
}

//-------------------------------------------------------------------------------------------------
// The launch weapon's minimum range, measured like the weapon measures it (2D, from the launcher's bounding
// sphere), from where the launcher will stand when it fires.
//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::isSiteInRange( Int site, Int munitionIndex, const Coord3D *pos ) const
{
	const WeaponTemplate *weapon = getLaunchWeapon( munitionIndex );
	if( weapon == nullptr || pos == nullptr )
		return FALSE;

	Coord3D padPos;
	getSitePositions( site, nullptr, &padPos, nullptr );
	Real dx = pos->x - padPos.x;
	Real dy = pos->y - padPos.y;
	Real dist = sqrtf( dx * dx + dy * dy ) - m_launchers[munitionIndex]->getTemplateGeometryInfo().getBoundingSphereRadius();
	const Real MARGIN = 1.0f;
	return dist >= weapon->getMinimumAttackRange() + MARGIN && dist <= weapon->getUnmodifiedAttackRange();
}

//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::canLaunchAt( const ThingTemplate *munition, const Coord3D *pos ) const
{
	Int index = findMunitionIndex( munition );
	if( index < 0 || pos == nullptr )
		return FALSE;

	for( Int s = 0; s < (Int)m_sites.size(); ++s )
	{
		const Site &site = m_sites[s];
		if( site.m_state == SITE_LOADED && site.m_munition != nullptr && munition->isEquivalentTo( site.m_munition )
			&& isSiteInRange( s, index, pos ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Start a sortie: of the loaded sites that may fire at the target, the one farthest from it goes (its pad lies
// on the sheltered side of the facility). The launcher object appears at the site's exit.
//-------------------------------------------------------------------------------------------------
Bool MissileLaunchSiteUpdate::launchMunition( const ThingTemplate *munition, Object *target, const Coord3D *pos )
{
	Object *facility = getObject();
	Player *player = facility->getControllingPlayer();
	Int index = findMunitionIndex( munition );
	// Hypersonic @feature D-189: the delay applies to this call only, whether or not it launches
	const UnsignedInt delay = m_nextLaunchDelay;
	m_nextLaunchDelay = 0;
	if( player == nullptr || index < 0 || pos == nullptr || facility->isEffectivelyDead() )
		return FALSE;
	if( m_nextLaunchFrame > TheGameLogic->getFrame() )
		return FALSE;

	Int best = -1;
	Real bestDistSqr = -1.0f;
	for( Int s = 0; s < (Int)m_sites.size(); ++s )
	{
		const Site &site = m_sites[s];
		if( site.m_state != SITE_LOADED || site.m_munition == nullptr || !munition->isEquivalentTo( site.m_munition )
			|| !isSiteInRange( s, index, pos ) )
			continue;
		Coord3D padPos;
		getSitePositions( s, nullptr, &padPos, nullptr );
		Real distSqr = sqr( pos->x - padPos.x ) + sqr( pos->y - padPos.y );
		if( distSqr > bestDistSqr )
		{
			best = s;
			bestDistSqr = distSqr;
		}
	}
	if( best < 0 )
		return FALSE;

	Object *launcher = TheThingFactory->newObject( m_launchers[index], player->getDefaultTeam() );
	if( launcher == nullptr )
		return FALSE;
	launcher->setProducer( facility );
	// part of the facility: the player neither selects nor commands it, and it never attacks on its own
	launcher->setStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_UNSELECTABLE, OBJECT_STATUS_NO_ATTACK ) );
	if( launcher->getAI() != nullptr )
		launcher->getAI()->setIsRecruitable( FALSE );	// nor does the AI take it into its teams

	Site &site = m_sites[best];
	site.m_state = SITE_SORTIE;
	site.m_launcherID = launcher->getID();
	site.m_targetID = target != nullptr ? target->getID() : INVALID_ID;
	site.m_targetPos = *pos;
	endSiteReveal( site );	// Hypersonic @feature D-213: none should be left; never two
	site.m_revealID = createTargetReveal( pos, site.m_munition );	// Hypersonic @feature D-213: the reveal starts with the order
	placeLauncher( launcher, best, 0.0f );
	startPhase( site, PHASE_EXIT );
	site.m_framesLeft += delay;	// Hypersonic @feature D-189: waits at the exit first (getLauncherFraction stays 0)
	// Hypersonic @feature (D-199): the gate object starts closed and replaces the facility's own closed gate once it draws
	if( m_siteSortieFrame.size() != m_sites.size() )
		m_siteSortieFrame.assign( m_sites.size(), 0 );
	m_siteSortieFrame[best] = TheGameLogic->getFrame();
	updateGateLook( site, launcher );

	m_nextLaunchFrame = TheGameLogic->getFrame() + getMissileLaunchSiteUpdateModuleData()->m_launchGapFrames;
	setWakeFrame( facility, UPDATE_SLEEP_NONE );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Put the launcher on its way between exit (0) and pad (1), facing outward (a TEL backs in on its return).
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::placeLauncher( Object *launcher, Int site, Real fraction ) const
{
	Coord3D exitPos, padPos;
	Real angle = 0.0f;
	getSitePositions( site, &exitPos, &padPos, &angle );

	Coord3D pos;
	pos.x = exitPos.x + (padPos.x - exitPos.x) * fraction;
	pos.y = exitPos.y + (padPos.y - exitPos.y) * fraction;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	launcher->setPosition( &pos );
	launcher->setOrientation( angle );
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::startPhase( Site &site, Int phase )
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	site.m_phase = phase;
	switch( phase )
	{
		case PHASE_EXIT:		site.m_framesLeft = data->m_exitFrames; break;
		case PHASE_PREPARE:	site.m_framesLeft = data->m_prepareFrames; break;
		case PHASE_LAUNCH:	site.m_framesLeft = data->m_launchFrames; break;
		case PHASE_HOLD:
		{
			// Hypersonic @feature D-084: a random wait on the pad before ignition, drawn per sortie from the synced
			// logic random stream (every client draws the same number)
			UnsignedInt lo = data->m_launchHoldMinFrames;
			UnsignedInt hi = data->m_launchHoldMaxFrames > lo ? data->m_launchHoldMaxFrames : lo;
			site.m_framesLeft = hi > lo ? (UnsignedInt)GameLogicRandomValue( (Int)lo, (Int)hi ) : lo;
			break;
		}
		case PHASE_POST_LAUNCH:	site.m_framesLeft = data->m_postLaunchFrames; break;	// Hypersonic @feature D-084
		default:						site.m_framesLeft = data->m_returnFrames; break;
	}
}

//-------------------------------------------------------------------------------------------------
// D-251: only human players use the lift-off gap. Read saved timers on this facility's opted-in modules;
// no player-wide timer, wall clock, extra random draw or order rejection. AI planning/timing stays unchanged.
UnsignedInt MissileLaunchSiteUpdate::getPlayerSalvoReadyFrame() const
{
	const Player *player = getObject()->getControllingPlayer();
	if( player == nullptr || player->getPlayerType() != PLAYER_HUMAN
		|| getMissileLaunchSiteUpdateModuleData()->m_playerSalvoGapFrames == 0 )
		return 0;

	UnsignedInt ready = 0;
	static const NameKeyType key = NAMEKEY("MissileLaunchSiteUpdate");
	for( BehaviorModule **m = getObject()->getBehaviorModules(); *m; ++m )
	{
		if( (*m)->getModuleNameKey() != key )
			continue;
		const MissileLaunchSiteUpdate *other = (const MissileLaunchSiteUpdate *)*m;
		if( other->getMissileLaunchSiteUpdateModuleData()->m_playerSalvoGapFrames > 0
			&& other->m_nextPlayerSalvoFrame > ready )
			ready = other->m_nextPlayerSalvoFrame;
	}
	return ready;
}

//-------------------------------------------------------------------------------------------------
// Fire the launcher's own PRIMARY weapon (its missile, with the weapon's scatter, minimum range and
// upgrades) at the target object's current position, else at the ordered position. A failed shot (the
// target came inside the minimum range) keeps the missile, and the launcher brings it back.
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::fireMissile( Site &site, Object *launcher )
{
	Weapon *weapon = launcher->getWeaponInWeaponSlot( PRIMARY_WEAPON );
	if( weapon == nullptr )
		return;

	Coord3D aim = site.m_targetPos;
	Object *target = site.m_targetID != INVALID_ID ? TheGameLogic->findObjectByID( site.m_targetID ) : nullptr;
	if( target != nullptr && !target->isEffectivelyDead() )
		aim = *target->getPosition();

	ObjectID projectile = INVALID_ID;
	weapon->fireWeapon( launcher, &aim, &projectile );
	if( projectile != INVALID_ID )
	{
		// D-251: reserve a gap only after a real lift-off, following the D-084 hold. Sibling modules read
		// this timer too; the computer player's original launch timing is unchanged.
		const Player *player = getObject()->getControllingPlayer();
		if( player != nullptr && player->getPlayerType() == PLAYER_HUMAN
			&& getMissileLaunchSiteUpdateModuleData()->m_playerSalvoGapFrames > 0 )
			m_nextPlayerSalvoFrame = TheGameLogic->getFrame() + getMissileLaunchSiteUpdateModuleData()->m_playerSalvoGapFrames;
		if( TheHypersonicScenario != nullptr )
			TheHypersonicScenario->noteAI( "MISSILE LIFTOFF facility=%u launcher=%u missile=%u type=%s",
				(UnsignedInt)getObject()->getID(), (UnsignedInt)launcher->getID(), (UnsignedInt)projectile,
				site.m_munition->getName().str() );
		site.m_munition = nullptr;	// it is on its way
		handTargetReveal( site, projectile, &aim );	// Hypersonic @feature D-143, D-213
	}

	// the launcher shows its missile gone (projectile bone feedback)
	launcher->adjustModelConditionForWeaponStatus();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature missile target reveal (Generals Hypersonic D-143, D-213): a vision object for the facility's player
// at the aim point (before scatter; base radius and stealth rules are the object's INI), from the launch order on. The
// missile takes it over when it leaves the launcher and ends it TargetRevealLinger after it is gone
// (MissileAIUpdate::endTargetReveal); D-245 vanishing carriers linger from planned submunition impact instead.
// A launch that never fires ends it the same way (endSiteReveal). The object's own
// DeletionUpdate lifetime is only a safety limit.
//-------------------------------------------------------------------------------------------------
ObjectID MissileLaunchSiteUpdate::createTargetReveal( const Coord3D *pos, const ThingTemplate *munition )
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	if( data->m_targetRevealName.isEmpty() || pos == nullptr )
		return INVALID_ID;
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( data->m_targetRevealName );
	Player *player = getObject()->getControllingPlayer();
	if( tmpl == nullptr || player == nullptr )
		return INVALID_ID;
	Object *reveal = TheThingFactory->newObject( tmpl, player->getDefaultTeam() );
	if( reveal == nullptr )
		return INVALID_ID;
	Coord3D groundPos = *pos;
	groundPos.z = TheTerrainLogic->getGroundHeight( groundPos.x, groundPos.y );
	reveal->setPosition( &groundPos );
	// D-245 Update 8: only a vanishing cluster carrier needs vision over its whole landing pattern.
	// The reveal is centred before scatter: carrier miss + ring + submunition miss, plus 30 for the impact FX.
	const WeaponTemplate *weapon = getMunitionWeapon( munition );
	if( weapon != nullptr && weapon->isClusterReleaseWeapon() && weapon->getClusterReleaseCarrierVanishes() )
	{
		const Real radius = weapon->getScatterRadius() + weapon->getClusterRadius()
			+ weapon->getClusterWeapon()->getScatterRadius() + 30.0f;
		reveal->setVisionRange( max(reveal->getVisionRange(), radius) );
		reveal->setShroudClearingRange( max(reveal->getShroudClearingRange(), radius) );
	}
	return reveal->getID();
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-213): the missile left the launcher: it takes over the site's reveal (moved to where it was
// actually fired, a named target may have moved since the order; created now if the order made none).
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::handTargetReveal( Site &site, ObjectID projectileID, const Coord3D *aim )
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Object *projectile = TheGameLogic->findObjectByID( projectileID );
	const AIUpdateInterface *ai = projectile != nullptr ? projectile->getAI() : nullptr;
	MissileAIUpdate *missileAI = ai != nullptr ? const_cast<MissileAIUpdate *>( ai->getMissileAIUpdate() ) : nullptr;
	if( missileAI == nullptr || aim == nullptr )
	{
		endSiteReveal( site );	// nothing would end it
		return;
	}
	Object *reveal = site.m_revealID != INVALID_ID ? TheGameLogic->findObjectByID( site.m_revealID ) : nullptr;
	site.m_revealID = INVALID_ID;
	if( reveal == nullptr || reveal->isDestroyed() )
	{
		ObjectID id = createTargetReveal( aim, projectile->getTemplate() );
		reveal = id != INVALID_ID ? TheGameLogic->findObjectByID( id ) : nullptr;
		if( reveal == nullptr )
			return;
	}
	else
	{
		Coord3D groundPos = *aim;
		groundPos.z = TheTerrainLogic->getGroundHeight( groundPos.x, groundPos.y );
		reveal->setPosition( &groundPos );
	}
	missileAI->setTargetReveal( reveal->getID(), data->m_targetRevealLingerFrames );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-213): the site's launch ends without a missile in flight: its reveal ends TargetRevealLinger
// from now, as after an impact.
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::endSiteReveal( Site &site )
{
	if( site.m_revealID == INVALID_ID )
		return;
	Object *reveal = TheGameLogic->findObjectByID( site.m_revealID );
	site.m_revealID = INVALID_ID;
	if( reveal == nullptr || reveal->isDestroyed() )
		return;
	const UnsignedInt linger = getMissileLaunchSiteUpdateModuleData()->m_targetRevealLingerFrames;
	static const NameKeyType key_DeletionUpdate = NAMEKEY( "DeletionUpdate" );
	DeletionUpdate *deletion = (DeletionUpdate *)reveal->findUpdateModule( key_DeletionUpdate );
	if( deletion != nullptr && linger > 0 )
		deletion->setLifetimeRange( linger, linger );
	else
		TheGameLogic->destroyObject( reveal );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature cook-off (Generals Hypersonic D-142): a missile lost on its launcher rolls once (synced logic
// random stream) and on success its cook-off weapon goes off at 'pos' like a projectile detonation (damage + the
// weapon's detonation FX), from the facility, so it hurts everyone in range including the facility (SELF).
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::cookOff( const ThingTemplate *munition, const Coord3D *pos )
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Int index = findMunitionIndex( munition );
	if( index < 0 || pos == nullptr || data->m_cookOffChance <= 0.0f )
		return;
	const WeaponTemplate *weapon = m_cookOffWeapons[index];
	if( weapon == nullptr || TheWeaponStore == nullptr )
		return;
	const Bool cookedOff = GameLogicRandomValueReal( 0.0f, 1.0f ) < data->m_cookOffChance;
	// Hypersonic @feature (D-148 test) the harness reports every roll
	if( TheHypersonicScenario != nullptr )
	{
		AsciiString label;
		label.format( "%s on its launcher", munition->getName().str() );
		TheHypersonicScenario->noteCookOff( label.str(), pos, cookedOff );
	}
	if( !cookedOff )
		return;
	TheWeaponStore->handleProjectileDetonation( weapon, getObject(), pos, 0 );
}

//-------------------------------------------------------------------------------------------------
// Where the launcher stands during its current phase, between the exit (0) and the pad (1).
//-------------------------------------------------------------------------------------------------
Real MissileLaunchSiteUpdate::getLauncherFraction( const Site &site ) const
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	if( site.m_phase == PHASE_EXIT )
	{
		// Hypersonic @feature D-189: a delayed sortie (more frames left than the drive) waits at the exit
		if( site.m_framesLeft > data->m_exitFrames )
			return 0.0f;
		return data->m_exitFrames > 0 ? 1.0f - (Real)site.m_framesLeft / (Real)data->m_exitFrames : 1.0f;
	}
	if( site.m_phase == PHASE_RETURN )
		return data->m_returnFrames > 0 ? (Real)site.m_framesLeft / (Real)data->m_returnFrames : 0.0f;
	return 1.0f;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature silo gate look (Generals Hypersonic D-199): a silo gate object (no pad) is created closed; it
// shows DOOR_1_OPENING from the moment its gate starts to open (after a D-189 delay) through the launch, and
// DOOR_1_CLOSING while it closes (its draw module animates both). TELs are left alone.
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::updateGateLook( const Site &site, Object *launcher ) const
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	const Drawable *draw = launcher != nullptr ? launcher->getDrawable() : nullptr;
	if( draw == nullptr || data->m_padBone.isNotEmpty() )
		return;
	const Bool closing = site.m_phase == PHASE_RETURN;
	const Bool opening = !closing && !( site.m_phase == PHASE_EXIT && site.m_framesLeft > data->m_exitFrames );
	const ModelConditionFlags &flags = draw->getModelConditionFlags();
	if( flags.test( MODELCONDITION_DOOR_1_OPENING ) == opening && flags.test( MODELCONDITION_DOOR_1_CLOSING ) == closing )
		return;
	ModelConditionFlags clr, set;
	if( opening ) set.set( MODELCONDITION_DOOR_1_OPENING ); else clr.set( MODELCONDITION_DOOR_1_OPENING );
	if( closing ) set.set( MODELCONDITION_DOOR_1_CLOSING ); else clr.set( MODELCONDITION_DOOR_1_CLOSING );
	launcher->clearAndSetModelConditionFlags( clr, set );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature silo covers (Generals Hypersonic D-199): the facility model carries each silo's closed gate as
// sub-object <SiteCoverSubObject><NN>; it is hidden while that site's gate object stands in for it (sortie) and while
// a destroyed gate is rebuilt (regenerating: the shaft stays open). Drawing only; applied when it changes. A new gate
// object is first drawn a frame after it is created, so the cover stays until then (hiding it at once showed the open
// shaft for one frame: the owner's "black flash" on the gate); for that frame both identical gates are drawn.
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::updateSiteCovers()
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Drawable *draw = getObject()->getDrawable();
	if( data->m_siteCoverSubObject.isEmpty() || draw == nullptr )
		return;
	if( m_siteCoverHidden.size() != m_sites.size() )
		m_siteCoverHidden.assign( m_sites.size(), -1 );
	if( m_siteSortieFrame.size() != m_sites.size() )
		m_siteSortieFrame.assign( m_sites.size(), 0 );
	const UnsignedInt now = TheGameLogic->getFrame();
	Bool changed = FALSE;
	AsciiString name;
	for( Int s = 0; s < (Int)m_sites.size(); ++s )
	{
		const Bool out = m_sites[s].m_state == SITE_SORTIE && now >= m_siteSortieFrame[s] + 2;
		const Int hidden = ( out || m_sites[s].m_state == SITE_REGENERATING ) ? 1 : 0;
		if( m_siteCoverHidden[s] == hidden )
			continue;
		m_siteCoverHidden[s] = hidden;
		name.format( "%s%02d", data->m_siteCoverSubObject.str(), s + 1 );
		draw->showSubObject( name, hidden == 0 );
		changed = TRUE;
	}
	if( changed )
		draw->updateSubObjects();
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::updateSortie( Int index )
{
	Site &site = m_sites[index];
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Object *launcher = site.m_launcherID != INVALID_ID ? TheGameLogic->findObjectByID( site.m_launcherID ) : nullptr;
	if( launcher == nullptr || launcher->isEffectivelyDead() )
	{
		// destroyed outside: the launcher and any missile it carried are lost; a replacement comes later
		// Hypersonic @feature D-142: a missile still on it may cook off where the launcher stood (a launcher already
		// deleted stood where placeLauncher put it last frame: the frame count has not moved since)
		const ThingTemplate *lost = site.m_munition;
		Coord3D lostPos;
		if( launcher != nullptr )
			lostPos = *launcher->getPosition();
		else
		{
			Coord3D exitPos, padPos;
			getSitePositions( index, &exitPos, &padPos, nullptr );
			Real fraction = getLauncherFraction( site );
			lostPos.x = exitPos.x + (padPos.x - exitPos.x) * fraction;
			lostPos.y = exitPos.y + (padPos.y - exitPos.y) * fraction;
			lostPos.z = TheTerrainLogic->getGroundHeight( lostPos.x, lostPos.y );
		}
		site.m_state = SITE_REGENERATING;
		site.m_munition = nullptr;
		site.m_launcherID = INVALID_ID;
		site.m_targetID = INVALID_ID;
		site.m_framesLeft = data->m_regenerateFrames;
		endSiteReveal( site );	// Hypersonic @feature D-213
		cookOff( lost, &lostPos );	// last: the blast may kill the facility, which resets the sites
		return;
	}
	// taken over (hijacked, crew sniped) or disabled (EMP): it never fires for anyone; lost like a killed one
	if( launcher->getControllingPlayer() != getObject()->getControllingPlayer() || launcher->isDisabled() )
	{
		const ThingTemplate *lost = site.m_munition;	// Hypersonic @feature D-142
		Coord3D lostPos = *launcher->getPosition();
		launcher->kill();
		site.m_state = SITE_REGENERATING;
		site.m_munition = nullptr;
		site.m_launcherID = INVALID_ID;
		site.m_targetID = INVALID_ID;
		site.m_framesLeft = data->m_regenerateFrames;
		endSiteReveal( site );	// Hypersonic @feature D-213
		cookOff( lost, &lostPos );
		return;
	}
	// the facility drives it: an order from anywhere else (a script moving the whole default team) is dropped
	AIUpdateInterface *ai = launcher->getAI();
	if( ai != nullptr && !ai->isIdle() )
		ai->aiIdle( CMD_FROM_AI );

	if( site.m_framesLeft > 0 )
		--site.m_framesLeft;

	// where the launcher stands during this phase
	placeLauncher( launcher, index, getLauncherFraction( site ) );
	updateGateLook( site, launcher );	// Hypersonic @feature (D-199)

	if( site.m_framesLeft > 0 )
		return;

	switch( site.m_phase )
	{
		case PHASE_EXIT:
		{
			// on the pad: erect. The turret pitches up to its FirePitch while it aims at a point just ahead.
			if( ai != nullptr )
			{
				Coord3D ahead = *launcher->getPosition();
				const Coord3D *dir = launcher->getUnitDirectionVector2D();
				ahead.x += dir->x * ERECT_AIM_DISTANCE;
				ahead.y += dir->y * ERECT_AIM_DISTANCE;
				ai->setTurretTargetPosition( TURRET_MAIN, &ahead );
				ai->friend_notifyStateMachineChanged();	// wake the (sleepy) AI so the turret moves now
			}
			startPhase( site, PHASE_PREPARE );
			break;
		}
		case PHASE_PREPARE:
			// Hypersonic @feature D-084: erected; hold a random time before ignition (no hold configured: straight on)
			if( data->m_launchHoldMinFrames > 0 || data->m_launchHoldMaxFrames > 0 )
				startPhase( site, PHASE_HOLD );
			else
				startPhase( site, PHASE_LAUNCH );
			break;
		case PHASE_HOLD:
			startPhase( site, PHASE_LAUNCH );
			break;
		case PHASE_LAUNCH:
		{
			// D-251: hold this prepared launcher at zero until the previous lift-off gap expires.
			// Never repeat ignition/random hold, reject an order, or slow a computer-player salvo.
			if( TheGameLogic->getFrame() < getPlayerSalvoReadyFrame() )
				return;
			fireMissile( site, launcher );
			// Hypersonic @feature D-084: stay erected on the pad a moment after the missile leaves
			if( data->m_postLaunchFrames > 0 )
			{
				startPhase( site, PHASE_POST_LAUNCH );
				break;
			}
		}
		// fall through: no post-launch hold
		case PHASE_POST_LAUNCH:
		{
			if( ai != nullptr )
			{
				ai->recenterTurret( TURRET_MAIN );
				ai->friend_notifyStateMachineChanged();	// wake the (sleepy) AI so the turret moves now
			}
			startPhase( site, PHASE_RETURN );
			break;
		}
		default:
		{
			// back inside: the launcher goes out of play; a missile it could not fire stays loaded
			TheGameLogic->destroyObject( launcher );
			site.m_launcherID = INVALID_ID;
			site.m_targetID = INVALID_ID;
			endSiteReveal( site );	// Hypersonic @feature D-213: no missile in flight (one brought back, or already handed over)
			site.m_state = site.m_munition != nullptr ? SITE_LOADED : SITE_EMPTY;
			site.m_framesLeft = 0;
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// A computer player buys missiles here while it has money to spare: one purchase at a time, the types in
// turn, and only to keep the launch sites fed (stored + queued below the sites that have, or will soon have,
// no missile), never to fill the reserve magazine, so its money goes to the rest of its build.
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::restockForComputer()
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Object *facility = getObject();
	Player *player = facility->getControllingPlayer();
	UnsignedInt now = TheGameLogic->getFrame();
	if( data->m_computerRestockReserve < 0 || player == 0 || player->getPlayerType() != PLAYER_COMPUTER
		|| now < m_nextRestockFrame || m_munitions.empty() )
		return;
	m_nextRestockFrame = now + RESTOCK_CHECK_FRAMES;

	ProductionUpdateInterface *pu = facility->getProductionUpdateInterface();
	if( pu == 0 || pu->getProductionCount() > 0 )
		return;

	// sites without a missile: empty, being repaired, or out on a sortie (back empty once it has fired)
	Int needed = 0;
	for( std::vector<Site>::const_iterator it = m_sites.begin(); it != m_sites.end(); ++it )
	{
		if( it->m_state == SITE_EMPTY || it->m_state == SITE_REGENERATING || it->m_state == SITE_SORTIE )
			++needed;
	}
	if( (Int)m_magazine.size() >= needed )	// nothing is queued here (checked above)
		return;

	// Hypersonic @feature (D-122) captured facilities on other factions retain their old restock.
	if( player->getSide() != "Iran" )
	{
		Int count = (Int)m_munitions.size();
		for( Int k = 0; k < count; ++k )
		{
			Int index = (m_restockIndex + k) % count;
			const ThingTemplate *munition = m_munitions[index];
			if( (Int)player->getMoney()->countMoney() < munition->calcCostToBuild( player ) + data->m_computerRestockReserve )
				continue;
			if( pu->queueCreateUnit( munition, pu->requestUniqueUnitID() ) )
			{
				m_restockIndex = (index + 1) % count;
				return;
			}
		}
		return;
	}

	// Hypersonic @feature (D-122) three Kheibars per Fateh opportunity, never fall back to a cheap
	// missile just because army-reserved money cannot afford Kheibar. One rare Fattah at most.
	const ThingTemplate *kheibar = TheThingFactory->findTemplate( "IranMissileKheibarShekan" );
	const ThingTemplate *fateh = TheThingFactory->findTemplate( "IranMissileFateh110" );
	const ThingTemplate *fattah = TheThingFactory->findTemplate( "IranMissileFattah" );
	// Hypersonic @feature (D-189) strike doctrine: every bay and silo stays loaded (Kheibar Shekan, with one Fateh-110
	// in the TEL rotation: the cheap second missile of a GLA rebuild-hole salvo), no "killable target" probe; the
	// D-122 probes and the single Fattah only with the harness flag MunitionPicker = D122.
	const Bool probe = TheHypersonicScenario != nullptr && TheHypersonicScenario->isD122MunitionPicker();
	const Bool tel = findMunitionIndex( kheibar ) >= 0;
	const ThingTemplate *buy = tel ? kheibar : fattah;
	Int ownedFateh = getStoredCount( fateh );
	for( std::vector<Site>::const_iterator cheapStock = m_sites.begin(); cheapStock != m_sites.end(); ++cheapStock )
		if( fateh != 0 && cheapStock->m_munition != 0 && fateh->isEquivalentTo( cheapStock->m_munition ) ) ++ownedFateh;
	if( tel && m_restockIndex == 3 && fateh != 0 && ownedFateh == 0
		&& ( !probe || iranComputerMunitionHasTarget( facility, fateh, 1 ) ) )
		buy = fateh;
	// Hypersonic @feature (D-245) the silo gates take the Qadr too: once its science is bought (canBuild), up to two of
	// the four gates hold a Qadr, the rest a Fattah (kept for defended high-value targets, D-122/D-189)
	const ThingTemplate *qadr = TheThingFactory->findTemplate( "IranMissileQadr" );
	if( !tel && qadr != 0 && findMunitionIndex( qadr ) >= 0 && player->canBuild( qadr ) )
	{
		Int ownedQadr = getStoredCount( qadr );
		Int ownedFattah = fattah != 0 ? getStoredCount( fattah ) : 0;
		for( std::vector<Site>::const_iterator silo = m_sites.begin(); silo != m_sites.end(); ++silo )
		{
			if( silo->m_munition != 0 && qadr->isEquivalentTo( silo->m_munition ) ) ++ownedQadr;
			if( silo->m_munition != 0 && fattah != 0 && fattah->isEquivalentTo( silo->m_munition ) ) ++ownedFattah;
		}
		if( ownedQadr < 2 || ownedFattah >= 2 )
			buy = qadr;
	}
	if( buy == 0 || findMunitionIndex( buy ) < 0 )
		return;
	// Loading and not-yet-fired sorties are paid stock too. D-122: do not refill the second Fattah silo (D-189: both).
	// D-245: the silos count every type they hold (Fattah and Qadr share the gates)
	Int owned = tel ? getStoredCount( buy ) : (Int)m_magazine.size();
	for( std::vector<Site>::const_iterator stock = m_sites.begin(); stock != m_sites.end(); ++stock )
		if( stock->m_munition != 0 && ( !tel || buy->isEquivalentTo( stock->m_munition ) ) ) ++owned;
	if( !tel && owned >= ( probe ? 1 : (Int)m_sites.size() ) )
		return;
	Int prospective = tel ? 4 : 1;
	if( tel )
	{
		for( std::vector<Site>::const_iterator other = m_sites.begin(); other != m_sites.end(); ++other )
			if( other->m_munition != 0 && !buy->isEquivalentTo( other->m_munition ) ) --prospective;
		for( std::vector<const ThingTemplate*>::const_iterator stored = m_magazine.begin(); stored != m_magazine.end(); ++stored )
			if( !buy->isEquivalentTo( *stored ) ) --prospective;
	}
	if( prospective < 1 || ( probe && !iranComputerMunitionHasTarget( facility, buy, prospective ) ) )
		return; // no worthwhile lethal salvo even with the bays stocked: keep army money
	if( (Int)player->getMoney()->countMoney() < buy->calcCostToBuild( player ) + data->m_computerRestockReserve + ( tel ? 0 : 2000 ) )
		return;
	if( pu->queueCreateUnit( buy, pu->requestUniqueUnitID() ) )
	{
		m_restockIndex = (m_restockIndex + 1) % 4;
		if( !tel ) m_nextRestockFrame = now + LOGICFRAMES_PER_SECOND * 180;
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime MissileLaunchSiteUpdate::update()
{
	const MissileLaunchSiteUpdateModuleData *data = getMissileLaunchSiteUpdateModuleData();
	Object *obj = getObject();

	if( obj->isEffectivelyDead() )
		return UPDATE_SLEEP_FOREVER;

	// frozen while being built; selling it gives up everything outside too
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return UPDATE_SLEEP_NONE;
	if( obj->testStatus( OBJECT_STATUS_SOLD ) )
	{
		killOutsideLaunchers( TRUE );
		updateSiteCovers();	// Hypersonic @feature (D-199) the gate it killed: close the shaft again while it is sold
		return UPDATE_SLEEP_FOREVER;
	}

	restockForComputer();

	Bool busy = FALSE;
	for( Int s = 0; s < (Int)m_sites.size(); ++s )
	{
		Site &site = m_sites[s];
		switch( site.m_state )
		{
			case SITE_EMPTY:
				if( !m_magazine.empty() )
				{
					// reload from the magazine, oldest purchase first
					site.m_munition = m_magazine.front();
					m_magazine.erase( m_magazine.begin() );
					site.m_state = SITE_LOADING;
					site.m_framesLeft = data->m_loadFrames > 0 ? data->m_loadFrames : 1;
					if( isHypersonicScenarioInstantLoad() )
						site.m_framesLeft = 1;	// dev test harness (D-049)
					busy = TRUE;
				}
				break;
			case SITE_LOADING:
				if( site.m_framesLeft > 0 )
					--site.m_framesLeft;
				if( site.m_framesLeft == 0 )
					site.m_state = SITE_LOADED;
				busy = TRUE;
				break;
			case SITE_REGENERATING:
				if( site.m_framesLeft > 0 )
					--site.m_framesLeft;
				if( site.m_framesLeft == 0 )
					site.m_state = SITE_EMPTY;
				busy = TRUE;
				break;
			case SITE_SORTIE:
				updateSortie( s );
				busy = TRUE;
				break;
			default:
				break;
		}
	}
	updateSiteCovers();	// Hypersonic @feature (D-199)

	if( busy || !m_magazine.empty() )
		return UPDATE_SLEEP_NONE;

	// a computer owner keeps checking its stock
	const Player *player = obj->getControllingPlayer();
	if( data->m_computerRestockReserve >= 0 && player != nullptr && player->getPlayerType() == PLAYER_COMPUTER )
		return UPDATE_SLEEP( RESTOCK_CHECK_FRAMES );

	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
// Launchers outside belong to the facility: they die with it (killed, with their death effects) or leave
// with it (silently, when it is sold or removed).
//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::killOutsideLaunchers( Bool silently )
{
	// Hypersonic @feature D-142: the missiles still on launchers that die here (not silently) may cook off; the
	// sites are cleared first and the blasts follow, so a chain reaction never sees a half-cleared site
	std::vector<const ThingTemplate*> lost;
	std::vector<Coord3D> lostPos;
	for( std::vector<Site>::iterator it = m_sites.begin(); it != m_sites.end(); ++it )
	{
		if( it->m_launcherID == INVALID_ID )
			continue;
		Object *launcher = TheGameLogic->findObjectByID( it->m_launcherID );
		if( launcher != nullptr && !launcher->isEffectivelyDead() )
		{
			if( silently )
				TheGameLogic->destroyObject( launcher );
			else
				launcher->kill();
		}
		if( !silently && launcher != nullptr && it->m_munition != nullptr )
		{
			lost.push_back( it->m_munition );
			lostPos.push_back( *launcher->getPosition() );
		}
		it->m_launcherID = INVALID_ID;
		it->m_munition = nullptr;
		it->m_state = SITE_EMPTY;
		endSiteReveal( *it );	// Hypersonic @feature D-213
	}
	m_magazine.clear();
	for( size_t i = 0; i < lost.size(); ++i )
		cookOff( lost[i], &lostPos[i] );
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::onDie( const DamageInfo * /*damageInfo*/ )
{
	killOutsideLaunchers( FALSE );
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::onDelete()
{
	killOutsideLaunchers( TRUE );

	// extend base class
	UpdateModule::onDelete();
}

//-------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::onCapture( Player *oldOwner, Player *newOwner )
{
	UpdateModule::onCapture( oldOwner, newOwner );

	// the launchers outside change sides with the facility
	for( std::vector<Site>::iterator it = m_sites.begin(); it != m_sites.end(); ++it )
	{
		Object *launcher = it->m_launcherID != INVALID_ID ? TheGameLogic->findObjectByID( it->m_launcherID ) : nullptr;
		if( launcher != nullptr && newOwner != nullptr )
			launcher->setTeam( newOwner->getDefaultTeam() );
		// Hypersonic @feature (D-213): a launch under way keeps its reveal, now for the new owner (a fresh object: a
		// team change alone would not redo its vision)
		if( it->m_revealID != INVALID_ID )
		{
			endSiteReveal( *it );
			it->m_revealID = createTargetReveal( &it->m_targetPos, it->m_munition );
		}
	}
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

// ------------------------------------------------------------------------------------------------
static void xferTemplate( Xfer *xfer, const ThingTemplate **tmpl )
{
	AsciiString name;
	if( xfer->getXferMode() != XFER_LOAD && *tmpl != nullptr )
		name = (*tmpl)->getName();
	xfer->xferAsciiString( &name );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		*tmpl = name.isEmpty() ? nullptr : TheThingFactory->findTemplate( name );
		DEBUG_ASSERTCRASH( name.isEmpty() || *tmpl != nullptr, ("MissileLaunchSiteUpdate::xfer - unknown munition '%s'", name.str()) );
	}
}

// ------------------------------------------------------------------------------------------------
/** Magazine, sites and timers, shared by crc and xfer */
// ------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::xferState( Xfer *xfer, XferVersion version )
{
	// magazine, oldest first
	UnsignedShort storedCount = (UnsignedShort)m_magazine.size();
	xfer->xferUnsignedShort( &storedCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_magazine.clear();
	for( UnsignedShort i = 0; i < storedCount; ++i )
	{
		const ThingTemplate *munition = xfer->getXferMode() == XFER_LOAD ? nullptr : m_magazine[i];
		xferTemplate( xfer, &munition );
		if( xfer->getXferMode() == XFER_LOAD && munition != nullptr )
			m_magazine.push_back( munition );
	}

	// sites (the count comes from INI; a save with a different count keeps what fits)
	UnsignedShort siteCount = (UnsignedShort)m_sites.size();
	xfer->xferUnsignedShort( &siteCount );
	for( UnsignedShort j = 0; j < siteCount; ++j )
	{
		Site loaded;
		Site &site = j < m_sites.size() ? m_sites[j] : loaded;
		xfer->xferInt( &site.m_state );
		xfer->xferInt( &site.m_phase );
		xferTemplate( xfer, &site.m_munition );
		xfer->xferUnsignedInt( &site.m_framesLeft );
		xfer->xferObjectID( &site.m_launcherID );
		xfer->xferObjectID( &site.m_targetID );
		xfer->xferCoord3D( &site.m_targetPos );
		if( version >= 2 )
			xfer->xferObjectID( &site.m_revealID );	// Hypersonic @feature D-213
		else if( xfer->getXferMode() == XFER_LOAD )
			site.m_revealID = INVALID_ID;
	}

	xfer->xferUnsignedInt( &m_nextLaunchFrame );
	xfer->xferUnsignedInt( &m_nextRestockFrame );
	xfer->xferInt( &m_restockIndex );
	if( version >= 3 )
		xfer->xferUnsignedInt( &m_nextPlayerSalvoFrame );
	else if( xfer->getXferMode() == XFER_LOAD )
		m_nextPlayerSalvoFrame = 0;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

	// magazine, sites and timers
	xferState( xfer, 3 );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Hypersonic @feature D-213 the sites' target reveal
	* 3: Hypersonic @feature D-251 human-player lift-off gap */
// ------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// magazine, sites and timers
	xferState( xfer, version );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MissileLaunchSiteUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

	// Hypersonic @feature (D-199): the drawable restores its own sub-object visibility; re-apply ours on the next update
	m_siteCoverHidden.assign( m_sites.size(), -1 );
	m_siteSortieFrame.assign( m_sites.size(), 0 );

}
