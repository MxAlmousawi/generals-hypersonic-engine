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

// FILE: MunitionMagazineUpdate.cpp /////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Launch facility magazine and typed launchers (see MunitionMagazineUpdate.h).
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/HypersonicScenario.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/CruiseFlightUpdate.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"

// how often a computer player's facility considers buying a munition
static const UnsignedInt RESTOCK_CHECK_FRAMES = LOGICFRAMES_PER_SECOND * 2;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionMagazineUpdateModuleData::MunitionMagazineUpdateModuleData()
{
	m_capacity = 0;
	m_launcherTurnRate = 0.0f;
	m_loadFrames = 0;
	m_simultaneousLoads = 1;
	m_computerRestockReserve = -1;
}

//-------------------------------------------------------------------------------------------------
// Munition = <object> [storage units, default 1]
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdateModuleData::parseMunition( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	MunitionMagazineUpdateModuleData *self = (MunitionMagazineUpdateModuleData *)instance;
	const char *name = ini->getNextToken();
	const char *size = ini->getNextTokenOrNull();
	Int units = size != nullptr ? INI::scanInt( size ) : 1;
	if( units < 1 )
		throw INI_INVALID_DATA;
	self->m_munitionNames.push_back( AsciiString( name ) );
	self->m_munitionSizes.push_back( units );
}

