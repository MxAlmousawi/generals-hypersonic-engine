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

// FILE: HypersonicScenario.h ///////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Dev test harness (Generals Hypersonic D-049). Only exists when the game is started
//         with -hypersonicScenario <scenario file> <output folder>: it starts a skirmish on the flat test map
//         (Iran = local player, one enemy of a chosen faction, no AI, no scripts, no victory check), then runs the
//         scenario file: setup (money, sciences, upgrades, objects, magazines), timed actions, camera keyframes,
//         screenshots and a CSV log of object values, and quits after its duration. Without the flag
//         TheHypersonicScenario stays null and nothing here runs.
//         Hypersonic @feature (D-071 AI battle harness) AIBattle = <Iran difficulty> <Enemy difficulty>: instead, Iran and
//         the enemy are both skirmish AI players with their normal start and scripts on a stock map, the local player
//         only observes, the victory check runs, and the game quits when one side is defeated (result.txt, battle.csv).
//         Hypersonic @feature (AI team battle) Player = <PlayerTemplate> <team> [difficulty] [start] lines (with AIBattle):
//         up to 7 AI players (the local observer keeps slot 0), allied by team; the game quits when one team is left.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameType.h"
#include "Common/GameCommon.h"

#include <map>
#include <stdio.h>
#include <vector>

class Object;
class Player;

//-------------------------------------------------------------------------------------------------
class HypersonicScenario
{
public:

	HypersonicScenario();
	~HypersonicScenario();

	void startGame();					///< client: read the scenario file and start the skirmish (after the intro)
	void onGameStarted();			///< logic: end of GameLogic::startNewGame: players, scripts, setup commands
	void logicUpdate();				///< logic: end of each GameLogic::update: timed commands, log
	void clientUpdate();			///< client: start of each GameClient::update: UI, camera, screenshots, quit

	Bool isInstantLoad() const { return m_instantLoad; }
	Bool isCampaign() const { return m_campaignName.isNotEmpty(); } // Hypersonic @feature (D-244)
	Bool isAIBattle() const { return m_aiBattle; }	///< Hypersonic @feature (D-071 AI battle harness) a normal skirmish between two AIs
	const AsciiString &getMapName() const { return m_mapName; }

	// Hypersonic @feature (D-110) strike log: each AI munition launch at an object and whether that object dies within
	// STRIKE_OUTCOME_SECONDS (events.txt). MunitionPicker = Legacy runs the D-078 target choice for comparison.
	Bool isLegacyMunitionPicker() const { return m_legacyMunitionPicker; }
	Bool isD122MunitionPicker() const { return m_d122MunitionPicker; }	///< Hypersonic @feature (D-189) MunitionPicker = D122
	void noteMunitionLaunch( const char *power, const Object *target );
	// D-244 Phase 2: valid script command target/order, not confirmation that a power launched.
	void noteChallengePowerTarget( const AsciiString &team, const AsciiString &command, const Object *source, const Object *target );

	// Hypersonic @feature (D-126) IranTeamTarget = Legacy: the Iran AIs choose their enemy and approach path the stock way
	// (comparison runs). noteAI writes an AI line to events.txt; sideLabel names a player as the battle files do.
	Bool isLegacyIranTeamTarget() const { return m_legacyIranTeamTarget; }
	Bool isStockTeamUnits() const { return m_stockTeamUnits; }	///< Hypersonic @feature (D-189 optional units) IranTeamUnits = Stock
	void noteAI( const char *format, ... );

	// Hypersonic @feature (D-148 test) every cook-off roll of a munition (D-142: a death module's "<weapon> None" pick, or
	// a launcher's missile): "COOK-OFF" / "no cook-off" in events.txt and as an on-screen message.
	void noteCookOff( const char *munition, const Coord3D *pos, Bool cookedOff );
	const char *sideLabel( const Player *player ) const;

private:

	struct Command
	{
		Int												m_frame;		///< -1: setup (at game start)
		Int												m_line;			///< scenario file line, for messages
		std::vector<AsciiString>	m_args;			///< verb first
	};

	struct CameraKey
	{
		Int					m_frame;
		Bool				m_follow;			///< look at the object m_tag instead of m_x, m_y
		AsciiString	m_tag;
		Real				m_x, m_y;
		Real				m_height;
		Real				m_pitch;			///< degrees
		Real				m_yaw;				///< degrees
	};

