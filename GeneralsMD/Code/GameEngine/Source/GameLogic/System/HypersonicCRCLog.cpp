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

// FILE: HypersonicCRCLog.cpp /////////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Network CRC diagnostics (see HypersonicCRCLog.h, Generals Hypersonic D-091).
//         This file is identical in the desktop engine and the Android port; keep it that way.
//
//         File format (text, '\n' line ends), HypersonicCRC_<frame>.txt:
//           HypersonicCRC 1
//           frame <n>
//           crc <final CRC, hex>
//           engine <compiler> <cpu>
//           switches <NAME>=<0|1> ...
//           seedcrc <GetGameLogicRandomSeedCRC(), hex>
//           records
//           <tag> <key=value ...> run=<running CRC after this record, hex>
//             B <hex of every byte hashed for this record; one group per xfer call, {} = nested snapshot>
//           ...
//           end
//         Tags: S = section marker/end, O = object, P = player, C = chunk of a large section,
//         X = the port's GeneralsOnline revision tail. Reals are written as <decimal>|<IEEE bits>.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/HypersonicCRCLog.h"

#include "Common/GameDefines.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"

#include <stdio.h>
#include <string.h>

namespace
{
	const Int MAX_LOGGED_FRAMES = 6;
	const Int CHUNK_BYTES = 1024;
	const Int RECENT_CRCS = 16;

	Bool s_checked = FALSE;			///< marker looked up for the current game
	Bool s_enabled = FALSE;
	UnsignedInt s_lastFrame = 0;
	Int s_logged = 0;
	UnsignedInt s_recentFrame[RECENT_CRCS];
	UnsignedInt s_recentCRC[RECENT_CRCS];
	Int s_recentNext = 0;
	Int s_recentCount = 0;

	void appendReal( std::string &s, const char *key, Real r )
	{
		UnsignedInt bits;
		memcpy( &bits, &r, sizeof(bits) );
		char buf[96];
		sprintf( buf, " %s=%.4f|%08X", key, (double)r, (unsigned)bits );
		s += buf;
	}

	void appendCoord( std::string &s, const char *key, const Coord3D *c )
	{
		std::string k( key );
		appendReal( s, (k + "x").c_str(), c->x );
		appendReal( s, (k + "y").c_str(), c->y );
		appendReal( s, (k + "z").c_str(), c->z );
	}

	template <class FLAGS>
	void appendBits( std::string &s, const char *key, const FLAGS &flags )
	{
		s += " ";
		s += key;
		s += "=";
		Bool any = FALSE;
		char buf[16];
		for( Int i = 0; i < flags.size(); ++i )
		{
			if( !flags.test( i ) )
				continue;
			sprintf( buf, any ? ",%d" : "%d", (int)i );
			s += buf;
			any = TRUE;
		}
		if( !any )
			s += "-";
	}

	// no spaces in a value, so every record splits cleanly on spaces
	std::string word( const char *text )
	{
		std::string w( text ? text : "" );
		if( w.empty() )
			return "-";
		for( size_t i = 0; i < w.size(); ++i )
			if( w[i] == ' ' || w[i] == '\t' || w[i] == '\r' || w[i] == '\n' )
				w[i] = '_';
		return w;
	}

	std::string playerName( Player *player )
	{
		AsciiString name;
		name.translate( player->getPlayerDisplayName() );
		return word( name.str() );
	}

	FILE *openUserFile( const char *leaf )
	{
		if( TheGlobalData == nullptr )
			return nullptr;
		AsciiString path = TheGlobalData->getPath_UserData();
		path.concat( leaf );
		return fopen( path.str(), "wb" );
	}

	const char *compilerName()
	{
#if defined(__clang__)
		return "clang";
#elif defined(__GNUC__)
		return "gcc";
#elif defined(_MSC_VER) && _MSC_VER < 1300
		return "msvc-vc6";
#elif defined(_MSC_VER)
		return "msvc";
#else
		return "unknown";
#endif
	}

	const char *cpuName()
	{
#if defined(__aarch64__) || defined(_M_ARM64)
		return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
		return "x64";
#elif defined(__i386__) || defined(_M_IX86)
		return "x86";
#else
		return "unknown";
#endif
	}
}

