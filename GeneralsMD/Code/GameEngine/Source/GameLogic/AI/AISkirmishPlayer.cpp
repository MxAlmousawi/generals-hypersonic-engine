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

// AISkirmishPlayer.cpp
// Computerized opponent
// Author: Michael S. Booth, January 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/HypersonicScenario.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/TerrainVisual.h"


#define USE_DOZER 1



///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

AISkirmishPlayer::AISkirmishPlayer( Player *p ) :	AIPlayer(p),
m_curFlankBaseDefense(0),
m_curFrontBaseDefense(0),
m_curFrontLeftDefenseAngle(0),
m_curFrontRightDefenseAngle(0),
m_curLeftFlankLeftDefenseAngle(0),
m_curLeftFlankRightDefenseAngle(0),
m_curRightFlankLeftDefenseAngle(0),
m_curRightFlankRightDefenseAngle(0),
m_frameToCheckEnemy(0),
m_currentEnemy(nullptr)

{
	m_frameLastBuildingBuilt = TheGameLogic->getFrame();
	m_iranRoadCount = 0;
	p->setCanBuildUnits(true); // turn on ai production by default.
}

AISkirmishPlayer::~AISkirmishPlayer()
{
	clearTeamsInQueue();
}


/**
 * Build our base.
 */
void AISkirmishPlayer::processBaseBuilding()
{
	//
	// Refresh base buildings. Scan through list, if a building is missing,
	// rebuild it, unless it's rebuild count is zero.
	//
	if (m_readyToBuildStructure)
	{
		const ThingTemplate *bldgPlan=nullptr;
		BuildListInfo	*bldgInfo = nullptr;
		Bool isPriority = false;
		Object *bldg = nullptr;
		const ThingTemplate *powerPlan=nullptr;
		BuildListInfo	*powerInfo = nullptr;
		Bool isUnderPowered = !m_player->getEnergy()->hasSufficientPower();
		Bool powerUnderConstruction = false;
		for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *curPlan = TheThingFactory->findTemplate( name );
			if (!curPlan) {
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
				continue;
			}
			bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			// check for hole.
			if (info->getObjectID() != INVALID_ID) {
				// used to have a building.
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
				if (bldg==nullptr) {
					// got destroyed.
					ObjectID priorID;
					priorID = info->getObjectID();
					info->setObjectID(INVALID_ID);
					info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					// Scan for a GLA hole.	KINDOF_REBUILD_HOLE
					Object *obj;
					for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() ) {
						if (!obj->isKindOf(KINDOF_REBUILD_HOLE)) continue;
						RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
						if( rhbi ) {
							ObjectID spawnerID = rhbi->getSpawnerID();
							if (priorID == spawnerID) {
								DEBUG_LOG(("AI Found hole to rebuild %s", curPlan->getName().str()));
								info->setObjectID(obj->getID());
							}
						}
 					}
				}	else {
					if (bldg->getControllingPlayer() == m_player) {
						// Check for built or dozer missing.
						if( bldg->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
						{
							if (bldg->isKindOf(KINDOF_FS_POWER) && !bldg->isKindOf(KINDOF_CASH_GENERATOR))
							{
								powerUnderConstruction = true;
							}
							// make sure dozer is working on him.
							ObjectID builder = bldg->getBuilderID();
							Object* myDozer = TheGameLogic->findObjectByID(builder);

              if (myDozer && ( myDozer->getControllingPlayer() != m_player || myDozer->isDisabledByType( DISABLED_UNMANNED ) ) )
              {//I don't expect this dozer to work well with me.
                myDozer = nullptr;
                bldg->setBuilder( nullptr );
              }

							if (myDozer==nullptr) {
								DEBUG_LOG(("AI's Dozer got killed (or captured).  Find another dozer."));
								queueDozer();
 								myDozer = findDozer(bldg->getPosition());
								if (myDozer==nullptr || myDozer->getAI()==nullptr) {
									continue;
								}
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}	else {
								// make sure he is building.
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}
						}
					} else {
						// oops, got captured.
						info->setObjectID(INVALID_ID);
						info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					}
				}
			}
			if (info->getObjectID()==INVALID_ID && info->getObjectTimestamp()>0) {
				// this object was built at some time, and got destroyed at or near objectTimestamp.
				// Wait a few seconds before initiating a rebuild.
				if (info->getObjectTimestamp()+TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame()) {
					continue;
				}	else {
					DEBUG_LOG(("Enabling rebuild for %s", info->getTemplateName().str()));
					// Hypersonic @bugfix (D-174) a lost Iran defense keeps the frame it was lost at, a fresh Iran defense
					// order its creation stamp (saved with the build list), so one that never gets built can be given up
					// (releaseIranStaleRebuild)
					if (!(m_player->getSide() == "Iran" && (isIranDefenseRebuild(info) || isIranFreshOrder(info))))
						info->setObjectTimestamp(0); // ready to build.
				}
			}
			if (bldg) {
				continue; // already built.
			}
			// Make sure it is safe to build here.
			if (!isLocationSafe(info->getLocation(), curPlan)) {
				continue;
			}
			// Hypersonic @feature (D-174) a lost Iran defense is rebuilt once it can be paid for; until then it does
			// not hold up the rest of the base building the way a waiting priority order does
			if (m_player->getSide() == "Iran" && isIranDefenseRebuild(info) && m_player->getMoney()->countMoney() < (UnsignedInt)curPlan->calcCostToBuild(m_player)) {
				continue;
			}
			if (info->isPriorityBuild()) {
				// Always take priority build, unless we already have priority build.
				if (!isPriority) {
					bldgPlan = curPlan;
					bldgInfo = info;
					isPriority = true;
				}
			}
			if (curPlan->isKindOf(KINDOF_FS_POWER)) {
				if (powerPlan==nullptr && !curPlan->isKindOf(KINDOF_CASH_GENERATOR)) {
					if (isUnderPowered || info->isAutomaticBuild()) {
						powerPlan = curPlan;
						powerInfo = info;
					}
				}
			}
			if (!info->isAutomaticBuild()) {
				continue; // marked to not build automatically.
			}
			Object *dozer = findDozer(info->getLocation());
			if (dozer==nullptr) {
				if (isUnderPowered) {
					queueDozer();
				}
				continue;
			}
			if (TheBuildAssistant->canMakeUnit(dozer, bldgPlan)!=CANMAKE_OK) {
				if (info->isBuildable()) {
					AsciiString bldgName = info->getTemplateName();
					bldgName.concat(" - Dozer unable to build - money or technology missing.");
					TheScriptEngine->AppendDebugMessage(bldgName, false);
				}
				continue;
			}
			// check if this building has any "rebuilds" left
			if (info->isBuildable())
			{
				if (bldgPlan == nullptr) {
					bldgPlan = curPlan;
					bldgInfo = info;
				}
			}
		}
		if (powerInfo && powerPlan && !powerPlan->isEquivalentTo(bldgPlan)) {
			if (!powerUnderConstruction) {
				bldgPlan = powerPlan;
				bldgInfo = powerInfo;
				DEBUG_LOG(("Forcing build of power plant."));
			}
		}
		if (bldgPlan && bldgInfo) {
#ifdef USE_DOZER
			// dozer-construct the building
			bldg = buildStructureWithDozer(bldgPlan, bldgInfo);
			// store the object with the build order
			if (bldg)
			{
				bldgInfo->setObjectID( bldg->getID() );
				bldgInfo->decrementNumRebuilds();

				m_readyToBuildStructure = false;
				m_structureTimer = TheAI->getAiData()->m_structureSeconds*LOGICFRAMES_PER_SECOND;
				if (m_player->getMoney()->countMoney() < TheAI->getAiData()->m_resourcesPoor) {
					m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresPoorMod;
				}	else if (m_player->getMoney()->countMoney() > TheAI->getAiData()->m_resourcesWealthy) {
					m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresWealthyMod;
				}
				m_frameLastBuildingBuilt = TheGameLogic->getFrame();
				// only build one building per delay loop
			}

#else
			// force delay between rebuilds
			Int framesToBuild = bldgPlan->calcTimeToBuild(m_player);
			if (TheGameLogic->getFrame() - m_frameLastBuildingBuilt < framesToBuild)
			{
				m_buildDelay = framesToBuild - (TheGameLogic->getFrame() - m_frameLastBuildingBuilt);
				return;
			}	else {
				// building is missing, (re)build it
				// deduct money to build, if we have it
				Int cost = bldgPlan->calcCostToBuild( m_player );
				if (m_player->getMoney()->countMoney() >= cost)
				{
					// we have the money, deduct it
					m_player->getMoney()->withdraw( cost );

					// inst-construct the building
					bldg = buildStructureNow(bldgPlan, bldgInfo);
					// store the object with the build order
					if (bldg)
					{
						bldgInfo->setObjectID( bldg->getID() );
						bldgInfo->decrementNumRebuilds();

						m_readyToBuildStructure = false;
						m_structureTimer = TheAI->getAiData()->m_structureSeconds*LOGICFRAMES_PER_SECOND;
						if (m_player->getMoney()->countMoney() < TheAI->getAiData()->m_resourcesPoor) {
							m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresPoorMod;
						}	else if (m_player->getMoney()->countMoney() > TheAI->getAiData()->m_resourcesWealthy) {
							m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresWealthyMod;
						}
						m_frameLastBuildingBuilt = TheGameLogic->getFrame();
					}
				}
			}
#endif
		}
	}
}

/**
 * Invoked when a unit I am training comes into existence
 */
void AISkirmishPlayer::onUnitProduced( Object *factory, Object *unit )
{
	AIPlayer::onUnitProduced(factory, unit);
}

/**
 * Search the computer player's buildings for one that can build the given request
 * and start training the unit.
 * If busyOK is true, it will queue a unit even if one is building.  This lets
 * script invoked teams "push" to the front of the queue.
 */
Bool AISkirmishPlayer::startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName)
{
	Object *factory = findFactory(order->m_thing, busyOK);
	if( factory )
	{
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu && pu->queueCreateUnit( order->m_thing, pu->requestUniqueUnitID() )) {
			order->m_factoryID = factory->getID();
			if (TheGlobalData->m_debugAI) {
				AsciiString teamStr = "Queuing ";
				teamStr.concat(order->m_thing->getName());
				teamStr.concat(" for ");
				teamStr.concat(teamName);
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			}
			return true;
		}
	}

	return FALSE;

}