	/// Hypersonic @feature free camera (D-272): CameraEye = <s> <x y | @tag> <h> <x y | @tag | ahead> <h or distance> [fov], or <s> off
	struct EyeKey
	{
		Int					m_frame;
		Bool				m_off;
		Bool				m_eyeFollow;
		AsciiString	m_eyeTag;
		Real				m_eyeX, m_eyeY, m_eyeH;		///< m_eyeH above the ground, or above the followed object
		Int					m_lookMode;							///< 0 = point, 1 = object, 2 = ahead of the eye's object
		AsciiString	m_lookTag;
		Real				m_lookX, m_lookY, m_lookH;	///< m_lookH above the ground or object; ahead: the distance
		Real				m_fov;									///< degrees, 0 = the game's lens
		Coord3D			m_lastEye, m_lastLook;		///< where it resolved last: a followed object that is gone stays there
		Bool				m_haveEye, m_haveLook;
	};

	struct LogRequest
	{
		AsciiString	m_what;				///< object tag, or else a template name (every object of it)
		Int					m_everyFrames;
	};

	struct Tracked
	{
		Tracked() : m_lastFrame( -1 ), m_reported( FALSE ) { m_lastPos.zero(); }
		AsciiString	m_label;
		Coord3D			m_lastPos;
		Int					m_lastFrame;		///< -1: not logged yet
		Bool				m_reported;			///< its death or removal is in the log
	};

	Bool parseFile( const AsciiString &path );
	void parseLine( std::vector<AsciiString> &tokens, Int line );
	void runCommand( const Command &cmd );
	void writeLog();
	void observeBaseConstruction(); // Hypersonic @feature (D-244 fix 1): real foundation starts, every logic frame
	void observeChallenge(); // Hypersonic @feature (D-244 Phase 2): passive stage, raid and target trace
	void logObject( Object *obj, const AsciiString &label );
	void event( const char *format, ... );

	Player *findPlayer( const AsciiString &name ) const;
	Object *findTagged( const AsciiString &tag ) const;
	Bool readPos( const Command &cmd, size_t index, Coord3D *pos ) const;
	Object *placeObject( const AsciiString &tag, const AsciiString &owner, const AsciiString &templateName,
		const Coord3D &pos, Real angleDegrees );
	void applyCamera();
	void applyFreeCamera();												///< Hypersonic @feature (D-272)
	Bool resolveEyeKey( EyeKey &k, Coord3D &eye, Coord3D &look );	///< FALSE: its object has not existed yet
	void takeScreenshot();
	void finish( const char *winner = nullptr );

	// Hypersonic @feature (D-071 AI battle harness)
	struct BattleStats
	{
		Int m_units;
		Int m_buildings;
	};
	void countObjects( std::vector<BattleStats> &stats ) const;	///< one per side (m_sides)
	Int sideOf( const Player *player ) const;					///< index in m_sides, -1: none
	void writeBattleLine();
	void writeResult( const char *winner );
	void applyLowDetail();

	// Hypersonic @feature (AI team battle) Player = <PlayerTemplate> <team> [difficulty] [start]
	struct BattlePlayer
	{
		AsciiString	m_faction;
		Int					m_team;							///< 1-based as in the file
		Int					m_slotState;
		Int					m_start;						///< 0-based start position, -1: random
		Player*			m_player;
		Int					m_eliminatedFrame;	///< -1: still in the game
		Int					m_eliminationOrder;	///< 1: first out
	};
	Bool isTeamBattle() const { return !m_teamPlayers.empty(); }
	void updateTeamBattle( Int frame );
	void writeTeamResult( const char *winner );

	// Hypersonic @feature (D-110) strike log
	struct StrikeRecord
	{
		ObjectID		m_target;
		AsciiString	m_targetName;
		AsciiString	m_power;
		Int					m_frame;
		Bool				m_resolved;
	};
	void updateStrikes( Bool atEnd );

	// Hypersonic @feature (D-112) production log (production.csv): objects each side got per template, upgrades it
	// completed, and how busy each kind of production building was (sampled once a game second)
	void sampleProduction();
	void writeProduction();
	void writeAIState();	///< Hypersonic @feature (D-190) AIState = <seconds>: Iran AI flags, counters, teams (events.txt)
	void writeAIHelp();	///< Hypersonic (2026-10-20) AI HELP: each Iran AI's ally-help flags and helping units (events.txt)

