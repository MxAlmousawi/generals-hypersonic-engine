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

// FILE: HypersonicScenario.cpp /////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Dev test harness (see HypersonicScenario.h, Generals Hypersonic D-049).
//
//         Scenario file: one "Key = values" per line, ';' or '#' starts a comment, keys ignore case.
//           Map = <map path>                 (default Maps\HypersonicScenario\HypersonicScenario.map)
//           Campaign = <name> easy|normal|hard    real campaign; preserves mission scripts, AI and victory
//           ChallengeFast = Yes|No            test only: CH_DebugFast before mission scripts start
//           Enemy = <PlayerTemplate>          (default FactionAmerica)
//           IranColor / EnemyColor = <index>    multiplayer colours, defaults 0 (Gold) / 1 (Red); two-side skirmish only
//           Duration = <seconds>              game time, then the game quits
//           Seed = <n>, InstantLoad = Yes|No, HideUI = Yes|No
//           Speed = max|<fps>                 render (= logic) frame rate; default max: as fast as the PC renders
//           Money / Science / Upgrade / Object / Magazine = <args>   setup, run at game start (see runCommand)
//           At = <seconds> <command> <args>    a command at that game time
//           At = <s> BaseStatus <player> <label> <x> <y> <radius>   complete/building structures
//           At = <s> PlayerStatus <player>    money, living objects and counts per template
//           At = <s> Protect <tag> Yes|No     test only: object's stock indestructible flag
//           At = <s> KillBase <player> <label> <x> <y> <radius>   kill structures inside test area
//           Camera = <seconds> <x> <y> <height> [pitch] [yaw]   or   Camera = <seconds> @<tag> <height> [pitch] [yaw]
//             (@<template>: follows the highest living object of that template)
//           CameraFree = <seconds>          the Camera keys stop then: the player moves the camera (manual tests)
//           CameraEye = <seconds> <x y | @tag> <height> <x y | @tag | ahead> <height or distance> [fov]   free camera for
//           clips (D-272): the eye and the point it looks at, heights above the ground or the tagged object, can look up;
//           keys interpolate. CameraEye = <seconds> off back to the Camera keys. Sky = Yes draws the sky box.
//           Screenshot = <seconds>,  Screenshots = <from> <to> <every msec>
//           Log = <tag or template> <every msec>
//         Players are "Iran" (the local player), "Enemy" and "Neutral". Positions are world x y on the ground.
//
//         Hypersonic @feature (D-071 AI battle harness)
//           AIBattle = <Iran difficulty> <Enemy difficulty>   Easy|Medium|Hard: both are skirmish AIs (normal start,
//             their skirmish scripts, victory check on), the local player is an observer; quits at the first defeat
//             (or Duration) and writes result.txt; battle.csv gets a line every 60 game seconds
//           LowDetail = Yes|No                 lowest client detail (Options "Low" and less); default Yes in an AI battle
//           DrawEvery = <n>                    render only every n-th frame (client only; screenshot frames always)
//         Hypersonic @feature (D-126): IranTeamTarget = Legacy   the Iran AIs pick their enemy and approach path the stock
//           way (comparison runs); otherwise events.txt gets AI TARGET (enemy changes) and AI PATH (push path) lines
//         Hypersonic @feature (D-110): MunitionPicker = Legacy   the Iran AI aims its launches the D-078 way (comparison
//           runs); every AI launch at an object is logged in events.txt with the object's fate (STRIKE lines)
//         Hypersonic @feature (D-190): AIState = <seconds>|Off   (AI battle default 30) an AI STATE line in events.txt: the
//           Iran AI's attack flags/counters, its Karrar/Zolfaghar count in its outer perimeter and its teams (units/in
//           outer perimeter/state)
//         Hypersonic @feature (D-189 optional units): IranTeamUnits = Stock   a team unit without its factory/tech holds the
//           whole team even when it is optional (stock engine rule; comparison runs)
//         Hypersonic @feature (D-189): MunitionPicker = D122   the D-110/D-122 kill-only picker and its restock probes
//           instead of the D-189 strike doctrine (drone breach volleys, missile salvos; BREACH / MISSILE PLAN lines)
//         Hypersonic @feature (D-246): At = <s> Tag <tag> <template> [<player>]   binds the newest living match
//           (highest object ID), including factory-produced units; logs success/failure to events.txt
//         D-246: Button <tag> <command button> [x y] selects in client + logic, clicks ControlBar,
//           and completes targeted clicks through CommandTranslator (no direct script power calls).
//         Hypersonic @feature (D-099/D-246): At = <s> Select <tag> selects that object in client and logic
//         Hypersonic @feature (D-223): At = <s> Options [Open]   opens the Options menu (as the in-game menu's Options button;
//           Open: also drops down its camera zoom-out list);
//           At = <s> CameraZoomOut <height>   applies that zoom-out choice like the Options menu's Accept (not saved) and
//           zooms the player camera fully out
//         Hypersonic @feature (voice showcase): At = <s> Voice <tag> <Select|Move|Attack|other key>   plays that
//           object's voice like a player order does (other keys: UnitSpecificSounds, e.g. VoiceCreate)
//         Hypersonic @feature (AI team battle): Player = <PlayerTemplate> <team> [Easy|Medium|Hard] [start]   with AIBattle:
//           instead of Iran vs Enemy, up to 7 AI players (slots 1-7, the observer keeps slot 0), allied by team number
//           (1-4), start 1-based (default random); the game quits when one team is left. battle.csv, production.csv
//           and result.txt get one column/row per player (p1..p7 in file order), events.txt an ELIMINATED line each.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/HypersonicScenario.h"

#include "Common/FramePacer.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/Team.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/BuildAssistant.h"
#include "GameClient/ControlBar.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindowManager.h"
#include "Common/OptionPreferences.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"
#include "GameNetwork/GameInfo.h"

#include <algorithm>
#include <stdarg.h>
#include <stdlib.h>

HypersonicScenario *TheHypersonicScenario = nullptr;

static const char *const DEFAULT_MAP = "Maps\\HypersonicScenario\\HypersonicScenario.map";

//-------------------------------------------------------------------------------------------------
static Int secondsToFrame( const AsciiString &s )
{
	return REAL_TO_INT_FLOOR( (Real)atof( s.str() ) * LOGICFRAMES_PER_SECOND + 0.5f );
}

//-------------------------------------------------------------------------------------------------
static Bool isYes( const AsciiString &s )
{
	return s.compareNoCase( "yes" ) == 0 || s.compareNoCase( "true" ) == 0 || s.compareNoCase( "1" ) == 0;
}

//-------------------------------------------------------------------------------------------------
static Bool is( const AsciiString &s, const char *keyword )
{
	return s.compareNoCase( keyword ) == 0;
}

//-------------------------------------------------------------------------------------------------
/// Hypersonic @feature (D-071 AI battle harness) Easy|Medium|Hard -> skirmish AI slot state
static Int aiSlotState( const AsciiString &s )
{
	if( is( s, "Easy" ) )
		return SLOT_EASY_AI;
	if( is( s, "Medium" ) || is( s, "Normal" ) )
		return SLOT_MED_AI;
	return SLOT_BRUTAL_AI;
}

//-------------------------------------------------------------------------------------------------
static const char *aiSlotName( Int state )
{
	return state == SLOT_EASY_AI ? "Easy" : state == SLOT_MED_AI ? "Medium" : "Hard";
}

static const Int BATTLE_LINE_FRAMES = 60 * LOGICFRAMES_PER_SECOND;	///< battle.csv: a line every 60 game seconds
static const Int STRIKE_CHECK_FRAMES = LOGICFRAMES_PER_SECOND / 2;	///< Hypersonic @feature (D-110) strike log: how often a target's fate is checked
static const Real STRIKE_OUTCOME_SECONDS = 90.0f;	///< Hypersonic @feature (D-110) a target that dies this soon after a launch counts as killed

