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

// AISkirmishPlayer.h
// Computerized opponent
// Author: Michael S. Booth, January 2002

#pragma once

#include "Common/GameMemory.h"
#include "GameLogic/AIPlayer.h"

class BuildListInfo;
class SpecialPowerTemplate;


/**
 * The computer-controlled opponent.
 */
class AISkirmishPlayer : public AIPlayer
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AISkirmishPlayer, "AISkirmishPlayer"  )

public:	 // AISkirmish specific methods.

	AISkirmishPlayer( Player *p );							///< constructor
	// Hypersonic @feature (D-172) minRangeFrom/minRange: only aim at least minRange (2D) from that point
	virtual Bool computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius, const Coord3D *minRangeFrom = nullptr, Real minRange = 0.0f) override; ///< Calculates best pos for weapon given radius.

public:	// AIPlayer interface methods.

	virtual void update() override;											///< simulates the behavior of a player

	virtual void newMap() override;											///< New map loaded call.

	/// Invoked when a unit I am training comes into existence
	virtual void onUnitProduced( Object *factory, Object *unit ) override;

	virtual void buildSpecificAITeam(TeamPrototype *teamProto, Bool priorityBuild) override; ///< Builds this team immediately.

	virtual void buildSpecificAIBuilding(const AsciiString &thingName) override; ///< Builds this building as soon as possible.

	virtual void buildAIBaseDefense(Bool flank) override; ///< Builds base defense on front or flank of base.

	virtual void buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank) override; ///< Builds base defense on front or flank of base.

	virtual void recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius) override; ///< Builds this team immediately.

	virtual Bool isSkirmishAI() override {return true;}

	virtual Bool checkBridges(Object *unit, Waypoint *way) override;

	virtual Player *getAiEnemy() override;	///< Solo AI attacks based on scripting.  Only skirmish auto-acquires an enemy at this point.  jba.

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

	virtual void doBaseBuilding() override;
	virtual void checkReadyTeams() override;
	virtual void checkQueuedTeams() override;
	virtual void doTeamBuilding() override;
	virtual Object *findDozer(const Coord3D *pos) override;
	virtual void queueDozer() override;

protected:

	virtual Bool selectTeamToBuild() override;			///< determine the next team to build
	virtual Bool selectTeamToReinforce( Int minPriority ) override;			///< determine the next team to reinforce
	virtual Bool startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName) override;	///< find a production building that can handle the order, and start building

	virtual Bool isAGoodIdeaToBuildTeam( TeamPrototype *proto ) override;		///< return true if team should be built
	virtual void processBaseBuilding() override;		///< do base-building behaviors
	virtual void processTeamBuilding() override;		///< do team-building behaviors

protected:
	void adjustBuildList(BuildListInfo *list);
	Int getMyEnemyPlayerIndex();
	void acquireEnemy();
	// Hypersonic @feature (D-126, D-128) the Iran AIs of a team share one enemy: the closest
	Bool isIranTeamTargeting() const;
	Player *getIranTeamLeader();
	void acquireIranEnemy();
	// Hypersonic @bugfix (D-174) Iran AI front/flank defenses stay on the arc toward the attack path
	void buildIranBaseDefenseStructure(const ThingTemplate *tTemplate, const AsciiString &thingName, Bool flank);
	// D-174: those orders are rebuilt when lost: a fresh order has IRAN_DEFENSE_REBUILDS, one that was built has fewer
	enum { IRAN_DEFENSE_REBUILDS = 1000 };
	static Bool isIranDefenseRebuild(BuildListInfo *info);
	// D-174: a lost one not rebuilt for a long time is given up (stops blocking new orders)
	Bool releaseIranStaleRebuild(BuildListInfo *info);
	static Bool isIranFreshOrder(BuildListInfo *info);
	void stampIranOrder(BuildListInfo *info);
	// Hypersonic @bugfix (D-174) front defenses go on the approach between the bases, just outside the base edge
	Bool getIranEnemyBasePosition(Coord3D *pos);
	Real getIranBaseFrontEdge(const Coord2D &dir);
	Bool buildIranFrontDefense(const ThingTemplate *tTemplate, const AsciiString &thingName, const Coord2D &dir,
		Real enemyDistance);
	// Hypersonic @bugfix (D-174) buildings ordered through the flank/front scripts go behind the front defense line
	Real getIranFrontDefenseLine(const Coord2D &enemyDir, Real enemyDistance, Int *count);
	Bool buildIranBuildingBehindFront(const ThingTemplate *tTemplate, const AsciiString &thingName, const Coord2D &dir,
		const Coord2D &enemyDir, Real enemyDistance, const char *orderLabel);
	// Hypersonic @bugfix (D-225) the army's ground road to the enemy base stays free of new defenses and buildings
	void findIranRoad(const Coord3D *enemyPos);
	void prepareIranRoad();
	Bool isOnIranRoad(const Coord3D *pos, Real structureRadius) const;
	enum { IRAN_ROAD_MAX_POINTS = 128 };
	Real m_iranRoadX[IRAN_ROAD_MAX_POINTS];	// not saved: found again for every order
	Real m_iranRoadY[IRAN_ROAD_MAX_POINTS];
	Int m_iranRoadCount;					// -1: this order's road not looked for yet
	Coord3D m_iranRoadEnemy;

protected:
	Int m_curFrontBaseDefense; // First is 0.
	Int m_curFlankBaseDefense; // First is 0.
	Real m_curFrontLeftDefenseAngle;
	Real m_curFrontRightDefenseAngle;
	Real m_curLeftFlankLeftDefenseAngle;
	Real m_curLeftFlankRightDefenseAngle;
	Real m_curRightFlankLeftDefenseAngle;
	Real m_curRightFlankRightDefenseAngle;

	UnsignedInt m_frameToCheckEnemy;
	Player			*m_currentEnemy;

};