	AsciiString									m_scenarioFile;
	AsciiString									m_outputDir;
	AsciiString									m_mapName;
	AsciiString m_campaignName; // Hypersonic @feature (D-244)
	GameDifficulty m_campaignDifficulty;
	AsciiString									m_enemyFaction;
	Int m_iranColor; ///< IranColor: multiplayer colour index, default 0 (Gold)
	Int m_enemyColor; ///< EnemyColor: multiplayer colour index, default 1 (Red)
	Int													m_seed;
	Int													m_durationFrames;
	Bool												m_instantLoad;
	Bool												m_hideUI;
	Int													m_speedFps;					///< Speed: render/logic frame limit, 0 = none (as fast as possible)
	// Hypersonic @feature (D-071 AI battle harness)
	Bool												m_aiBattle;					///< AIBattle: both sides are skirmish AIs, the local player observes
	Int													m_iranSlotState;		///< SlotState of the Iran AI (SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI)
	Int													m_enemySlotState;
	Int													m_lowDetail;				///< LowDetail: -1 not given (AI battle: yes), 0 no, 1 yes
	Int													m_drawEvery;				///< DrawEvery: render one frame in N (1: every frame)
	Bool												m_sky;					///< Hypersonic @feature (D-272) Sky = Yes
	Int													m_cameraFreeFrame;	///< Hypersonic @feature CameraFree: the Camera keys stop here (-1: never)
	FILE*												m_battle;						///< battle.csv
	Bool												m_legacyMunitionPicker;	///< Hypersonic @feature (D-110) MunitionPicker = Legacy
	Bool												m_d122MunitionPicker;		///< Hypersonic @feature (D-189) MunitionPicker = D122
	Bool												m_legacyIranTeamTarget;	///< Hypersonic @feature (D-126) IranTeamTarget = Legacy
	Bool												m_stockTeamUnits;		///< Hypersonic @feature (D-189 optional units) IranTeamUnits = Stock
	Int													m_aiStateFrames;		///< Hypersonic @feature (D-190) AIState: AI STATE line every n frames (0: off)
	std::vector<StrikeRecord>		m_strikes;					///< Hypersonic @feature (D-110)
	UnsignedInt									m_battleStartMs;		///< timeGetTime() at the first logic frame (real time of the game)
	// Hypersonic @feature (D-112) production log
	FILE*												m_production;				///< production.csv
	std::map<ObjectID, Bool>		m_seenObjects;			///< objects already counted in m_built
	std::vector< std::map<AsciiString, Int> >		m_built;					///< per side: objects per template that appeared
	std::vector< std::map<AsciiString, Bool> >	m_upgradesDone;	///< per side: player upgrades already reported
	std::vector< std::map<AsciiString, Int> >		m_busySamples;		///< per side, per production template: samples with a non-empty queue
	std::vector< std::map<AsciiString, Int> >		m_allSamples;		///< per side, per production template: samples (one per building per second)
	// Hypersonic @feature (AI team battle) the sides the battle files report: Iran, Enemy (1v1) or each Player line
	std::vector<Player*>				m_sides;
	std::vector<AsciiString>		m_sideNames;				///< "iran", "enemy" / "p1".."p7"
	std::vector<BattlePlayer>		m_teamPlayers;			///< Player lines (empty: the 1v1 battle)
	Int													m_eliminations;			///< team battle: players out so far

	std::vector<Command>				m_commands;			///< in file order; timed ones run when their frame comes
	std::vector<EyeKey>				m_eyeKeys;			///< Hypersonic @feature (D-272) sorted by frame
	Bool										m_freeCameraOn;
	std::vector<CameraKey>			m_cameraKeys;		///< sorted by frame
	std::vector<Int>						m_shotFrames;		///< sorted, unique
	std::vector<LogRequest>			m_logRequests;
	std::map<AsciiString, ObjectID>	m_tags;
	std::map<ObjectID, Tracked>	m_tracked;
	// Only the harness watches these areas; never changes players, AI or construction.
	struct BaseWatch
	{
		BaseWatch() : m_player( nullptr ), m_x( 0 ), m_y( 0 ), m_radius( 0 ), m_starts( 0 ) {}
		Player *m_player;
		AsciiString m_owner, m_label;
		Real m_x, m_y, m_radius;
		Int m_starts;
		std::map<ObjectID, Bool> m_seen;
	};
	std::vector<BaseWatch> m_baseWatches;
	std::map<AsciiString, Int> m_challengeValues;
	std::map<ObjectID, ObjectID> m_challengeTargets;

	Player*											m_iran;
	Player*											m_enemy;
	Bool												m_started;					///< onGameStarted has run
	Bool												m_clientStarted;		///< first client update in the game has run
	Bool												m_finished;
	size_t											m_nextCommand;
	size_t											m_nextShot;
	Int													m_shotCount;
	FILE*												m_log;							///< log.csv
	UnsignedInt									m_logStartMs;				///< timeGetTime() when log.csv was opened (wallms column)
	FILE*												m_events;						///< events.txt
};

extern HypersonicScenario *TheHypersonicScenario;	///< null unless started with -hypersonicScenario

/// launch facilities load launchers and TEL bays in one frame (scenario InstantLoad)
inline Bool isHypersonicScenarioInstantLoad() { return TheHypersonicScenario != nullptr && TheHypersonicScenario->isInstantLoad(); }