//-------------------------------------------------------------------------------------------------
// Launchers = <object> <count> [load time in msec, default LoadTime]
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdateModuleData::parseLaunchers( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	MunitionMagazineUpdateModuleData *self = (MunitionMagazineUpdateModuleData *)instance;
	LauncherGroup group;
	group.m_munitionName = ini->getNextToken();
	group.m_count = INI::scanInt( ini->getNextToken() );
	const char *loadTime = ini->getNextTokenOrNull();
	group.m_loadFrames = loadTime != nullptr ? (UnsignedInt)ceilf( ConvertDurationFromMsecsToFrames( (Real)INI::scanUnsignedInt( loadTime ) ) ) : 0;
	if( group.m_count < 1 )
		throw INI_INVALID_DATA;
	self->m_launcherGroups.push_back( group );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "Capacity",								INI::parseInt,									nullptr, offsetof( MunitionMagazineUpdateModuleData, m_capacity ) },
		{ "Munition",								parseMunition,									nullptr, 0 },
		{ "Launchers",							parseLaunchers,									nullptr, 0 },
		{ "LauncherBone",						INI::parseAsciiString,					nullptr, offsetof( MunitionMagazineUpdateModuleData, m_launcherBone ) },
		{ "SeatBone",								INI::parseAsciiString,					nullptr, offsetof( MunitionMagazineUpdateModuleData, m_seatBone ) },
		{ "LauncherTurnRate",				INI::parseAngularVelocityReal,	nullptr, offsetof( MunitionMagazineUpdateModuleData, m_launcherTurnRate ) },
		{ "LoadTime",								INI::parseDurationUnsignedInt,	nullptr, offsetof( MunitionMagazineUpdateModuleData, m_loadFrames ) },
		{ "SimultaneousLoads",			INI::parseInt,									nullptr, offsetof( MunitionMagazineUpdateModuleData, m_simultaneousLoads ) },
		{ "ComputerRestockReserve",	INI::parseInt,									nullptr, offsetof( MunitionMagazineUpdateModuleData, m_computerRestockReserve ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionMagazineUpdate::MunitionMagazineUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	const MunitionMagazineUpdateModuleData *data = getMunitionMagazineUpdateModuleData();
	for( std::vector<AsciiString>::const_iterator it = data->m_munitionNames.begin(); it != data->m_munitionNames.end(); ++it )
	{
		const ThingTemplate *munition = TheThingFactory->findTemplate( *it );
		DEBUG_ASSERTCRASH( munition != nullptr, ("MunitionMagazineUpdate: unknown Munition '%s'", it->str()) );
		m_munitions.push_back( munition );	// kept even if unknown, so the sizes stay in step
	}

	for( std::vector<MunitionMagazineUpdateModuleData::LauncherGroup>::const_iterator g = data->m_launcherGroups.begin();
		g != data->m_launcherGroups.end(); ++g )
	{
		Launcher launcher;
		launcher.m_munition = TheThingFactory->findTemplate( g->m_munitionName );
		DEBUG_ASSERTCRASH( launcher.m_munition != nullptr, ("MunitionMagazineUpdate: unknown Launchers munition '%s'", g->m_munitionName.str()) );
		launcher.m_loadFrames = g->m_loadFrames > 0 ? g->m_loadFrames : data->m_loadFrames;
		launcher.m_state = LAUNCHER_EMPTY;
		launcher.m_framesLeft = 0;
		launcher.m_occupantID = INVALID_ID;
		launcher.m_angle = 0.0f;
		launcher.m_wantedAngle = 0.0f;
		if( launcher.m_munition != nullptr )
		{
			for( Int i = 0; i < g->m_count; ++i )
				m_launchers.push_back( launcher );
		}
	}

	LauncherGeometry unknown;
	unknown.m_known = FALSE;
	unknown.m_pivot.zero();
	unknown.m_seat.Make_Identity();
	unknown.m_restYaw = 0.0f;
	m_geometry.resize( m_launchers.size(), unknown );

	m_nextRestockFrame = 0;
	m_restockIndex = 0;

	// a purchase (addMunition) or a launch wakes us; a computer owner restocks from update
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MunitionMagazineUpdate::~MunitionMagazineUpdate()
{
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::findMunitionIndex( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return -1;
	for( size_t i = 0; i < m_munitions.size(); ++i )
	{
		if( m_munitions[i] != nullptr && munition->isEquivalentTo( m_munitions[i] ) )
			return (Int)i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::storesMunition( const ThingTemplate *munition ) const
{
	return findMunitionIndex( munition ) >= 0;
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::getCapacity() const
{
	return getMunitionMagazineUpdateModuleData()->m_capacity;
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::getMunitionSize( const ThingTemplate *munition ) const
{
	Int index = findMunitionIndex( munition );
	const std::vector<Int> &sizes = getMunitionMagazineUpdateModuleData()->m_munitionSizes;
	return index >= 0 && index < (Int)sizes.size() ? sizes[index] : 1;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Pad overflow (D-080): munitions waiting outside the magazine for their launcher count as
// stored here (they wait like the stored ones), but take no storage units (getStoredSize).
Int MunitionMagazineUpdate::getStoredCount( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return (Int)( m_magazine.size() + m_overflow.size() );

	Int count = 0;
	for( std::vector<const ThingTemplate*>::const_iterator it = m_magazine.begin(); it != m_magazine.end(); ++it )
	{
		if( munition->isEquivalentTo( *it ) )
			++count;
	}
	for( std::vector<const ThingTemplate*>::const_iterator ov = m_overflow.begin(); ov != m_overflow.end(); ++ov )
	{
		if( munition->isEquivalentTo( *ov ) )
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::getStoredSize() const
{
	Int units = 0;
	for( std::vector<const ThingTemplate*>::const_iterator it = m_magazine.begin(); it != m_magazine.end(); ++it )
		units += getMunitionSize( *it );
	return units;
}

//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::addMunition( const ThingTemplate *munition )
{
	if( munition == nullptr )
		return;

	// Hypersonic @feature Pad overflow (D-080): one that does not fit the magazine waits for its launcher (the
	// purchase was allowed because an empty launcher of its type was free for it)
	if( getStoredSize() + getMunitionSize( munition ) <= getCapacity() )
		m_magazine.push_back( munition );
	else
		m_overflow.push_back( munition );
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Pad overflow (D-080): the first empty launcher of this type, -1 when none.
Int MunitionMagazineUpdate::findEmptyLauncher( const ThingTemplate *munition ) const
{
	for( Int i = 0; i < (Int)m_launchers.size(); ++i )
	{
		if( m_launchers[i].m_state == LAUNCHER_EMPTY && munition->isEquivalentTo( m_launchers[i].m_munition ) )
			return i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Iran AI strike targeting (D-110): a drone delivers its CruiseFlightUpdate Warhead.
const WeaponTemplate *MunitionMagazineUpdate::getMunitionWeapon( const ThingTemplate *munition ) const
{
	if( munition == nullptr || !storesMunition( munition ) )
		return nullptr;
	const ModuleInfo &info = munition->getBehaviorModuleInfo();
	for( Int i = 0; i < info.getCount(); ++i )
	{
		if( info.getNthName( i ) == "CruiseFlightUpdate" )
		{
			const CruiseFlightUpdateModuleData *data = (const CruiseFlightUpdateModuleData *)info.getNthData( i );
			return data != nullptr ? data->m_warhead : nullptr;
		}
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Pad overflow (D-080): empty launchers of this type less the munitions of this type that
// already wait for one (stored or overflow). Launchers being loaded are not empty. The purchase check
// (ProductionUpdate::hasMagazineRoomFor) also subtracts the ones queued.
Int MunitionMagazineUpdate::getUnclaimedLauncherCount( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return 0;
	Int empty = 0;
	for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		if( it->m_state == LAUNCHER_EMPTY && munition->isEquivalentTo( it->m_munition ) )
			++empty;
	}
	Int unclaimed = empty - getStoredCount( munition );
	return unclaimed > 0 ? unclaimed : 0;
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::getLauncherCount( const ThingTemplate *munition ) const
{
	if( munition == nullptr )
		return (Int)m_launchers.size();

	Int count = 0;
	for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		if( munition->isEquivalentTo( it->m_munition ) )
			++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
// The munition on a launcher while it is still there (waiting or preparing); nullptr once it has flown off,
// died or gone.
Object *MunitionMagazineUpdate::getOccupant( const Launcher &launcher ) const
{
	if( launcher.m_state != LAUNCHER_OCCUPIED || launcher.m_occupantID == INVALID_ID )
		return nullptr;
	Object *obj = TheGameLogic->findObjectByID( launcher.m_occupantID );
	if( obj == nullptr || obj->isEffectivelyDead() )
		return nullptr;
	const CruiseFlightUpdate *flight = CruiseFlightUpdate::findFlight( obj );
	if( flight != nullptr && !flight->isOnLauncher() )
		return nullptr;
	return obj;
}

//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::isReadyToLaunch( const Launcher &launcher ) const
{
	const Object *obj = getOccupant( launcher );
	if( obj == nullptr || obj->isDisabled() || obj->getControllingPlayer() != getObject()->getControllingPlayer() )
		return FALSE;
	const CruiseFlightUpdate *flight = CruiseFlightUpdate::findFlight( obj );
	return flight != nullptr && flight->isWaitingOnLauncher();
}

//-------------------------------------------------------------------------------------------------
Int MunitionMagazineUpdate::getLoadedCount( const ThingTemplate *munition, Bool readyOnly ) const
{
	Int count = 0;
	for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		if( munition != nullptr && !munition->isEquivalentTo( it->m_munition ) )
			continue;
		if( readyOnly )
		{
			if( !isReadyToLaunch( *it ) )
				continue;
		}
		else
		{
			const Object *obj = getOccupant( *it );
			if( obj == nullptr || obj->getControllingPlayer() != getObject()->getControllingPlayer() )
				continue;
		}
		++count;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::getNextLoad( const ThingTemplate *munition, UnsignedInt *finishFrame, Real *fractionDone ) const
{
	const Launcher *best = nullptr;
	for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		if( it->m_state != LAUNCHER_LOADING )
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
		*fractionDone = best->m_loadFrames > 0 ? 1.0f - (Real)best->m_framesLeft / (Real)best->m_loadFrames : 1.0f;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::getLauncherTurn( Int index, Real *angle ) const
{
	if( index < 0 || index >= (Int)m_launchers.size() )
		return FALSE;
	if( angle )
		*angle = m_launchers[index].m_angle;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Launcher pivot and seat of launcher 'index' at rest, from the facility's (public) bones LauncherBone01.. and
// SeatBone01... Read once; the facility never moves. Missing bones fall back to the facility's own position.
//-------------------------------------------------------------------------------------------------
const MunitionMagazineUpdate::LauncherGeometry &MunitionMagazineUpdate::getGeometry( Int index ) const
{
	LauncherGeometry &geometry = m_geometry[index];
	if( geometry.m_known )
		return geometry;

	const MunitionMagazineUpdateModuleData *data = getMunitionMagazineUpdateModuleData();
	const Object *facility = getObject();
	AsciiString name;
	Bool found = TRUE;

	name.format( "%s%02d", data->m_launcherBone.str(), index + 1 );
	if( !facility->getSingleLogicalBonePosition( name.str(), &geometry.m_pivot, nullptr ) )
		found = FALSE;

	Coord3D seatPos;
	name.format( "%s%02d", data->m_seatBone.str(), index + 1 );
	if( !facility->getSingleLogicalBonePosition( name.str(), &seatPos, &geometry.m_seat ) )
		found = FALSE;
	geometry.m_seat.Set_Translation( Vector3( seatPos.x, seatPos.y, seatPos.z ) );
	geometry.m_restYaw = geometry.m_seat.Get_Z_Rotation();

	// keep the fallback for now, but look again next time (the drawable may not have its model yet)
	geometry.m_known = found;
	return geometry;
}

//-------------------------------------------------------------------------------------------------
// The munition sits on the seat of its launcher, turned with the launcher about the pivot.
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::placeOccupant( Int index, Object *munition ) const
{
	const LauncherGeometry &geometry = getGeometry( index );
	Matrix3D turn( true );
	turn.Translate( geometry.m_pivot.x, geometry.m_pivot.y, geometry.m_pivot.z );
	turn.Rotate_Z( m_launchers[index].m_angle );
	turn.Translate( -geometry.m_pivot.x, -geometry.m_pivot.y, -geometry.m_pivot.z );
	Matrix3D mtx;
	Matrix3D::Multiply( turn, geometry.m_seat, &mtx );
	munition->setTransformMatrix( &mtx );
}

//-------------------------------------------------------------------------------------------------
// The load is complete: the munition appears on its launcher, as if the facility had produced it.
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::deliverToLauncher( Int index )
{
	Launcher &launcher = m_launchers[index];
	Object *facility = getObject();
	Player *player = facility->getControllingPlayer();
	launcher.m_state = LAUNCHER_EMPTY;
	launcher.m_occupantID = INVALID_ID;
	if( player == nullptr )
		return;

	Object *munition = TheThingFactory->newObject( launcher.m_munition, player->getDefaultTeam() );
	if( munition == nullptr )
		return;
	munition->setProducer( facility );
	// part of the facility until launched: never selected or commanded (D-042; CruiseFlightUpdate sets it too)
	munition->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
	placeOccupant( index, munition );
	launcher.m_state = LAUNCHER_OCCUPIED;
	launcher.m_occupantID = munition->getID();

	// like ProductionUpdate: creation voice, score and stats
	AudioEventRTS voiceCreated = *munition->getTemplate()->getVoiceCreated();
	voiceCreated.setObjectID( munition->getID() );
	TheAudio->addAudioEvent( &voiceCreated );

	player->onUnitCreated( facility, munition );
	for( BehaviorModule **m = munition->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}

	player->getAcademyStats()->recordProduction( munition, facility );
}

//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::canLaunchAt( const ThingTemplate *munition, const Coord3D * /*pos*/ ) const
{
	if( munition == nullptr )
		return FALSE;
	for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		if( munition->isEquivalentTo( it->m_munition ) && isReadyToLaunch( *it ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-042): a launch at a ground point. Of the ready launchers of this type, the one that has
// to turn least goes: its munition picks its impact point and prepares, and the launcher turns toward it.
//-------------------------------------------------------------------------------------------------
Bool MunitionMagazineUpdate::launchMunition( const ThingTemplate *munition, Object * /*target*/, const Coord3D *pos )
{
	if( munition == nullptr || pos == nullptr || getObject()->isEffectivelyDead() )
		return FALSE;

	Int best = -1;
	Real bestTurn = 0.0f;
	for( Int i = 0; i < (Int)m_launchers.size(); ++i )
	{
		if( !munition->isEquivalentTo( m_launchers[i].m_munition ) || !isReadyToLaunch( m_launchers[i] ) )
			continue;
		const LauncherGeometry &geometry = getGeometry( i );
		Real turn = (Real)fabs( normalizeAngle( (Real)atan2( pos->y - geometry.m_pivot.y, pos->x - geometry.m_pivot.x ) - geometry.m_restYaw ) );
		if( best < 0 || turn < bestTurn )
		{
			best = i;
			bestTurn = turn;
		}
	}
	if( best < 0 )
		return FALSE;

	Object *obj = getOccupant( m_launchers[best] );
	CruiseFlightUpdate *flight = CruiseFlightUpdate::findFlight( obj );
	if( flight == nullptr || !flight->launch( pos ) )
		return FALSE;

	const LauncherGeometry &geometry = getGeometry( best );
	const Coord3D *impact = flight->getImpactPoint();
	Real dx = impact->x - geometry.m_pivot.x;
	Real dy = impact->y - geometry.m_pivot.y;
	m_launchers[best].m_wantedAngle = (dx * dx + dy * dy > 1.0f) ? normalizeAngle( (Real)atan2( dy, dx ) - geometry.m_restYaw ) : 0.0f;

	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// A computer player buys munitions here while it has money to spare: one purchase at a time, the types in turn,
// and only to keep the launchers fed (a type is bought while fewer are stored than its launchers are free), never
// to fill the magazine, so its money goes to the rest of its build.
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::restockForComputer()
{
	const MunitionMagazineUpdateModuleData *data = getMunitionMagazineUpdateModuleData();
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

	// Hypersonic @feature (D-122) captured facilities on other factions retain their old restock.
	if( player->getSide() != "Iran" )
	{
		Int count = (Int)m_munitions.size();
		for( Int k = 0; k < count; ++k )
		{
			Int index = (m_restockIndex + k) % count;
			const ThingTemplate *munition = m_munitions[index];
			if( munition == 0 )
				continue;

			Int free = 0;
			for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
			{
				if( it->m_state == LAUNCHER_EMPTY && munition->isEquivalentTo( it->m_munition ) )
					++free;
			}
			if( getStoredCount( munition ) >= free )
				continue;
			if( getStoredSize() + getMunitionSize( munition ) > getCapacity() )
				continue;
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

	// Hypersonic @feature (D-122) fill the three jet pads first; slow drones need a safe
	// building opportunity. No reserve-magazine accumulation and no cheap fallback below the reserve.
	// Hypersonic @feature (D-189) strike doctrine: every pad stays loaded (the breach volley needs all of them); the
	// D-122 "killable target" probe for the slow types only with the harness flag MunitionPicker = D122.
	const Bool probe = TheHypersonicScenario != nullptr && TheHypersonicScenario->isD122MunitionPicker();
	// Hypersonic @feature (D-189) while the player has no completed Missile Facility the drone restock keeps
	// IRAN_SAVE_FOR_MISSILE_FACILITY more in the bank, so the AI can save for it (it holds the full price)
	enum { IRAN_SAVE_FOR_MISSILE_FACILITY = 2500 };
	Int reserve = data->m_computerRestockReserve;
	const ThingTemplate *missileFacility = TheThingFactory->findTemplate( "IranBallisticMissileFacility" );
	if( missileFacility != nullptr )
	{
		Int built = 0;
		player->countObjectsByThingTemplate( 1, &missileFacility, TRUE, &built, TRUE );
		if( built == 0 )
			reserve += IRAN_SAVE_FOR_MISSILE_FACILITY;
	}
	const char *order[3] = { "IranAirShahed238", "IranAirArash", "IranAirShahed136" };
	for( Int priority = 0; priority < 3; ++priority )
	{
		const ThingTemplate *munition = TheThingFactory->findTemplate( order[priority] );
		if( munition == 0 || findMunitionIndex( munition ) < 0 ) continue;
		Int free = 0;
		for( std::vector<Launcher>::const_iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
			if( it->m_state == LAUNCHER_EMPTY && munition->isEquivalentTo( it->m_munition ) ) ++free;
		if( getStoredCount( munition ) >= free ) continue;
		// Jets seed the mixed salvo pool (at most three pads); otherwise both the jet and
		// heavy-drone probes could wait forever for the other half of a defense salvo.
		if( probe && priority > 0 && !iranComputerMunitionHasTarget( facility, munition, getLauncherCount( munition ) ) ) continue;
		if( getStoredSize() + getMunitionSize( munition ) > getCapacity() ) continue;
		if( (Int)player->getMoney()->countMoney() < munition->calcCostToBuild( player ) + reserve )	// Hypersonic @feature (D-189)
			return;
		if( pu->queueCreateUnit( munition, pu->requestUniqueUnitID() ) ) return;
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime MunitionMagazineUpdate::update()
{
	const MunitionMagazineUpdateModuleData *data = getMunitionMagazineUpdateModuleData();
	Object *obj = getObject();

	if( obj->isEffectivelyDead() )
		return UPDATE_SLEEP_FOREVER;

	// frozen while being built; selling it gives up everything on it (a disabled object gets no updates at
	// all, so loading pauses then too)
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return UPDATE_SLEEP_NONE;
	if( obj->testStatus( OBJECT_STATUS_SOLD ) )
	{
		removeOccupants( FALSE );
		return UPDATE_SLEEP_FOREVER;
	}

	Bool busy = FALSE;
	Bool occupied = FALSE;
	Int loading = 0;
	for( Int i = 0; i < (Int)m_launchers.size(); ++i )
	{
		Launcher &launcher = m_launchers[i];
		Object *occupant = nullptr;
		if( launcher.m_state == LAUNCHER_LOADING )
		{
			if( launcher.m_framesLeft > 0 )
				--launcher.m_framesLeft;
			if( launcher.m_framesLeft == 0 )
				deliverToLauncher( i );
			else
				++loading;
			busy = TRUE;
		}
		else if( launcher.m_state == LAUNCHER_OCCUPIED )
		{
			occupant = getOccupant( launcher );
			if( occupant != nullptr && ( occupant->isDisabledByType( DISABLED_UNMANNED )
				|| occupant->getControllingPlayer() != obj->getControllingPlayer() ) )
			{
				// taken from the facility on its launcher (a pilot snipe leaves it unmanned and neutral; a capture of
				// the facility moves its munitions along in onCapture): it can never launch, so it is lost
				occupant->kill();
				occupant = nullptr;
			}
			if( occupant == nullptr )
			{
				// flown off, destroyed or gone: free for the next load
				launcher.m_state = LAUNCHER_EMPTY;
				launcher.m_occupantID = INVALID_ID;
			}
			else
				occupied = TRUE;
		}

		// the launcher turns toward the target while its munition prepares, else back to rest
		const CruiseFlightUpdate *flight = occupant != nullptr ? CruiseFlightUpdate::findFlight( occupant ) : nullptr;
		Real wanted = (flight != nullptr && flight->isPreparing()) ? launcher.m_wantedAngle : 0.0f;
		if( launcher.m_angle != wanted )
		{
			Real delta = normalizeAngle( wanted - launcher.m_angle );
			Real maxTurn = data->m_launcherTurnRate;
			if( maxTurn <= 0.0f || fabs( delta ) <= maxTurn )
				launcher.m_angle = wanted;	// there
			else
				launcher.m_angle = normalizeAngle( launcher.m_angle + (delta > 0.0f ? maxTurn : -maxTurn) );
			if( occupant != nullptr )
				placeOccupant( i, occupant );
			busy = TRUE;
		}
		if( flight != nullptr && flight->isPreparing() )
			busy = TRUE;
	}

	// start new loads: the oldest purchase that has a free launcher of its type. Hypersonic @feature Pad overflow
	// (D-080): the ones waiting outside the magazine go first (they have nowhere else to go), then the magazine.
	for( Int pass = 0; pass < 2; ++pass )
	{
		std::vector<const ThingTemplate*> &queue = pass == 0 ? m_overflow : m_magazine;
		for( size_t k = 0; k < queue.size() && loading < data->m_simultaneousLoads; /* ++k */ )
		{
			Int free = findEmptyLauncher( queue[k] );
			if( free < 0 )
			{
				++k;
				continue;
			}
			Launcher &launcher = m_launchers[free];
			launcher.m_state = LAUNCHER_LOADING;
			launcher.m_framesLeft = launcher.m_loadFrames > 0 ? launcher.m_loadFrames : 1;
			if( isHypersonicScenarioInstantLoad() )
				launcher.m_framesLeft = 1;	// dev test harness (D-049)
			queue.erase( queue.begin() + k );
			++loading;
			busy = TRUE;
		}
	}

	restockForComputer();

	// launchers free up when a munition flies off or is destroyed, so keep watching while anything waits
	if( busy || !m_magazine.empty() || !m_overflow.empty() )
		return UPDATE_SLEEP_NONE;

	const Player *player = obj->getControllingPlayer();
	if( data->m_computerRestockReserve >= 0 && player != nullptr && player->getPlayerType() == PLAYER_COMPUTER )
		return UPDATE_SLEEP( RESTOCK_CHECK_FRAMES );

	// a munition waiting on a launcher may be destroyed or sniped there: look again now and then
	if( occupied )
		return UPDATE_SLEEP( LOGICFRAMES_PER_SECOND );

	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
// The munitions on the launchers belong to the facility: they die with it (killed, with their death effects)
// or leave with it (silently, when it is sold or removed). The magazine and loads are lost. Munitions already
// in the air fly on.
//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::removeOccupants( Bool kill )
{
	for( std::vector<Launcher>::iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		Object *occupant = getOccupant( *it );
		if( occupant != nullptr )
		{
			if( kill )
				occupant->kill();
			else
				TheGameLogic->destroyObject( occupant );
		}
		it->m_state = LAUNCHER_EMPTY;
		it->m_occupantID = INVALID_ID;
		it->m_framesLeft = 0;
	}
	m_magazine.clear();
	m_overflow.clear();	// Hypersonic @feature (D-080) lost with the facility too (D-027)
}

//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::onDie( const DamageInfo * /*damageInfo*/ )
{
	removeOccupants( TRUE );
}

//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::onDelete()
{
	removeOccupants( FALSE );

	// extend base class
	UpdateModule::onDelete();
}

//-------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::onCapture( Player *oldOwner, Player *newOwner )
{
	UpdateModule::onCapture( oldOwner, newOwner );

	// the munitions on the launchers change sides with the facility
	for( std::vector<Launcher>::iterator it = m_launchers.begin(); it != m_launchers.end(); ++it )
	{
		Object *occupant = getOccupant( *it );
		if( occupant != nullptr && newOwner != nullptr )
			occupant->setTeam( newOwner->getDefaultTeam() );
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
		DEBUG_ASSERTCRASH( name.isEmpty() || *tmpl != nullptr, ("MunitionMagazineUpdate::xfer - unknown munition '%s'", name.str()) );
	}
}

// ------------------------------------------------------------------------------------------------
/** Magazine, launchers and timers, shared by crc and xfer */
// ------------------------------------------------------------------------------------------------
static void xferTemplateList( Xfer *xfer, std::vector<const ThingTemplate*> *list )
{
	UnsignedShort storedCount = (UnsignedShort)list->size();
	xfer->xferUnsignedShort( &storedCount );
	if( xfer->getXferMode() == XFER_LOAD )
		list->clear();
	for( UnsignedShort i = 0; i < storedCount; ++i )
	{
		const ThingTemplate *munition = xfer->getXferMode() == XFER_LOAD ? nullptr : (*list)[i];
		xferTemplate( xfer, &munition );
		if( xfer->getXferMode() == XFER_LOAD && munition != nullptr )
			list->push_back( munition );
	}
}

// ------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::xferState( Xfer *xfer, Int version )
{
	// magazine, oldest first
	xferTemplateList( xfer, &m_magazine );

	// Hypersonic @feature Pad overflow (D-080): munitions waiting outside the magazine (none in older saves)
	if( version >= 3 )
		xferTemplateList( xfer, &m_overflow );
	else if( xfer->getXferMode() == XFER_LOAD )
		m_overflow.clear();

	// launchers (types and count come from INI; a save with a different count keeps what fits)
	UnsignedShort launcherCount = (UnsignedShort)m_launchers.size();
	xfer->xferUnsignedShort( &launcherCount );
	for( UnsignedShort j = 0; j < launcherCount; ++j )
	{
		Launcher loaded;
		Launcher &launcher = j < m_launchers.size() ? m_launchers[j] : loaded;
		xfer->xferInt( &launcher.m_state );
		xfer->xferUnsignedInt( &launcher.m_framesLeft );
		xfer->xferObjectID( &launcher.m_occupantID );
		xfer->xferReal( &launcher.m_angle );
		xfer->xferReal( &launcher.m_wantedAngle );
	}

	xfer->xferUnsignedInt( &m_nextRestockFrame );
	xfer->xferInt( &m_restockIndex );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

	// magazine and launchers
	xferState( xfer, 3 );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version (pads of a ParkingPlaceBehavior, D-039)
	* 2: Typed launchers (D-042); version 1 saves cannot be loaded
	* 3: Pad overflow (D-080): munitions waiting outside the magazine for their launcher */
// ------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	if( version < 2 )
		throw XFER_INVALID_VERSION;	// the D-039 pad layout cannot be read as typed launchers

	// extend base class
	UpdateModule::xfer( xfer );

	// magazine and launchers
	xferState( xfer, version );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MunitionMagazineUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