static void appendSwitch( std::string &s, const char *name, int value )
{
	char b[96];
	if( value < 0 )
		sprintf( b, " %s=undef", name );
	else
		sprintf( b, " %s=%d", name, value );
	s += b;
}

static std::string switchesLine()
{
	std::string s( "switches" );
#ifdef RETAIL_COMPATIBLE_CRC
	appendSwitch( s, "RETAIL_COMPATIBLE_CRC", (int)(RETAIL_COMPATIBLE_CRC) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_CRC", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_DATA
	appendSwitch( s, "RETAIL_COMPATIBLE_DATA", (int)(RETAIL_COMPATIBLE_DATA) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_DATA", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_XFER_SAVE
	appendSwitch( s, "RETAIL_COMPATIBLE_XFER_SAVE", (int)(RETAIL_COMPATIBLE_XFER_SAVE) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_XFER_SAVE", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_PATHFINDING
	appendSwitch( s, "RETAIL_COMPATIBLE_PATHFINDING", (int)(RETAIL_COMPATIBLE_PATHFINDING) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_PATHFINDING", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION
	appendSwitch( s, "RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION", (int)(RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_PATHFINDING_ALLOCATION", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM
	appendSwitch( s, "RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM", (int)(RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_CIRCLE_FILL_ALGORITHM", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_NETWORKING
	appendSwitch( s, "RETAIL_COMPATIBLE_NETWORKING", (int)(RETAIL_COMPATIBLE_NETWORKING) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_NETWORKING", -1 );
#endif
#ifdef RETAIL_COMPATIBLE_AIGROUP
	appendSwitch( s, "RETAIL_COMPATIBLE_AIGROUP", (int)(RETAIL_COMPATIBLE_AIGROUP) );
#else
	appendSwitch( s, "RETAIL_COMPATIBLE_AIGROUP", -1 );
#endif
#ifdef PRESERVE_BUILDING_RESUMPTION_DELAY
	appendSwitch( s, "PRESERVE_BUILDING_RESUMPTION_DELAY", (int)(PRESERVE_BUILDING_RESUMPTION_DELAY) );
#else
	appendSwitch( s, "PRESERVE_BUILDING_RESUMPTION_DELAY", -1 );
#endif
#ifdef PRESERVE_CHINOOK_PASSENGER_DUMPING
	appendSwitch( s, "PRESERVE_CHINOOK_PASSENGER_DUMPING", (int)(PRESERVE_CHINOOK_PASSENGER_DUMPING) );
#else
	appendSwitch( s, "PRESERVE_CHINOOK_PASSENGER_DUMPING", -1 );
#endif
#ifdef PRESERVE_HARDCODED_BLACK_LOTUS_CASH_HACK
	appendSwitch( s, "PRESERVE_HARDCODED_BLACK_LOTUS_CASH_HACK", (int)(PRESERVE_HARDCODED_BLACK_LOTUS_CASH_HACK) );
#else
	appendSwitch( s, "PRESERVE_HARDCODED_BLACK_LOTUS_CASH_HACK", -1 );
#endif
#ifdef PRESERVE_MULTI_CRATE_PICKUP
	appendSwitch( s, "PRESERVE_MULTI_CRATE_PICKUP", (int)(PRESERVE_MULTI_CRATE_PICKUP) );
#else
	appendSwitch( s, "PRESERVE_MULTI_CRATE_PICKUP", -1 );
#endif
#ifdef PRESERVE_NO_XP_FROM_FLAME_KILLS
	appendSwitch( s, "PRESERVE_NO_XP_FROM_FLAME_KILLS", (int)(PRESERVE_NO_XP_FROM_FLAME_KILLS) );
#else
	appendSwitch( s, "PRESERVE_NO_XP_FROM_FLAME_KILLS", -1 );
#endif
#ifdef PRESERVE_NO_XP_FROM_OCL_KILLS
	appendSwitch( s, "PRESERVE_NO_XP_FROM_OCL_KILLS", (int)(PRESERVE_NO_XP_FROM_OCL_KILLS) );
#else
	appendSwitch( s, "PRESERVE_NO_XP_FROM_OCL_KILLS", -1 );
#endif
#ifdef PRESERVE_NO_XP_FROM_POISON_KILLS
	appendSwitch( s, "PRESERVE_NO_XP_FROM_POISON_KILLS", (int)(PRESERVE_NO_XP_FROM_POISON_KILLS) );
#else
	appendSwitch( s, "PRESERVE_NO_XP_FROM_POISON_KILLS", -1 );
#endif
#ifdef PRESERVE_OCCUPANT_DETECTION_VIA_DRAG_SELECTION
	appendSwitch( s, "PRESERVE_OCCUPANT_DETECTION_VIA_DRAG_SELECTION", (int)(PRESERVE_OCCUPANT_DETECTION_VIA_DRAG_SELECTION) );
#else
	appendSwitch( s, "PRESERVE_OCCUPANT_DETECTION_VIA_DRAG_SELECTION", -1 );
#endif
#ifdef PRESERVE_PERPETUAL_HORDE_BONUS
	appendSwitch( s, "PRESERVE_PERPETUAL_HORDE_BONUS", (int)(PRESERVE_PERPETUAL_HORDE_BONUS) );
#else
	appendSwitch( s, "PRESERVE_PERPETUAL_HORDE_BONUS", -1 );
#endif
#ifdef PRESERVE_PREMATURE_BATTLE_BUS_DEATH
	appendSwitch( s, "PRESERVE_PREMATURE_BATTLE_BUS_DEATH", (int)(PRESERVE_PREMATURE_BATTLE_BUS_DEATH) );
#else
	appendSwitch( s, "PRESERVE_PREMATURE_BATTLE_BUS_DEATH", -1 );
#endif
#ifdef PRESERVE_RADAR_WARNING_SUPPRESSION
	appendSwitch( s, "PRESERVE_RADAR_WARNING_SUPPRESSION", (int)(PRESERVE_RADAR_WARNING_SUPPRESSION) );
#else
	appendSwitch( s, "PRESERVE_RADAR_WARNING_SUPPRESSION", -1 );
#endif
#ifdef PRESERVE_STRUCTURE_STEALTH_DURING_REPAIR
	appendSwitch( s, "PRESERVE_STRUCTURE_STEALTH_DURING_REPAIR", (int)(PRESERVE_STRUCTURE_STEALTH_DURING_REPAIR) );
#else
	appendSwitch( s, "PRESERVE_STRUCTURE_STEALTH_DURING_REPAIR", -1 );
#endif
#ifdef PRESERVE_TUNNEL_HEAL_STACKING
	appendSwitch( s, "PRESERVE_TUNNEL_HEAL_STACKING", (int)(PRESERVE_TUNNEL_HEAL_STACKING) );
#else
	appendSwitch( s, "PRESERVE_TUNNEL_HEAL_STACKING", -1 );
#endif
#ifdef PRESERVE_UNRELIABLE_FIRESTORMS
	appendSwitch( s, "PRESERVE_UNRELIABLE_FIRESTORMS", (int)(PRESERVE_UNRELIABLE_FIRESTORMS) );
#else
	appendSwitch( s, "PRESERVE_UNRELIABLE_FIRESTORMS", -1 );
#endif
#ifdef PRESERVE_SNIPING_EMPTY_STINGER_SITES
	appendSwitch( s, "PRESERVE_SNIPING_EMPTY_STINGER_SITES", (int)(PRESERVE_SNIPING_EMPTY_STINGER_SITES) );
#else
	appendSwitch( s, "PRESERVE_SNIPING_EMPTY_STINGER_SITES", -1 );
#endif
#ifdef PRESERVE_RETAIL_SCRIPTED_CAMERA
	appendSwitch( s, "PRESERVE_RETAIL_SCRIPTED_CAMERA", (int)(PRESERVE_RETAIL_SCRIPTED_CAMERA) );
#else
	appendSwitch( s, "PRESERVE_RETAIL_SCRIPTED_CAMERA", -1 );
#endif
	return s;
}

// ------------------------------------------------------------------------------------------------
Bool HypersonicCRCLog::wantFrame( UnsignedInt frame )
{
	// a frame at or before the last one we saw means a new game (or replay): look for the marker again
	if( !s_checked || frame <= s_lastFrame )
	{
		FILE *marker = fopen( "hypersonic_crc_log.txt", "r" );
		s_enabled = marker != nullptr;
		if( marker )
			fclose( marker );
		s_checked = TRUE;
		s_logged = 0;
		s_recentNext = 0;
		s_recentCount = 0;
	}
	s_lastFrame = frame;

	if( !s_enabled || s_logged >= MAX_LOGGED_FRAMES )
		return FALSE;
	++s_logged;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::noteLocalCRC( UnsignedInt frame, UnsignedInt crc )
{
	if( !s_enabled )
		return;
	s_recentFrame[s_recentNext] = frame;
	s_recentCRC[s_recentNext] = crc;
	s_recentNext = (s_recentNext + 1) % RECENT_CRCS;
	if( s_recentCount < RECENT_CRCS )
		++s_recentCount;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::logMismatch( UnsignedInt frame, Int count, const Int *playerIndex, const UnsignedInt *crc,
	Int numConnectedPlayers )
{
	if( !s_enabled )
		return;

	char leaf[64];
	sprintf( leaf, "HypersonicCRC_mismatch_%u.txt", (unsigned)frame );
	FILE *fp = openUserFile( leaf );
	if( fp == nullptr )
		return;

	fprintf( fp, "HypersonicCRCMismatch 1\n" );
	fprintf( fp, "frame %u\n", (unsigned)frame );
	fprintf( fp, "engine %s %s\n", compilerName(), cpuName() );
	fprintf( fp, "connected_players %d\n", (int)numConnectedPlayers );
	fprintf( fp, "reported_crcs %d\n", (int)count );
	for( Int i = 0; i < count; ++i )
	{
		Player *player = ThePlayerList ? ThePlayerList->getNthPlayer( playerIndex[i] ) : nullptr;
		fprintf( fp, "player %d name=%s local=%d crc=%08X\n", (int)playerIndex[i],
			player ? playerName( player ).c_str() : "-",
			(player && player->isLocalPlayer()) ? 1 : 0, (unsigned)crc[i] );
	}
	// our own recent CRCs, oldest first: the reported ones belong to one of these frames
	for( Int j = 0; j < s_recentCount; ++j )
	{
		Int k = (s_recentNext - s_recentCount + j + RECENT_CRCS) % RECENT_CRCS;
		fprintf( fp, "local_crc frame=%u crc=%08X\n", (unsigned)s_recentFrame[k], (unsigned)s_recentCRC[k] );
	}
	fprintf( fp, "end\n" );
	fclose( fp );
}

// ------------------------------------------------------------------------------------------------
HypersonicCRCLog::HypersonicCRCLog( UnsignedInt frame ) :
	m_frame( frame ),
	m_pendingBytes( 0 ),
	m_depth( 0 ),
	m_players( FALSE ),
	m_playerIndex( 0 ),
	m_chunks( FALSE ),
	m_chunkIndex( 0 ),
	m_recordIndex( 0 )
{
	m_out.reserve( 1 << 20 );
	m_pending.reserve( 4096 );
}

// ------------------------------------------------------------------------------------------------
HypersonicCRCLog::~HypersonicCRCLog()
{
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::xferImplementation( void *data, Int dataSize )
{
	XferCRC::xferImplementation( data, dataSize );	// the CRC itself: unchanged

	if( data == nullptr || dataSize <= 0 )
		return;

	static const char HEX[] = "0123456789ABCDEF";
	const unsigned char *bytes = (const unsigned char *)data;
	if( !m_pending.empty() && m_pending[m_pending.size() - 1] != '{' )
		m_pending += ' ';
	for( Int i = 0; i < dataSize; ++i )
	{
		m_pending += HEX[bytes[i] >> 4];
		m_pending += HEX[bytes[i] & 15];
	}
	m_pendingBytes += dataSize;

	if( m_chunks && m_pendingBytes >= CHUNK_BYTES )
	{
		char text[64];
		sprintf( text, "%d", (int)m_chunkIndex++ );
		emit( "C", (m_chunkSection + " chunk=" + text).c_str() );
	}
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::xferSnapshot( Snapshot *snapshot )
{
	if( snapshot == nullptr )
		return;

	m_pending += '{';
	++m_depth;
	XferCRC::xferSnapshot( snapshot );
	--m_depth;
	m_pending += '}';

	if( m_players && m_depth == 1 )
		emitPlayer( m_playerIndex++ );
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::emit( const char *tag, const char *text )
{
	char run[32];
	sprintf( run, " run=%08X\n", (unsigned)getCRC() );
	m_out += tag;
	m_out += ' ';
	m_out += text;
	m_out += run;
	if( !m_pending.empty() )
	{
		m_out += "  B ";
		m_out += m_pending;
		m_out += '\n';
	}
	m_pending.erase();
	m_pendingBytes = 0;
	++m_recordIndex;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::record( const char *tag, const char *text )
{
	emit( tag, text );
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::recordObject( Object *obj )
{
	std::string s;
	char buf[128];

	const ThingTemplate *tmpl = obj->getTemplate();
	sprintf( buf, "id=%u", (unsigned)obj->getID() );
	s += buf;
	s += " tmpl=" + word( tmpl ? tmpl->getName().str() : nullptr );

	const Team *team = obj->getTeam();
	Player *player = team ? obj->getControllingPlayer() : nullptr;
	sprintf( buf, " player=%d team=%u", player ? (int)player->getPlayerIndex() : -1,
		team ? (unsigned)team->getID() : 0u );
	s += buf;

	appendCoord( s, "pos", obj->getPosition() );
	appendReal( s, "ang", obj->getOrientation() );

	BodyModuleInterface *body = obj->getBodyModule();
	if( body )
	{
		appendReal( s, "hp", body->getHealth() );
		appendReal( s, "maxhp", body->getMaxHealth() );
		appendReal( s, "dmgscalar", body->getDamageScalar() );
	}

	appendBits( s, "status", obj->getStatusBits() );
	sprintf( buf, " wbonus=%08X", (unsigned)obj->getWeaponBonusCondition() );
	s += buf;

	const PhysicsBehavior *physics = obj->getPhysics();
	if( physics )
		appendCoord( s, "vel", physics->getVelocity() );

	// client-side, not hashed; listed because it shows what the object is doing
	Drawable *draw = obj->getDrawable();
	if( draw )
		appendBits( s, "model", draw->getModelConditionFlags() );

	emit( "O", s.c_str() );
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::emitPlayer( Int index )
{
	std::string s;
	char buf[128];
	sprintf( buf, "index=%d", (int)index );
	s += buf;

	Player *player = ThePlayerList ? ThePlayerList->getNthPlayer( index ) : nullptr;
	if( player )
	{
		s += " name=" + playerName( player );
		s += " side=" + word( player->getSide().str() );
		sprintf( buf, " type=%d money=%u", (int)player->getPlayerType(), (unsigned)player->getMoney()->countMoney() );
		s += buf;
	}
	emit( "P", s.c_str() );
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::beginPlayers()
{
	m_players = TRUE;
	m_playerIndex = 0;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::endPlayers()
{
	m_players = FALSE;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::beginChunks( const char *sectionName )
{
	m_chunks = TRUE;
	m_chunkSection = sectionName;
	m_chunkIndex = 0;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::endChunks()
{
	m_chunks = FALSE;
}

// ------------------------------------------------------------------------------------------------
void HypersonicCRCLog::finish( UnsignedInt finalCRC, UnsignedInt seedCRC )
{
	if( !m_pending.empty() )
		emit( "S", "tail" );

	char leaf[64];
	sprintf( leaf, "HypersonicCRC_%u.txt", (unsigned)m_frame );
	FILE *fp = openUserFile( leaf );
	if( fp == nullptr )
		return;

	fprintf( fp, "HypersonicCRC 1\n" );
	fprintf( fp, "frame %u\n", (unsigned)m_frame );
	fprintf( fp, "crc %08X\n", (unsigned)finalCRC );
	fprintf( fp, "engine %s %s\n", compilerName(), cpuName() );
	fprintf( fp, "%s\n", switchesLine().c_str() );
	fprintf( fp, "seedcrc %08X\n", (unsigned)seedCRC );
	fprintf( fp, "records %d\n", (int)m_recordIndex );
	fwrite( m_out.data(), 1, m_out.size(), fp );
	fprintf( fp, "end\n" );
	fclose( fp );
}