/**
 * Check if this team is buildable, doesn't exceed maximum limits, meets conditions, and isn't under construction.
 */
Bool AISkirmishPlayer::isAGoodIdeaToBuildTeam( TeamPrototype *proto )
{
	// Check condition.
	if (!proto->evaluateProductionCondition()) {
		return false;
	}
	// check build limit
	if (proto->countTeamInstances() >= proto->getTemplateInfo()->m_maxInstances){
		if (TheGlobalData->m_debugAI) {
			AsciiString str;
			str.format("Team %s not chosen - %d already exist.", proto->getName().str(), proto->countTeamInstances());
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;	// Max already built.
	}

	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		if (team->m_team->getPrototype() == proto) {
			return false; // currently building one of these.
		}
	}
	Bool needMoney;
	if (!isPossibleToBuildTeam( proto, true, needMoney)) {
		if (TheGlobalData->m_debugAI) {
			AsciiString str;
			if (needMoney) {
				str.format("Team %s not chosen - Not enough money.", proto->getName().str());
			} else {
				str.format("Team %s not chosen - Factory/tech missing or busy.", proto->getName().str());
			}
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;
	}
	return true;
}

/**
 * See if any existing teams need reinforcements, and have higher priority.
 */
Bool AISkirmishPlayer::selectTeamToReinforce( Int minPriority )
{
	return AIPlayer::selectTeamToReinforce(minPriority);
}

/**
 * Determine the next team to build.  Return true if one was selected.
 */
Bool AISkirmishPlayer::selectTeamToBuild()
{
	return AIPlayer::selectTeamToBuild();
}

/**
	Build a specific building.
	*/
void AISkirmishPlayer::buildSpecificAIBuilding(const AsciiString &thingName)
{
	//
	Bool found = false;
	Bool foundUnbuilt = false;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->getTemplateName()==thingName)
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
			if (!bldgPlan) {
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
				continue;
			}
			Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			found = true;
			if (bldg) {
				continue; // already built.
			}
			if (info->isPriorityBuild()) {
				continue; // already marked for priority build.
			}
			foundUnbuilt = true;
			info->markPriorityBuild();
			break;
		}
	}
	if (foundUnbuilt) {
		m_buildDelay = 0;
		AsciiString buildingStr = "Queueing building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' for construction.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}	else if (found) {
		AsciiString buildingStr = "Warning - all instances of building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' are already built or queued for build, not queueing.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}	else {
		AsciiString buildingStr = "Error - could not find building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' in the building template list.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}
}



/**
	Gets the player index of my enemy.
	*/
Int AISkirmishPlayer::getMyEnemyPlayerIndex() {
	Int playerNdx;
	if (m_currentEnemy) {
		return m_currentEnemy->getPlayerIndex();
	}
	// For now, return first human player, as there should only be one. jba
	for (playerNdx=0; playerNdx<ThePlayerList->getPlayerCount(); playerNdx++) {
		if (ThePlayerList->getNthPlayer(playerNdx)->getPlayerType() == PLAYER_HUMAN) {
			break;
		}
	}
	return playerNdx;
}

/**
	Get the AI's enemy.  Recalc if it has been a while (5 seconds.)
*/
void AISkirmishPlayer::acquireEnemy()
{
	// Hypersonic @feature (D-126, D-128) Iran AIs: the closest enemy, shared by the team (stock AIs unchanged)
	if( isIranTeamTargeting() )
	{
		acquireIranEnemy();
		return;
	}

	Player *bestEnemy = nullptr;
	Real bestDistanceSqr = HUGE_DIST*HUGE_DIST;

	if (m_currentEnemy) {
		Bool inBadShape = !m_currentEnemy->hasAnyUnits() || !m_currentEnemy->hasAnyBuildFacility();
		if (!inBadShape) return;
	}

	// look for the closest enemy.
	Int i;
	for (i=0; i<ThePlayerList->getPlayerCount(); i++) {
		Player *curPlayer = ThePlayerList->getNthPlayer(i);
		if (m_player->getRelationship(curPlayer->getDefaultTeam()) == ENEMIES) {
			if (curPlayer->hasAnyObjects()==false) continue; // not much of an enemy.
			// ok, we got an enemy;
			// If a player is out of units, or out of build facilities, we can lower his priority.
			Bool inBadShape = !curPlayer->hasAnyUnits() || !curPlayer->hasAnyBuildFacility();

			Coord3D enemyPos = m_baseCenter;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, i);
			enemyPos.x = bounds.lo.x + bounds.width()/2;
			enemyPos.y = bounds.lo.y + bounds.height()/2;
			Real curDistSqr = sqr(enemyPos.x-m_baseCenter.x) + sqr(enemyPos.y-m_baseCenter.y);

			//Fudge for in bad shape.	 If an enemy is crippled, concentrate on the other ones.
			if (inBadShape) {
				curDistSqr = HUGE_DIST*HUGE_DIST*0.5f;
			}
			// See if other ai's are attacking this target.
			// We don't want the ai's to gang up on one enemy.
			Int k;
			for (k=0; k<ThePlayerList->getPlayerCount(); k++) {
				if (k==i) continue;  // don't count self.
				Player *somePlayer = ThePlayerList->getNthPlayer(k);
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==curPlayer)) {
					// Some ai is already targeting this guy.  Add a distance penalty.
					curDistSqr += (500*500);
				}
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==m_player)) {
					// he is attacking me.  So I will (gently) prefer to attack him.
					curDistSqr -= (25*25);
					if (curDistSqr<0) curDistSqr = 0;
				}
			}

			// Ai enemy - will take if we don't get a better offer.
			if (curDistSqr<bestDistanceSqr) {
				bestEnemy = curPlayer;
				bestDistanceSqr = curDistSqr;
			}
		}
	}
	if (bestEnemy!=nullptr && (bestEnemy!=m_currentEnemy)) {
		m_currentEnemy = bestEnemy;
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" acquiring target enemy player: ");
		msg.concat(TheNameKeyGenerator->keyToName(m_currentEnemy->getPlayerNameKey()));
		TheScriptEngine->AppendDebugMessage( msg, false);
	}
}



/**
	Get the AI's enemy.  Recalc if it has been a while (20 seconds.)
*/
//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-126, D-128) Iran AI team targeting (owner: mass into one overwhelming push, keep pushing until
// the enemy is destroyed, then the next). Only players whose side is Iran; stock AIs keep the stock choice.
// The team leader (the lowest-index Iran skirmish AI with objects among the mutual allies) picks; the other Iran AIs of
// the team take its enemy. D-128 (owner: "it shouldn't target the weakest, it should target the closest"): the enemy
// whose base (its first Command Center in the object list, else the centre of its structures) is closest to the
// leader's base; ties: the lower player index. The current enemy is kept until it is eliminated (no objects left), then
// the next closest. Re-evaluated every 5 s (getAiEnemy). Synced logic: object list and player order only.
//-------------------------------------------------------------------------------------------------
static Int s_iranEnemyDelegation = 0;	///< recursion guard: a leader asked for its enemy decides itself

static Bool isIranSkirmishAI( Player *player )
{
	return player != nullptr && player->isSkirmishAIPlayer() && player->getSide() == "Iran";
}

Bool AISkirmishPlayer::isIranTeamTargeting() const
{
	return m_player != nullptr && m_player->getSide() == "Iran"
		&& !( TheHypersonicScenario != nullptr && TheHypersonicScenario->isLegacyIranTeamTarget() );
}

Player *AISkirmishPlayer::getIranTeamLeader()
{
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == m_player )
			return m_player;
		if( !isIranSkirmishAI( p ) || p->getDefaultTeam() == nullptr || m_player->getDefaultTeam() == nullptr
			|| !p->hasAnyObjects() )
			continue;
		if( m_player->getRelationship( p->getDefaultTeam() ) == ALLIES && p->getRelationship( m_player->getDefaultTeam() ) == ALLIES )
			return p;
	}
	return m_player;
}

void AISkirmishPlayer::acquireIranEnemy()
{
	Int count = ThePlayerList->getPlayerCount();
	if( count > MAX_PLAYER_COUNT )
		count = MAX_PLAYER_COUNT;
	Bool hostile[MAX_PLAYER_COUNT];
	Bool hasCenter[MAX_PLAYER_COUNT];
	Coord2D basePos[MAX_PLAYER_COUNT];
	Int i;
	for( i = 0; i < count; ++i )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		hostile[i] = p != nullptr && p != m_player && p->getDefaultTeam() != nullptr
			&& m_player->getRelationship( p->getDefaultTeam() ) == ENEMIES && p->hasAnyObjects();
		hasCenter[i] = false;
		basePos[i].x = basePos[i].y = 0.0f;
	}
	// each enemy's base: its first Command Center in the object list
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isKindOf( KINDOF_COMMANDCENTER ) || obj->isEffectivelyDead() )
			continue;
		Player *owner = obj->getControllingPlayer();
		if( owner == nullptr )
			continue;
		const Int index = owner->getPlayerIndex();
		if( index < 0 || index >= count || !hostile[index] || hasCenter[index] || ThePlayerList->getNthPlayer( index ) != owner )
			continue;
		hasCenter[index] = true;
		basePos[index].x = obj->getPosition()->x;
		basePos[index].y = obj->getPosition()->y;
	}
	// our base: the leader's base centre
	Coord2D home;
	home.x = m_baseCenter.x;
	home.y = m_baseCenter.y;
	if( !m_baseCenterSet )
	{
		Region2D bounds;
		getPlayerStructureBounds( &bounds, m_player->getPlayerIndex(), true );
		home.x = bounds.lo.x + bounds.width()/2;
		home.y = bounds.lo.y + bounds.height()/2;
	}
	Real dist[MAX_PLAYER_COUNT];
	Int best = -1;
	for( i = 0; i < count; ++i )
	{
		dist[i] = 0.0f;
		if( !hostile[i] )
			continue;
		if( !hasCenter[i] )
		{
			// no Command Center: the centre of its structures (stock acquireEnemy's base position)
			Region2D bounds;
			getPlayerStructureBounds( &bounds, i, true );
			basePos[i].x = bounds.lo.x + bounds.width()/2;
			basePos[i].y = bounds.lo.y + bounds.height()/2;
		}
		const Real dx = basePos[i].x - home.x;
		const Real dy = basePos[i].y - home.y;
		dist[i] = dx*dx + dy*dy;	// squared: compared only
		if( best < 0 || dist[i] < dist[best] )	// ties: the lower index
			best = i;
	}
	if( best < 0 )
		return;	// no enemy left: keep what we have
	const char *reason = m_currentEnemy == nullptr ? "first, closest" : "closest";
	const Int current = m_currentEnemy != nullptr ? m_currentEnemy->getPlayerIndex() : -1;
	if( current >= 0 && current < count && ThePlayerList->getNthPlayer( current ) == m_currentEnemy )
	{
		if( hostile[current] )
			return;	// keep the current enemy until it is eliminated
		reason = "eliminated; next closest";
	}
	Player *choice = ThePlayerList->getNthPlayer( best );
	if( choice == m_currentEnemy )
		return;
	m_currentEnemy = choice;
	if( TheHypersonicScenario != nullptr )
	{
		char list[512];
		list[0] = 0;
		for( i = 0; i < count; ++i )
		{
			if( !hostile[i] )
				continue;
			char one[64];
			snprintf( one, sizeof( one ), " %s=%.0f%s", TheHypersonicScenario->sideLabel( ThePlayerList->getNthPlayer( i ) ),
				(Real)sqrt( dist[i] ), hasCenter[i] ? "" : "(no CC)" );
			strlcat( list, one, sizeof( list ) );
		}
		TheHypersonicScenario->noteAI( "AI TARGET %s -> %s (%s; leader) distance:%s", TheHypersonicScenario->sideLabel( m_player ),
			TheHypersonicScenario->sideLabel( m_currentEnemy ), reason, list );
	}
}