//-------------------------------------------------------------------------------------------------
/// the names of the set bits, joined by '|'
template <class FLAGS>
static AsciiString bitNames( const FLAGS &flags )
{
	AsciiString result;
	for( Int bit = 0; bit < flags.size(); ++bit )
	{
		const char *name = flags.getBitNameIfSet( bit );
		if( name == nullptr )
			continue;
		if( result.isNotEmpty() )
			result.concat( '|' );
		result.concat( name );
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
/// the camera's object to follow when the name is not a tag: the highest living object of that template
static Coord3D s_lastLookAt;					///< where the camera looked last (a followed object that is gone keeps it there)
static Bool s_haveLastLookAt = FALSE;

static Object *highestOfTemplate( const AsciiString &templateName )
{
	Object *best = nullptr;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || obj->getTemplate()->getName() != templateName )
			continue;
		if( best == nullptr || obj->getHeightAboveTerrain() > best->getHeightAboveTerrain() )
			best = obj;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
HypersonicScenario::HypersonicScenario() :
	m_mapName( DEFAULT_MAP ),
	m_campaignDifficulty( DIFFICULTY_NORMAL ),
	m_enemyFaction( "FactionAmerica" ),
	m_iranColor( 0 ),
	m_enemyColor( 1 ),
	m_seed( 1 ),
	m_durationFrames( 30 * LOGICFRAMES_PER_SECOND ),
	m_instantLoad( FALSE ),
	m_hideUI( TRUE ),
	m_speedFps( 0 ),
	m_aiBattle( FALSE ),
	m_iranSlotState( SLOT_BRUTAL_AI ),
	m_enemySlotState( SLOT_BRUTAL_AI ),
	m_lowDetail( -1 ),
	m_drawEvery( 1 ),
	m_sky( FALSE ),
	m_freeCameraOn( FALSE ),
	m_cameraFreeFrame( -1 ),
	m_battle( nullptr ),
	m_legacyMunitionPicker( FALSE ),
	m_d122MunitionPicker( FALSE ),
	m_legacyIranTeamTarget( FALSE ),
	m_stockTeamUnits( FALSE ),
	m_aiStateFrames( -1 ),
	m_battleStartMs( 0 ),
	m_production( nullptr ),
	m_eliminations( 0 ),
	m_iran( nullptr ),
	m_enemy( nullptr ),
	m_started( FALSE ),
	m_clientStarted( FALSE ),
	m_finished( FALSE ),
	m_nextCommand( 0 ),
	m_nextShot( 0 ),
	m_shotCount( 0 ),
	m_log( nullptr ),
	m_logStartMs( 0 ),
	m_events( nullptr )
{
	m_scenarioFile = TheGlobalData->m_hypersonicScenario;
	m_outputDir = TheGlobalData->m_hypersonicScenarioOutput;
	if( m_outputDir.isEmpty() )
		m_outputDir = ".";
}

//-------------------------------------------------------------------------------------------------
HypersonicScenario::~HypersonicScenario()
{
	if( m_log )
		fclose( m_log );
	if( m_events )
		fclose( m_events );
	if( m_battle )
		fclose( m_battle );
	if( m_production )
		fclose( m_production );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-126) AI decisions (enemy choice, push path) for events.txt
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::noteAI( const char *format, ... )
{
	if( m_events == nullptr || m_finished )
		return;
	char buffer[1024];
	va_list args;
	va_start( args, format );
	vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	buffer[sizeof( buffer ) - 1] = 0;
	event( "%s", buffer );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::noteCookOff( const char *munition, const Coord3D *pos, Bool cookedOff )
{
	if( m_events == nullptr || m_finished || munition == nullptr || pos == nullptr )
		return;
	event( "%s %s at %.0f %.0f height %.0f", cookedOff ? "COOK-OFF" : "no cook-off", munition, pos->x, pos->y,
		pos->z - ( TheTerrainLogic ? TheTerrainLogic->getGroundHeight( pos->x, pos->y ) : 0.0f ) );
	if( TheInGameUI )
	{
		UnicodeString text;
		AsciiString ascii;
		ascii.format( "%s: %s", cookedOff ? "COOK-OFF" : "no cook-off", munition );
		text.translate( ascii );
		RGBColor color;
		color.red = 1.0f;
		color.green = cookedOff ? 0.35f : 1.0f;
		color.blue = cookedOff ? 0.2f : 1.0f;
		TheInGameUI->messageNoFormat( &color, text );
	}
}

const char *HypersonicScenario::sideLabel( const Player *player ) const
{
	const Int side = sideOf( player );
	if( side >= 0 && side < (Int)m_sideNames.size() )
		return m_sideNames[side].str();
	return "?";
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::event( const char *format, ... )
{
	if( m_events == nullptr )
		return;
	char buffer[1024];
	va_list args;
	va_start( args, format );
	vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	buffer[sizeof( buffer ) - 1] = 0;

	UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	fprintf( m_events, "[f%05u t%7.2f] %s\n", frame, (Real)frame / LOGICFRAMES_PER_SECOND, buffer );
	fflush( m_events );
}

//-------------------------------------------------------------------------------------------------
Bool HypersonicScenario::parseFile( const AsciiString &path )
{
	FILE *file = fopen( path.str(), "r" );
	if( file == nullptr )
	{
		event( "ERROR cannot open scenario file %s", path.str() );
		return FALSE;
	}

	char line[1024];
	Int lineNumber = 0;
	while( fgets( line, sizeof( line ), file ) )
	{
		++lineNumber;
		char *comment = strpbrk( line, ";#" );
		if( comment )
			*comment = 0;
		char *equals = strchr( line, '=' );
		if( equals == nullptr )
			continue;
		*equals = 0;

		std::vector<AsciiString> tokens;
		AsciiString key( line );
		key.trim();
		tokens.push_back( key );
		AsciiString rest( equals + 1 );
		AsciiString token;
		while( rest.nextToken( &token, " \t\r\n," ) )
			tokens.push_back( token );
		if( is( key, "Map" ) )
		{
			// Hypersonic @feature (D-071 AI battle harness) the whole value: stock map paths have spaces
			AsciiString value( equals + 1 );
			value.trim();
			if( value.isNotEmpty() )
				m_mapName = value;
		}
		else if( key.isNotEmpty() )
			parseLine( tokens, lineNumber );
	}
	fclose( file );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::parseLine( std::vector<AsciiString> &tokens, Int line )
{
	const AsciiString &key = tokens[0];
	const size_t count = tokens.size();

	if( is( key, "Map" ) && count > 1 )
		m_mapName = tokens[1];
	// Hypersonic @feature (D-244): ordinary campaign launch and difficulty flags.
	else if( is( key, "Campaign" ) && count > 1 )
	{
		m_campaignName = tokens[1];
		if( count > 2 )
		{
			if( is( tokens[2], "easy" ) ) m_campaignDifficulty = DIFFICULTY_EASY;
			else if( is( tokens[2], "normal" ) || is( tokens[2], "medium" ) ) m_campaignDifficulty = DIFFICULTY_NORMAL;
			else if( is( tokens[2], "hard" ) ) m_campaignDifficulty = DIFFICULTY_HARD;
			else event( "ERROR line %d: Campaign difficulty must be easy, normal or hard", line );
		}
	}
	else if( is( key, "Enemy" ) && count > 1 )
		m_enemyFaction = tokens[1];
	else if( is( key, "IranColor" ) || is( key, "EnemyColor" ) )
	{
		// Explicit multiplayer colour index; malformed/out-of-range values keep the previous/default colour.
		char *end = nullptr;
		const long color = count == 2 ? strtol( tokens[1].str(), &end, 10 ) : -1;
		if( count != 2 || end == tokens[1].str() || *end != 0 || color < 0 ||
			TheMultiplayerSettings == nullptr || color >= TheMultiplayerSettings->getNumColors() )
			event( "ERROR line %d: %s requires a valid multiplayer colour index", line, key.str() );
		else if( is( key, "IranColor" ) )
			m_iranColor = (Int)color;
		else
			m_enemyColor = (Int)color;
	}
	else if( is( key, "Duration" ) && count > 1 )
		m_durationFrames = secondsToFrame( tokens[1] );
	else if( is( key, "Seed" ) && count > 1 )
		m_seed = atoi( tokens[1].str() );
	else if( is( key, "InstantLoad" ) && count > 1 )
		m_instantLoad = isYes( tokens[1] );
	else if( is( key, "HideUI" ) && count > 1 )
		m_hideUI = isYes( tokens[1] );
	else if( is( key, "Speed" ) && count > 1 )
		m_speedFps = is( tokens[1], "max" ) ? 0 : atoi( tokens[1].str() );
	// Hypersonic @feature (D-071 AI battle harness)
	else if( is( key, "AIBattle" ) && count > 2 )
	{
		m_aiBattle = TRUE;
		m_iranSlotState = aiSlotState( tokens[1] );
		m_enemySlotState = aiSlotState( tokens[2] );
	}
	// Hypersonic @feature (AI team battle) Player = <PlayerTemplate> <team> [difficulty] [start]
	else if( is( key, "Player" ) && count > 2 )
	{
		BattlePlayer bp;
		bp.m_faction = tokens[1];
		bp.m_team = atoi( tokens[2].str() );
		bp.m_slotState = count > 3 ? aiSlotState( tokens[3] ) : SLOT_BRUTAL_AI;
		bp.m_start = count > 4 ? atoi( tokens[4].str() ) - 1 : -1;
		bp.m_player = nullptr;
		bp.m_eliminatedFrame = -1;
		bp.m_eliminationOrder = 0;
		if( bp.m_team < 1 || bp.m_team > 4 )
			event( "ERROR line %d: Player team must be 1-4", line );
		else if( m_teamPlayers.size() >= MAX_SLOTS - 1 )
			event( "ERROR line %d: at most %d Player lines (slot 0 is the observer)", line, MAX_SLOTS - 1 );
		else
			m_teamPlayers.push_back( bp );
	}
	else if( is( key, "LowDetail" ) && count > 1 )
		m_lowDetail = isYes( tokens[1] ) ? 1 : 0;
	else if( is( key, "MunitionPicker" ) && count > 1 )	// Hypersonic @feature (D-110)
	{
		m_legacyMunitionPicker = is( tokens[1], "Legacy" );
		m_d122MunitionPicker = is( tokens[1], "D122" );	// Hypersonic @feature (D-189)
	}
	else if( is( key, "IranTeamTarget" ) && count > 1 )	// Hypersonic @feature (D-126)
		m_legacyIranTeamTarget = is( tokens[1], "Legacy" );
	else if( is( key, "IranTeamUnits" ) && count > 1 )	// Hypersonic @feature (D-189 optional units)
		m_stockTeamUnits = is( tokens[1], "Stock" );
	else if( is( key, "AIState" ) && count > 1 )	// Hypersonic @feature (D-190)
		m_aiStateFrames = is( tokens[1], "Off" ) ? 0 : secondsToFrame( tokens[1] );
	else if( is( key, "CameraFree" ) && count > 1 )	// Hypersonic @feature (manual tests): hand the camera to the player
		m_cameraFreeFrame = secondsToFrame( tokens[1] );
	else if( is( key, "DrawEvery" ) && count > 1 )
	{
		m_drawEvery = atoi( tokens[1].str() );
		if( m_drawEvery < 1 )
			m_drawEvery = 1;
	}
	else if( is( key, "At" ) && count > 2 )
	{
		Command cmd;
		cmd.m_frame = secondsToFrame( tokens[1] );
		cmd.m_line = line;
		cmd.m_args.assign( tokens.begin() + 2, tokens.end() );
		m_commands.push_back( cmd );
	}
	else if( is( key, "Money" ) || is( key, "Science" ) || is( key, "Upgrade" ) || is( key, "Object" ) || is( key, "Magazine" ) || is( key, "ChallengeFast" ) )
	{
		Command cmd;
		cmd.m_frame = -1;
		cmd.m_line = line;
		cmd.m_args = tokens;
		m_commands.push_back( cmd );
	}
	else if( is( key, "Camera" ) && count > 3 )
	{
		CameraKey cam;
		cam.m_frame = secondsToFrame( tokens[1] );
		size_t next;
		if( tokens[2].startsWith( "@" ) )
		{
			cam.m_follow = TRUE;
			cam.m_tag = tokens[2].str() + 1;
			cam.m_x = cam.m_y = 0.0f;
			next = 3;
		}
		else
		{
			cam.m_follow = FALSE;
			cam.m_x = (Real)atof( tokens[2].str() );
			cam.m_y = (Real)atof( tokens[3].str() );
			next = 4;
		}
		cam.m_height = count > next ? (Real)atof( tokens[next].str() ) : 300.0f;
		cam.m_pitch = count > next + 1 ? (Real)atof( tokens[next + 1].str() ) : 37.5f;
		cam.m_yaw = count > next + 2 ? (Real)atof( tokens[next + 2].str() ) : 0.0f;
		size_t at = 0;
		while( at < m_cameraKeys.size() && m_cameraKeys[at].m_frame <= cam.m_frame )
			++at;
		m_cameraKeys.insert( m_cameraKeys.begin() + at, cam );
	}
	else if( is( key, "Sky" ) && count > 1 )	// Hypersonic @feature (D-272)
		m_sky = is( tokens[1], "Yes" );
	else if( is( key, "CameraEye" ) && count > 2 )	// Hypersonic @feature (D-272)
	{
		EyeKey k;
		k.m_frame = secondsToFrame( tokens[1] );
		k.m_off = is( tokens[2], "off" );
		k.m_eyeFollow = FALSE;
		k.m_eyeX = k.m_eyeY = k.m_eyeH = k.m_lookX = k.m_lookY = k.m_lookH = k.m_fov = 0.0f;
		k.m_lookMode = 0;
		k.m_haveEye = k.m_haveLook = FALSE;
		if( !k.m_off )
		{
			size_t next = 2;
			if( tokens[next].startsWith( "@" ) )
			{
				k.m_eyeFollow = TRUE;
				k.m_eyeTag = tokens[next].str() + 1;
				next += 1;
			}
			else if( count > next + 1 )
			{
				k.m_eyeX = (Real)atof( tokens[next].str() );
				k.m_eyeY = (Real)atof( tokens[next + 1].str() );
				next += 2;
			}
			if( count > next )
				k.m_eyeH = (Real)atof( tokens[next++].str() );
			if( count > next && tokens[next].startsWith( "@" ) )
			{
				k.m_lookMode = 1;
				k.m_lookTag = tokens[next++].str() + 1;
			}
			else if( count > next && is( tokens[next], "ahead" ) )
			{
				k.m_lookMode = 2;
				next += 1;
			}
			else if( count > next + 1 )
			{
				k.m_lookX = (Real)atof( tokens[next].str() );
				k.m_lookY = (Real)atof( tokens[next + 1].str() );
				next += 2;
			}
			if( count > next )
				k.m_lookH = (Real)atof( tokens[next++].str() );
			if( count > next )
				k.m_fov = (Real)atof( tokens[next++].str() );
			const Bool eyeOk = k.m_eyeFollow ? k.m_eyeTag.isNotEmpty() : TRUE;
			const Bool lookOk = k.m_lookMode == 1 ? k.m_lookTag.isNotEmpty() : ( k.m_lookMode == 2 ? k.m_eyeFollow : TRUE );
			if( next != count || count < 5 || !eyeOk || !lookOk )
			{
				event( "ERROR line %d: bad CameraEye (ahead needs an @tag eye)", line );
				return;
			}
		}
		size_t at = 0;
		while( at < m_eyeKeys.size() && m_eyeKeys[at].m_frame <= k.m_frame )
			++at;
		m_eyeKeys.insert( m_eyeKeys.begin() + at, k );
	}
	else if( is( key, "Screenshot" ) && count > 1 )
	{
		m_shotFrames.push_back( secondsToFrame( tokens[1] ) );
	}
	else if( is( key, "Screenshots" ) && count > 3 )
	{
		Int from = secondsToFrame( tokens[1] );
		Int to = secondsToFrame( tokens[2] );
		Int every = REAL_TO_INT_FLOOR( (Real)atof( tokens[3].str() ) * LOGICFRAMES_PER_MSEC_REAL + 0.5f );
		if( every < 1 )
			every = 1;
		for( Int frame = from; frame <= to; frame += every )
			m_shotFrames.push_back( frame );
	}
	else if( is( key, "Log" ) && count > 1 )
	{
		LogRequest request;
		request.m_what = tokens[1];
		request.m_everyFrames = count > 2 ? REAL_TO_INT_FLOOR( (Real)atof( tokens[2].str() ) * LOGICFRAMES_PER_MSEC_REAL + 0.5f ) : LOGICFRAMES_PER_SECOND;
		if( request.m_everyFrames < 1 )
			request.m_everyFrames = 1;
		m_logRequests.push_back( request );
	}
	else
	{
		event( "WARNING line %d: unknown or incomplete key '%s'", line, key.str() );
	}
}

//-------------------------------------------------------------------------------------------------
// Client, once the intro is done: set up the skirmish like the skirmish menu does and start it.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::startGame()
{
	AsciiString eventsPath;
	eventsPath.format( "%s\\events.txt", m_outputDir.str() );
	m_events = fopen( eventsPath.str(), "w" );
	event( "scenario %s", m_scenarioFile.str() );
	// Hypersonic @feature (D-145) GameData as loaded (the harness itself lifts the zoom limit, so check the INI value)
	event( "GameData MaxCameraHeight %.1f CameraHeight %.1f", TheGlobalData->m_maxCameraHeight, TheGlobalData->m_cameraHeight );

	if( !parseFile( m_scenarioFile ) )
	{
		TheGameEngine->setQuitting( TRUE );
		return;
	}

	// stable sort by frame (setup commands have frame -1): insertion sort keeps the file order of equal frames
	for( size_t sorted = 1; sorted < m_commands.size(); ++sorted )
	{
		Command cmd = m_commands[sorted];
		size_t at = sorted;
		while( at > 0 && m_commands[at - 1].m_frame > cmd.m_frame )
		{
			m_commands[at] = m_commands[at - 1];
			--at;
		}
		m_commands[at] = cmd;
	}
	std::sort( m_shotFrames.begin(), m_shotFrames.end() );
	m_shotFrames.erase( std::unique( m_shotFrames.begin(), m_shotFrames.end() ), m_shotFrames.end() );

	AsciiString logPath;
	logPath.format( "%s\\log.csv", m_outputDir.str() );
	m_log = fopen( logPath.str(), "w" );
	m_logStartMs = timeGetTime();
	if( m_log )
		fprintf( m_log, "frame,time,label,id,template,owner,x,y,z,height,hp,maxhp,speed,vspeed,status,model,wallms\n" );

	// Hypersonic @feature (D-244): retain the mission's own players/scripts instead of creating a skirmish.
	if( isCampaign() )
	{
		TheCampaignManager->setCampaign( m_campaignName );
		m_mapName = TheCampaignManager->getCurrentMap();
		if( m_mapName.isEmpty() )
		{
			event( "ERROR unknown or empty campaign %s", m_campaignName.str() );
			TheGameEngine->setQuitting( TRUE );
			return;
		}
		TheCampaignManager->setGameDifficulty( m_campaignDifficulty );
		TheScriptEngine->setGlobalDifficulty( m_campaignDifficulty );
		TheWritableGlobalData->m_breakTheMovie = FALSE;
		TheWritableGlobalData->m_initialFile = m_mapName;
		TheWritableGlobalData->m_mapName = m_mapName;
		InitRandom( m_seed );
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument( GAME_SINGLE_PLAYER );
		msg->appendIntegerArgument( m_campaignDifficulty );
		msg->appendIntegerArgument( TheCampaignManager->getRankPoints() );
		event( "starting campaign %s map %s difficulty %d (mission scripts preserved)",
			m_campaignName.str(), m_mapName.str(), (Int)m_campaignDifficulty );
		return;
	}

	const Int iranTemplate = ThePlayerTemplateStore->getTemplateNumByName( "FactionIran" );
	const Int enemyTemplate = ThePlayerTemplateStore->getTemplateNumByName( m_enemyFaction );
	if( iranTemplate < 0 || enemyTemplate < 0 )
	{
		event( "ERROR unknown PlayerTemplate (FactionIran or %s)", m_enemyFaction.str() );
		TheGameEngine->setQuitting( TRUE );
		return;
	}

	// the intro left this set (Intro::doPostIntro); the main menu would clear it. While set nothing is drawn.
	TheWritableGlobalData->m_breakTheMovie = FALSE;

	// no shell, and quit to the desktop when the game ends (GameLogicDispatch, Shell)
	TheWritableGlobalData->m_initialFile = m_mapName;

	if( TheSkirmishGameInfo == nullptr )
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP( 0 );
	TheSkirmishGameInfo->enterGame();

	if( m_aiBattle )
	{
		// Hypersonic @feature (D-071 AI battle harness) slot 0: the local human as an observer (no start position,
		// no objects, not counted by the victory check); slot 1: Iran AI; slot 2: enemy AI, like the skirmish menu
		GameSlot observer;
		observer.setState( SLOT_PLAYER, UnicodeString( L"Observer" ) );
		observer.setName( UnicodeString( L"Observer" ) );
		observer.setColor( 2 );
		observer.setPlayerTemplate( PLAYERTEMPLATE_OBSERVER );
		observer.setStartPos( -1 );
		observer.setTeamNumber( -1 );
		TheSkirmishGameInfo->setSlot( 0, observer );

		if( isTeamBattle() )
		{
			// Hypersonic @feature (AI team battle) slots 1..n: the Player lines, allied by team (GameSlot team 0-3)
			for( size_t i = 0; i < m_teamPlayers.size(); ++i )
			{
				const BattlePlayer &bp = m_teamPlayers[i];
				const Int tmpl = ThePlayerTemplateStore->getTemplateNumByName( bp.m_faction );
				if( tmpl < 0 )
				{
					event( "ERROR unknown PlayerTemplate %s (Player line %d)", bp.m_faction.str(), (Int)i + 1 );
					TheGameEngine->setQuitting( TRUE );
					return;
				}
				GameSlot ai;
				ai.setState( (SlotState)bp.m_slotState );
				ai.setColor( (Int)i );
				ai.setPlayerTemplate( tmpl );
				ai.setStartPos( bp.m_start );
				ai.setTeamNumber( bp.m_team - 1 );
				TheSkirmishGameInfo->setSlot( (Int)i + 1, ai );
			}
		}
		else
		{
		GameSlot iranAI;
		iranAI.setState( (SlotState)m_iranSlotState );
		iranAI.setColor( m_iranColor );
		iranAI.setPlayerTemplate( iranTemplate );
		iranAI.setStartPos( 0 );
		iranAI.setTeamNumber( -1 );
		TheSkirmishGameInfo->setSlot( 1, iranAI );

		GameSlot enemyAI;
		enemyAI.setState( (SlotState)m_enemySlotState );
		enemyAI.setColor( m_enemyColor );
		enemyAI.setPlayerTemplate( enemyTemplate );
		enemyAI.setStartPos( 1 );
		enemyAI.setTeamNumber( -1 );
		TheSkirmishGameInfo->setSlot( 2, enemyAI );
		}

		if( m_lowDetail != 0 )
			applyLowDetail();
		// the fps-based dynamic detail also scales slow deaths and debris (logic): keep it at the full level
		TheWritableGlobalData->m_enableDynamicLOD = FALSE;
	}
	else
	{
		GameSlot iran;
		iran.setState( SLOT_PLAYER, UnicodeString( L"Iran" ) );
		iran.setName( UnicodeString( L"Iran" ) );
		iran.setColor( m_iranColor );
		iran.setPlayerTemplate( iranTemplate );
		iran.setStartPos( 0 );
		iran.setTeamNumber( -1 );
		TheSkirmishGameInfo->setSlot( 0, iran );

		GameSlot enemy;
		enemy.setState( SLOT_EASY_AI );
		enemy.setColor( m_enemyColor );
		enemy.setPlayerTemplate( enemyTemplate );
		enemy.setStartPos( 1 );
		enemy.setTeamNumber( -1 );
		TheSkirmishGameInfo->setSlot( 1, enemy );

		if( m_lowDetail == 1 )
			applyLowDetail();
	}

	TheSkirmishGameInfo->setSeed( m_seed );
	TheSkirmishGameInfo->setStartingCash( TheMultiplayerSettings->getDefaultStartingMoney() );
	TheSkirmishGameInfo->setSuperweaponRestriction( 0 );
	TheSkirmishGameInfo->setMap( m_mapName );
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( m_mapName ) : nullptr;
	if( md == nullptr )
	{
		event( "%s map %s is not in the map cache", m_aiBattle ? "ERROR" : "WARNING", m_mapName.str() );
		if( m_aiBattle )	// Hypersonic @feature (D-071 AI battle harness) no skirmish sides, nothing to measure
		{
			TheGameEngine->setQuitting( TRUE );
			return;
		}
	}
	TheSkirmishGameInfo->setMapCRC( md ? md->m_CRC : 0 );
	TheSkirmishGameInfo->setMapSize( md ? md->m_filesize : 0 );

	TheWritableGlobalData->m_mapName = m_mapName;
	TheSkirmishGameInfo->startGame( 0 );
	InitRandom( m_seed );

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( LOGICFRAMES_PER_SECOND );
	if( m_aiBattle && isTeamBattle() )
	{
		event( "starting AI team battle on %s: %d AI players, %d frames", m_mapName.str(), (Int)m_teamPlayers.size(), m_durationFrames );
		for( size_t i = 0; i < m_teamPlayers.size(); ++i )
			event( "  p%d %s %s team %d start %d", (Int)i + 1, m_teamPlayers[i].m_faction.str(), aiSlotName( m_teamPlayers[i].m_slotState ),
				m_teamPlayers[i].m_team, m_teamPlayers[i].m_start + 1 );
	}
	else if( m_aiBattle )
		event( "starting AI battle on %s: Iran AI (%s) vs %s AI (%s), %d frames", m_mapName.str(), aiSlotName( m_iranSlotState ),
			m_enemyFaction.str(), aiSlotName( m_enemySlotState ), m_durationFrames );
	else
		event( "starting %s, Iran vs %s, %d frames", m_mapName.str(), m_enemyFaction.str(), m_durationFrames );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-071 AI battle harness) LowDetail: the Options menu "Low" detail preset and less, client
// settings only (not saved to the options file). Trees stay as the preset has them.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::applyLowDetail()
{
	Bool applied = FALSE;
	if( TheGameLODManager )
	{
		// the "Low" preset with everything optional off, applied like the Options menu does (shadows re-allocated)
		StaticGameLODInfo &low = TheGameLODManager->m_staticGameLODInfo[STATIC_GAME_LOD_LOW];
		low.m_useShadowVolumes = FALSE;
		low.m_useShadowDecals = FALSE;
		low.m_useCloudMap = FALSE;
		low.m_useLightMap = FALSE;
		low.m_showSoftWaterEdge = FALSE;
		low.m_useTreeSway = FALSE;
		low.m_useHeatEffects = FALSE;
		applied = TheGameLODManager->setStaticLODLevel( STATIC_GAME_LOD_LOW );
	}
	if( !applied )
	{
		// static LOD disabled or already "Low": the same values directly
		TheWritableGlobalData->m_useShadowVolumes = FALSE;
		TheWritableGlobalData->m_useShadowDecals = FALSE;
		TheWritableGlobalData->m_useCloudMap = FALSE;
		TheWritableGlobalData->m_useLightMap = FALSE;
		TheWritableGlobalData->m_useTreeSway = FALSE;
		TheWritableGlobalData->m_useHeatEffects = FALSE;
	}
	TheWritableGlobalData->m_enableBehindBuildingMarkers = FALSE;
	TheWritableGlobalData->m_useFX = FALSE;	// no particles (like -noFX)
	event( "low detail: static LOD %s, texture reduction %d, shadows, particles and heat effects off",
		TheGameLODManager ? TheGameLODManager->getStaticGameLODLevelName( TheGameLODManager->getStaticLODLevel() ) : "?",
		TheGlobalData->m_textureReductionFactor );
}

//-------------------------------------------------------------------------------------------------
// Logic, end of GameLogic::startNewGame (before the first logic update): take the enemy's AI and every script
// away, reveal the map, run the setup commands.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::onGameStarted()
{
	if( m_aiBattle )
	{
		// Hypersonic @feature (D-071 AI battle harness) keep the AIs, their scripts, start buildings and the shroud
		const Player *local = ThePlayerList->getLocalPlayer();
		if( isTeamBattle() )
		{
			// Hypersonic @feature (AI team battle) "Iran" = the first Iran player, "Enemy" = the first player of another team
			size_t i;
			m_iran = m_enemy = nullptr;
			Int iranTeam = 0;
			for( i = 0; i < m_teamPlayers.size(); ++i )
			{
				BattlePlayer &bp = m_teamPlayers[i];
				bp.m_player = ThePlayerList->getPlayerFromSlotIndex( (Int)i + 1 );
				AsciiString name;
				name.format( "p%d", (Int)i + 1 );
				m_sides.push_back( bp.m_player );
				m_sideNames.push_back( name );
				if( m_iran == nullptr && bp.m_player && bp.m_faction.compareNoCase( "FactionIran" ) == 0 )
				{
					m_iran = bp.m_player;
					iranTeam = bp.m_team;
				}
				event( "game started: p%d = player %d (%s, %s), team %d", (Int)i + 1, bp.m_player ? bp.m_player->getPlayerIndex() : -1,
					bp.m_player && bp.m_player->getPlayerTemplate() ? bp.m_player->getPlayerTemplate()->getName().str() : "?",
					bp.m_player && bp.m_player->getPlayerType() == PLAYER_COMPUTER ? "AI" : "NOT AI", bp.m_team );
				if( bp.m_player == nullptr )
					event( "ERROR AI team battle: p%d missing", (Int)i + 1 );
			}
			for( i = 0; i < m_teamPlayers.size() && m_enemy == nullptr; ++i )
				if( m_teamPlayers[i].m_team != iranTeam )
					m_enemy = m_teamPlayers[i].m_player;
			// allies check (the game sets the relationships from the slot teams)
			for( i = 0; i < m_teamPlayers.size(); ++i )
				for( size_t j = 0; j < m_teamPlayers.size(); ++j )
				{
					Player *a = m_teamPlayers[i].m_player;
					Player *b = m_teamPlayers[j].m_player;
					if( i == j || a == nullptr || b == nullptr )
						continue;
					const Bool sameTeam = m_teamPlayers[i].m_team == m_teamPlayers[j].m_team;
					const Relationship r = a->getRelationship( b->getDefaultTeam() );
					if( sameTeam != ( r == ALLIES ) )
						event( "WARNING p%d -> p%d relationship %d but teams %d / %d", (Int)i + 1, (Int)j + 1, (Int)r,
							m_teamPlayers[i].m_team, m_teamPlayers[j].m_team );
				}
			event( "local player %d (observer: %s)", local ? local->getPlayerIndex() : -1, local && local->isPlayerObserver() ? "yes" : "NO" );
		}
		else
		{
		m_iran = ThePlayerList->getPlayerFromSlotIndex( 1 );
		m_enemy = ThePlayerList->getPlayerFromSlotIndex( 2 );
		m_sides.push_back( m_iran );
		m_sides.push_back( m_enemy );
		m_sideNames.push_back( "iran" );
		m_sideNames.push_back( "enemy" );
		event( "game started: AI battle, Iran player %d (%s, %s), enemy player %d (%s, %s), local player %d (observer: %s)",
			m_iran ? m_iran->getPlayerIndex() : -1,
			m_iran && m_iran->getPlayerTemplate() ? m_iran->getPlayerTemplate()->getName().str() : "?",
			m_iran && m_iran->getPlayerType() == PLAYER_COMPUTER ? "AI" : "NOT AI",
			m_enemy ? m_enemy->getPlayerIndex() : -1,
			m_enemy && m_enemy->getPlayerTemplate() ? m_enemy->getPlayerTemplate()->getName().str() : "?",
			m_enemy && m_enemy->getPlayerType() == PLAYER_COMPUTER ? "AI" : "NOT AI",
			local ? local->getPlayerIndex() : -1, local && local->isPlayerObserver() ? "yes" : "NO" );
		if( m_iran == nullptr || m_enemy == nullptr )
			event( "ERROR AI battle: Iran or enemy player missing" );
		}
		m_started = TRUE;
		m_built.resize( m_sides.size() );
		m_upgradesDone.resize( m_sides.size() );
		m_busySamples.resize( m_sides.size() );
		m_allSamples.resize( m_sides.size() );

		AsciiString battlePath;
		battlePath.format( "%s\\battle.csv", m_outputDir.str() );
		m_battle = fopen( battlePath.str(), "w" );
		m_battleStartMs = timeGetTime();
		if( m_battle && isTeamBattle() )
		{
			// Hypersonic @feature (AI team battle) the same seven columns per player, prefixed p1_..p7_
			fprintf( m_battle, "time_min,wall_s" );
			for( size_t i = 0; i < m_sideNames.size(); ++i )
			{
				const char *n = m_sideNames[i].str();
				fprintf( m_battle, ",%s_money,%s_units,%s_buildings,%s_units_killed,%s_units_lost,%s_buildings_killed,%s_buildings_lost",
					n, n, n, n, n, n, n );
			}
			fprintf( m_battle, "\n" );
			fflush( m_battle );
		}
		else if( m_battle )
		{
			fprintf( m_battle, "time_min,wall_s,"
				"iran_money,iran_units,iran_buildings,iran_units_killed,iran_units_lost,iran_buildings_killed,iran_buildings_lost,"
				"enemy_money,enemy_units,enemy_buildings,enemy_units_killed,enemy_units_lost,enemy_buildings_killed,enemy_buildings_lost\n" );
			fflush( m_battle );
		}
		// Hypersonic @feature (D-112) production log
		AsciiString productionPath;
		productionPath.format( "%s\\production.csv", m_outputDir.str() );
		m_production = fopen( productionPath.str(), "w" );
		if( m_production )
		{
			fprintf( m_production, "time_min,side,kind,name,value,samples\n" );
			fflush( m_production );
		}

		while( m_nextCommand < m_commands.size() && m_commands[m_nextCommand].m_frame < 0 )
			runCommand( m_commands[m_nextCommand++] );
		return;
	}

	m_iran = ThePlayerList->getLocalPlayer();
	m_enemy = nullptr;
	for( Int index = 0; index < ThePlayerList->getPlayerCount(); ++index )
	{
		Player *player = ThePlayerList->getNthPlayer( index );
		if( player == nullptr || player == m_iran || player == ThePlayerList->getNeutralPlayer() || player->getPlayerTemplate() == nullptr )
			continue;
		if( m_enemy == nullptr || player->getPlayerTemplate()->getName().compareNoCase( m_enemyFaction ) == 0 )
			m_enemy = player;
	}
	if( isCampaign() )
	{
		// D-244: mission scripts own the scene, camera, shroud, input and construction.
		m_started = TRUE;
		event( "campaign started: local player %d, enemy player %d, difficulty %d; scripts/AI retained",
			m_iran ? m_iran->getPlayerIndex() : -1, m_enemy ? m_enemy->getPlayerIndex() : -1,
			(Int)TheCampaignManager->getGameDifficulty() );
		while( m_nextCommand < m_commands.size() && m_commands[m_nextCommand].m_frame < 0 )
			runCommand( m_commands[m_nextCommand++] );
		return;
	}
	if( m_enemy )
		m_enemy->setPlayerType( PLAYER_HUMAN, FALSE );	// no AI: it only does what the scenario says

	for( Int side = 0; side < TheSidesList->getNumSides(); ++side )
	{
		ScriptList *scripts = TheSidesList->getSideInfo( side )->getScriptList();
		deleteInstance( scripts );
		TheSidesList->getSideInfo( side )->setScriptList( newInstance( ScriptList ) );
	}

	if( m_iran )
		ThePartitionManager->revealMapForPlayerPermanently( m_iran->getPlayerIndex() );

	m_started = TRUE;
	event( "game started: Iran player %d, enemy player %d", m_iran ? m_iran->getPlayerIndex() : -1, m_enemy ? m_enemy->getPlayerIndex() : -1 );

	while( m_nextCommand < m_commands.size() && m_commands[m_nextCommand].m_frame < 0 )
		runCommand( m_commands[m_nextCommand++] );
}

//-------------------------------------------------------------------------------------------------
Player *HypersonicScenario::findPlayer( const AsciiString &name ) const
{
	if( is( name, "Iran" ) )
		return m_iran;
	if( is( name, "Enemy" ) )
		return m_enemy;
	if( is( name, "Neutral" ) )
		return ThePlayerList->getNeutralPlayer();
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
Object *HypersonicScenario::findTagged( const AsciiString &tag ) const
{
	AsciiString name = tag.startsWith( "@" ) ? AsciiString( tag.str() + 1 ) : tag;
	std::map<AsciiString, ObjectID>::const_iterator it = m_tags.find( name );
	if( it == m_tags.end() )
		return nullptr;
	return TheGameLogic->findObjectByID( it->second );
}

//-------------------------------------------------------------------------------------------------
Bool HypersonicScenario::readPos( const Command &cmd, size_t index, Coord3D *pos ) const
{
	if( cmd.m_args.size() <= index + 1 )
		return FALSE;
	pos->x = (Real)atof( cmd.m_args[index].str() );
	pos->y = (Real)atof( cmd.m_args[index + 1].str() );
	pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Like GameLogic's placeObjectAtPosition for the skirmish start buildings: a finished object.
//-------------------------------------------------------------------------------------------------
Object *HypersonicScenario::placeObject( const AsciiString &tag, const AsciiString &owner, const AsciiString &templateName,
	const Coord3D &pos, Real angleDegrees )
{
	Player *player = findPlayer( owner );
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( templateName );
	if( player == nullptr || tmpl == nullptr )
	{
		event( "ERROR Object %s: unknown owner '%s' or template '%s'", tag.str(), owner.str(), templateName.str() );
		return nullptr;
	}

	Team *team = player->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if( obj == nullptr )
		return nullptr;
	obj->setOrientation( angleDegrees * PI / 180.0f );
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	if( team )
		team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
	{
		player->onStructureCreated( nullptr, obj );
		// Hypersonic @bugfix (D-141) the finished object's power was already counted when it joined the team;
		// onStructureConstructionComplete counts it again, so take it out once.
		obj->friend_adjustPowerForPlayer( FALSE );
		player->onStructureConstructionComplete( nullptr, obj, FALSE );
	}
	else
	{
		player->onUnitCreated( nullptr, obj );
	}

	m_tags[tag] = obj->getID();
	event( "placed %s = %s (id %d) for %s at %.0f %.0f", tag.str(), templateName.str(), (Int)obj->getID(), owner.str(), pos.x, pos.y );
	return obj;
}

//-------------------------------------------------------------------------------------------------
// Commands (setup keys or "At = <seconds> <command> ..."):
//   Object <tag> <owner> <template> <x> <y> [angle]      Money <player> <amount>
//   Science <player> <science>                          Upgrade <player|tag> <upgrade>
//   Magazine <tag> <munition template> <count>           Kill <tag>
//   Buy <tag> <unit template> <count>   (queue purchases at a factory like the player's button, D-080)
//   Power <tag> <special power> [<x> <y> | @<target>]    Move <tag> <x> <y>
//   Attack <tag> <target tag>                           AttackGround <tag> <x> <y>
//   Enter <tag> <container tag>   (garrison / board like a player's right-click)
//   Button <tag> <command button> <x> <y>   (a unit's button aimed at a position, like the player's click, D-092)
//   Stop <tag>
//   Health <tag> <percent of max health>   Energy <player>   (power, money and score to events.txt, D-141)
//   Ready <tag>   (every special power of the object ready now, D-163)
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::runCommand( const Command &cmd )
{
	const std::vector<AsciiString> &a = cmd.m_args;
	const AsciiString &verb = a[0];
	const size_t n = a.size();
	Coord3D pos;

	if( is( verb, "ChallengeFast" ) && n >= 2 )
	{
		// Only explicit scenario setup can enable this; normal campaigns never enter the harness.
		if( !isCampaign() || m_campaignName.compareNoCase( "IRAN_CHALLENGE" ) != 0 || cmd.m_frame >= 0 )
		{
			event( "ERROR line %d: ChallengeFast requires IRAN_CHALLENGE setup", cmd.m_line );
			return;
		}
		ScriptAction *action = newInstance( ScriptAction )( ScriptAction::SET_FLAG );
		action->setNextAction( nullptr );
		action->getParameter( 0 )->friend_setString( "CH_DebugFast" );
		action->getParameter( 0 )->friend_setInt( 0 );
		action->getParameter( 1 )->friend_setInt( isYes( a[1] ) ? 1 : 0 );
		TheScriptEngine->friend_executeAction( action );
		deleteInstance( action );
		event( "CHALLENGE DEBUG CH_DebugFast=%d (harness setup only)", isYes( a[1] ) ? 1 : 0 );
	}
	else if( is( verb, "Object" ) && n >= 6 )
	{
		readPos( cmd, 4, &pos );
		placeObject( a[1], a[2], a[3], pos, n > 6 ? (Real)atof( a[6].str() ) : 0.0f );
	}
	else if( is( verb, "Tag" ) )
	{
		if( n != 3 && n != 4 )
		{
			event( "ERROR line %d: Tag needs <tag> <template> [<player>]", cmd.m_line );
			return;
		}
		Player *player = n == 4 ? findPlayer( a[3] ) : nullptr;
		if( n == 4 && player == nullptr )
		{
			event( "ERROR line %d: Tag %s: unknown player %s", cmd.m_line, a[1].str(), a[3].str() );
			return;
		}
		Object *newest = nullptr;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || obj->getTemplate()->getName() != a[2] ||
				( player && obj->getControllingPlayer() != player ) )
				continue;
			// IDs are allocated monotonically; list order is not a creation-order contract.
			if( newest == nullptr || obj->getID() > newest->getID() )
				newest = obj;
		}
		if( newest == nullptr )
		{
			event( "ERROR line %d: Tag %s %s %s: no living match (binding unchanged)", cmd.m_line,
				a[1].str(), a[2].str(), n == 4 ? a[3].str() : "any player" );
			return;
		}
		m_tags[a[1]] = newest->getID();
		event( "tag %s = %s (id %d, producer %d) for %s", a[1].str(), a[2].str(),
			(Int)newest->getID(), (Int)newest->getProducerID(), sideLabel( newest->getControllingPlayer() ) );
	}
	else if( is( verb, "Money" ) && n >= 3 )
	{
		Player *player = findPlayer( a[1] );
		if( player == nullptr )
		{
			event( "ERROR line %d: unknown player %s", cmd.m_line, a[1].str() );
			return;
		}
		Money *money = player->getMoney();
		money->withdraw( money->countMoney(), FALSE );
		money->deposit( (UnsignedInt)atoi( a[2].str() ), FALSE, FALSE );
		event( "money %s = %u", a[1].str(), money->countMoney() );
	}
	else if( is( verb, "Science" ) && n >= 3 )
	{
		Player *player = findPlayer( a[1] );
		ScienceType science = TheScienceStore->getScienceFromInternalName( a[2] );
		if( player == nullptr || science == SCIENCE_INVALID )
		{
			event( "ERROR line %d: Science %s %s: unknown player or science", cmd.m_line, a[1].str(), a[2].str() );
			return;
		}
		Bool ok = player->grantScience( science );
		event( "science %s %s: %s", a[1].str(), a[2].str(), ok ? "granted" : "not granted" );
	}
	else if( is( verb, "Upgrade" ) && n >= 3 )
	{
		const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( a[2] );
		if( upgrade == nullptr )
		{
			event( "ERROR line %d: unknown upgrade %s", cmd.m_line, a[2].str() );
			return;
		}
		Player *player = findPlayer( a[1] );
		Object *obj = player ? nullptr : findTagged( a[1] );
		if( player && upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER )
			player->addUpgrade( upgrade, UPGRADE_STATUS_COMPLETE );
		else if( player )
		{
			for( Object *each = TheGameLogic->getFirstObject(); each; each = each->getNextObject() )
				if( each->getControllingPlayer() == player )
					each->giveUpgrade( upgrade );
		}
		else if( obj )
			obj->giveUpgrade( upgrade );
		else
		{
			event( "ERROR line %d: Upgrade: unknown player or tag %s", cmd.m_line, a[1].str() );
			return;
		}
		event( "upgrade %s %s", a[1].str(), a[2].str() );
	}
	else if( is( verb, "Magazine" ) && n >= 4 )
	{
		Object *obj = findTagged( a[1] );
		const ThingTemplate *munition = TheThingFactory->findTemplate( a[2] );
		MunitionMagazineInterface *magazine = nullptr;
		if( obj )
		{
			for( BehaviorModule **m = obj->getBehaviorModules(); *m && magazine == nullptr; ++m )
			{
				MunitionMagazineInterface *mi = (*m)->getMunitionMagazineInterface();
				if( mi && munition && mi->storesMunition( munition ) )
					magazine = mi;
			}
		}
		if( magazine == nullptr )
		{
			event( "ERROR line %d: Magazine %s %s: no such object or it does not store that munition", cmd.m_line, a[1].str(), a[2].str() );
			return;
		}
		Int count = atoi( a[3].str() );
		for( Int i = 0; i < count; ++i )
			magazine->addMunition( munition );
		event( "magazine %s + %d %s (stored %d, units %d/%d)", a[1].str(), count, a[2].str(), magazine->getStoredCount( nullptr ),
			magazine->getStoredSize(), magazine->getCapacity() );
	}
	else if( is( verb, "Buy" ) && n >= 4 )
	{
		// Hypersonic @feature (D-080) a purchase as the build button makes it: refused when canMakeUnit refuses
		Object *obj = findTagged( a[1] );
		const ThingTemplate *unit = TheThingFactory->findTemplate( a[2] );
		ProductionUpdateInterface *pu = obj ? obj->getProductionUpdateInterface() : nullptr;
		if( pu == nullptr || unit == nullptr )
		{
			event( "ERROR line %d: Buy %s %s: no such factory or template", cmd.m_line, a[1].str(), a[2].str() );
			return;
		}
		Int count = atoi( a[3].str() );
		for( Int i = 0; i < count; ++i )
		{
			CanMakeType why = TheBuildAssistant->canMakeUnit( obj, unit );
			Bool queued = why == CANMAKE_OK && pu->queueCreateUnit( unit, pu->requestUniqueUnitID() );
			event( "buy %s %s #%d: %s (canMake %d, queue %d)", a[1].str(), a[2].str(), i + 1,
				queued ? "queued" : "REFUSED", (Int)why, pu->getProductionCount() );
		}
	}
	// Hypersonic @feature upgrade badge (Generals Hypersonic D-099): select an object (client-side), so a
	// HideUI = No scenario shows its command bar
	else if( is( verb, "Select" ) && n >= 2 )
	{
		Object *obj = findTagged( a[1] );
		Drawable *draw = obj ? obj->getDrawable() : nullptr;
		if( draw && TheInGameUI )
		{
			TheInGameUI->deselectAllDrawables();
			TheInGameUI->selectDrawable( draw );
			// selectDrawable updates the client only. Real clicks also send this logic selection.
			GameMessage* selection = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
			selection->appendBooleanArgument(TRUE);
			selection->appendObjectIDArgument(obj->getID());
		}
		event( "select %s: %s", a[1].str(), draw ? "done (client + logic message)" : "no such object" );
	}
	// Hypersonic @feature (voice showcase): play an object's voice event, as the client does for a player's order
	// (harness Move/Attack orders come from script and stay silent)
	else if( is( verb, "Voice" ) && n >= 3 )
	{
		Object *obj = findTagged( a[1] );
		const AudioEventRTS *voice = nullptr;
		if( obj )
		{
			const ThingTemplate *tmpl = obj->getTemplate();
			if( is( a[2], "Select" ) )
				voice = tmpl->getVoiceSelect();
			else if( is( a[2], "Move" ) )
				voice = tmpl->getVoiceMove();
			else if( is( a[2], "Attack" ) )
				voice = tmpl->getVoiceAttack();
			else
				voice = tmpl->getPerUnitSound( a[2] );
		}
		if( voice && TheAudio )
		{
			AudioEventRTS sound = *voice;
			sound.setObjectID( obj->getID() );
			TheAudio->addAudioEvent( &sound );
		}
		event( "voice %s %s: %s", a[1].str(), a[2].str(), !obj ? "no such object" : voice ? voice->getEventName().str() : "no such voice" );
	}
	else if( is( verb, "Protect" ) && n >= 3 )
	{
		Object *obj = findTagged( a[1] );
		BodyModuleInterface *body = obj ? obj->getBodyModule() : nullptr;
		if( body == nullptr )
		{
			event( "ERROR line %d: Protect %s: no such object", cmd.m_line, a[1].str() );
			return;
		}
		body->setIndestructible( isYes( a[2] ) );
		event( "protect %s = %d (harness only)", a[1].str(), isYes( a[2] ) ? 1 : 0 );
	}
	else if( is( verb, "Health" ) && n >= 3 )
	{
		// Hypersonic @feature (D-141) set an object's health to a percentage of its max health (no damage event)
		Object *obj = findTagged( a[1] );
		BodyModuleInterface *body = obj ? obj->getBodyModule() : nullptr;
		if( body == nullptr )
		{
			event( "ERROR line %d: Health %s: no such object", cmd.m_line, a[1].str() );
			return;
		}
		Real percent = (Real)atof( a[2].str() );
		body->internalChangeHealth( percent / 100.0f * body->getMaxHealth() - body->getHealth() );
		event( "health %s = %.0f / %.0f", a[1].str(), body->getHealth(), body->getMaxHealth() );
	}
	// Hypersonic @feature camera zoom-out option (Generals Hypersonic D-223): the Options menu, for a look at its layout
	else if( is( verb, "Options" ) )
	{
		WindowLayout *optLayout = TheShell ? TheShell->getOptionsLayout( TRUE ) : nullptr;
		if( optLayout )
		{
			optLayout->runInit();
			optLayout->hide( FALSE );
			optLayout->bringForward();
		}
		// "Options Open": also drop down the camera zoom-out list (as a click on it does)
		GameWindow *zoomCombo = TheWindowManager ? TheWindowManager->winGetWindowFromId( nullptr,
			TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ComboBoxCameraZoomOut" ) ) : nullptr;
		if( n >= 2 && is( a[1], "Open" ) && zoomCombo )
			TheWindowManager->winSendInputMsg( zoomCombo, GWM_LEFT_UP, 0, 0 );
		event( "options menu: %s, camera zoom-out box %s", optLayout ? "open" : "no layout", zoomCombo ? "found" : "MISSING" );
	}
	// Hypersonic @feature (D-223): a zoom-out choice as the Options menu applies it (not saved), then fully zoomed out
	else if( is( verb, "CameraZoomOut" ) && n >= 2 )
	{
		const Real oldMax = TheGlobalData->m_maxCameraHeight;
		OptionPreferences::applyCameraZoomOut( OptionPreferences::getCameraZoomOutChoice(
			OptionPreferences::findCameraZoomOutIndex( (Real)atof( a[1].str() ) ) ) );
		if( TheTacticalView )
			TheTacticalView->setZoomToDefault();
		event( "camera zoom-out %s: MaxCameraHeight %.1f -> %.1f, height above ground %.1f", a[1].str(), oldMax,
			TheGlobalData->m_maxCameraHeight, TheTacticalView ? TheTacticalView->getHeightAboveGround() : 0.0f );
	}
	else if( is( verb, "Ready" ) && n >= 2 )
	{
		// Hypersonic @feature (D-163 test) make every special power of the object ready now (its "ready" EVA plays)
		Object *obj = findTagged( a[1] );
		if( obj == nullptr )
		{
			event( "ERROR line %d: Ready %s: no such object", cmd.m_line, a[1].str() );
			return;
		}
		Int count = 0;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
			if( sp ) { sp->setReadyFrame( TheGameLogic->getFrame() ); ++count; }
		}
		event( "ready %s: %d special power(s)", a[1].str(), count );
	}
	// Hypersonic @feature (D-244): non-mutating diagnostics of real mission objects/construction.
	else if( is( verb, "BaseStatus" ) && n >= 6 )
	{
		Player *player = findPlayer( a[1] );
		const Real x = (Real)atof( a[3].str() ), y = (Real)atof( a[4].str() );
		const Real radius = (Real)atof( a[5].str() );
		if( player == nullptr )
		{
			event( "ERROR line %d: BaseStatus unknown player %s", cmd.m_line, a[1].str() );
			return;
		}
		size_t watch = 0;
		while( watch < m_baseWatches.size() && ( m_baseWatches[watch].m_player != player || m_baseWatches[watch].m_label != a[2] ) )
			++watch;
		if( watch == m_baseWatches.size() )
		{
			BaseWatch area;
			area.m_player = player;
			area.m_owner = a[1]; area.m_label = a[2];
			area.m_x = x; area.m_y = y; area.m_radius = radius;
			m_baseWatches.push_back( area );
		}
		observeBaseConstruction();
		Int complete = 0, building = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( player == nullptr || obj->getControllingPlayer() != player || obj->isEffectivelyDead() ||
				!obj->isKindOf( KINDOF_STRUCTURE ) ) continue;
			const Coord3D *pos = obj->getPosition();
			const Real dx = pos->x - x, dy = pos->y - y;
			if( dx * dx + dy * dy > radius * radius ) continue;
			if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ) ++building;
			else ++complete;
		}
		AsciiString aliveName;
		aliveName.format( "CH_%s_Alive", a[2].str() );
		const TFlag *alive = TheScriptEngine->getFlagByName( aliveName );
		event( "BASE STATUS %s owner %s complete %d under_construction %d total %d radius %.0f construction_starts %d base_build_enabled %d alive %s",
			a[2].str(), a[1].str(), complete, building, complete + building, radius,
			m_baseWatches[watch].m_starts, (Int)player->getCanBuildBase(), alive == nullptr ? "-" : alive->value ? "1" : "0" );
	}
	// Hypersonic @feature (D-244 fix 1): test-only visibility after the scene.
	else if( is( verb, "RevealMap" ) && n >= 2 )
	{
		Player *player = findPlayer( a[1] );
		if( player ) ThePartitionManager->revealMapForPlayerPermanently( player->getPlayerIndex() );
		event( "RevealMap %s %s", a[1].str(), player ? "revealed (harness only)" : "ERROR unknown player" );
	}
	else if( is( verb, "PlayerStatus" ) && n >= 2 )
	{
		Player *player = findPlayer( a[1] );
		if( player == nullptr )
		{
			event( "ERROR line %d: PlayerStatus unknown player %s", cmd.m_line, a[1].str() );
			return;
		}
		std::map<AsciiString, Int> counts;
		Int buildings = 0, units = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getControllingPlayer() != player || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) ) continue;
			++counts[obj->getTemplate()->getName()];
			if( obj->isKindOf( KINDOF_STRUCTURE ) ) ++buildings; else ++units;
		}
		event( "PLAYER STATUS %s money %u buildings %d units %d", a[1].str(), player->getMoney()->countMoney(), buildings, units );
		for( std::map<AsciiString, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it )
			event( "PLAYER OBJECTS %s %s %d", a[1].str(), it->first.str(), it->second );
	}
	else if( is( verb, "Energy" ) && n >= 2 )
	{
		// Hypersonic @feature (D-141) the player's power, money and score counters at this moment
		Player *player = findPlayer( a[1] );
		if( player == nullptr )
		{
			event( "ERROR line %d: Energy: unknown player %s", cmd.m_line, a[1].str() );
			return;
		}
		ScoreKeeper *score = player->getScoreKeeper();
		event( "energy %s: production %d consumption %d money %u buildings_built %d money_spent %d", a[1].str(),
			player->getEnergy()->getProduction(), player->getEnergy()->getConsumption(), player->getMoney()->countMoney(),
			score->getTotalBuildingsBuilt(), score->getTotalMoneySpent() );
	}
	else if( is( verb, "KillBase" ) && n >= 6 )
	{
		Player *player = findPlayer( a[1] );
		if( player == nullptr )
		{
			event( "ERROR line %d: KillBase unknown player %s", cmd.m_line, a[1].str() );
			return;
		}
		const Real x = (Real)atof( a[3].str() ), y = (Real)atof( a[4].str() );
		const Real radius = (Real)atof( a[5].str() );
		Int killed = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; )
		{
			Object *next = obj->getNextObject();
			const Real dx = obj->getPosition()->x - x, dy = obj->getPosition()->y - y;
			if( obj->getControllingPlayer() == player && !obj->isEffectivelyDead() && obj->isKindOf( KINDOF_STRUCTURE ) &&
				dx * dx + dy * dy <= radius * radius )
			{
				event( "CHALLENGE TEST kill_base %s id %d template %s", a[2].str(), (Int)obj->getID(), obj->getTemplate()->getName().str() );
				obj->kill();
				++killed;
			}
			obj = next;
		}
		event( "CHALLENGE TEST kill_base %s structures %d radius %.0f", a[2].str(), killed, radius );
	}
	else if( is( verb, "Kill" ) && n >= 2 )
	{
		Object *obj = findTagged( a[1] );
		if( obj )
			obj->kill();
		event( "kill %s: %s", a[1].str(), obj ? "done" : "no such object" );
	}
	// Hypersonic @feature (D-142 test) KillNear <template> <x> <y> [radius]: kill the nearest living object of that
	// template within radius (default 30, 2D) of a point, e.g. one TEL on its pad or one drone on its launcher
	else if( is( verb, "KillNear" ) && n >= 4 )
	{
		const Real x = (Real)atof( a[2].str() );
		const Real y = (Real)atof( a[3].str() );
		const Real radius = n >= 5 ? (Real)atof( a[4].str() ) : 30.0f;
		Object *best = nullptr;
		Real bestDist = radius * radius;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->isEffectivelyDead() || obj->getTemplate()->getName() != a[1] )
				continue;
			const Real dx = obj->getPosition()->x - x;
			const Real dy = obj->getPosition()->y - y;
			if( dx * dx + dy * dy <= bestDist )
			{
				best = obj;
				bestDist = dx * dx + dy * dy;
			}
		}
		if( best )
			best->kill();
		event( "killnear %s at %.0f %.0f: %s", a[1].str(), x, y, best ? "done" : "nothing there" );
	}
	else if( is( verb, "Power" ) && n >= 3 )
	{
		Object *obj = findTagged( a[1] );
		const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate( a[2] );
		SpecialPowerModuleInterface *module = ( obj && power ) ? obj->getSpecialPowerModule( power ) : nullptr;
		if( module == nullptr )
		{
			event( "ERROR line %d: Power %s %s: no such object or power module", cmd.m_line, a[1].str(), a[2].str() );
			return;
		}
		if( n >= 4 && a[3].startsWith( "@" ) )
		{
			Object *target = findTagged( a[3] );
			if( target )
				module->doSpecialPowerAtObject( target, COMMAND_FIRED_BY_SCRIPT );
			event( "power %s %s at %s%s", a[1].str(), a[2].str(), a[3].str(), target ? "" : " (no such target)" );
		}
		else if( readPos( cmd, 3, &pos ) )
		{
			module->doSpecialPowerAtLocation( &pos, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
			event( "power %s %s at %.0f %.0f", a[1].str(), a[2].str(), pos.x, pos.y );
		}
		else
		{
			module->doSpecialPower( COMMAND_FIRED_BY_SCRIPT );
			event( "power %s %s", a[1].str(), a[2].str() );
		}
	}
	else if( is( verb, "Button" ) )
	{
		Object *obj = n >= 2 ? findTagged( a[1] ) : nullptr;
		const CommandButton *button = n >= 3 ? TheControlBar->findCommandButton( a[2] ) : nullptr;
		const Bool needsTarget = button && BitIsSet(button->getOptions(), COMMAND_OPTION_NEED_TARGET);
		if (!obj || !obj->getDrawable() || !obj->isLocallyControlled() || !button ||
			(needsTarget ? n != 5 || !readPos(cmd, 3, &pos) : n != 3 && (n != 5 || !readPos(cmd, 3, &pos))))
		{
			event("ERROR line %d: Button: needs local <tag> <command button> [<x> <y>] (position required for targeted buttons)", cmd.m_line);
			return;
		}
		if (TheInGameUI->getSelectCount() != 1 || TheInGameUI->getFirstSelectedDrawable() != obj->getDrawable())
		{
			TheInGameUI->deselectAllDrawables();
			TheInGameUI->selectDrawable(obj->getDrawable());
			TheControlBar->evaluateContextUI();
		}
		// If already selected, use the displayed bar as-is: mode-swap refresh is part of the test.
		GameMessage* selection = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		selection->appendBooleanArgument(TRUE);
		selection->appendObjectIDArgument(obj->getID());
		GameWindow* control = nullptr;
		for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
		{
			AsciiString name;
			name.format("ControlBar.wnd:ButtonCommand%02d", i + 1);
			GameWindow* win = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
			if (win && !win->winIsHidden() && (const void*)GadgetButtonGetData(win) == (const void*)button)
				control = win;
		}
		if (!control || TheControlBar->getCommandAvailability(button, obj, control) != COMMAND_AVAILABLE)
		{
			event("button %s %s REFUSED (not shown/available in %s)", a[1].str(), a[2].str(), obj->getCommandSetString().str());
			return;
		}
		TheControlBar->processCommandUI(control, GBM_SELECTED);
		if (needsTarget)
		{
			CommandTranslator translator;
			GameMessage::Type result = translator.evaluateContextCommand(nullptr, &pos, CommandTranslator::DO_COMMAND);
			event("button %s %s at %.0f %.0f player message %d", a[1].str(), a[2].str(), pos.x, pos.y, (Int)result);
			TheInGameUI->setGUICommand(nullptr);
		}
		else
			event("button %s %s player click queued", a[1].str(), a[2].str());
	}
	else if( is( verb, "Move" ) || is( verb, "Attack" ) || is( verb, "AttackGround" ) || is( verb, "Stop" ) || is( verb, "Enter" ) )
	{
		Object *obj = n >= 2 ? findTagged( a[1] ) : nullptr;
		AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
		if( ai == nullptr )
		{
			event( "ERROR line %d: %s: no such object or it has no AI", cmd.m_line, verb.str() );
			return;
		}
		if( is( verb, "Move" ) && readPos( cmd, 2, &pos ) )
			ai->aiMoveToPosition( &pos, CMD_FROM_SCRIPT );
		else if( is( verb, "AttackGround" ) && readPos( cmd, 2, &pos ) )
			ai->aiAttackPosition( &pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
		else if( is( verb, "Attack" ) && n >= 3 && findTagged( a[2] ) )
			ai->aiAttackObject( findTagged( a[2] ), NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
		else if( is( verb, "Stop" ) )
			ai->aiIdle( CMD_FROM_SCRIPT );
		else if( is( verb, "Enter" ) && n >= 3 && findTagged( a[2] ) )
			ai->aiEnter( findTagged( a[2] ), CMD_FROM_SCRIPT );
		else
		{
			event( "ERROR line %d: %s: bad arguments", cmd.m_line, verb.str() );
			return;
		}
		event( "%s %s", verb.str(), a[1].str() );
	}
	else
	{
		event( "ERROR line %d: unknown or incomplete command '%s'", cmd.m_line, verb.str() );
	}
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::logObject( Object *obj, const AsciiString &label )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Coord3D *pos = obj->getPosition();
	Tracked &tracked = m_tracked[obj->getID()];
	Bool first = tracked.m_lastFrame < 0;
	Real speed = 0.0f;
	Real vspeed = 0.0f;
	if( !first && (Int)frame > tracked.m_lastFrame )
	{
		const Real seconds = (Real)( (Int)frame - tracked.m_lastFrame ) / LOGICFRAMES_PER_SECOND;
		const Real dx = pos->x - tracked.m_lastPos.x;
		const Real dy = pos->y - tracked.m_lastPos.y;
		const Real dz = pos->z - tracked.m_lastPos.z;
		speed = sqrtf( dx * dx + dy * dy + dz * dz ) / seconds;
		vspeed = dz / seconds;
	}
	tracked.m_label = label;
	tracked.m_lastPos = *pos;
	tracked.m_lastFrame = (Int)frame;

	const BodyModuleInterface *body = obj->getBodyModule();
	const Player *owner = obj->getControllingPlayer();
	const char *ownerName = owner == m_iran ? "Iran" : owner == m_enemy ? "Enemy" : "Neutral";
	AsciiString statusText = bitNames( obj->getStatusBits() );
	AsciiString modelText;
	if( obj->getDrawable() )
		modelText = bitNames( obj->getDrawable()->getModelConditionFlags() );
	AsciiString speedText, vspeedText;
	if( !first )
	{
		speedText.format( "%.1f", speed );
		vspeedText.format( "%.1f", vspeed );
	}

	fprintf( m_log, "%u,%.3f,%s,%d,%s,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%s,%s,%s,%u\n",
		frame, (Real)frame / LOGICFRAMES_PER_SECOND, label.str(), (Int)obj->getID(), obj->getTemplate()->getName().str(),
		ownerName, pos->x, pos->y, pos->z, obj->getHeightAboveTerrain(),
		body ? body->getHealth() : 0.0f, body ? body->getMaxHealth() : 0.0f,
		speedText.str(), vspeedText.str(),
		statusText.str(), modelText.str(), (UnsignedInt)( timeGetTime() - m_logStartMs ) );	// wallms (D-048): real time, frame pacing
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeLog()
{
	if( m_log == nullptr )
		return;
	const Int frame = (Int)TheGameLogic->getFrame();
	std::map<ObjectID, Bool> seen;

	for( size_t r = 0; r < m_logRequests.size(); ++r )
	{
		const LogRequest &request = m_logRequests[r];
		if( frame % request.m_everyFrames != 0 )
			continue;
		Object *tagged = findTagged( request.m_what );
		if( tagged )
		{
			seen[tagged->getID()] = TRUE;
			logObject( tagged, request.m_what );
			continue;
		}
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getTemplate()->getName() != request.m_what || seen.find( obj->getID() ) != seen.end() )
				continue;
			seen[obj->getID()] = TRUE;
			logObject( obj, request.m_what );
		}
	}

	// objects logged before that are dead or gone now
	for( std::map<ObjectID, Tracked>::iterator it = m_tracked.begin(); it != m_tracked.end(); ++it )
	{
		Tracked &tracked = it->second;
		if( tracked.m_reported )
			continue;
		Object *obj = TheGameLogic->findObjectByID( it->first );
		if( obj != nullptr && !obj->isEffectivelyDead() )
			continue;
		const char *what = obj ? "DIED" : "REMOVED";
		tracked.m_reported = TRUE;
		event( "%s id %d %s near %.0f %.0f %.0f (last logged frame %d)", tracked.m_label.str(), (Int)it->first, what,
			tracked.m_lastPos.x, tracked.m_lastPos.y, tracked.m_lastPos.z, tracked.m_lastFrame );
		fprintf( m_log, "%d,%.3f,%s,%d,,,%.1f,%.1f,%.1f,,,,,,%s,,\n", frame, (Real)frame / LOGICFRAMES_PER_SECOND,
			tracked.m_label.str(), (Int)it->first, tracked.m_lastPos.x, tracked.m_lastPos.y, tracked.m_lastPos.z, what );
	}
	fflush( m_log );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::observeBaseConstruction()
{
	// Hypersonic @feature (D-244 fix 1): registration is by BaseStatus. Scan every
	// frame so a whole build between two status samples still has its start logged.
	for( size_t i = 0; i < m_baseWatches.size(); ++i )
	{
		BaseWatch &area = m_baseWatches[i];
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getControllingPlayer() != area.m_player || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) ) continue;
			const Real dx = obj->getPosition()->x - area.m_x, dy = obj->getPosition()->y - area.m_y;
			if( dx * dx + dy * dy > area.m_radius * area.m_radius || area.m_seen.find( obj->getID() ) != area.m_seen.end() ) continue;
			area.m_seen[obj->getID()] = TRUE;
			if( !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ) continue;
			++area.m_starts;
			Object *builder = TheGameLogic->findObjectByID( obj->getBuilderID() );
			event( "BASE STATUS %s owner %s construction_start id %d template %s builder %d builder_name %s producer %d progress %.3f construction_starts %d",
				area.m_label.str(), area.m_owner.str(), (Int)obj->getID(), obj->getTemplate()->getName().str(),
				(Int)obj->getBuilderID(), builder ? builder->getName().str() : "-", (Int)obj->getProducerID(),
				obj->getConstructionPercent(), area.m_starts );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::observeChallenge()
{
	if( !isCampaign() || m_campaignName.compareNoCase( "IRAN_CHALLENGE" ) != 0 || TheScriptEngine == nullptr )
		return;
	static const char *const BASES[] = { "Incirlik", "AinAlAsad", "AlUdeid", "GulfAirbase", "Bagram" };
	static const char *const TYPES[] = { "Air", "Ground", "Power" };
	const TCounter *stage = TheScriptEngine->getCounter( "CH_Stage" );
	if( stage && ( m_challengeValues.find( "CH_Stage" ) == m_challengeValues.end() || m_challengeValues["CH_Stage"] != stage->value ) )
	{
		event( "CHALLENGE STAGE CH_Stage=%d", stage->value );
		m_challengeValues["CH_Stage"] = stage->value;
	}
	for( Int b = 0; b < (Int)ARRAY_SIZE( BASES ); ++b )
	{
		AsciiString name;
		name.format( "CH_%s_Alive", BASES[b] );
		const TFlag *alive = TheScriptEngine->getFlagByName( name );
		if( alive && ( m_challengeValues.find( name ) == m_challengeValues.end() || m_challengeValues[name] != (Int)alive->value ) )
		{
			event( "CHALLENGE BASE %s alive=%d", BASES[b], (Int)alive->value );
			m_challengeValues[name] = (Int)alive->value;
		}
		name.format( "CH_%s_Target", BASES[b] );
		const TCounter *priorityTarget = TheScriptEngine->getCounter( name );
		if( priorityTarget && ( m_challengeValues.find( name ) == m_challengeValues.end() || m_challengeValues[name] != priorityTarget->value ) )
		{
			event( "CHALLENGE TARGET base=%s priority_target_code=%d", BASES[b], priorityTarget->value );
			m_challengeValues[name] = priorityTarget->value;
		}
		for( Int t = 0; t < (Int)ARRAY_SIZE( TYPES ); ++t )
		{
			name.format( "CH_%s_%s", BASES[b], TYPES[t] );
			const TCounter *counter = TheScriptEngine->getCounter( name );
			if( counter == nullptr || counter->isCountdownTimer ) continue;
			std::map<AsciiString, Int>::const_iterator old = m_challengeValues.find( name );
			if( old == m_challengeValues.end() || old->second != counter->value )
			{
				const Int before = old == m_challengeValues.end() ? 0 : old->second;
				const Int targetCode = priorityTarget ? priorityTarget->value : 0;
				AsciiString powerName;
				powerName.format( "CH_%s_LastPower", BASES[b] );
				const TCounter *lastPower = t == 2 ? TheScriptEngine->getCounter( powerName ) : nullptr;
				const Int powerKind = lastPower ? lastPower->value : 0;
				const char *powerLabel = powerKind == 1 ? "A10" : powerKind == 2 ? "Carpet" : powerKind == 3 ? "Spectre" : "none";
				const char *policy = t == 2 ? ( powerKind == 3 ? "command_sector" : "nearest_key_building" ) :
					( targetCode == 0 ? "generic_hunt" : "priority_key_building_hunt" );
				event( "%s base=%s type=%s count=%d delta=%d target_policy=%s priority_target_code=%d power_kind=%s%s",
					old == m_challengeValues.end() && counter->value == 0 ? "CHALLENGE COUNTER" : "CHALLENGE RAID",
					BASES[b], TYPES[t], counter->value, counter->value - before,
					policy, targetCode, powerLabel, powerKind == 3 ? " target_waypoint=Player_1_Start" : "" );
				m_challengeValues[name] = counter->value;
			}
		}
	}
	// Current victims are observed rather than predicted from script intent; names/teams
	// tie every unit back to the base/raid that created it. Never changes AI or RNG.
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getControllingPlayer() != m_enemy || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) ||
			obj->isKindOf( KINDOF_PROJECTILE ) || obj->getAI() == nullptr ) continue;
		Team *team = obj->getTeam();
		const AsciiString teamName = team ? team->getName() : AsciiString( "-" );
		if( !obj->getName().startsWith( "CH_" ) && !teamName.startsWith( "CH_" ) ) continue;
		Object *victim = obj->getAI()->getCurrentVictim();
		const ObjectID victimID = victim ? victim->getID() : INVALID_ID;
		std::map<ObjectID, ObjectID>::const_iterator old = m_challengeTargets.find( obj->getID() );
		if( old != m_challengeTargets.end() && old->second == victimID ) continue;
		event( "CHALLENGE UNIT id=%d name=%s team=%s template=%s target_id=%d target_name=%s target_template=%s x=%.0f y=%.0f",
			(Int)obj->getID(), obj->getName().str(), teamName.str(), obj->getTemplate()->getName().str(),
			(Int)victimID, victim ? victim->getName().str() : "-", victim ? victim->getTemplate()->getName().str() : "-",
			obj->getPosition()->x, obj->getPosition()->y );
		m_challengeTargets[obj->getID()] = victimID;
	}
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::noteChallengePowerTarget( const AsciiString &team, const AsciiString &command,
	const Object *source, const Object *target )
{
	if( !isCampaign() || m_campaignName.compareNoCase( "IRAN_CHALLENGE" ) != 0 || !team.startsWith( "CH_" ) ||
		source == nullptr || target == nullptr ) return;
	event( "CHALLENGE POWER_TARGET team=%s command=%s source_id=%d target_id=%d target_name=%s target_template=%s x=%.0f y=%.0f status=valid_target_order",
		team.str(), command.str(), (Int)source->getID(), (Int)target->getID(), target->getName().str(),
		target->getTemplate()->getName().str(), target->getPosition()->x, target->getPosition()->y );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::logicUpdate()
{
	if( !m_started || m_finished )
		return;
	const Int frame = (Int)TheGameLogic->getFrame();
	observeBaseConstruction();
	while( m_nextCommand < m_commands.size() && m_commands[m_nextCommand].m_frame <= frame )
		runCommand( m_commands[m_nextCommand++] );
	observeChallenge();
	writeLog();
	if( frame % STRIKE_CHECK_FRAMES == 0 )
		updateStrikes( FALSE );	// Hypersonic @feature (D-110)

	// Hypersonic @feature (D-071 AI battle harness) a line every 60 game seconds; quit at the first defeat
	// (VictoryConditions has updated this frame, just before this call)
	if( m_aiBattle && isTeamBattle() )
	{
		updateTeamBattle( frame );	// Hypersonic @feature (AI team battle)
	}
	else if( m_aiBattle && m_iran && m_enemy )
	{
		if( frame == 0 )
			m_battleStartMs = timeGetTime();
		if( frame % LOGICFRAMES_PER_SECOND == 0 )
			sampleProduction();	// Hypersonic @feature (D-112)
		if( frame > 0 && frame % BATTLE_LINE_FRAMES == 0 )
		{
			writeBattleLine();
			writeProduction();	// Hypersonic @feature (D-112)
		}
		const Int aiStateFrames = m_aiStateFrames < 0 ? 30 * LOGICFRAMES_PER_SECOND : m_aiStateFrames;
		if( aiStateFrames > 0 && frame > 0 && frame % aiStateFrames == 0 )
		{
			writeAIState();	// Hypersonic @feature (D-190)
			writeAIHelp();	// Hypersonic (2026-10-20)
		}

		const Bool iranWon = TheVictoryConditions->hasAchievedVictory( m_iran );
		const Bool enemyWon = TheVictoryConditions->hasAchievedVictory( m_enemy );
		const Bool bothLost = TheVictoryConditions->hasBeenDefeated( m_iran ) && TheVictoryConditions->hasBeenDefeated( m_enemy );
		if( iranWon || enemyWon || bothLost )
			finish( iranWon ? "Iran" : enemyWon ? "Enemy" : "none (both defeated)" );
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-110) strike log. Called by the Iran AI's launch script after a launch at 'target'.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::noteMunitionLaunch( const char *power, const Object *target )
{
	if( m_finished || target == nullptr )
		return;
	StrikeRecord rec;
	rec.m_target = target->getID();
	rec.m_targetName = target->getTemplate()->getName();
	rec.m_power = power;
	rec.m_frame = (Int)TheGameLogic->getFrame();
	rec.m_resolved = FALSE;
	m_strikes.push_back( rec );
	const BodyModuleInterface *body = target->getBodyModule();
	event( "STRIKE LAUNCH %s -> %s id %u hp %.0f/%.0f%s", power, rec.m_targetName.str(), (UnsignedInt)rec.m_target,
		body ? body->getHealth() : 0.0f, body ? body->getMaxHealth() : 0.0f, m_legacyMunitionPicker ? " (legacy)" : "" );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-110) a launch's target that died within STRIKE_OUTCOME_SECONDS counts as killed, else as
// survived; at the end of the game the open ones are reported as they stand.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::updateStrikes( Bool atEnd )
{
	const Int frame = TheGameLogic ? (Int)TheGameLogic->getFrame() : 0;
	for( size_t i = 0; i < m_strikes.size(); ++i )
	{
		StrikeRecord &rec = m_strikes[i];
		if( rec.m_resolved )
			continue;
		const Object *obj = TheGameLogic->findObjectByID( rec.m_target );
		const Real after = (Real)( frame - rec.m_frame ) / LOGICFRAMES_PER_SECOND;
		if( obj == nullptr || obj->isEffectivelyDead() )
		{
			event( "STRIKE KILLED %s -> %s id %u after %.1f s", rec.m_power.str(), rec.m_targetName.str(), (UnsignedInt)rec.m_target, after );
			rec.m_resolved = TRUE;
		}
		else if( atEnd || after > STRIKE_OUTCOME_SECONDS )
		{
			const BodyModuleInterface *body = obj->getBodyModule();
			event( "STRIKE SURVIVED %s -> %s id %u after %.1f s hp %.0f/%.0f%s", rec.m_power.str(), rec.m_targetName.str(),
				(UnsignedInt)rec.m_target, after, body ? body->getHealth() : 0.0f, body ? body->getMaxHealth() : 0.0f,
				atEnd && after <= STRIKE_OUTCOME_SECONDS ? " (game ended)" : "" );
			rec.m_resolved = TRUE;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-071 AI battle harness) living units (infantry, vehicles, aircraft; no projectiles) and
// finished buildings of the two players.
//-------------------------------------------------------------------------------------------------
Int HypersonicScenario::sideOf( const Player *player ) const
{
	if( player == nullptr )
		return -1;
	for( size_t i = 0; i < m_sides.size(); ++i )
		if( m_sides[i] == player )
			return (Int)i;
	return -1;
}

void HypersonicScenario::countObjects( std::vector<BattleStats> &all ) const
{
	all.resize( m_sides.size() );
	for( size_t i = 0; i < all.size(); ++i )
		all[i].m_units = all[i].m_buildings = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Int side = sideOf( obj->getControllingPlayer() );
		BattleStats *stats = side >= 0 ? &all[side] : nullptr;
		if( stats == nullptr || obj->isEffectivelyDead() )
			continue;
		if( obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			if( !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
				++stats->m_buildings;
		}
		else if( !obj->isKindOf( KINDOF_PROJECTILE ) &&
			( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_AIRCRAFT ) ) )
		{
			++stats->m_units;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic (2026-10-20) AI HELP, for team games: per Iran AI the ally bases it counts as under attack (flags
// '_IRAN_ALLY_HELP_<start>', tools/iran_ai.py ally_help_scripts) and its units in teams in the 'Helping' state.
// Written only when one of them is on, so 1v1 logs stay as they were.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeAIHelp()
{
	if( m_events == nullptr || TheScriptEngine == nullptr || ThePlayerList == nullptr )
		return;
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		if( player == nullptr || player->getPlayerType() != PLAYER_COMPUTER || player->getSide() != "Iran" )
			continue;
		const Int q = player->getMpStartIndex();
		AsciiString flags;
		for( Int s = 1; s <= 8; ++s )
		{
			AsciiString name;
			name.format( "_IRAN_ALLY_HELP_%d%d", s, q );
			const TFlag *flag = TheScriptEngine->getFlagByName( name );
			if( flag != nullptr && flag->value )
			{
				AsciiString item;
				item.format( " %d", s );
				flags.concat( item );
			}
		}
		Int helping = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getControllingPlayer() != player || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) )
				continue;
			const Team *team = obj->getTeam();
			if( team != nullptr && team->getState() == "Helping" )
				++helping;
		}
		if( !flags.isEmpty() || helping > 0 )
			event( "AI HELP %s (start %d): ally bases attacked:%s, helping units %d", sideLabel( player ), q + 1,
				flags.isEmpty() ? " none" : flags.str(), helping );
	}
}

struct AIStateTeam	///< Hypersonic @feature (D-190) one team in the AI STATE log
{
	AIStateTeam() : units( 0 ), inOuter( 0 ) {}
	Int units;
	Int inOuter;
	AsciiString state;
};

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-190) AI STATE: the Iran AI's attack flags and counters (skirmish names carry the player's
// start index), its main tanks inside its outer perimeter, and per team: units / of them in the outer perimeter / state
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeAIState()
{
	if( m_iran == nullptr || m_events == nullptr || TheScriptEngine == nullptr )
		return;
	const Int q = m_iran->getMpStartIndex();
	AsciiString line = "AI STATE";
	static const char *const COUNTERS[] = { "_ATTACK_WAVE", "_ESCALATION", "_IRAN_MASS_FALLBACK", "_IRAN_PUSH_T",
		"_IRAN_ALERT_HOLD", "_IRAN_ALERT_CAP_T" };
	for( Int c = 0; c < (Int)ARRAY_SIZE( COUNTERS ); ++c )
	{
		AsciiString name, item;
		name.format( "%s%d", COUNTERS[c], q );
		const TCounter *counter = TheScriptEngine->getCounter( name );
		if( counter == nullptr )
			item.format( " %s=-", COUNTERS[c] );
		else if( counter->isCountdownTimer )
			item.format( " %s=%.0fs", COUNTERS[c], (Real)counter->value / LOGICFRAMES_PER_SECOND );
		else
			item.format( " %s=%d", COUNTERS[c], counter->value );
		line.concat( item );
	}
	static const char *const FLAGS[] = { "_LAUNCH_ATTACK", "_IRAN_PUSH", "_INVASION_ALERT", "_INVASION_ALERT_RESPONSE",
		"_IRAN_ENEMY_GLA" };
	for( Int f = 0; f < (Int)ARRAY_SIZE( FLAGS ); ++f )
	{
		AsciiString name, item;
		name.format( "%s%d", FLAGS[f], q );
		const TFlag *flag = TheScriptEngine->getFlagByName( name );
		item.format( " %s=%s", FLAGS[f], flag == nullptr ? "-" : flag->value ? "1" : "0" );
		line.concat( item );
	}
	AsciiString outerName;
	outerName.format( "OuterPerimeter%d", q + 1 );
	PolygonTrigger *outer = TheTerrainLogic->getTriggerAreaByName( outerName );
	std::map<AsciiString, AIStateTeam> teams;
	Int tanks = 0, tanksOuter = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getControllingPlayer() != m_iran || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE )
			|| obj->isKindOf( KINDOF_PROJECTILE ) || obj->getAI() == nullptr )
			continue;
		ICoord3D pos;
		pos.x = REAL_TO_INT( obj->getPosition()->x );
		pos.y = REAL_TO_INT( obj->getPosition()->y );
		pos.z = REAL_TO_INT( obj->getPosition()->z );
		const Bool inOuter = outer != nullptr && outer->pointInTrigger( pos );
		const AsciiString &tmpl = obj->getTemplate()->getName();
		if( tmpl == "IranVehicleKarrar" || tmpl == "IranVehicleZolfaghar" )
		{
			++tanks;
			tanksOuter += inOuter ? 1 : 0;
		}
		Team *team = obj->getTeam();
		AsciiString teamName = team ? team->getName() : AsciiString( "?" );
		AIStateTeam &info = teams[teamName];
		if( info.units == 0 && team )
			info.state = team->getState();
		++info.units;
		info.inOuter += inOuter ? 1 : 0;
	}
	AsciiString item;
	item.format( " tanks=%d inOuter=%d (%s)", tanks, tanksOuter, outer ? outerName.str() : "no outer perimeter" );
	line.concat( item );
	event( "%s", line.str() );
	AsciiString teamLine = "AI TEAMS";
	for( std::map<AsciiString, AIStateTeam>::const_iterator it = teams.begin(); it != teams.end(); ++it )
	{
		AsciiString t;
		t.format( " | %s %d/%d%s%s", it->first.str(), it->second.units, it->second.inOuter,
			it->second.state.isEmpty() ? "" : " ", it->second.state.str() );
		if( teamLine.getLength() + t.getLength() > 1000 )
		{
			event( "%s", teamLine.str() );
			teamLine = "AI TEAMS";
		}
		teamLine.concat( t );
	}
	event( "%s", teamLine.str() );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeBattleLine()
{
	if( m_battle == nullptr || m_sides.empty() )
		return;
	std::vector<BattleStats> stats;
	countObjects( stats );
	fprintf( m_battle, "%.1f,%.0f", (Real)TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60.0f,
		(Real)( timeGetTime() - m_battleStartMs ) / 1000.0f );
	for( size_t i = 0; i < m_sides.size(); ++i )
	{
		Player *const *players = &m_sides[0];
		if( players[i] == nullptr )
		{
			fprintf( m_battle, ",,,,,,," );
			continue;
		}
		ScoreKeeper *score = players[i]->getScoreKeeper();
		fprintf( m_battle, ",%u,%d,%d,%d,%d,%d,%d", players[i]->getMoney()->countMoney(), stats[i].m_units, stats[i].m_buildings,
			score->getTotalUnitsDestroyed(), score->getTotalUnitsLost(), score->getTotalBuildingsDestroyed(), score->getTotalBuildingsLost() );
	}
	fprintf( m_battle, "\n" );
	fflush( m_battle );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-112) production log, once a game second: new objects per template (counted when first seen,
// so the starting objects count too), newly completed player upgrades (written at once), and for every finished
// building with a production queue whether its queue holds anything.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::sampleProduction()
{
	if( m_production == nullptr || m_sides.empty() )
		return;
	const Real minutes = (Real)TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60.0f;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Int side = sideOf( obj->getControllingPlayer() );
		if( side < 0 || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) )
			continue;
		const AsciiString &name = obj->getTemplate()->getName();
		if( m_seenObjects.find( obj->getID() ) == m_seenObjects.end() )
		{
			m_seenObjects[obj->getID()] = TRUE;
			++m_built[side][name];
		}
		if( obj->isKindOf( KINDOF_STRUCTURE ) && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{
			ProductionUpdateInterface *production = obj->getProductionUpdateInterface();
			if( production )
			{
				++m_allSamples[side][name];
				if( production->getProductionCount() > 0 )
					++m_busySamples[side][name];
			}
		}
	}
	for( size_t i = 0; i < m_sides.size(); ++i )
	{
		if( m_sides[i] == nullptr )
			continue;
		for( UpgradeTemplate *up = TheUpgradeCenter->firstUpgradeTemplate(); up; up = up->friend_getNext() )
		{
			if( up->getUpgradeType() != UPGRADE_TYPE_PLAYER || !m_sides[i]->hasUpgradeComplete( up ) )
				continue;
			if( m_upgradesDone[i].find( up->getUpgradeName() ) != m_upgradesDone[i].end() )
				continue;
			m_upgradesDone[i][up->getUpgradeName()] = TRUE;
			fprintf( m_production, "%.2f,%s,upgrade,%s,1,\n", minutes, m_sideNames[i].str(), up->getUpgradeName().str() );
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-112) every 60 game seconds: objects per template so far (cumulative), and per production
// building template the busy samples / all samples of this minute.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeProduction()
{
	if( m_production == nullptr )
		return;
	const Real minutes = (Real)TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60.0f;
	for( size_t i = 0; i < m_sides.size(); ++i )
	{
		const char *side = m_sideNames[i].str();
		for( std::map<AsciiString, Int>::const_iterator it = m_built[i].begin(); it != m_built[i].end(); ++it )
			fprintf( m_production, "%.1f,%s,built,%s,%d,\n", minutes, side, it->first.str(), it->second );
		for( std::map<AsciiString, Int>::const_iterator jt = m_allSamples[i].begin(); jt != m_allSamples[i].end(); ++jt )
			fprintf( m_production, "%.1f,%s,busy,%s,%d,%d\n", minutes, side, jt->first.str(), m_busySamples[i][jt->first], jt->second );
		m_allSamples[i].clear();
		m_busySamples[i].clear();
	}
	fflush( m_production );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeResult( const char *winner )
{
	if( isTeamBattle() )
	{
		writeTeamResult( winner );	// Hypersonic @feature (AI team battle)
		return;
	}
	AsciiString path;
	path.format( "%s\\result.txt", m_outputDir.str() );
	FILE *file = fopen( path.str(), "w" );
	if( file == nullptr )
		return;
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Real gameSeconds = (Real)frame / LOGICFRAMES_PER_SECOND;
	const Real wallSeconds = (Real)( timeGetTime() - m_battleStartMs ) / 1000.0f;
	fprintf( file, "winner %s\n", winner );
	fprintf( file, "game_time_min %.2f\n", gameSeconds / 60.0f );
	fprintf( file, "frames %u\n", frame );
	fprintf( file, "real_time_s %.0f\n", wallSeconds );
	fprintf( file, "speed_game_s_per_real_s %.2f\n", wallSeconds > 0.0f ? gameSeconds / wallSeconds : 0.0f );
	fprintf( file, "map %s\n", m_mapName.str() );
	fprintf( file, "iran FactionIran %s\n", aiSlotName( m_iranSlotState ) );
	fprintf( file, "enemy %s %s\n", m_enemyFaction.str(), aiSlotName( m_enemySlotState ) );
	if( m_iran && m_enemy )
	{
		std::vector<BattleStats> stats;
		countObjects( stats );
		Player *players[2] = { m_iran, m_enemy };
		fprintf( file, "\n%-18s %10s %10s\n", "stat", "Iran", "Enemy" );
		ScoreKeeper *a = players[0]->getScoreKeeper();
		ScoreKeeper *b = players[1]->getScoreKeeper();
		fprintf( file, "%-18s %10d %10d\n", "units_built", a->getTotalUnitsBuilt(), b->getTotalUnitsBuilt() );
		fprintf( file, "%-18s %10d %10d\n", "units_lost", a->getTotalUnitsLost(), b->getTotalUnitsLost() );
		fprintf( file, "%-18s %10d %10d\n", "units_killed", a->getTotalUnitsDestroyed(), b->getTotalUnitsDestroyed() );
		fprintf( file, "%-18s %10d %10d\n", "buildings_built", a->getTotalBuildingsBuilt(), b->getTotalBuildingsBuilt() );
		fprintf( file, "%-18s %10d %10d\n", "buildings_lost", a->getTotalBuildingsLost(), b->getTotalBuildingsLost() );
		fprintf( file, "%-18s %10d %10d\n", "buildings_killed", a->getTotalBuildingsDestroyed(), b->getTotalBuildingsDestroyed() );
		fprintf( file, "%-18s %10d %10d\n", "money_earned", a->getTotalMoneyEarned(), b->getTotalMoneyEarned() );
		fprintf( file, "%-18s %10d %10d\n", "money_spent", a->getTotalMoneySpent(), b->getTotalMoneySpent() );
		fprintf( file, "%-18s %10u %10u\n", "money_now", players[0]->getMoney()->countMoney(), players[1]->getMoney()->countMoney() );
		fprintf( file, "%-18s %10d %10d\n", "units_alive", stats[0].m_units, stats[1].m_units );
		fprintf( file, "%-18s %10d %10d\n", "buildings_alive", stats[0].m_buildings, stats[1].m_buildings );

		// what each side has at the end
		for( Int i = 0; i < 2; ++i )
		{
			std::map<AsciiString, Int> alive;
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
				if( obj->getControllingPlayer() == players[i] && !obj->isEffectivelyDead() && !obj->isKindOf( KINDOF_PROJECTILE ) )
					++alive[obj->getTemplate()->getName()];
			fprintf( file, "\n%s objects alive at the end:\n", i == 0 ? "Iran" : "Enemy" );
			for( std::map<AsciiString, Int>::const_iterator it = alive.begin(); it != alive.end(); ++it )
				fprintf( file, "  %s x%d\n", it->first.str(), it->second );
		}
	}
	fclose( file );
	event( "AI battle over: winner %s at %.1f min (%.2f game s per real s)", winner, gameSeconds / 60.0f,
		wallSeconds > 0.0f ? gameSeconds / wallSeconds : 0.0f );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (AI team battle) once a logic frame: battle.csv / production.csv, eliminations (a player whose
// objects are gone, as VictoryConditions judges it), and the end when only one team has players left.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::updateTeamBattle( Int frame )
{
	if( frame == 0 )
		m_battleStartMs = timeGetTime();
	if( frame % LOGICFRAMES_PER_SECOND == 0 )
		sampleProduction();
	if( frame > 0 && frame % BATTLE_LINE_FRAMES == 0 )
	{
		writeBattleLine();
		writeProduction();
	}
	const Int aiStateFrames = m_aiStateFrames < 0 ? 30 * LOGICFRAMES_PER_SECOND : m_aiStateFrames;
	if( aiStateFrames > 0 && frame > 0 && frame % aiStateFrames == 0 )
		writeAIHelp();	// Hypersonic (2026-10-20)
	if( frame <= 1 )
		return;

	Int teamsLeft = 0;
	Int lastTeam = 0;
	Bool teamSeen[5] = { FALSE, FALSE, FALSE, FALSE, FALSE };
	for( size_t i = 0; i < m_teamPlayers.size(); ++i )
	{
		BattlePlayer &bp = m_teamPlayers[i];
		if( bp.m_player == nullptr )
			continue;
		if( bp.m_eliminatedFrame < 0 && TheVictoryConditions->hasSinglePlayerBeenDefeated( bp.m_player ) )
		{
			bp.m_eliminatedFrame = frame;
			bp.m_eliminationOrder = ++m_eliminations;
			event( "ELIMINATED p%d %s (team %d) at %.2f min, #%d out", (Int)i + 1, bp.m_faction.str(), bp.m_team,
				(Real)frame / LOGICFRAMES_PER_SECOND / 60.0f, bp.m_eliminationOrder );
		}
		if( bp.m_eliminatedFrame < 0 && !teamSeen[bp.m_team] )
		{
			teamSeen[bp.m_team] = TRUE;
			++teamsLeft;
			lastTeam = bp.m_team;
		}
	}
	if( teamsLeft <= 1 )
	{
		AsciiString winner;
		if( teamsLeft == 1 )
			winner.format( "team %d", lastTeam );
		else
			winner = "none (all defeated)";
		finish( winner.str() );
	}
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (AI team battle) result.txt: winner, the players, a per-player stats table, the elimination
// order, and each player's living objects at the end.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::writeTeamResult( const char *winner )
{
	size_t i;
	AsciiString path;
	path.format( "%s\\result.txt", m_outputDir.str() );
	FILE *file = fopen( path.str(), "w" );
	if( file == nullptr )
		return;
	const UnsignedInt frame = TheGameLogic->getFrame();
	const Real gameSeconds = (Real)frame / LOGICFRAMES_PER_SECOND;
	const Real wallSeconds = (Real)( timeGetTime() - m_battleStartMs ) / 1000.0f;
	fprintf( file, "winner %s\n", winner );
	fprintf( file, "game_time_min %.2f\n", gameSeconds / 60.0f );
	fprintf( file, "frames %u\n", frame );
	fprintf( file, "real_time_s %.0f\n", wallSeconds );
	fprintf( file, "speed_game_s_per_real_s %.2f\n", wallSeconds > 0.0f ? gameSeconds / wallSeconds : 0.0f );
	fprintf( file, "map %s\n", m_mapName.str() );
	for( i = 0; i < m_teamPlayers.size(); ++i )
	{
		const BattlePlayer &bp = m_teamPlayers[i];
		fprintf( file, "p%d %s %s team %d start %d player %d\n", (Int)i + 1, bp.m_faction.str(), aiSlotName( bp.m_slotState ),
			bp.m_team, bp.m_start + 1, bp.m_player ? bp.m_player->getPlayerIndex() : -1 );
	}

	std::vector<BattleStats> stats;
	countObjects( stats );
	const size_t n = m_teamPlayers.size();
	fprintf( file, "\n%-18s", "stat" );
	for( i = 0; i < n; ++i )
	{
		AsciiString head;
		head.format( "p%d", (Int)i + 1 );
		fprintf( file, " %10s", head.str() );
	}
	fprintf( file, "\n%-18s", "faction" );
	for( i = 0; i < n; ++i )
	{
		AsciiString f = m_teamPlayers[i].m_faction;
		if( f.startsWith( "Faction" ) )
			f = AsciiString( f.str() + 7 );
		fprintf( file, " %10s", f.str() );
	}
	fprintf( file, "\n%-18s", "team" );
	for( i = 0; i < n; ++i )
		fprintf( file, " %10d", m_teamPlayers[i].m_team );
	fprintf( file, "\n" );
	static const char *const names[] = { "units_built", "units_lost", "units_killed", "buildings_built", "buildings_lost",
		"buildings_killed", "money_earned", "money_spent", "money_now", "units_alive", "buildings_alive" };
	for( Int row = 0; row < 11; ++row )
	{
		fprintf( file, "%-18s", names[row] );
		for( i = 0; i < n; ++i )
		{
			Player *p = m_teamPlayers[i].m_player;
			if( p == nullptr )
			{
				fprintf( file, " %10s", "-" );
				continue;
			}
			ScoreKeeper *k = p->getScoreKeeper();
			Int v = 0;
			switch( row )
			{
				case 0: v = k->getTotalUnitsBuilt(); break;
				case 1: v = k->getTotalUnitsLost(); break;
				case 2: v = k->getTotalUnitsDestroyed(); break;
				case 3: v = k->getTotalBuildingsBuilt(); break;
				case 4: v = k->getTotalBuildingsLost(); break;
				case 5: v = k->getTotalBuildingsDestroyed(); break;
				case 6: v = k->getTotalMoneyEarned(); break;
				case 7: v = k->getTotalMoneySpent(); break;
				case 8: v = (Int)p->getMoney()->countMoney(); break;
				case 9: v = stats[i].m_units; break;
				default: v = stats[i].m_buildings; break;
			}
			fprintf( file, " %10d", v );
		}
		fprintf( file, "\n" );
	}
	fprintf( file, "%-18s", "eliminated_min" );
	for( i = 0; i < n; ++i )
	{
		AsciiString t;
		if( m_teamPlayers[i].m_eliminatedFrame >= 0 )
			t.format( "%.2f", (Real)m_teamPlayers[i].m_eliminatedFrame / LOGICFRAMES_PER_SECOND / 60.0f );
		else
			t = "-";
		fprintf( file, " %10s", t.str() );
	}
	fprintf( file, "\n%-18s", "eliminated_order" );
	for( i = 0; i < n; ++i )
	{
		if( m_teamPlayers[i].m_eliminationOrder > 0 )
			fprintf( file, " %10d", m_teamPlayers[i].m_eliminationOrder );
		else
			fprintf( file, " %10s", "-" );
	}
	fprintf( file, "\n" );

	for( i = 0; i < n; ++i )
	{
		Player *p = m_teamPlayers[i].m_player;
		if( p == nullptr )
			continue;
		std::map<AsciiString, Int> alive;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			if( obj->getControllingPlayer() == p && !obj->isEffectivelyDead() && !obj->isKindOf( KINDOF_PROJECTILE ) )
				++alive[obj->getTemplate()->getName()];
		fprintf( file, "\np%d %s objects alive at the end:\n", (Int)i + 1, m_teamPlayers[i].m_faction.str() );
		for( std::map<AsciiString, Int>::const_iterator it = alive.begin(); it != alive.end(); ++it )
			fprintf( file, "  %s x%d\n", it->first.str(), it->second );
	}
	fclose( file );
	event( "AI team battle over: winner %s at %.1f min (%.2f game s per real s)", winner, gameSeconds / 60.0f,
		wallSeconds > 0.0f ? gameSeconds / wallSeconds : 0.0f );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::applyCamera()
{
	if( m_cameraKeys.empty() || TheTacticalView == nullptr )
		return;
	// Hypersonic @feature (D-244 fix 1): a later first key must leave the opening
	// mission camera in control until that time (previously key 0 applied early).
	if( (Int)TheGameLogic->getFrame() < m_cameraKeys[0].m_frame )
		return;
	// Hypersonic @feature CameraFree: from then on the player's camera, with the normal zoom limits again
	if( m_cameraFreeFrame >= 0 && (Int)TheGameLogic->getFrame() >= m_cameraFreeFrame )
	{
		TheTacticalView->setZoomLimited( TRUE );	// every frame: a skipped client frame must not leave it unlimited
		return;
	}
	const Real frame = (Real)TheGameLogic->getFrame();
	size_t next = 0;
	while( next < m_cameraKeys.size() && (Real)m_cameraKeys[next].m_frame <= frame )
		++next;
	const CameraKey &a = m_cameraKeys[next > 0 ? next - 1 : 0];
	const CameraKey &b = m_cameraKeys[next < m_cameraKeys.size() ? next : m_cameraKeys.size() - 1];
	Real t = 0.0f;
	if( b.m_frame > a.m_frame )
		t = ( frame - (Real)a.m_frame ) / (Real)( b.m_frame - a.m_frame );
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;

	Coord3D posA, posB;
	posA.x = a.m_x; posA.y = a.m_y; posA.z = 0.0f;
	posB.x = b.m_x; posB.y = b.m_y; posB.z = 0.0f;
	Object *followA = a.m_follow ? findTagged( a.m_tag ) : nullptr;
	Object *followB = b.m_follow ? findTagged( b.m_tag ) : nullptr;
	if( a.m_follow && followA == nullptr )
		followA = highestOfTemplate( a.m_tag );
	if( b.m_follow && followB == nullptr )
		followB = highestOfTemplate( b.m_tag );
	if( followA ) posA = *followA->getPosition();
	else if( a.m_follow && s_haveLastLookAt ) posA = s_lastLookAt;
	if( followB ) posB = *followB->getPosition();
	else if( b.m_follow && s_haveLastLookAt ) posB = s_lastLookAt;

	Coord3D pos;
	pos.x = posA.x + ( posB.x - posA.x ) * t;
	pos.y = posA.y + ( posB.y - posA.y ) * t;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	s_lastLookAt = pos;
	s_haveLastLookAt = TRUE;
	const Real height = a.m_height + ( b.m_height - a.m_height ) * t;
	const Real pitch = a.m_pitch + ( b.m_pitch - a.m_pitch ) * t;
	const Real yaw = a.m_yaw + ( b.m_yaw - a.m_yaw ) * t;

	TheTacticalView->setZoomLimited( FALSE );
	TheTacticalView->setAngle( yaw * PI / 180.0f );
	TheTacticalView->setPitch( pitch * PI / 180.0f );
	TheTacticalView->lookAt( &pos );
	TheTacticalView->setHeightAboveGround( height );
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature free camera (Generals Hypersonic D-272, release clips): an eye and a point to look at, so the camera
// can look up at a missile, stand on the ground or ride on a unit. Scenario harness only.
Bool HypersonicScenario::resolveEyeKey( EyeKey &k, Coord3D &eye, Coord3D &look )
{
	Object *eyeObject = nullptr;
	if( k.m_eyeFollow )
	{
		eyeObject = findTagged( k.m_eyeTag );
		if( eyeObject == nullptr )
			eyeObject = highestOfTemplate( k.m_eyeTag );
		if( eyeObject )
		{
			k.m_lastEye = *eyeObject->getPosition();
			k.m_lastEye.z += k.m_eyeH;
			k.m_haveEye = TRUE;
		}
	}
	else
	{
		k.m_lastEye.x = k.m_eyeX;
		k.m_lastEye.y = k.m_eyeY;
		k.m_lastEye.z = TheTerrainLogic->getGroundHeight( k.m_eyeX, k.m_eyeY ) + k.m_eyeH;
		k.m_haveEye = TRUE;
	}
	if( !k.m_haveEye )
		return FALSE;
	eye = k.m_lastEye;
	if( k.m_lookMode == 1 )
	{
		Object *lookObject = findTagged( k.m_lookTag );
		if( lookObject == nullptr )
			lookObject = highestOfTemplate( k.m_lookTag );
		if( lookObject )
		{
			k.m_lastLook = *lookObject->getPosition();
			k.m_lastLook.z += k.m_lookH;
			k.m_haveLook = TRUE;
		}
	}
	else if( k.m_lookMode == 2 )
	{
		if( eyeObject )
		{
			const Vector3 forward = eyeObject->getTransformMatrix()->Get_X_Vector();
			k.m_lastLook.x = eye.x + forward.X * k.m_lookH;
			k.m_lastLook.y = eye.y + forward.Y * k.m_lookH;
			k.m_lastLook.z = eye.z + forward.Z * k.m_lookH;
			k.m_haveLook = TRUE;
		}
	}
	else
	{
		k.m_lastLook.x = k.m_lookX;
		k.m_lastLook.y = k.m_lookY;
		k.m_lastLook.z = TheTerrainLogic->getGroundHeight( k.m_lookX, k.m_lookY ) + k.m_lookH;
		k.m_haveLook = TRUE;
	}
	if( !k.m_haveLook )
		return FALSE;
	look = k.m_lastLook;
	return TRUE;
}

void HypersonicScenario::applyFreeCamera()
{
	if( m_sky )
		TheWritableGlobalData->m_drawSkyBox = TRUE;	// reset when a game ends; the maps' scripts normally own this
	if( m_eyeKeys.empty() || TheTacticalView == nullptr )
		return;
	const Real frame = (Real)TheGameLogic->getFrame();
	size_t next = 0;
	while( next < m_eyeKeys.size() && (Real)m_eyeKeys[next].m_frame <= frame )
		++next;
	Bool on = next > 0 && !m_eyeKeys[next - 1].m_off;
	Coord3D eye, look;
	Real fov = 0.0f;
	if( on )
	{
		EyeKey &a = m_eyeKeys[next - 1];
		EyeKey &b = next < m_eyeKeys.size() && !m_eyeKeys[next].m_off ? m_eyeKeys[next] : a;
		Real t = 0.0f;
		if( b.m_frame > a.m_frame )
			t = ( frame - (Real)a.m_frame ) / (Real)( b.m_frame - a.m_frame );
		Coord3D eyeA, lookA, eyeB, lookB;
		const Bool haveA = resolveEyeKey( a, eyeA, lookA );
		const Bool haveB = resolveEyeKey( b, eyeB, lookB );
		if( !haveA && !haveB )
			on = FALSE;	// its object has not existed yet: wait
		else
		{
			if( !haveA ) { eyeA = eyeB; lookA = lookB; }
			if( !haveB ) { eyeB = eyeA; lookB = lookA; }
			eye.x = eyeA.x + ( eyeB.x - eyeA.x ) * t;
			eye.y = eyeA.y + ( eyeB.y - eyeA.y ) * t;
			eye.z = eyeA.z + ( eyeB.z - eyeA.z ) * t;
			look.x = lookA.x + ( lookB.x - lookA.x ) * t;
			look.y = lookA.y + ( lookB.y - lookA.y ) * t;
			look.z = lookA.z + ( lookB.z - lookA.z ) * t;
			// a key without a lens keeps the other key's
			const Real fovA = a.m_fov > 0.0f ? a.m_fov : b.m_fov;
			const Real fovB = b.m_fov > 0.0f ? b.m_fov : a.m_fov;
			fov = fovA + ( fovB - fovA ) * t;
		}
	}
	if( !on )
	{
		if( m_freeCameraOn )
		{
			TheTacticalView->setFreeCamera( nullptr, nullptr, 0.0f );
			TheTacticalView->setZoomLimited( TRUE );	// the Camera keys turn it off again if they are in use
			m_freeCameraOn = FALSE;
		}
		return;
	}
	m_freeCameraOn = TRUE;
	// the pivot under the eye: the terrain is drawn around it and the sounds are heard from it
	Coord3D pivot = eye;
	pivot.z = TheTerrainLogic->getGroundHeight( eye.x, eye.y );
	TheTacticalView->setZoomLimited( FALSE );
	TheTacticalView->lookAt( &pivot );
	TheTacticalView->setFreeCamera( &eye, &look, fov );
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::takeScreenshot()
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	AsciiString path;
	path.format( "%s\\shot_f%05u_t%06.2f.png", m_outputDir.str(), frame, (Real)frame / LOGICFRAMES_PER_SECOND );
	TheDisplay->takeScreenShotToFile( path.str() );
	++m_shotCount;
}

//-------------------------------------------------------------------------------------------------
void HypersonicScenario::finish( const char *winner )
{
	if( m_finished )
		return;
	m_finished = TRUE;
	updateStrikes( TRUE );	// Hypersonic @feature (D-110)
	if( m_aiBattle )
	{
		// Hypersonic @feature (D-071 AI battle harness)
		if( winner == nullptr || TheGameLogic->getFrame() % BATTLE_LINE_FRAMES != 0 )	// a timeout comes before that frame's logic line
		{
			writeBattleLine();
			writeProduction();	// Hypersonic @feature (D-112)
		}
		writeResult( winner ? winner : "none (timeout)" );
		if( m_battle )
		{
			fclose( m_battle );
			m_battle = nullptr;
		}
		if( m_production )
		{
			fclose( m_production );
			m_production = nullptr;
		}
		TheWritableGlobalData->m_disableRender = FALSE;
	}
	event( "done: %d screenshots", m_shotCount );
	AsciiString donePath;
	donePath.format( "%s\\done.txt", m_outputDir.str() );
	FILE *done = fopen( donePath.str(), "w" );
	if( done )
	{
		fprintf( done, "frames %u\nscreenshots %d\n", TheGameLogic->getFrame(), m_shotCount );
		fclose( done );
	}
	if( m_log )
	{
		fclose( m_log );
		m_log = nullptr;
	}
	TheGameEngine->setQuitting( TRUE );
}

//-------------------------------------------------------------------------------------------------
// Client, start of each GameClient::update: a screenshot asked for here is taken at the end of this update's draw.
//-------------------------------------------------------------------------------------------------
void HypersonicScenario::clientUpdate()
{
	if( !m_started || m_finished || !TheGameLogic->isInGame() || TheGameLogic->isLoadingMap() )
		return;

	if( !m_clientStarted )
	{
		m_clientStarted = TRUE;
		// Hypersonic @feature (D-048) run as fast as possible: the logic time scale stays off, so the engine runs one
		// logic frame per rendered frame (canUpdateRegularGameLogic) and every time in the file stays logic time;
		// only the render frame limit goes (Speed = max) or is set to Speed fps.
		TheFramePacer->enableLogicTimeScale( FALSE );
		if( m_speedFps > 0 )
		{
			TheFramePacer->setFramesPerSecondLimit( m_speedFps );
			TheWritableGlobalData->m_useFpsLimit = TRUE;
		}
		else
		{
			TheWritableGlobalData->m_useFpsLimit = FALSE;
		}
		event( "speed: %s", m_speedFps > 0 ? "limited" : "max (render frame limit off)" );
		if( m_hideUI )
		{
			HideControlBar();
			if( TheMouse )
				TheMouse->setVisibility( FALSE );
		}
	}

	const Int frame = (Int)TheGameLogic->getFrame();
	Bool shot = FALSE;
	if( m_nextShot < m_shotFrames.size() && m_shotFrames[m_nextShot] <= frame )
	{
		takeScreenshot();
		shot = TRUE;
		while( m_nextShot < m_shotFrames.size() && m_shotFrames[m_nextShot] <= frame )
			++m_nextShot;
	}

	// Hypersonic @feature (D-071 AI battle harness) DrawEvery: skip rendering (client only) except every n-th frame
	// and screenshot frames
	if( m_drawEvery > 1 )
		TheWritableGlobalData->m_disableRender = !shot && ( frame % m_drawEvery ) != 0;

	if( frame >= m_durationFrames )
	{
		finish();
		return;
	}

	applyCamera();
	applyFreeCamera();	// Hypersonic @feature (D-272)
}