Player *AISkirmishPlayer::getAiEnemy()
{
	// Hypersonic @feature (D-126) an Iran AI that is not its team's leader takes the leader's enemy
	if( isIranTeamTargeting() && s_iranEnemyDelegation == 0 )
	{
		Player *leader = getIranTeamLeader();
		if( leader != m_player )
		{
			++s_iranEnemyDelegation;
			Player *shared = leader->getCurrentEnemy();
			--s_iranEnemyDelegation;
			if( shared != nullptr && shared != m_player && shared->getDefaultTeam() != nullptr
				&& m_player->getRelationship( shared->getDefaultTeam() ) == ENEMIES && shared->hasAnyObjects() )
			{
				if( shared != m_currentEnemy )
				{
					m_currentEnemy = shared;
					if( TheHypersonicScenario != nullptr )
						TheHypersonicScenario->noteAI( "AI TARGET %s -> %s (leader %s's enemy)", TheHypersonicScenario->sideLabel( m_player ),
							TheHypersonicScenario->sideLabel( m_currentEnemy ), TheHypersonicScenario->sideLabel( leader ) );
				}
				return m_currentEnemy;
			}
		}
	}
	if (TheGameLogic->getFrame()>=m_frameToCheckEnemy) {
		m_frameToCheckEnemy = TheGameLogic->getFrame() + 5*LOGICFRAMES_PER_SECOND;
		acquireEnemy();
	}
	return m_currentEnemy;
}

/**
	Build base defense structures on the front or flank of the base.
*/
void AISkirmishPlayer::buildAIBaseDefense(Bool flank)
{
	const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo;
	AsciiString defenseTemplateName;
	while (resInfo) {
		if (resInfo->m_side == m_player->getSide()) {
			defenseTemplateName = resInfo->m_baseDefenseStructure1;
			break;
		}
		resInfo = resInfo->m_next;
	}
	if (resInfo) {
		buildAIBaseDefenseStructure(resInfo->m_baseDefenseStructure1, flank);
	}
}

/**
	Build base defense structures on the front or flank of the base.
	Base defenses are placed as follows:
	m_baseCenter and m_baseRadius are calculated on map load.
	Defenses are placed along the this circle.
	Front defenses (!flank) are placed starting at the "Center" approach path.
	The first front defense is placed towards th Center path.  Number 2 is placed
	to the left of #1, #3 is placed to the right of #1, #4 is placed to the left of
	#2 and so on.  So it looks like:

												#1
									 #2 			#3
					#6  #4								 #5	  #7
		  #8																	#9

	The flank base defenses cover the "Flank" approach, and the "Backdoor" approach.
	They alternate between these two, so the first flank defense covers flank, and the second
	covers backdoor, and continue to alternate.  They cover the approach using the same
	pattern as front above.
	John A.

	*/
void AISkirmishPlayer::buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank)
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate(thingName);
	if (tTemplate==nullptr) {
		DEBUG_CRASH(("Couldn't find base defense structure '%s' for side %s", thingName.str(), m_player->getSide().str()));
		return;
	}
	if (m_player->getSide() == "Iran")
	{
		buildIranBaseDefenseStructure(tTemplate, thingName, flank);
		return;
	}
	do {
		AsciiString pathLabel;
		if (flank) {
			if (m_curFlankBaseDefense&1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
			}	else {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
			}
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			if (flank) return;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex());
			goalPos.x = bounds.lo.x + bounds.width()/2;
			goalPos.y = bounds.lo.y + bounds.height()/2;
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		Real defenseDistance = m_baseRadius;
		defenseDistance += TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;
		offset.x *= defenseDistance;
		offset.y *= defenseDistance;

		Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		Real baseCircumference = 2*PI*defenseDistance;
		Real angleOffset = 2*PI*(structureRadius*4/baseCircumference);

		Int selector;
		Real angle;
		if (flank) {
			selector = m_curFlankBaseDefense>>1;
			if (m_curFlankBaseDefense&1) {
				if (selector&1) {
					m_curLeftFlankRightDefenseAngle -= angleOffset;
					angle = m_curLeftFlankRightDefenseAngle;
				}	else {
					angle = m_curLeftFlankLeftDefenseAngle;
					m_curLeftFlankLeftDefenseAngle += angleOffset;
				}
			}	else {
				if (selector&1) {
					m_curRightFlankRightDefenseAngle -= angleOffset;
					angle = m_curRightFlankRightDefenseAngle;
				}	else {
					angle = m_curRightFlankLeftDefenseAngle;
					m_curRightFlankLeftDefenseAngle += angleOffset;
				}
			}

		} else {
			selector = m_curFrontBaseDefense;
			if (selector&1) {
				m_curFrontRightDefenseAngle -= angleOffset;
				angle = m_curFrontRightDefenseAngle;
			}	else {
				angle = m_curFrontLeftDefenseAngle;
				m_curFrontLeftDefenseAngle += angleOffset;
			}
		}

		if (angle > PI/3) break;
		Real s = sin(angle);
		Real c = cos(angle);

// TheSuperHackers @info helmutbuhler 21/04/2025 This debug mutates the code to become CRC incompatible
#if defined(RTS_DEBUG) || !RETAIL_COMPATIBLE_CRC
		DEBUG_LOG(("buildAIBaseDefenseStructure -- Angle is %f sin %f, cos %f", 180*angle/PI, s, c));
		DEBUG_LOG(("buildAIBaseDefenseStructure -- Offset is %f  %f, Final Position is %f, %f",
			offset.x, offset.y,
			offset.x*c - offset.y*s,
			offset.y*c + offset.x*s
			));
#endif
		Coord3D buildPos = m_baseCenter;
		buildPos.x += offset.x*c - offset.y*s;
		buildPos.y += offset.y*c + offset.x*s;

		/* See if we can build there. */
		Bool canBuild;
		Real placeAngle = tTemplate->getPlacementViewAngle();
		canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
			BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		if (flank) {
			m_curFlankBaseDefense++;
		} else {
			m_curFrontBaseDefense++;
		}
		if (canBuild) {
			m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
			break;
		}
	}	while (true);

}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) the stock ring above fills the left front arc up to 60 degrees and then fails every
// other order, while the right side steps on round the base without a limit (later "front" defenses end up at the
// side and behind the base). For the Iran AI each flank order (and a front order that buildIranFrontDefense below
// cannot place) scans the arc from the attack path outwards, both sides up to 60 degrees, in up to IRAN_DEFENSE_ROWS
// rows further out: the first legal spot no pending build order holds wins. A lost one is rebuilt at its spot once it
// can be paid for. Front defenses: buildIranFrontDefense.
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::isIranDefenseRebuild(BuildListInfo *info)
{
	// 500-999: an IRAN_DEFENSE_REBUILDS order built at least once (stock and map entries never start that high)
	return info->isPriorityBuild() && info->getNumRebuilds() >= IRAN_DEFENSE_REBUILDS - 500
		&& info->getNumRebuilds() < IRAN_DEFENSE_REBUILDS;
}

Bool AISkirmishPlayer::isIranFreshOrder(BuildListInfo *info)
{
	// IRAN_DEFENSE_REBUILDS: placed by the Iran AI, never built (starting construction takes one rebuild off)
	return info->isPriorityBuild() && info->getNumRebuilds() == IRAN_DEFENSE_REBUILDS;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) stamps a fresh or moved Iran defense order with the current frame (saved with the build
// list). The stamp is set back by the stock rebuild delay: processBaseBuilding waits that long after a stamp (it was
// meant for lost buildings), so the order is not held up. Synced logic only.
//-------------------------------------------------------------------------------------------------
void AISkirmishPlayer::stampIranOrder(BuildListInfo *info)
{
	const UnsignedInt delayFrames = TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND;
	const UnsignedInt now = TheGameLogic->getFrame();
	info->setObjectTimestamp(now+1 > delayFrames + 1 ? now+1 - delayFrames : 1);	// has to be non-zero
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) a lost Iran defense whose rebuild has not been started, or a fresh Iran defense order
// whose construction has not been started, for IRAN_REBUILD_GIVE_UP_SECONDS (unsafe spot, no dozer, no money) is
// given up: the entry is emptied (no template, no rebuilds), so it no longer blocks or counts against new orders or
// holds its spot, and the next order picks a spot afresh (often the same one). The time counts from the entry's object
// timestamp (saved with the build list; processBaseBuilding keeps it for these entries) plus the stock rebuild delay:
// the lost frame, or the order's stamp (stampIranOrder). One without a stamp (0) starts counting now. Synced logic
// only. Returns true when released.
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::releaseIranStaleRebuild(BuildListInfo *info)
{
	static const UnsignedInt IRAN_REBUILD_GIVE_UP_SECONDS = 180;
	// a dead object's ID stays until processBaseBuilding's next pass, which stamps the lost frame; an order whose
	// construction has started has its object
	const Bool fresh = isIranFreshOrder(info);
	if (!(fresh || isIranDefenseRebuild(info)) || info->getObjectID() != INVALID_ID)
		return FALSE;
	if (info->getObjectTimestamp() == 0) {
		stampIranOrder(info);
		return FALSE;
	}
	const UnsignedInt delayFrames = TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (info->getObjectTimestamp() + delayFrames + IRAN_REBUILD_GIVE_UP_SECONDS*LOGICFRAMES_PER_SECOND > now)
		return FALSE;
	if (TheHypersonicScenario != nullptr)
		TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s at %.0f %.0f not built for %d s: given up",
			TheHypersonicScenario->sideLabel(m_player), info->getTemplateName().str(), fresh ? "order" : "lost one",
			info->getLocation()->x, info->getLocation()->y,
			(Int)((now - info->getObjectTimestamp() - delayFrames)/LOGICFRAMES_PER_SECOND));
	info->setTemplateName(AsciiString::TheEmptyString);
	info->setNumRebuilds(0);
	info->setObjectID(INVALID_ID);
	info->setObjectTimestamp(0);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) the enemy base the front faces: the current enemy (an Iran team AI takes its leader's
// enemy, D-126), its first Command Center in the object list, else the centre of its structures. Synced logic only.
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::getIranEnemyBasePosition(Coord3D *pos)
{
	Player *enemy = getAiEnemy();
	if (enemy == nullptr || enemy == m_player)
		return FALSE;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isKindOf(KINDOF_COMMANDCENTER) && !obj->isEffectivelyDead() && obj->getControllingPlayer() == enemy) {
			*pos = *obj->getPosition();
			return TRUE;
		}
	}
	Region2D bounds;
	getPlayerStructureBounds(&bounds, enemy->getPlayerIndex(), TRUE);
	if (bounds.lo.x == 0.0f && bounds.lo.y == 0.0f && bounds.hi.x == 0.0f && bounds.hi.y == 0.0f)
		return FALSE;
	pos->x = bounds.lo.x + bounds.width()/2;
	pos->y = bounds.lo.y + bounds.height()/2;
	pos->z = 0.0f;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-225) (owner, community maps: "Iran is blocking its own units": the front rows filled the only
// bridge or pass, the army stayed home and only drones and missiles attacked). The army's road: a ground path
// IRAN_ROAD_DIAMETER cells wide from an own ground unit in the base (a dozer first) to the enemy base, as the
// pathfinder finds it now (round standing structures, over bridges). The placements below keep new defenses and
// buildings off it (isOnIranRoad), so the road stays open; a new structure only bends the next road. No unit in the
// base or no path: no road, placement as before. Synced logic only.
//-------------------------------------------------------------------------------------------------
void AISkirmishPlayer::findIranRoad(const Coord3D *enemyPos)
{
	static const Int IRAN_ROAD_DIAMETER = 4;		// cells: a tank column
	static const Real GOAL_STEP = 150.0f;			// a goal inside a large structure moves toward this base
	m_iranRoadCount = 0;
	const Real ringDistance = m_baseRadius + TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;

	// the path starts on a free cell: the spot of an own ground unit near the base centre
	const Object *start = nullptr;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || obj->isEffectivelyDead() || obj->isKindOf(KINDOF_STRUCTURE)
			|| obj->isKindOf(KINDOF_AIRCRAFT) || obj->isAirborneTarget() || obj->getContainedBy() != nullptr
			|| !(obj->isKindOf(KINDOF_VEHICLE) || obj->isKindOf(KINDOF_INFANTRY)))
			continue;
		const Coord3D *at = obj->getPosition();
		if (sqr(at->x-m_baseCenter.x) + sqr(at->y-m_baseCenter.y) > sqr(ringDistance))
			continue;
		if (start == nullptr || obj->isKindOf(KINDOF_DOZER))
			start = obj;
		if (start->isKindOf(KINDOF_DOZER))
			break;
	}
	Coord2D back;
	back.x = m_baseCenter.x - enemyPos->x;
	back.y = m_baseCenter.y - enemyPos->y;
	const Real enemyDistance = back.length();
	if (enemyDistance < 1.0f)
		return;
	back.x /= enemyDistance;
	back.y /= enemyDistance;

	// no unit in the base (the dozer away at an expansion, the army out): start just outside the base edge toward the
	// enemy instead, stepping out until a path is found (a start inside a structure finds none)
	Coord2D toEnemy;
	toEnemy.x = -back.x;
	toEnemy.y = -back.y;
	const Real edge = start == nullptr ? getIranBaseFrontEdge(toEnemy) : 0.0f;
	Path *path = nullptr;
	Coord3D from;
	for (Int out = 0; out < (start == nullptr ? 3 : 1) && path == nullptr; ++out)
	{
		if (start != nullptr) {
			from = *start->getPosition();
		} else {
			from = m_baseCenter;
			from.x += toEnemy.x*(edge + 40.0f + out*GOAL_STEP*0.5f);
			from.y += toEnemy.y*(edge + 40.0f + out*GOAL_STEP*0.5f);
			from.z = TheTerrainLogic->getGroundHeight(from.x, from.y);
		}
		for (Int step = 0; step < 4 && path == nullptr && step*GOAL_STEP < enemyDistance*0.5f; ++step)
		{
			Coord3D goal = *enemyPos;
			goal.x += back.x*GOAL_STEP*step;
			goal.y += back.y*GOAL_STEP*step;
			goal.z = TheTerrainLogic->getGroundHeight(goal.x, goal.y);
			path = TheAI->pathfinder()->findGroundPath(&from, &goal, IRAN_ROAD_DIAMETER, FALSE);
		}
	}
	if (path == nullptr) {
		if (TheHypersonicScenario != nullptr)
			TheHypersonicScenario->noteAI("AI ROAD %s: no ground path from %.0f %.0f (%s) to the enemy base at %.0f %.0f, no road",
				TheHypersonicScenario->sideLabel(m_player), from.x, from.y, start ? "unit" : "base edge", enemyPos->x, enemyPos->y);
		return;
	}
	for (PathNode *node = path->getFirstNode(); node && m_iranRoadCount < IRAN_ROAD_MAX_POINTS; node = node->getNextOptimized())
	{
		m_iranRoadX[m_iranRoadCount] = node->getPosition()->x;
		m_iranRoadY[m_iranRoadCount] = node->getPosition()->y;
		++m_iranRoadCount;
	}
	deleteInstance(path);
	if (TheHypersonicScenario != nullptr && m_iranRoadCount > 0)
		TheHypersonicScenario->noteAI("AI ROAD %s: %d points from %.0f %.0f to %.0f %.0f", TheHypersonicScenario->sideLabel(m_player),
			m_iranRoadCount, m_iranRoadX[0], m_iranRoadY[0], m_iranRoadX[m_iranRoadCount-1], m_iranRoadY[m_iranRoadCount-1]);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-225) the order's road, looked for once, when its first placement loop starts (the early
// returns of the repeating orders need no pathfind)
//-------------------------------------------------------------------------------------------------
void AISkirmishPlayer::prepareIranRoad()
{
	if (m_iranRoadCount < 0)
		findIranRoad(&m_iranRoadEnemy);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-225) TRUE when a structure of this radius at pos would come within IRAN_ROAD_CLEARANCE of
// the road (about two tank widths stay free on each side)
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::isOnIranRoad(const Coord3D *pos, Real structureRadius) const
{
	static const Real IRAN_ROAD_CLEARANCE = 60.0f;
	const Real reachSqr = sqr(structureRadius + IRAN_ROAD_CLEARANCE);
	for (Int i = 0; i < m_iranRoadCount; ++i)
	{
		const Real ax = m_iranRoadX[i];
		const Real ay = m_iranRoadY[i];
		const Real bx = i + 1 < m_iranRoadCount ? m_iranRoadX[i+1] : ax;
		const Real by = i + 1 < m_iranRoadCount ? m_iranRoadY[i+1] : ay;
		const Real dx = bx - ax;
		const Real dy = by - ay;
		const Real lengthSqr = dx*dx + dy*dy;
		Real t = lengthSqr > 0.0f ? ((pos->x-ax)*dx + (pos->y-ay)*dy)/lengthSqr : 0.0f;
		if (t < 0.0f)
			t = 0.0f;
		else if (t > 1.0f)
			t = 1.0f;
		if (sqr(ax + t*dx - pos->x) + sqr(ay + t*dy - pos->y) < reachSqr)
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) the base edge toward the enemy: the farthest reach along dir of the own buildings (not
// defenses) and of the build list's sites (planned buildings count from the start) within 45 degrees of dir and
// inside the defense ring (far expansions do not count)
//-------------------------------------------------------------------------------------------------
Real AISkirmishPlayer::getIranBaseFrontEdge(const Coord2D &dir)
{
	static const Real SECTOR_COS = 0.7071f;			// cos 45 degrees
	const Real ringDistance = m_baseRadius + TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;
	Real edge = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() != m_player || !obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_FS_BASE_DEFENSE)
			|| obj->isEffectivelyDead())
			continue;
		const Real rx = obj->getPosition()->x - m_baseCenter.x;
		const Real ry = obj->getPosition()->y - m_baseCenter.y;
		const Real along = rx*dir.x + ry*dir.y;
		const Real dist = sqrt(rx*rx + ry*ry);
		if (along <= 0.0f || dist > ringDistance || along < dist*SECTOR_COS)
			continue;
		const Real reach = along + obj->getGeometryInfo().getBoundingCircleRadius();
		if (reach > edge)
			edge = reach;
	}
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (info->getTemplateName().isEmpty())
			continue;
		const ThingTemplate *plan = TheThingFactory->findTemplate(info->getTemplateName());
		if (plan == nullptr || plan->isKindOf(KINDOF_FS_BASE_DEFENSE))
			continue;
		const Real rx = info->getLocation()->x - m_baseCenter.x;
		const Real ry = info->getLocation()->y - m_baseCenter.y;
		const Real along = rx*dir.x + ry*dir.y;
		const Real dist = sqrt(rx*rx + ry*ry);
		if (along <= 0.0f || dist > ringDistance || along < dist*SECTOR_COS)
			continue;
		const Real reach = along + plan->getTemplateGeometryInfo().getBoundingCircleRadius();
		if (reach > edge)
			edge = reach;
	}
	return edge;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) (owner: "in the middle, which matters the most, they don't [have defenses], or have
// one"; "the enemy stacks up their defenses, our side has like a War Factory at the front"). The ring above put front
// defenses at m_baseRadius + extra from the build-list centre: on Tournament Desert that is down in the central
// valley, where the enemy's guard teams stand (the dozer never finds it safe, those spots were never built, or were
// lost at once), and then every later front order was blocked by the lost one's pending rebuild.
// A front defense now goes on the line toward the enemy base just outside the base edge: the farthest own building
// (not a defense) within 45 degrees of that line, plus clearance. Each row is scanned from the line outwards up to
// +-30 degrees; when a row is blocked (base buildings, cliffs) the next row further out along the approach, never
// past half way to the enemy; when all are full, the same rows out to +-50 degrees. Spots of pending orders are kept free, so the orders fill in side by side.
// A lost front defense (a D-174 rebuild entry within 45 degrees of the line) is not a reason to skip the order: its
// entry is moved to the spot chosen now (often its own spot again), so losses neither block the front nor pile up
// extra entries. Returns false when no spot was found (the caller falls back to the ring).
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::buildIranFrontDefense(const ThingTemplate *tTemplate, const AsciiString &thingName, const Coord2D &dir,
	Real enemyDistance)
{
	static const Int FRONT_ROWS = 8;
	static const Real FRONT_HALF_ARC = PI/6;		// +-30 degrees from the line between the bases
	static const Real WIDE_HALF_ARC = PI*5/18;		// then +-50 degrees when the narrow front is full
	static const Real SECTOR_COS = 0.7071f;			// cos 45 degrees: a lost defense that counts as "in front"
	const Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real spacing = structureRadius*3 > 90.0f ? structureRadius*3 : 90.0f;
	const Real rowStep = structureRadius*2.5f > 70.0f ? structureRadius*2.5f : 70.0f;
	const Real holdRadius = structureRadius*2 > structureRadius + 40.0f ? structureRadius*2 : structureRadius + 40.0f;
	const Real placeAngle = tTemplate->getPlacementViewAngle();

	// the base edge on the approach
	const Real edge = getIranBaseFrontEdge(dir);
	Real start = edge + structureRadius + 40.0f;
	if (start < structureRadius*4)
		start = structureRadius*4;

	// a lost front defense of this type: its entry is moved, not doubled
	BuildListInfo *reuse = nullptr;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (!isIranDefenseRebuild(info) || info->getTemplateName() != thingName
			|| (info->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(info->getObjectID()) != nullptr))
			continue;
		const Real rx = info->getLocation()->x - m_baseCenter.x;
		const Real ry = info->getLocation()->y - m_baseCenter.y;
		const Real dist = sqrt(rx*rx + ry*ry);
		if (dist > 0.0f && rx*dir.x + ry*dir.y >= dist*SECTOR_COS) {
			reuse = info;
			break;
		}
	}

	// the script's count sees only standing defenses: while IRAN_FRONT_PENDING_LIMIT front orders of this type were
	// placed but never built (unsafe spot, dozer busy), a new order is skipped, so unbuilt orders do not pile up
	if (reuse == nullptr) {
		static const Int IRAN_FRONT_PENDING_LIMIT = 2;
		static const Real PENDING_COS = 0.5f;		// cos 60 degrees: the wide arc and the ring fallback's reach
		Int pending = 0;
		for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
		{
			// a fresh order still has all its rebuilds; one under construction has its object
			if (!info->isPriorityBuild() || info->getNumRebuilds() != IRAN_DEFENSE_REBUILDS || info->getTemplateName() != thingName
				|| (info->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(info->getObjectID()) != nullptr))
				continue;
			const Real rx = info->getLocation()->x - m_baseCenter.x;
			const Real ry = info->getLocation()->y - m_baseCenter.y;
			const Real dist = sqrt(rx*rx + ry*ry);
			if (dist > 0.0f && rx*dir.x + ry*dir.y >= dist*PENDING_COS && !releaseIranStaleRebuild(info))
				++pending;	// an order not started for long is given up instead
		}
		if (pending >= IRAN_FRONT_PENDING_LIMIT) {
			if (TheHypersonicScenario != nullptr)
				TheHypersonicScenario->noteAI("AI DEFENSE %s front %s toward %.0f deg: skipped, %d front orders of it not built yet",
					TheHypersonicScenario->sideLabel(m_player), thingName.str(), atan2(dir.y, dir.x)*180/PI, pending);
			return TRUE;	// handled: no ring fallback either
		}
	}

	prepareIranRoad();	// D-225
	for (Int pass = 0; pass < 2; ++pass)
	for (Int row = 0; row < FRONT_ROWS; ++row)
	{
		const Real distance = start + row*rowStep;
		if (row > 0 && distance > enemyDistance*0.5f)
			break;
		const Real angleStep = spacing/distance;
		const Int steps = (Int)((pass == 0 ? FRONT_HALF_ARC : WIDE_HALF_ARC)/angleStep);
		for (Int k = 0; k <= 2*steps; ++k)
		{
			// 0, +1, -1, +2, -2, ... steps from the line
			const Real angle = angleStep * (Real)((k+1)/2) * ((k&1) ? 1.0f : -1.0f);
			if (pass == 1 && fabs(angle) <= FRONT_HALF_ARC)
				continue;	// tried in the first pass
			const Real s = sin(angle);
			const Real c = cos(angle);
			Coord3D buildPos = m_baseCenter;
			buildPos.x += distance*(dir.x*c - dir.y*s);
			buildPos.y += distance*(dir.y*c + dir.x*s);
			// the overlap test below compares heights: at z 0 it misses every object standing on higher ground
			buildPos.z = TheTerrainLogic->getGroundHeight(buildPos.x, buildPos.y);

			// a spot already ordered but not standing (the overlap test below sees only objects)
			Bool held = FALSE;
			for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
			{
				if (info == reuse || !info->isBuildable()
					|| (info->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(info->getObjectID()) != nullptr))
					continue;
				const Coord3D *at = info->getLocation();
				if (sqr(at->x-buildPos.x) + sqr(at->y-buildPos.y) < sqr(holdRadius)) {
					held = TRUE;
					break;
				}
			}
			if (held || isOnIranRoad(&buildPos, structureRadius))	// D-225: the army's road stays open
				continue;

			const Bool canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
				BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
			TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
			buildPos.z = m_baseCenter.z;	// build orders keep the base centre's z (the dozer adds the ground height)
			if (!canBuild)
				continue;
			Coord3D lostAt;
			if (reuse) {
				lostAt = *reuse->getLocation();
				reuse->setLocation(buildPos);
				reuse->setAngle(placeAngle);
				// the give-up time (releaseIranStaleRebuild) counts from the move
				stampIranOrder(reuse);
			} else {
				m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
				// D-174 (owner: "defenses should always be rebuilt when possible"): rebuilt at this spot when lost
				m_player->getBuildList()->setNumRebuilds(IRAN_DEFENSE_REBUILDS);
				stampIranOrder(m_player->getBuildList());	// the give-up time counts from here
			}
			if (TheHypersonicScenario != nullptr)
				TheHypersonicScenario->noteAI("AI DEFENSE %s front %s toward %.0f deg (enemy %.0f away): base edge %.0f, %s row %d at %.0f, %+.0f deg -> %.0f %.0f%s (base centre %.0f %.0f, radius %.0f)",
					TheHypersonicScenario->sideLabel(m_player), thingName.str(), atan2(dir.y, dir.x)*180/PI, enemyDistance, edge,
					pass == 0 ? "narrow" : "wide", row,
					distance, angle*180/PI, buildPos.x, buildPos.y, reuse ? " (moves a lost one's rebuild)" : "", m_baseCenter.x,
					m_baseCenter.y, m_baseRadius);
			if (reuse && TheHypersonicScenario != nullptr)
				TheHypersonicScenario->noteAI("AI DEFENSE %s front %s lost one was at %.0f %.0f", TheHypersonicScenario->sideLabel(m_player),
					thingName.str(), lostAt.x, lostAt.y);
			return TRUE;
		}
	}
	if (TheHypersonicScenario != nullptr)
		TheHypersonicScenario->noteAI("AI DEFENSE %s front %s toward %.0f deg: no legal spot (base edge %.0f, start %.0f); ring fallback",
			TheHypersonicScenario->sideLabel(m_player), thingName.str(), atan2(dir.y, dir.x)*180/PI, edge, start);
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) the near side of the front defense line: the smallest reach toward the enemy (along
// enemyDir, minus the defense's radius) of the own base defenses, standing or still ordered, within 45 degrees of
// enemyDir, beyond the base edge (the in-base Dehlavieh sites are not the front) and not past half way to the enemy.
// count = how many there are (0: no front line yet, returns 0).
//-------------------------------------------------------------------------------------------------
Real AISkirmishPlayer::getIranFrontDefenseLine(const Coord2D &enemyDir, Real enemyDistance, Int *count)
{
	static const Real SECTOR_COS = 0.7071f;			// cos 45 degrees
	const Real edge = getIranBaseFrontEdge(enemyDir);
	Real line = 0.0f;
	Int found = 0;
	for (Object *def = TheGameLogic->getFirstObject(); def; def = def->getNextObject())
	{
		if (def->getControllingPlayer() != m_player || !def->isKindOf(KINDOF_FS_BASE_DEFENSE) || !def->isKindOf(KINDOF_STRUCTURE)
			|| def->isEffectivelyDead())
			continue;
		const Real rx = def->getPosition()->x - m_baseCenter.x;
		const Real ry = def->getPosition()->y - m_baseCenter.y;
		const Real along = rx*enemyDir.x + ry*enemyDir.y;
		const Real dist = sqrt(rx*rx + ry*ry);
		if (along <= edge || along < dist*SECTOR_COS || dist > enemyDistance*0.5f)
			continue;
		const Real reach = along - def->getGeometryInfo().getBoundingCircleRadius();
		if (found == 0 || reach < line)
			line = reach;
		++found;
	}
	for (BuildListInfo *plan = m_player->getBuildList(); plan; plan = plan->getNext())
	{
		if (plan->getTemplateName().isEmpty() || !plan->isBuildable()
			|| (plan->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(plan->getObjectID()) != nullptr))
			continue;	// a standing one was counted above
		const ThingTemplate *planTemplate = TheThingFactory->findTemplate(plan->getTemplateName());
		if (planTemplate == nullptr || !planTemplate->isKindOf(KINDOF_FS_BASE_DEFENSE))
			continue;
		const Real rx = plan->getLocation()->x - m_baseCenter.x;
		const Real ry = plan->getLocation()->y - m_baseCenter.y;
		const Real along = rx*enemyDir.x + ry*enemyDir.y;
		const Real dist = sqrt(rx*rx + ry*ry);
		if (along <= edge || along < dist*SECTOR_COS || dist > enemyDistance*0.5f)
			continue;
		const Real reach = along - planTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		if (found == 0 || reach < line)
			line = reach;
		++found;
	}
	*count = found;
	return line;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @bugfix (D-174) (owner: the extra War Factory stood out in front of the bunkers). A building ordered
// through the flank/front scripts (not a base defense) goes behind the front: its far side at least
// IRAN_BEHIND_CLEARANCE short of the front defense line (getIranFrontDefenseLine; with no front defenses yet, the base
// edge toward the enemy), and never inside the 45 degree cone toward the enemy, so the bunker rows and the way out to
// them stay free. Spots are scanned from the base centre outwards, ring by ring, each ring from the order's direction
// (flank/backdoor path) round both ways; the first pass takes only spots level with the base centre (the base
// plateau), the second any height. A spot must keep clear of every structure and of every pending order (bounding
// circles do not touch, which leaves lanes between buildings) and pass the stock legality test. Synced logic only.
// Returns false when nothing was found (the caller falls back to the ring and logs it).
//-------------------------------------------------------------------------------------------------
Bool AISkirmishPlayer::buildIranBuildingBehindFront(const ThingTemplate *tTemplate, const AsciiString &thingName,
	const Coord2D &dir, const Coord2D &enemyDir, Real enemyDistance, const char *orderLabel)
{
	static const Real SECTOR_COS = 0.7071f;			// cos 45 degrees: the front cone
	static const Real IRAN_BEHIND_CLEARANCE = 40.0f;	// the building's far side stays this far behind the line
	static const Real LEVEL_HEIGHT = 12.0f;			// first pass: ground within this of the base centre's ground
	enum { MAX_KEEP_CLEAR = 512 };
	const Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real placeAngle = tTemplate->getPlacementViewAngle();
	if (structureRadius <= 0.0f)
		return FALSE;
	const Real ringDistance = m_baseRadius + TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance;
	const Real maxDistance = ringDistance + 2*structureRadius*3;	// the stock ring's outer row
	const Real radialStep = structureRadius*0.5f > 25.0f ? structureRadius*0.5f : 25.0f;
	const Real arcStep = structureRadius*0.75f > 25.0f ? structureRadius*0.75f : 25.0f;

	// how far toward the enemy the building may reach
	Int frontDefenses = 0;
	const Real frontLine = getIranFrontDefenseLine(enemyDir, enemyDistance, &frontDefenses);
	Real limit;
	if (frontDefenses > 0) {
		limit = frontLine - IRAN_BEHIND_CLEARANCE;
	} else {
		limit = getIranBaseFrontEdge(enemyDir);	// no front line yet: do not push the base edge (the rows start there)
	}

	// circles to keep clear of: pending orders not standing yet (first, so a crowded area never leaves them out; the
	// legality test below sees standing structures anyway) and structures near the base
	Real keepX[MAX_KEEP_CLEAR];
	Real keepY[MAX_KEEP_CLEAR];
	Real keepR[MAX_KEEP_CLEAR];
	Int keepCount = 0;
	const Real nearby = maxDistance + structureRadius + 300.0f;
	for (BuildListInfo *pending = m_player->getBuildList(); pending && keepCount < MAX_KEEP_CLEAR; pending = pending->getNext())
	{
		if (pending->getTemplateName().isEmpty() || !pending->isBuildable()
			|| (pending->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(pending->getObjectID()) != nullptr))
			continue;
		if (sqr(pending->getLocation()->x-m_baseCenter.x) + sqr(pending->getLocation()->y-m_baseCenter.y) > sqr(nearby))
			continue;
		const ThingTemplate *pendingTemplate = TheThingFactory->findTemplate(pending->getTemplateName());
		keepX[keepCount] = pending->getLocation()->x;
		keepY[keepCount] = pending->getLocation()->y;
		keepR[keepCount] = pendingTemplate ? pendingTemplate->getTemplateGeometryInfo().getBoundingCircleRadius() : structureRadius;
		++keepCount;
	}
	for (Object *other = TheGameLogic->getFirstObject(); other && keepCount < MAX_KEEP_CLEAR; other = other->getNextObject())
	{
		if (!other->isKindOf(KINDOF_STRUCTURE) || other->isEffectivelyDead())
			continue;
		const Coord3D *at = other->getPosition();
		if (sqr(at->x-m_baseCenter.x) + sqr(at->y-m_baseCenter.y) > sqr(nearby))
			continue;
		keepX[keepCount] = at->x;
		keepY[keepCount] = at->y;
		keepR[keepCount] = other->getGeometryInfo().getBoundingCircleRadius();
		++keepCount;
	}

	prepareIranRoad();	// D-225
	const Real baseHeight = TheTerrainLogic->getGroundHeight(m_baseCenter.x, m_baseCenter.y);
	for (Int pass = 0; pass < 2; ++pass)
	for (Real distance = structureRadius; distance <= maxDistance; distance += radialStep)
	{
		const Real angleStep = arcStep/distance;
		const Int steps = (Int)(PI/angleStep);
		for (Int k = 0; k <= 2*steps; ++k)
		{
			// 0, +1, -1, +2, -2, ... steps from the order's direction
			const Real angle = angleStep * (Real)((k+1)/2) * ((k&1) ? 1.0f : -1.0f);
			const Real s = sin(angle);
			const Real c = cos(angle);
			const Real ox = distance*(dir.x*c - dir.y*s);
			const Real oy = distance*(dir.y*c + dir.x*s);
			const Real along = ox*enemyDir.x + oy*enemyDir.y;
			if (along > distance*SECTOR_COS || along + structureRadius > limit)
				continue;	// in the front cone, or reaching toward the bunker rows
			Coord3D buildPos = m_baseCenter;
			buildPos.x += ox;
			buildPos.y += oy;

			Bool blocked = FALSE;
			for (Int i = 0; i < keepCount; ++i)
			{
				if (sqr(keepX[i]-buildPos.x) + sqr(keepY[i]-buildPos.y) < sqr(keepR[i] + structureRadius)) {
					blocked = TRUE;
					break;
				}
			}
			if (blocked || isOnIranRoad(&buildPos, structureRadius))	// D-225: the army's road stays open
				continue;

			// the overlap test compares heights: at the base centre's z (0) it missed every object on higher ground
			buildPos.z = TheTerrainLogic->getGroundHeight(buildPos.x, buildPos.y);
			if (pass == 0 && fabs(buildPos.z - baseHeight) > LEVEL_HEIGHT)
				continue;
			const Bool canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
				BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
			TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
			if (!canBuild)
				continue;
			const Real groundZ = buildPos.z;
			buildPos.z = m_baseCenter.z;	// build orders keep the base centre's z (the dozer adds the ground height)
			m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
			// D-174 (owner: "defenses should always be rebuilt when possible"): rebuilt at this spot when lost
			m_player->getBuildList()->setNumRebuilds(IRAN_DEFENSE_REBUILDS);
			if (TheHypersonicScenario != nullptr)
				TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s behind the front: %s pass, r %.0f, %+.0f deg from the order -> %.0f %.0f (ground %.0f, base %.0f); toward enemy %.0f, far side %.0f, %s %.0f, %.0f behind it (base centre %.0f %.0f)",
					TheHypersonicScenario->sideLabel(m_player), orderLabel, thingName.str(), pass == 0 ? "level" : "any height",
					distance, angle*180/PI, buildPos.x, buildPos.y, groundZ, baseHeight, along, along + structureRadius,
					frontDefenses > 0 ? "front defense line" : "base edge (no front defenses)",
					frontDefenses > 0 ? frontLine : limit,
					(frontDefenses > 0 ? frontLine : limit) - (along + structureRadius), m_baseCenter.x, m_baseCenter.y);
			return TRUE;
		}
	}
	if (TheHypersonicScenario != nullptr)
		TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s behind the front: NO SPOT (%s %.0f, limit %.0f, %d circles); ring fallback",
			TheHypersonicScenario->sideLabel(m_player), orderLabel, thingName.str(),
			frontDefenses > 0 ? "front defense line" : "base edge", frontDefenses > 0 ? frontLine : limit, limit, keepCount);
	return FALSE;
}

void AISkirmishPlayer::buildIranBaseDefenseStructure(const ThingTemplate *tTemplate, const AsciiString &thingName, Bool flank)
{
	static const Int IRAN_DEFENSE_ROWS = 3;

	// Hypersonic @bugfix (D-174) the direction of the enemy base (front defenses; buildings keep out of it)
	const Bool isDefense = tTemplate->isKindOf(KINDOF_FS_BASE_DEFENSE);
	Coord3D enemyPos;
	Coord2D enemyDir;
	enemyDir.x = enemyDir.y = 0.0f;
	Real enemyDistance = 0.0f;
	if (getIranEnemyBasePosition(&enemyPos)) {
		enemyDir.x = enemyPos.x - m_baseCenter.x;
		enemyDir.y = enemyPos.y - m_baseCenter.y;
		enemyDistance = enemyDir.length();
		if (enemyDistance > 1.0f) {
			enemyDir.x /= enemyDistance;
			enemyDir.y /= enemyDistance;
		} else {
			enemyDir.x = enemyDir.y = 0.0f;
		}
	}
	const Bool haveEnemyDir = enemyDir.x != 0.0f || enemyDir.y != 0.0f;
	// D-225: every spot below keeps off the army's road, looked for only when a placement loop starts (prepareIranRoad)
	m_iranRoadCount = haveEnemyDir ? -1 : 0;
	if (haveEnemyDir)
		m_iranRoadEnemy = enemyPos;
	if (!flank && isDefense && haveEnemyDir && buildIranFrontDefense(tTemplate, thingName, enemyDir, enemyDistance))
		return;

	AsciiString pathLabel;
	if (flank) {
		// stock order: backdoor, flank, backdoor, ...
		pathLabel.format("%s%d", (m_curFlankBaseDefense&1) ? SKIRMISH_FLANK : SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
		m_curFlankBaseDefense++;
	}	else {
		pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
	}

	Coord3D goalPos = m_baseCenter;
	Bool aimed = FALSE;
	if (!flank && haveEnemyDir) {
		// the front faces the enemy's base: the centre path's nearest waypoint can lie off to one side of the base
		goalPos.x = enemyPos.x;
		goalPos.y = enemyPos.y;
		aimed = TRUE;
	}
	if (!aimed) {
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (!way)
			return;
		goalPos = *way->getLocation();
	}
	Coord2D dir;
	dir.x = goalPos.x-m_baseCenter.x;
	dir.y = goalPos.y-m_baseCenter.y;
	if (dir.x == 0.0f && dir.y == 0.0f)
		return;
	dir.normalize();

	// flank orders (and the front fallback): a lost defense of this type on this side (within 60 degrees of the
	// order's direction) waits for its rebuild (processBaseBuilding): that covers this order, so the repeating script
	// orders do not add one more spot for every defense lost. A lost one on another side does not block, and one not
	// rebuilt for a long time is given up (releaseIranStaleRebuild) and no longer blocks either. A building (its spot
	// behind the front is not tied to the order's side) still waits for a lost one of its type anywhere.
	{
		static const Real SIDE_COS = 0.5f;		// cos 60 degrees
		for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
		{
			// the entry keeps a dead object's ID until processBaseBuilding's next pass
			if (!isIranDefenseRebuild(info) || info->getTemplateName() != thingName
				|| (info->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(info->getObjectID()) != nullptr))
				continue;
			const Real rx = info->getLocation()->x - m_baseCenter.x;
			const Real ry = info->getLocation()->y - m_baseCenter.y;
			const Real dist = sqrt(rx*rx + ry*ry);
			if (isDefense && dist > 0.0f && rx*dir.x + ry*dir.y < dist*SIDE_COS)
				continue;	// another side
			if (releaseIranStaleRebuild(info))
				continue;
			if (TheHypersonicScenario != nullptr)
				TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s %s: waits for the lost one's rebuild at %.0f %.0f",
					TheHypersonicScenario->sideLabel(m_player), flank ? "flank" : "front", thingName.str(),
					aimed ? "toward enemy" : pathLabel.str(), info->getLocation()->x, info->getLocation()->y);
			return;
		}
	}

	// Hypersonic @bugfix (D-174) a building (War Factory, power plant, ...) goes behind the front defense line, out of
	// the front cone; the ring below only when no such spot exists anywhere in the base
	if (!isDefense && haveEnemyDir) {
		AsciiString orderLabel;
		orderLabel.format("%s %s", flank ? "flank" : "front", aimed ? "toward enemy" : pathLabel.str());
		if (buildIranBuildingBehindFront(tTemplate, thingName, dir, enemyDir, enemyDistance, orderLabel.str()))
			return;
	}

	const Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
	const Real spacing = structureRadius*4;		// stock spacing along the ring
	const Real placeAngle = tTemplate->getPlacementViewAngle();
	{
		prepareIranRoad();	// D-225
		for (Int row = 0; row < IRAN_DEFENSE_ROWS; ++row)
		{
			const Real distance = m_baseRadius + TheAI->getAiData()->m_skirmishBaseDefenseExtraDistance + row*structureRadius*3;
			if (distance <= 0.0f || spacing <= 0.0f)
				return;
			const Real angleStep = spacing/distance;
			const Int steps = (Int)((PI/3)/angleStep);
			for (Int k = 0; k <= 2*steps; ++k)
			{
				// 0, +1, -1, +2, -2, ... steps from the attack path
				const Real angle = angleStep * (Real)((k+1)/2) * ((k&1) ? 1.0f : -1.0f);
				const Real s = sin(angle);
				const Real c = cos(angle);
				const Real ox = distance*(dir.x*c - dir.y*s);
				const Real oy = distance*(dir.y*c + dir.x*s);
				Coord3D buildPos = m_baseCenter;
				buildPos.x += ox;
				buildPos.y += oy;
				// Hypersonic @bugfix (D-174) the overlap test compares heights: at the base centre's z (0) it missed every
				// object standing on higher ground
				buildPos.z = TheTerrainLogic->getGroundHeight(buildPos.x, buildPos.y);

				// a spot already ordered but not started yet (the overlap test below sees only objects)
				Bool held = FALSE;
				for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
				{
					if (!info->isBuildable() || (info->getObjectID() != INVALID_ID && TheGameLogic->findObjectByID(info->getObjectID()) != nullptr))
						continue;
					const Coord3D *at = info->getLocation();
					if (sqr(at->x-buildPos.x) + sqr(at->y-buildPos.y) < sqr(2*structureRadius)) {
						held = TRUE;
						break;
					}
				}
				if (held || isOnIranRoad(&buildPos, structureRadius))	// D-225: the army's road stays open
					continue;

				const Bool canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
					BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
				TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
				buildPos.z = m_baseCenter.z;	// build orders keep the base centre's z (the dozer adds the ground height)
				if (canBuild) {
					m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
					// D-174 (owner: "defenses should always be rebuilt when possible"): rebuilt at this spot when lost
					m_player->getBuildList()->setNumRebuilds(IRAN_DEFENSE_REBUILDS);
					if (isDefense)
						stampIranOrder(m_player->getBuildList());	// the give-up time counts from here
					if (TheHypersonicScenario != nullptr)
						TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s ring %s %.0f deg, row %d at %.0f, %+.0f deg%s -> %.0f %.0f",
							TheHypersonicScenario->sideLabel(m_player), flank ? "flank" : "front", thingName.str(),
							aimed ? "toward enemy" : pathLabel.str(), atan2(dir.y, dir.x)*180/PI, row, distance, angle*180/PI,
							(!isDefense && haveEnemyDir) ? " (building: FALLBACK, nothing behind the front)" : "", buildPos.x, buildPos.y);
					return;
				}
			}
		}
	}
	if (TheHypersonicScenario != nullptr)
		TheHypersonicScenario->noteAI("AI DEFENSE %s %s %s ring %s: no legal spot", TheHypersonicScenario->sideLabel(m_player),
			flank ? "flank" : "front", thingName.str(), aimed ? "toward enemy" : pathLabel.str());
}


/**
	Checks bridges along a waypoint path.  If any are destroyed, sends a dozer to fix, and returns true.
	If there is no bridge problem, returns false.
	*/
Bool AISkirmishPlayer::checkBridges(Object *unit, Waypoint *way)
{
	Coord3D unitPos = *unit->getPosition();
	AIUpdateInterface *ai = unit->getAI();
	if (!ai) return false; // no ai
	const LocomotorSet& locoSet = ai->getLocomotorSet();
	Waypoint *curWay;
	for (curWay = way; curWay; curWay = curWay->getNext()) {
		if (TheAI->pathfinder()->clientSafeQuickDoesPathExist(locoSet, &unitPos, curWay->getLocation())) {
			continue;
		}
		ObjectID brokenBridge = INVALID_ID;
		if (TheAI->pathfinder()->findBrokenBridge(locoSet, &unitPos, curWay->getLocation(), &brokenBridge)) {
			repairStructure(brokenBridge);
			return true;
		}
	}
	return false;

}


/**
	Build a specific team.  If priorityBuild, put at front of queue with priority set.
	*/
void AISkirmishPlayer::buildSpecificAITeam( TeamPrototype *teamProto, Bool priorityBuild)
{
	AIPlayer::buildSpecificAITeam(teamProto, priorityBuild);
}


/**
	Recruit a specific team, within the specific radius of the home position.
	*/
void AISkirmishPlayer::recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius)
{
	if (recruitRadius < 1) recruitRadius = 99999.0f;
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{
		if (teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to recruit singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false);
				return;
			}
		}
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation)
		{
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// create inactive team to place members into as they are built
		// when team is complete, the team is activated
		Team *theTeam = TheTeamFactory->createInactiveTeam( teamProto->getName() );
		AsciiString teamName = teamProto->getName();
		teamName.concat(" - Recruiting.");
		TheScriptEngine->AppendDebugMessage(teamName, false);
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
//		WorkOrder *orders = nullptr;
		Int i;
		Int unitsRecruited = 0;
		// Recruit.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].maxUnits;
				while (count>0) {
					Object *unit = theTeam->tryToRecruit(thing, &teamProto->getTemplateInfo()->m_homeLocation, recruitRadius);
					if (unit)
					{
						unitsRecruited++;

						AsciiString teamStr = "Team '";
						teamStr.concat(theTeam->getPrototype()->getName());
						teamStr.concat("' recruits ");
						teamStr.concat(thing->getName());
						teamStr.concat(" from team '");
						teamStr.concat(unit->getTeam()->getPrototype()->getName());
						teamStr.concat("'");
						TheScriptEngine->AppendDebugMessage(teamStr, false);

						unit->setTeam(theTeam);

						AIUpdateInterface *ai = unit->getAIUpdateInterface();
						if (ai)
						{
#ifdef DEBUG_LOGGING
							Coord3D pos = *unit->getPosition();
							Coord3D to = teamProto->getTemplateInfo()->m_homeLocation;
							DEBUG_LOG(("Moving unit from %f,%f to %f,%f", pos.x, pos.y , to.x, to.y ));
#endif
							ai->aiMoveToPosition( &teamProto->getTemplateInfo()->m_homeLocation, CMD_FROM_AI);
						}
					} else {
						break;
					}
					count--;
				}
			}
		}
		if (unitsRecruited>0)
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			// Put in front of queue.
			prependTo_TeamReadyQueue(team);
			team->m_priorityBuild = false;
			team->m_workOrders = nullptr;
			team->m_frameStarted = TheGameLogic->getFrame();
			team->m_team = theTeam;
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Finished recruiting.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}	else {
			//disband.
			if (!theTeam->getPrototype()->getIsSingleton()) {
				deleteInstance(theTeam);
				theTeam = nullptr;
			}
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Recruited 0 units, disbanding.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}
	}
}




/**
 * Train our teams.
 */
void AISkirmishPlayer::processTeamBuilding()
{
	// select a new team
	if (selectTeamToBuild()) {
		queueUnits();
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it's time to build another base building.
 */
void AISkirmishPlayer::doBaseBuilding()
{
	if (m_player->getCanBuildBase()) {
		// See if we are ready to start trying a structure.
		if (!m_readyToBuildStructure) {
			m_structureTimer--;
			if (m_structureTimer<=0) {
				m_readyToBuildStructure = true;
				m_buildDelay = 0;
			}
			if (m_structureTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_structureTimer = 3*LOGICFRAMES_PER_SECOND;
			}
		}
		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_buildDelay--;
		if (m_buildDelay<1) {
			if (m_readyToBuildStructure) {
				processBaseBuilding();
			}
			if (m_buildDelay<1) {	// processBaseBuilding may reset m_buildDelay.
				m_buildDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 2 seconds.
			}
			// Note that this timer gets shortcut when a building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any ready teams have finished moving to the rally point.
 */
void AISkirmishPlayer::checkReadyTeams()
{
	AIPlayer::checkReadyTeams();
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any queued teams have finished building, or have run out of time.
 */
void AISkirmishPlayer::checkQueuedTeams()
{
	AIPlayer::checkQueuedTeams();
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another ai team building.
 */
void AISkirmishPlayer::doTeamBuilding()
{
	// See if any teams are expired.
	if (m_player->getCanBuildUnits()) {
		// See if we are ready to start trying a team.
		if (!m_readyToBuildTeam) {
			m_teamTimer--;
			if (m_teamTimer<=0) {
				m_readyToBuildTeam = true;
				m_teamDelay = 0;
			}
			if (m_teamTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_teamTimer = 3*LOGICFRAMES_PER_SECOND;
			}
		}

		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_teamDelay--;
		if (m_teamDelay<1) {
			queueUnits(); // update the queues.
			if (m_readyToBuildTeam) {
				processTeamBuilding();
			}
			m_teamDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 5 seconds.
			// Note that this timer gets shortcut when a unit or building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Perform computer-controlled player AI
 */
void AISkirmishPlayer::update()
{
	AIPlayer::update();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Adjusts the build list to match the starting position.
 */
void AISkirmishPlayer::adjustBuildList(BuildListInfo *list)
{
	Bool foundStart = false;
	Coord3D startPos;

	// Find our command center location.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a command center.
			if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
				foundStart = true;
				startPos = *obj->getPosition();
				m_player->onStructureUndone(obj);
				TheAI->pathfinder()->removeObjectFromPathfindMap(obj);
				TheGameLogic->destroyObject(obj);
				break;
			}
		}
	}
	if (!foundStart) {
		DEBUG_LOG(("Couldn't find starting command center for ai player."));
		return;
	}
	// Find the location of the command center in the build list.
	Bool foundInBuildList = false;
	Coord3D buildPos;
	BuildListInfo *cur = list;
	while (cur) {
		const ThingTemplate *tTemplate = TheThingFactory->findTemplate(cur->getTemplateName());
		if (tTemplate && tTemplate->isKindOf(KINDOF_COMMANDCENTER)) {
			foundInBuildList = true;
			buildPos = *cur->getLocation();
			cur->setInitiallyBuilt(true);
		}
		cur = cur->getNext();
	}
	Region3D bounds;
	TheTerrainLogic->getMaximumPathfindExtent(&bounds);
	/* calculate section of 3x3 grid:
		6 7 8
		3 4 5
		0 1 2 */

	Int gridIndex = 0;
	if (startPos.x > bounds.lo.x + bounds.width()/3) {
		gridIndex++;
	}
	if (startPos.x > bounds.lo.x + 2*bounds.width()/3) {
		gridIndex++;
	}

	if (startPos.y > bounds.lo.y + bounds.height()/3) {
		gridIndex+=3;
	}
	if (startPos.y > bounds.lo.y + 2*bounds.height()/3) {
		gridIndex+=3;
	}

	Real angle = 0;
	if (TheAI->getAiData()->m_rotateSkirmishBases) {
		switch (gridIndex) {
			case 0 : angle = 0; break;
			case 1 : angle = PI/4; break;// 45 degrees.
			case 2 : angle = PI/2; break; // 90 degrees;
			case 3 : angle = -PI/4; break; // -45 degrees.
			case 4 : angle = 0; break;
			case 5 : angle = 3*PI/4; break; // 135 degrees.
			case 6 : angle = -PI/2; break; // -90 degrees;
			case 7 : angle = -3*PI/4; break; // -135 degrees.
			case 8 : angle = PI; break; // 180 degrees.
		}
	}

	angle += 3*PI/4;

	Real s = sin(angle);
	Real c = cos(angle);

	cur = list;
	while (cur) {
		const ThingTemplate *tTemplate = TheThingFactory->findTemplate(list->getTemplateName());
		if (tTemplate && tTemplate->isKindOf(KINDOF_COMMANDCENTER)) {
			foundInBuildList = true;
			Coord3D curPos = *cur->getLocation();
			// Transform to new coords.
			curPos.x -= buildPos.x;
			curPos.y -= buildPos.y;
			Real newX = curPos.x*c - curPos.y*s;
			Real newY = curPos.y*c + curPos.x*s;
			curPos.x = newX + startPos.x;
			curPos.y = newY + startPos.y;
			cur->setLocation(curPos);
			cur->setAngle(cur->getAngle());
		}
		cur = cur->getNext();
	}

}



//----------------------------------------------------------------------------------------------------------
/**
 * Find any things that build stuff & add them to the build list.  Then build any initially built
 * buildings.
 */
void AISkirmishPlayer::newMap()
{

	/* Get our proper build list. */
	AsciiString mySide = m_player->getSide();
	DEBUG_LOG(("AI Player side is %s", mySide.str()));
	const AISideBuildList *build = TheAI->getAiData()->m_sideBuildLists;
	while (build) {
		if (build->m_side == mySide) {
			BuildListInfo *buildList = build->m_buildList->duplicate();
			adjustBuildList(buildList); // adjust to  our start position.
			m_player->setBuildList(buildList);
			computeCenterAndRadiusOfBase(&m_baseCenter, &m_baseRadius);
			break;
		}
		build = build->m_next;
	}
	DEBUG_ASSERTLOG(build!=nullptr, ("Couldn't find build list for skirmish player."));

	// Build any with the initially built flag.
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {
			DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
			continue;
		}
		if (info->isInitiallyBuilt()) {
			buildStructureNow(bldgPlan, info);
		} else {
			info->incrementNumRebuilds(); // the initial build in the normal build list consumes a rebuild, so add one.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Queues up a dozer.
 */
void AISkirmishPlayer::queueDozer()
{
	AIPlayer::queueDozer();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Finds a dozer that isn't building or collecting resources.
 */
Object * AISkirmishPlayer::findDozer( const Coord3D *pos )
{
	return AIPlayer::findDozer(pos);
}


//----------------------------------------------------------------------------------------------------------
/**
 * Find a good spot to fire a superweapon.
 */
Bool AISkirmishPlayer::computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius, const Coord3D *minRangeFrom, Real minRange)
{

	Region2D bounds;
	getPlayerStructureBounds(&bounds, playerNdx);

	if( power->getSpecialPowerType() == SPECIAL_CLUSTER_MINES || power->getSpecialPowerType() == NUKE_SPECIAL_CLUSTER_MINES )
	{
		// hackus brutus - mine the entrances to our base.
		AsciiString pathLabel;
		Int mode = GameLogicRandomValue(0, 2);
		if (mode==1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
		}	else if (mode==2) {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			Region2D bounds;
			getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex());
			goalPos.x = bounds.lo.x + bounds.width()/2;
			goalPos.y = bounds.lo.y + bounds.height()/2;
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		offset.x *= m_baseRadius;
		offset.y *= m_baseRadius;
		*retPos = m_baseCenter;
		retPos->x += offset.x;
		retPos->y += offset.y;
		retPos->z = TheTerrainLogic->getGroundHeight(retPos->x, retPos->y);
		return TRUE;
	}

	// Hypersonic @feature (D-172) the cluster-mine branch above is unchanged (no MinimumRange power uses it)
	return AIPlayer::computeSuperweaponTarget(power, retPos, playerNdx, weaponRadius, minRangeFrom, minRange);

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer base class info
	AIPlayer::xfer( xfer );

	// front base defense
	xfer->xferInt( &m_curFrontBaseDefense );

	// flank base defense
	xfer->xferInt( &m_curFlankBaseDefense );

	// front left defense angle
	xfer->xferReal( &m_curFrontLeftDefenseAngle );

	// front right defense angle
	xfer->xferReal( &m_curFrontRightDefenseAngle );

	// left flank left defense angle
	xfer->xferReal( &m_curLeftFlankLeftDefenseAngle );

	// left flank right defense angle
	xfer->xferReal( &m_curLeftFlankRightDefenseAngle );

	// right flank left defense angle
	xfer->xferReal( &m_curRightFlankLeftDefenseAngle );

	// right flank right defense angle
	xfer->xferReal( &m_curRightFlankRightDefenseAngle );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::loadPostProcess()
{

}

