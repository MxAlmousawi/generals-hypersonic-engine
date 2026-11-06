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

// FILE: ScriptActions.cpp /////////////////////////////////////////////////////////////////////////
// The game scripting engine.  Interprets scripts.
// Author: John Ahlquist, Nov. 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/FramePacer.h"
#include "Common/GameAudio.h"
#include "Common/MapObject.h"							// For MAP_XY_FACTOR
#include "Common/PartitionSolver.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"									// For TheRadar
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"

#include "GameClient/Anim2D.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Snow.h"

#include "GameLogic/AI.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CaveContain.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/MobNexusContain.h"
#include "GameLogic/Module/RailroadGuideAIUpdate.h"
#include "GameLogic/Module/StickyBombUpdate.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/HypersonicScenario.h"
#include "GameLogic/Module/CruiseFlightUpdate.h"
#include "GameLogic/Module/HitChanceBehavior.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Module/MunitionMagazineUpdate.h"
#include "GameLogic/Module/RebuildHoleExposeDie.h"	// Hypersonic @feature (D-189)

#include <map>	// Hypersonic @feature (D-189 stale volley)


// Kind of hacky, but we need to dance on the guts of the terrain.
extern void oversizeTheTerrain(Int amount);

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// when you set controlling player or merge teams, we don't always update all the upgrade stuff
// or the indicator color. this allows us to force the situation. (srj)
static void updateTeamAndPlayerStuff( Object *obj, void *userData )
{
	obj->updateUpgradeModules();

	// srj sez: apparently we have to do this too, since Team::setControllingPlayer
	// does not. Might make more sense to do it there, but am scared to at this point.
	Drawable* draw = obj->getDrawable();
	if (draw)
	{
		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			draw->setIndicatorColor(obj->getNightIndicatorColor());
		else
			draw->setIndicatorColor(obj->getIndicatorColor());
	}
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

// STATICS ////////////////////////////////////////////////////////////////////////////////////////

// DEFINES ////////////////////////////////////////////////////////////////////////////////////////
#define REALLY_FAR	(100000 * MAP_XY_FACTOR)

// GLOBALS ////////////////////////////////////////////////////////////////////////////////////////
ScriptActionsInterface *TheScriptActions = nullptr;
GameWindow *ScriptActions::m_messageWindow = nullptr;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptActions::ScriptActions()
{
	m_suppressNewWindows = FALSE;
	m_unnamedUnit = AsciiString::TheEmptyString;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ScriptActions::~ScriptActions()
{
	reset(); // just in case.
}

//-------------------------------------------------------------------------------------------------
/** Init */
//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-126) the approach path each Iran AI team took at its move order (see iranApproachPath)
struct IranPathChoice
{
	TeamID				m_team;
	Int						m_label;	///< index in IRAN_PATH_LABELS
	UnsignedInt		m_frame;
};
static std::vector<IranPathChoice> s_iranPathChoices;	///< cleared for every game (ScriptActions::reset)
// Hypersonic @feature (D-189) per player and group (0 drones, 1 missiles): logic frame + 1 of a strike-doctrine run
// that launched nothing; the scripts' other launch powers of that group skip it in the same frame (same answer).
// Cleared for every game (ScriptActions::reset).
static UnsignedInt s_iranDoctrineIdleFrame[MAX_PLAYER_COUNT][2];
// Hypersonic @feature (D-189 stale volley) per Drone Facility: logic frame since which it has held a full volley
// (>= IRAN_BREACH_MIN_DRONES loaded) without launching. Cleared for every game (ScriptActions::reset).
static std::map<ObjectID, UnsignedInt> s_iranDroneFullSince;

void ScriptActions::init()
{

	reset();

}

//-------------------------------------------------------------------------------------------------
/** Reset */
//-------------------------------------------------------------------------------------------------
void ScriptActions::reset()
{
	m_suppressNewWindows = FALSE;
	closeWindows(FALSE); // Close victory or defeat windows.
	s_iranPathChoices.clear();	// Hypersonic @feature (D-126)
	memset( s_iranDoctrineIdleFrame, 0, sizeof( s_iranDoctrineIdleFrame ) );	// Hypersonic @feature (D-189)
	s_iranDroneFullSince.clear();	// Hypersonic @feature (D-189 stale volley)

}

//-------------------------------------------------------------------------------------------------
/** Update */
//-------------------------------------------------------------------------------------------------
void ScriptActions::update()
{
	// Empty for now.  jba.
}


//-------------------------------------------------------------------------------------------------
/** closeWindows */
//-------------------------------------------------------------------------------------------------
void ScriptActions::closeWindows( Bool suppressNewWindows )
{
	m_suppressNewWindows = suppressNewWindows;

	if (m_messageWindow) {
		TheWindowManager->winDestroy(m_messageWindow);
		m_messageWindow = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** doQuickVictory */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doQuickVictory()
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(TRUE);
	TheScriptEngine->startQuickEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doSetInfantryLightingOverride */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetInfantryLightingOverride(Real setting)
{
	DEBUG_ASSERTCRASH( (setting == -1.0f) || (setting > 0.0f), ("Invalid setting (%d) in Infantry Lighting Override script.", setting) );
	TheWritableGlobalData->m_scriptOverrideInfantryLightScale = setting;
}

//-------------------------------------------------------------------------------------------------
/** doVictory */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doVictory()
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if (!m_suppressNewWindows)
	{
		const Player *localPlayer = ThePlayerList->getLocalPlayer();
		Bool showObserverWindow = localPlayer->isPlayerObserver() || TheScriptEngine->hasShownMPLocalDefeatWindow();
		if(showObserverWindow)
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/ObserverQuit.wnd");
		else
		{
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/Victorious.wnd");
		}
	}
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(TRUE);
	TheScriptEngine->startEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doDefeat */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDefeat()
{
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	doDisableInput();
	if (!m_suppressNewWindows)
	{
		const Player *localPlayer = ThePlayerList->getLocalPlayer();
		Bool showObserverWindow = localPlayer->isPlayerObserver() || TheScriptEngine->hasShownMPLocalDefeatWindow();
		if(showObserverWindow)
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/ObserverQuit.wnd");
		else
		{
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/Defeat.wnd");
		}
	}
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	TheScriptEngine->startEndGameTimer();
}

//-------------------------------------------------------------------------------------------------
/** doLocalDefeat */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doLocalDefeat()
{
	TheScriptEngine->markMPLocalDefeatWindowShown();
	closeWindows(FALSE);
	TheGameLogic->closeWindows();
	if (!m_suppressNewWindows)
	{
		if(!TheVictoryConditions->amIObserver())
			m_messageWindow = TheWindowManager->winCreateFromScript("Menus/LocalDefeat.wnd");
	}
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	TheScriptEngine->startCloseWindowTimer();
}

//-------------------------------------------------------------------------------------------------
/** changeObjectPanelFlagForSingleObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::changeObjectPanelFlagForSingleObject(Object *obj, const AsciiString& flagToChange, Bool newVal )
{
	// Enabled flag
	if (flagToChange == TheObjectFlagsNames[0])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_DISABLED, !newVal);
		return;
	}

	// Powered flag
	if (flagToChange == TheObjectFlagsNames[1])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_UNPOWERED, !newVal);
		return;
	}

	// Indestructible flag
	if (flagToChange == TheObjectFlagsNames[2])
	{
		BodyModuleInterface* body = obj->getBodyModule();
		if (body)	{
			body->setIndestructible(newVal);
		}
		return;
	}

	// Unsellable flag
	if (flagToChange == TheObjectFlagsNames[3])
	{
		obj->setScriptStatus(OBJECT_STATUS_SCRIPT_UNSELLABLE, newVal);
		return;
	}

	// Selectable flag
	if (flagToChange == TheObjectFlagsNames[4])
	{
		if (obj->isSelectable() != newVal) {
			obj->setSelectable(newVal);
		}
		return;
	}

	// AI Recruitable flag
	if (flagToChange == TheObjectFlagsNames[5])
	{
		if (obj->getAIUpdateInterface()) {
			obj->getAIUpdateInterface()->setIsRecruitable(newVal);
		}

		return;
	}

	// Player targetable flag
	if( flagToChange == TheObjectFlagsNames[6] )
	{
		obj->setScriptStatus( OBJECT_STATUS_SCRIPT_TARGETABLE, newVal );
		return;
	}

}

//-------------------------------------------------------------------------------------------------
/** doDebugMessage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDebugMessage(const AsciiString& msg, Bool pause )
{
	TheScriptEngine->AppendDebugMessage(msg, pause);
}

//-------------------------------------------------------------------------------------------------
/** doPlaySoundEffect */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlaySoundEffect(const AsciiString& sound)
{
	AudioEventRTS audioEvent(sound);
	audioEvent.setIsLogicalAudio(true);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheAudio->addAudioEvent( &audioEvent );
}


//-------------------------------------------------------------------------------------------------
/** doPlaySoundEffectAt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlaySoundEffectAt(const AsciiString& sound, const AsciiString& waypoint)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
	if (!way) {
		return;
	}

	AudioEventRTS audioEvent(sound, way->getLocation());
	audioEvent.setIsLogicalAudio(true);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheAudio->addAudioEvent( &audioEvent );
}

//-------------------------------------------------------------------------------------------------
/** doEnableObjectSound */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableObjectSound(const AsciiString& objectName, Bool enable )
{
	Object *object = TheScriptEngine->getUnitNamed(objectName);
	if (!object)
	{
		return;
	}

	Drawable *drawable = object->getDrawable();
	if ( !drawable )
	{
		return;
	}

  drawable->enableAmbientSoundFromScript( enable );
}


//-------------------------------------------------------------------------------------------------
/** doDamageTeamMembers */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDamageTeamMembers(const AsciiString& team, Real amount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		theTeam->damageTeamMembers(amount);
	}
}

//-------------------------------------------------------------------------------------------------
/** doMoveToWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveToWaypoint(const AsciiString& team, const AsciiString& waypoint)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );

	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		AIGroupPtr theGroup = TheAI->createGroup();
		if (!theGroup) {
			return;
		}
#if RETAIL_COMPATIBLE_AIGROUP
		theTeam->getTeamAsAIGroup(theGroup);
#else
		theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
		Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
		if (way) {
			Coord3D destination = *way->getLocation();
			//DEBUG_LOG(("Moving team to waypoint %f, %f, %f", destination.x, destination.y, destination.z));
 			theGroup->groupMoveToPosition( &destination, false, CMD_FROM_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedMoveToWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedMoveToWaypoint(const AsciiString& unit, const AsciiString& waypoint)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
		if (!way) {
			return;
		}

		Coord3D destination = *way->getLocation();

		AIUpdateInterface *aiUpdate = theObj->getAIUpdateInterface();
		if (!aiUpdate) {
			return;
		}

		aiUpdate->clearWaypointQueue();
		theObj->leaveGroup();
		aiUpdate->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		aiUpdate->aiMoveToPosition( &destination, CMD_FROM_SCRIPT );

	}
}

//-------------------------------------------------------------------------------------------------
/** doCameraFollowNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraFollowNamed(const AsciiString& unit, Bool snapToUnit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheTacticalView->setCameraLock(theObj->getID());
		if (snapToUnit)
			TheTacticalView->snapToCameraLock();

		TheTacticalView->setSnapMode( View::LOCK_FOLLOW, 0.0f );
	}
}

//-------------------------------------------------------------------------------------------------
/** doStopCameraFollowUnit */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doStopCameraFollowUnit()
{
	TheTacticalView->setCameraLock(INVALID_ID);
}

//-------------------------------------------------------------------------------------------------
/** doSetTeamState */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetTeamState(const AsciiString& team, const AsciiString& state)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam) {
		theTeam->setState(state);
	}
}

/** doCreateReinforcements */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateReinforcements(const AsciiString& team, const AsciiString& waypoint)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( team );
	Coord3D destination;


	Bool needToMoveToDestination = false;
	//Validate the waypoint
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
	if (way==nullptr)
	{
		return;
	}

	destination = *way->getLocation();
	if (!theTeamProto) {
		DEBUG_LOG(("***WARNING - Team %s not found.", team.str()));
		return;
	}
	const TeamTemplateInfo *pInfo = theTeamProto->getTemplateInfo();
	Coord3D origin = destination;
	way = TheTerrainLogic->getWaypointByName(pInfo->m_startReinforceWaypoint);
	if (way) {
		origin = *way->getLocation();
		if (origin.x != destination.x || origin.y != destination.y) {
			needToMoveToDestination = true;
		}
	}

	//Create the team (not the units inside team).
	Team *theTeam = TheTeamFactory->createInactiveTeam( team );
	if (theTeam==nullptr) {
		return;
	}
	const ThingTemplate *transportTemplate;
	const ThingTemplate *unitTemplate;

	//Create the transport first (if applicable), so we can determine if it has paradrop capabilities.
	//If so, we'll be doing a lot of things differently!
	Object *transport=nullptr;
	ContainModuleInterface *contain = nullptr;
	transportTemplate = TheThingFactory->findTemplate( pInfo->m_transportUnitType );
	if( transportTemplate )
	{
		transport = TheThingFactory->newObject( transportTemplate, theTeam );
		transport->setPosition( &origin );
		transport->setOrientation( 0.0f );
		if( transport )
		{
			contain = transport->getContain();
		}
	}
	Int transportCount = 1;

	//Check to see if we have a transport, and if our transport has paradrop capabilities. If this is the
	//case, we'll need to create each unit inside "parachute containers".
	static NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY("DeliverPayloadAIUpdate");
	DeliverPayloadAIUpdate *dp = nullptr;
	if( transport )
	{
		dp = (DeliverPayloadAIUpdate*)transport->findUpdateModule(key_DeliverPayloadAIUpdate);
	}

	//Our transport has a deliverPayload update module. This means it'll do airborned drops.

	const ThingTemplate* putInContainerTemplate  = nullptr;
	if( dp )
	{
		//Check to see if we are packaging our units
		putInContainerTemplate = dp->getPutInContainerTemplateViaModuleData();
	}

	//Now create the units that make up the team.
	Int i, j;
	for (i=0; i<pInfo->m_numUnitsInfo; i++)
	{
		// get thing template based from map object name
		unitTemplate = TheThingFactory->findTemplate(pInfo->m_unitsInfo[i].unitThingName);
		Coord3D pos = origin;
		if (unitTemplate && theTeam)
		{
			Object *obj = nullptr;
			for (j=0; j<pInfo->m_unitsInfo[i].maxUnits; j++)
			{
				// create new object in the world
				obj = TheThingFactory->newObject( unitTemplate, theTeam );
				if( obj )
				{
					/// @todo - have better positioning for reinforcement units.
					pos.x = origin.x + 2.25*(j)*obj->getGeometryInfo().getMajorRadius();
					pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
					obj->setPosition( &pos );
					obj->setOrientation(0.0f);


				}
			}
			if (obj) pos.y += 2*obj->getGeometryInfo().getMajorRadius();
		}
		origin.y = pos.y;
	}
	origin = destination;
	if (pInfo->m_teamStartsFull)
	{
		// Have them load into transports in the team.
		EntriesVec vecOfUnits;
		SpacesVec vecOfTransports;

		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj) {
				continue;
			}
			if (obj==transport) {
				continue; // Skip the one we created.  The team loads into the transports on the team for team starts full. jba
			}
			if (obj->isKindOf(KINDOF_TRANSPORT))
			{
				ContainModuleInterface *contain = obj->getContain();
				if( contain )
				{
					vecOfTransports.push_back(std::make_pair(obj->getID(), ((TransportContain*)contain)->getContainMax()));
				}
				else
				{
					DEBUG_CRASH( ("doCreateReinforcement script -- transport doesn't have contain to hold guys.") );
				}
			}
			else
			{
				Int slots = obj->getTransportSlotCount();
				if (slots==0) slots = 0x7fffff; // 0 means lots.
				vecOfUnits.push_back(std::make_pair(obj->getID(), slots));
			}
		}

		// we've now built the necessary stuff. Partition the units.

		PartitionSolver partition(vecOfUnits, vecOfTransports, PREFER_FAST_SOLUTION);
		partition.solve();
		const SolutionVec& solution = partition.getSolution();
		for (size_t i = 0; i < solution.size(); ++i) {
			Object *unit = TheGameLogic->findObjectByID(solution[i].first);
			Object *trans = TheGameLogic->findObjectByID(solution[i].second);
			if (!unit || !trans) {
				continue;
			}
			ContainModuleInterface *contain = trans->getContain();
			if (contain)
			{
				contain->addToContain(unit);
			}
		}

	}
	contain = nullptr;
	if( transport )
	{
		contain = transport->getContain();
	}
	if (contain)
	{
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj)
			{
				continue;
			}
			if (obj->getTemplate()->isEquivalentTo(transport->getTemplate()) )
			{
				// it's our transport.
				continue;
			}
			if (obj->getContainedBy() != nullptr)
			{
				continue;
			}
			//Check to see if it's a valid transport for this unit, even if it's full.

			Coord3D pos = origin;
			pos.x += transportCount*transport->getGeometryInfo().getMajorRadius();
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);

			if (contain && contain->isValidContainerFor(obj, false))
			{
				//Now that we know it fits in the transport, check to see if it's full. If it is,
				//then we'll create a new transport.
				if (!contain->isValidContainerFor(obj, true))
				{
					// full, try building another.
					transport = TheThingFactory->newObject( transportTemplate, theTeam );
					transport->setPosition( &pos );
					transportCount++;
					transport->setOrientation(0.0f);
					if (transport)
					{
						contain = transport->getContain();
					}
				}
				//If our unit is going to be put in another container (such as infantry being contained by a parachute)
				//do so now.
				if( putInContainerTemplate )
				{
					Object* container = TheThingFactory->newObject( putInContainerTemplate, theTeam );
					container->setPosition( &pos );

					//Make sure this is valid.
					if( container->getContain() && container->getContain()->isValidContainerFor( obj, true ) )
					{
						container->getContain()->addToContain( obj );
						obj = container;
					}
					else
					{
						DEBUG_CRASH( ("doCreateReinforcements: PutInContainer %s is full, or not valid for the payload %s!", putInContainerTemplate->getName().str(), obj->getTemplate()->getName().str() ) );
					}
				}

				//Add our unit to the transport.
				contain->addToContain( obj );
			}
		}
	}


	if (theTeam)
	{
		if (transport)
		{
			for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
			{
				Object *obj = iter.cur();
				if (!obj)
				{
					continue;
				}
				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (obj->getTemplate()->isEquivalentTo(transport->getTemplate()) )
				{
					if( dp )
					{
						dp->deliverPayloadViaModuleData( &destination );
					}
					// it's our transport.
					else if( pInfo->m_transportsExit )
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToAndEvacuateAndExit(&destination, CMD_FROM_SCRIPT);
						}
					}
					else
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToAndEvacuate( &destination, CMD_FROM_SCRIPT );
						}
					}
				}
				else
				{
					// If there are any units that aren't transportable, move them to the goal.
					if( !obj->isDisabledByType( DISABLED_HELD ) )
					{
						if( ai )
						{
							ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
							ai->aiMoveToPosition(&destination, CMD_FROM_SCRIPT);
						}
					}
				}
			}
		}
		else
		{
			theTeam->setActive();
			if (needToMoveToDestination)
			{
				AIGroupPtr theGroup = TheAI->createGroup();
				if (!theGroup)
				{
					return;
				}
#if RETAIL_COMPATIBLE_AIGROUP
				theTeam->getTeamAsAIGroup(theGroup);
#else
				theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
				theGroup->groupMoveToPosition( &destination, false, CMD_FROM_SCRIPT );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doMoveCameraTo */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveCameraTo(const AsciiString& waypoint, Real sec, Real cameraStutterSec, Real easeIn, Real easeOut)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheTacticalView->moveCameraTo(&destination, sec*1000, cameraStutterSec*1000, true, easeIn*1000.0f, easeOut*1000.0f);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doZoomCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doZoomCamera(Real zoom, Real sec, Real easeIn, Real easeOut)
{
	TheTacticalView->zoomCamera(zoom, sec*1000.0f, easeIn*1000.0f, easeOut*1000.0f);
}

//-------------------------------------------------------------------------------------------------
/** doPitchCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPitchCamera(Real pitch, Real sec, Real easeIn, Real easeOut)
{
	TheTacticalView->pitchCamera(pitch, sec*1000.0f, easeIn*1000.0f, easeOut*1000.0f);
}

//-------------------------------------------------------------------------------------------------
/** doOversizeTheTerrain */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doOversizeTheTerrain(Int amount)
{
	oversizeTheTerrain(amount);
	TheTacticalView->forceRedraw();
}

//-------------------------------------------------------------------------------------------------
/** doSetupCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetupCamera(const AsciiString& waypoint, Real zoom, Real pitch, const AsciiString& lookAtWaypoint)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
	if (way==nullptr) return;
	Coord3D	pos = *way->getLocation();
	Waypoint *lookat = TheTerrainLogic->getWaypointByName(lookAtWaypoint);
	if (lookat==nullptr) return;
	Coord3D destination = *lookat->getLocation();
	TheTacticalView->moveCameraTo(&pos, 0, 0, true, 0.0f, 0.0f);
	TheTacticalView->cameraModLookToward(&destination);
	TheTacticalView->cameraModFinalPitch(pitch, 0.0f, 0.0f);
	TheTacticalView->cameraModFinalZoom(zoom, 0.0f, 0.0f);
}

//-------------------------------------------------------------------------------------------------
/** doModCameraLookToward */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraLookToward(const AsciiString& waypoint)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheTacticalView->cameraModLookToward(&destination);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doModCameraFinalLookToward */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraFinalLookToward(const AsciiString& waypoint)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheTacticalView->cameraModFinalLookToward(&destination);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doModCameraMoveToSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModCameraMoveToSelection()
{
	Int count=0;
	Coord3D destination;
	destination.x=destination.y=destination.z = 0;

	for (Drawable *d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
	{
		if (d->isSelected())
		{
			Coord3D pos = *d->getPosition();
			destination.x += pos.x;
			destination.y += pos.y;
			destination.z += pos.z;
			count++;
		}
	}
	if (count) {
		destination.z /= count;
		destination.x /= count;
		destination.y /= count;
		TheTacticalView->cameraModFinalMoveTo(&destination);
	}

}

//-------------------------------------------------------------------------------------------------
/** doResetCamera */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doResetCamera(const AsciiString& waypoint, Real sec, Real easeIn, Real easeOut)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			Coord3D destination = *way->getLocation();
			TheTacticalView->resetCamera(&destination, sec*1000, easeIn*1000.0f, easeOut*1000.0f);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doRotateCamera around the object we are looking at. */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCamera(Real rotations, Real sec, Real easeIn, Real easeOut)
{
	TheTacticalView->rotateCamera(rotations, sec*1000.0f, easeIn*1000.0f, easeOut*1000.0f);
}

//-------------------------------------------------------------------------------------------------
/** doRotateCameraTowardObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCameraTowardObject(const AsciiString& unitName, Real sec, Real holdSec, Real easeIn, Real easeOut)
{
	const Object *unit = TheScriptEngine->getUnitNamed(unitName);
	if (!unit)
		return;
	TheTacticalView->rotateCameraTowardObject(unit->getID(), sec*1000.0f, holdSec*1000.0f, easeIn*1000.0f, easeOut*1000.0f);
}

//-------------------------------------------------------------------------------------------------
/** doRotateCameraTowardWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRotateCameraTowardWaypoint(const AsciiString& waypointName, Real sec, Real easeIn, Real easeOut, Bool reverseRotation)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (way==nullptr) return;
	TheTacticalView->rotateCameraTowardPosition(way->getLocation(), sec*1000.0f, easeIn*1000.0f, easeOut*1000.0f, reverseRotation);
}

//-------------------------------------------------------------------------------------------------
/** doMoveAlongWaypointPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveCameraAlongWaypointPath(const AsciiString& waypoint, Real sec, Real cameraStutterSec, Real easeIn, Real easeOut)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext()) {
		if (way->getName() == waypoint) {
			TheTacticalView->moveCameraAlongWaypointPath(way, sec*1000, cameraStutterSec*1000, true, easeIn*1000.0f, easeOut*1000.0f);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doCreateObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateObject(const AsciiString& objectName, const AsciiString& thingName, const AsciiString& teamName, Coord3D *pos, Real angle )
{
	Object* pOldObj = nullptr;

	if (objectName != m_unnamedUnit) {
		 pOldObj = TheScriptEngine->getUnitNamed(objectName);

		if (pOldObj && !pOldObj->isEffectivelyDead()) {
			AsciiString str = "WARNING - Object with name ";
			str.concat(objectName);
			str.concat(" already exists. Failed Create.");
			TheScriptEngine->AppendDebugMessage(str, false);
				// Unit by that name already exists
			return;
		}
	}

	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam==nullptr) {
		// We may need to create the team.
		theTeam = TheTeamFactory->createTeam( teamName );
	}
	if (!theTeam) {
		TheScriptEngine->AppendDebugMessage("***WARNING - Team not found:***", false);
		TheScriptEngine->AppendDebugMessage(teamName, true);
		DEBUG_LOG(("WARNING - Team %s not found.", teamName.str()));
		return;
	}
	const ThingTemplate *thingTemplate;
	// get thing template based from map object name
	thingTemplate = TheThingFactory->findTemplate(thingName);
	if (thingTemplate) {
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, theTeam );
		if( obj )
		{
			if (objectName != m_unnamedUnit) {
				obj->setName(objectName);
				if (pOldObj || TheScriptEngine->didUnitExist(objectName)) {
					TheScriptEngine->transferObjectName(objectName, obj);
				} else {
					TheScriptEngine->addObjectToCache(obj);
				}
			}

			obj->setOrientation(angle);
			obj->setPosition( pos );

      if ( obj->isKindOf( KINDOF_BLAST_CRATER ) ) // since these footprints are permanent
      {
        TheTerrainLogic->createCraterInTerrain( obj );
        TheAI->pathfinder()->addObjectToPathfindMap( obj );
      }


		}
	} else {
		DEBUG_LOG(("WARNING - ThingTemplate '%s' not found.", thingName.str()));
	}
}

//-------------------------------------------------------------------------------------------------
/** doAttack */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAttack(const AsciiString& attackerName, const AsciiString& victimName)
{
	Team *attackingTeam = TheScriptEngine->getTeamNamed( attackerName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	const Team *victimTeam = TheScriptEngine->getTeamNamed( victimName );

	// sanity
	if( attackingTeam == nullptr || victimTeam == nullptr )
		return;

	AIGroupPtr aiGroup = TheAI->createGroup();
	if (!aiGroup) {
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	attackingTeam->getTeamAsAIGroup(aiGroup);
#else
	attackingTeam->getTeamAsAIGroup(aiGroup.Peek());
#endif
	aiGroup->groupAttackTeam(victimTeam, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);

}

//-------------------------------------------------------------------------------------------------
/** doNamedAttack */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttack(const AsciiString& attackerName, const AsciiString& victimName)
{
	/// @todo Implement me (MSB)

	Object *attackingObj = TheScriptEngine->getUnitNamed( attackerName );
	Object *victimObj = TheScriptEngine->getUnitNamed( victimName );

	if (!attackingObj || !victimObj) {
		return;
	}
	// tell every member of attacking team to attack a random member of victim team

	{
		AIUpdateInterface *aiUpdate = attackingObj->getAIUpdateInterface();

		if (aiUpdate)
		{
			/// @todo Teams should have a method that returns the number of members in the team (MSB)
			attackingObj->leaveGroup();
			aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			aiUpdate->aiForceAttackObject( victimObj, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBuilding(const AsciiString& buildingType)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildSpecificBuilding(buildingType);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildSupplyCenter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildSupplyCenter(const AsciiString& player, const AsciiString& buildingType, Int cash)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (thePlayer) {
		thePlayer->buildBySupplies(cash, buildingType);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildObjectNearestTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildObjectNearestTeam( const AsciiString& playerName, const AsciiString& buildingType, const AsciiString& teamName )
{
	Player *thePlayer = TheScriptEngine->getPlayerFromAsciiString( playerName );
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if( thePlayer && theTeam )
	{
		thePlayer->buildSpecificBuildingNearestTeam( buildingType, theTeam );
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildUpgrade */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildUpgrade(const AsciiString& player, const AsciiString& upgrade)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (thePlayer) {
		thePlayer->buildUpgrade(upgrade);
	}
}


//-------------------------------------------------------------------------------------------------
/** doBuildBaseDefense */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBaseDefense(Bool flank)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildBaseDefense(flank);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBuildBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildBaseStructure(const AsciiString& buildingType, Bool flank)
{
	// This action ALWAYS occur on the current player.
	Player *thePlayer = TheScriptEngine->getCurrentPlayer();
	if (thePlayer) {
		thePlayer->buildBaseDefenseStructure(buildingType, flank);
	}
}


//-------------------------------------------------------------------------------------------------
/** createUnitOnTeamAt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::createUnitOnTeamAt(const AsciiString& unitName, const AsciiString& objType, const AsciiString& teamName, const AsciiString& waypoint)
{
	Object* pOldObj = TheScriptEngine->getUnitNamed(unitName);

	if (pOldObj && !pOldObj->isEffectivelyDead()) {
		AsciiString str = "WARNING - Object with name ";
		str.concat(unitName);
		str.concat(" already exists. Failed Create.");
		TheScriptEngine->AppendDebugMessage(str, false);
			// Unit by that name already exists
		return;
	}

	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (theTeam==nullptr) {
		// We may need to create the team.
		theTeam = TheTeamFactory->createTeam( teamName );
	}
	if (!theTeam) {
		TheScriptEngine->AppendDebugMessage("***WARNING - Team not found:***", false);
		TheScriptEngine->AppendDebugMessage(teamName, true);
		DEBUG_LOG(("WARNING - Team %s not found.", teamName.str()));
		return;
	}
	const ThingTemplate *thingTemplate;
	// get thing template based from map object name
	thingTemplate = TheThingFactory->findTemplate(objType);
	if (thingTemplate) {
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, theTeam );
		if( obj )
		{
			if (unitName != m_unnamedUnit) {
				obj->setName(unitName);
				if (pOldObj || TheScriptEngine->didUnitExist(unitName)) {
					TheScriptEngine->transferObjectName(unitName, obj);
				} else {
					TheScriptEngine->addObjectToCache(obj);
				}
			}

			Waypoint *way = TheTerrainLogic->getWaypointByName( waypoint );
			if (way)
			{
				Coord3D destination = *way->getLocation();
				obj->setPosition(&destination);
			}
		}
	} else {
		DEBUG_LOG(("WARNING - ThingTemplate '%s' not found.", objType.str()));
	}
}

//-------------------------------------------------------------------------------------------------
/** updateNamedAttackPrioritySet */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateNamedAttackPrioritySet(const AsciiString& unitName, const AsciiString& attackPrioritySet)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}

	AIUpdateInterface *pInterface = theSrcUnit->getAIUpdateInterface();
	if (!pInterface) {
		return;
	}

	const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(attackPrioritySet);

	pInterface->setAttackInfo(info);

}

//-------------------------------------------------------------------------------------------------
/** updateTeamAttackPrioritySet */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateTeamAttackPrioritySet(const AsciiString& teamName, const AsciiString& attackPrioritySet)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(attackPrioritySet);

	if (info->getName().isNotEmpty()) {
		team->setAttackPriorityName(info->getName());
	}

	// Set team member's attack priority.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->setAttackInfo(info);
	}
}

//-------------------------------------------------------------------------------------------------
/** updateBaseConstructionSpeed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateBaseConstructionSpeed(const AsciiString& playerName, Int delayInSeconds)
{
	Player* thePlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (thePlayer) {
		thePlayer->setTeamDelaySeconds(delayInSeconds);
	}
}

//-------------------------------------------------------------------------------------------------
/** updateNamedSetAttitude */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateNamedSetAttitude(const AsciiString& unitName, Int attitude)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}

	AIUpdateInterface *pInterface = theSrcUnit->getAIUpdateInterface();
	if (!pInterface) {
		return;
	}

	pInterface->setAttitude((AttitudeType) attitude);
}

//-------------------------------------------------------------------------------------------------
/** updateTeamSetAttitude */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updateTeamSetAttitude(const AsciiString& teamName, Int attitude)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theSrcTeam) {
		return;
	}

	AIGroupPtr pAIGroup = TheAI->createGroup();
	if (!pAIGroup) {
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theSrcTeam->getTeamAsAIGroup(pAIGroup);
#else
	theSrcTeam->getTeamAsAIGroup(pAIGroup.Peek());
#endif
	pAIGroup->setAttitude((AttitudeType) attitude);
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetRepulsor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetRepulsor(const AsciiString& unitName, Bool repulsor)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed(unitName);
	if (!theSrcUnit) {
		return;
	}
	theSrcUnit->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REPULSOR ), repulsor);
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetRepulsor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetRepulsor(const AsciiString& teamName, Bool repulsor)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theSrcTeam) {
		return;
	}

	if (theSrcTeam)
	{
		for (DLINK_ITERATOR<Object> iter = theSrcTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();
			if (!obj)
			{
				continue;
			}
			obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REPULSOR ), repulsor );
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** doNamedAttackArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttackArea(const AsciiString& unitName, const AsciiString& areaName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theSrcUnit) {
		return;
	}

	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	if (!pTrig) {
		return;
	}


	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if( !aiUpdate )
	{
		return;
	}
	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiAttackArea(pTrig, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedAttackTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAttackTeam(const AsciiString& unitName, const AsciiString& teamName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theSrcUnit) {
		return;
	}

	const Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiAttackTeam(theTeam, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamAttackArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackArea(const AsciiString& teamName, const AsciiString& areaName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif

	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	if (!pTrig) {
		return;
	}

	theGroup->groupAttackArea(pTrig, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamAttackNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAttackNamed(const AsciiString& teamName, const AsciiString& unitName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	Object *theVictim = TheScriptEngine->getUnitNamed( unitName );
	if (!theVictim) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupAttackObject(theVictim, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doLoadAllTransports */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doLoadAllTransports(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if (!theTeam) {
		return;
	}

	EntriesVec vecOfUnits;
	SpacesVec vecOfTransports;

	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}

		if( obj->isKindOf(KINDOF_TRANSPORT) )
		{
			ContainModuleInterface *contain = obj->getContain();
			if( contain )
			{
				vecOfTransports.push_back(std::make_pair(obj->getID(), ((TransportContain*)obj->getContain())->getContainMax()));
			}
			else
			{
				DEBUG_CRASH( ("doLoadAllTransports script -- transport doesn't have a container!") );
			}
		}
		else
		{
			vecOfUnits.push_back(std::make_pair(obj->getID(), obj->getTransportSlotCount()));
		}
	}

	// we've now built the necessary stuff. Partition the units.

	PartitionSolver partition(vecOfUnits, vecOfTransports, PREFER_FAST_SOLUTION);
	partition.solve();
	const SolutionVec& solution = partition.getSolution();
	for (size_t i = 0; i < solution.size(); ++i) {
		Object *unit = TheGameLogic->findObjectByID(solution[i].first);
		Object *trans = TheGameLogic->findObjectByID(solution[i].second);
		if (!unit || !trans) {
			continue;
		}
		if( unit->getAIUpdateInterface() )
		{
			unit->getAIUpdateInterface()->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
			unit->getAIUpdateInterface()->aiEnter(trans, CMD_FROM_SCRIPT);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedEnterNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEnterNamed(const AsciiString& unitSrcName, const AsciiString& unitDestName)
{
	Object *theSrcUnit = TheScriptEngine->getUnitNamed( unitSrcName );
	if (!theSrcUnit) {
		return;
	}

	Object *theTransport = TheScriptEngine->getUnitNamed( unitDestName );
	if (!theTransport) {
		return;
	}

	AIUpdateInterface* aiUpdate = theSrcUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}
	theSrcUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiEnter(theTransport, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamEnterNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEnterNamed(const AsciiString& teamName, const AsciiString& unitDestName)
{
	Team *theSrcTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theSrcTeam) {
		return;
	}

	Object *theTransport = TheScriptEngine->getUnitNamed( unitDestName );
	if (!theTransport) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();

#if RETAIL_COMPATIBLE_AIGROUP
	theSrcTeam->getTeamAsAIGroup(theGroup);
#else
	theSrcTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupEnter(theTransport, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedExitAll */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedExitAll(const AsciiString& unitName)
{
	Object *theTransport = TheScriptEngine->getUnitNamed( unitName );
	if (!theTransport) {
		return;
	}

	AIUpdateInterface* aiUpdate = theTransport->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	theTransport->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiEvacuate( FALSE, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamExitAll */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamExitAll(const AsciiString& teamName)
{
	Team *theTeamOfTransports = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeamOfTransports) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();

#if RETAIL_COMPATIBLE_AIGROUP
	theTeamOfTransports->getTeamAsAIGroup(theGroup);
#else
	theTeamOfTransports->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupEvacuate( CMD_FROM_SCRIPT );
}


//-------------------------------------------------------------------------------------------------
/** doNamedSetGarrisonEvacDisposition */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetGarrisonEvacDisposition(const AsciiString& unitName, UnsignedInt disp )
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	ContainModuleInterface *contain = theUnit->getContain();
  if( contain )
		contain->setEvacDisposition( (EvacDisposition)disp );
    // should be safe to cast any-old int to this enum,
    // since only 1(EVAC_TO_LEFT) and 2(EVAC_TO_RIGHT) differ from default case

}




//-------------------------------------------------------------------------------------------------
/** doNamedFollowWaypoints */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFollowWaypoints(const AsciiString& unitName, const AsciiString& waypointPathLabel)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}
	Coord3D pos = *theUnit->getPosition();
	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this."));

	theUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiFollowWaypointPath(way, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doNamedFollowWaypointsExact */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFollowWaypointsExact(const AsciiString& unitName, const AsciiString& waypointPathLabel)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}
	Coord3D pos = *theUnit->getPosition();
	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this."));

	theUnit->leaveGroup();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiFollowWaypointPathExact(way, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature (D-126) Iran AI pushes attack the weakest side of the enemy base (owner: "attacking from the right
// places like finding a gap"). For an Iran skirmish AI team ordered onto the 'Center' approach path (every D-126 push),
// the map's Center, Flank and Backdoor paths into the enemy's base are compared by the enemy defenses along them: the
// path the team would take (from the waypoint nearest the team, following the links) and every hostile armed structure
// within IRAN_PATH_DEFENSE_REACH of it, rated build cost x health fraction. The least defended path wins; another path
// replaces Center (then Flank) only when it is at least IRAN_PATH_SWITCH_MARGIN cheaper. The choice is made at the move
// order and kept for that team's follow order (it comes later, from the path's start). Other labels (the Backdoor
// commando raid), stock AIs and IranTeamTarget = Legacy runs keep the requested path. Synced: waypoint and object order.
//-------------------------------------------------------------------------------------------------
static const Real IRAN_PATH_DEFENSE_REACH = 350.0f;
static const Real IRAN_PATH_SWITCH_MARGIN = 500.0f;
static const UnsignedInt IRAN_PATH_CHOICE_FRAMES = LOGICFRAMES_PER_SECOND * 300;
enum { IRAN_PATH_LABEL_COUNT = 3, IRAN_PATH_MAX_WAYPOINTS = 64 };
static const char *const IRAN_PATH_LABELS[IRAN_PATH_LABEL_COUNT] = { SKIRMISH_CENTER, SKIRMISH_FLANK, SKIRMISH_BACKDOOR };

static Real iranSegmentDistanceSqr( const Coord3D *a, const Coord3D *b, const Coord3D *point )
{
	const Real dx = b->x - a->x, dy = b->y - a->y;
	const Real lengthSqr = dx * dx + dy * dy;
	Real t = lengthSqr > 0.0f ? ( ( point->x - a->x ) * dx + ( point->y - a->y ) * dy ) / lengthSqr : 0.0f;
	if( t < 0.0f ) t = 0.0f;
	if( t > 1.0f ) t = 1.0f;
	return sqr( point->x - a->x - t * dx ) + sqr( point->y - a->y - t * dy );
}

// Enemy defense value along the path 'pathLabel' from the waypoint nearest 'from'; -1: no such path on this map.
static Real iranPathDefense( const Coord3D *from, const AsciiString &pathLabel, const std::vector<Coord3D> &defensePos,
	const std::vector<Real> &defenseValue )
{
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( from, pathLabel );
	if( way == nullptr )
		return -1.0f;
	const Coord3D *chain[IRAN_PATH_MAX_WAYPOINTS];
	Waypoint *visited[IRAN_PATH_MAX_WAYPOINTS];
	Int length = 0;
	while( way != nullptr && length < IRAN_PATH_MAX_WAYPOINTS )
	{
		Bool seen = FALSE;
		for( Int v = 0; v < length; ++v )
			if( visited[v] == way )
				seen = TRUE;
		if( seen )
			break;
		visited[length] = way;
		chain[length++] = way->getLocation();
		way = way->getNumLinks() > 0 ? way->getLink( 0 ) : nullptr;
	}
	Real total = 0.0f;
	for( size_t d = 0; d < defensePos.size(); ++d )
	{
		for( Int w = 0; w < length; ++w )
		{
			const Coord3D *next = w + 1 < length ? chain[w + 1] : chain[w];
			if( iranSegmentDistanceSqr( chain[w], next, &defensePos[d] ) <= sqr( IRAN_PATH_DEFENSE_REACH ) )
			{
				total += defenseValue[d];
				break;
			}
		}
	}
	return total;
}

// The path label (with the enemy start number) the team takes; 'decide': a move order (choose now), else reuse.
static AsciiString iranApproachPath( Team *team, const AsciiString &requested, const Coord3D *teamPos, Int mpNdx,
	Int members, Bool decide )
{
	AsciiString pathLabel;
	pathLabel.format( "%s%d", requested.str(), mpNdx );
	Player *aiPlayer = TheScriptEngine->getCurrentPlayer();
	if( team == nullptr || aiPlayer == nullptr || !aiPlayer->isSkirmishAIPlayer() || aiPlayer->getSide() != "Iran"
		|| requested.compareNoCase( SKIRMISH_CENTER ) != 0
		|| ( TheHypersonicScenario != nullptr && TheHypersonicScenario->isLegacyIranTeamTarget() ) )
		return pathLabel;
	const UnsignedInt now = TheGameLogic->getFrame();
	size_t c;
	if( !decide )
	{
		for( c = 0; c < s_iranPathChoices.size(); ++c )
		{
			const IranPathChoice &choice = s_iranPathChoices[c];
			if( choice.m_team != team->getID() || choice.m_frame > now || choice.m_frame + IRAN_PATH_CHOICE_FRAMES < now )
				continue;
			AsciiString kept;
			kept.format( "%s%d", IRAN_PATH_LABELS[choice.m_label], mpNdx );
			return TheTerrainLogic->getClosestWaypointOnPath( teamPos, kept ) != nullptr ? kept : pathLabel;
		}
	}
	// drop stale choices and this team's old one (replaced below)
	size_t keep = 0;
	for( c = 0; c < s_iranPathChoices.size(); ++c )
	{
		const IranPathChoice &choice = s_iranPathChoices[c];
		if( choice.m_team == team->getID() || choice.m_frame > now || choice.m_frame + IRAN_PATH_CHOICE_FRAMES < now )
			continue;
		s_iranPathChoices[keep++] = choice;
	}
	s_iranPathChoices.resize( keep );

	// hostile armed structures (base defenses, or any structure with a weapon)
	std::vector<Coord3D> defensePos;
	std::vector<Real> defenseValue;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_FS_FAKE )
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->getTeam() == nullptr
			|| aiPlayer->getRelationship( obj->getTeam() ) != ENEMIES )
			continue;
		if( !obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && obj->getWeaponInWeaponSlot( PRIMARY_WEAPON ) == nullptr )
			continue;
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body == nullptr || body->getMaxHealth() <= 0.0f )
			continue;
		const Int cost = obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() );
		defensePos.push_back( *obj->getPosition() );
		defenseValue.push_back( (Real)( cost > 0 ? cost : 500 ) * body->getHealth() / body->getMaxHealth() );
	}
	Real value[IRAN_PATH_LABEL_COUNT];
	Int best = -1;
	for( Int l = 0; l < IRAN_PATH_LABEL_COUNT; ++l )
	{
		AsciiString label;
		label.format( "%s%d", IRAN_PATH_LABELS[l], mpNdx );
		value[l] = iranPathDefense( teamPos, label, defensePos, defenseValue );
		if( value[l] < 0.0f )
			continue;
		if( best < 0 || value[l] + IRAN_PATH_SWITCH_MARGIN <= value[best] )
			best = l;
	}
	if( best < 0 )
		return pathLabel;	// no Center/Flank/Backdoor path to this enemy: the stock lookup decides
	IranPathChoice choice;
	choice.m_team = team->getID();
	choice.m_label = best;
	choice.m_frame = now;
	s_iranPathChoices.push_back( choice );
	if( TheHypersonicScenario != nullptr )
	{
		char values[3][32];
		for( Int v = 0; v < IRAN_PATH_LABEL_COUNT; ++v )
		{
			if( value[v] < 0.0f )
				strlcpy( values[v], "-", sizeof( values[v] ) );
			else
				snprintf( values[v], sizeof( values[v] ), "%.0f", value[v] );
		}
		Player *enemy = TheScriptEngine->getSkirmishEnemyPlayer();
		TheHypersonicScenario->noteAI( "AI PATH %s team '%s' (%d units) -> %s: Center %s / Flank %s / Backdoor %s => %s",
			TheHypersonicScenario->sideLabel( aiPlayer ), team->getName().str(), members,
			TheHypersonicScenario->sideLabel( enemy ), values[0], values[1], values[2], IRAN_PATH_LABELS[best] );
	}
	AsciiString result;
	result.format( "%s%d", IRAN_PATH_LABELS[best], mpNdx );
	return result;
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowSkirmishApproachPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowSkirmishApproachPath(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	Object *firstUnit=nullptr;
	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
		if (firstUnit==nullptr) {
			firstUnit = obj;
		}
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer==nullptr) return;
	Int mpNdx = enemyPlayer->getMpStartIndex()+1;

	// Hypersonic @feature (D-126) Iran AI: the path chosen at this team's move order
	AsciiString pathLabel = iranApproachPath( theTeam, waypointPathLabel, &pos, mpNdx, count, FALSE );
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, pathLabel );
	if (!way) {
		return;
	}

	Player *aiPlayer = TheScriptEngine->getCurrentPlayer();
	if (aiPlayer && firstUnit) {
		aiPlayer->checkBridges(firstUnit, way);
	}

	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, pathLabel), ("***Wrong waypoint purpose. Make jba fix this."));
	if (asTeam)
	{
		theGroup->groupFollowWaypointPathAsTeam(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowSkirmishApproachPath */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamMoveToSkirmishApproachPath(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer==nullptr) return;
	Int mpNdx = enemyPlayer->getMpStartIndex()+1;

	// Hypersonic @feature (D-126) Iran AI: the least defended path into the enemy base
	AsciiString pathLabel = iranApproachPath( theTeam, waypointPathLabel, &pos, mpNdx, count, TRUE );
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, pathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, pathLabel), ("***Wrong waypoint purpose. Make jba fix this."));
	theGroup->groupMoveToPosition(way->getLocation(), false, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowWaypoints */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowWaypoints(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this."));
	if (asTeam)
	{
		theGroup->groupFollowWaypointPathAsTeam(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFollowWaypointsExact */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFollowWaypointsExact(const AsciiString& teamName, const AsciiString& waypointPathLabel, Bool asTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Int count = 0;
	Coord3D pos;
	pos.x=pos.y=pos.z=0;

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		Coord3D objPos = *obj->getPosition();
		pos.x += objPos.x;
		pos.y += objPos.y;
		pos.z += objPos.z; // Not actually used by getClosestWaypointOnPath, but hey, might as well be correct.
		count++;
	}
	if (count==0) return; // empty team.
	pos.x /= count;
	pos.y /= count;
	pos.z /= count;

	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
	if (!way) {
		return;
	}
	DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPathLabel), ("***Wrong waypoint purpose. Make jba fix this."));
	if (asTeam)
	{
		theGroup->groupFollowWaypointPathAsTeamExact(way, CMD_FROM_SCRIPT);
	}	else {
		theGroup->groupFollowWaypointPathExact(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedGuard */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedGuard(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	theUnit->leaveGroup();
	Coord3D position = *theUnit->getPosition();
	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiGuardPosition(&position, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuard */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuard(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	// Have all the members of the team guard at their current pos.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardPosition */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardPosition(const AsciiString& teamName, const AsciiString& waypointName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !way) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Coord3D position = *way->getLocation();

	theGroup->groupGuardPosition( &position, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardObject */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardObject(const AsciiString& teamName, const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !theUnit) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupGuardObject( theUnit, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardArea(const AsciiString& teamName, const AsciiString& areaName)
{
	PolygonTrigger *pTrig = TheScriptEngine->getQualifiedTriggerAreaByName(areaName);
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam || !pTrig) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupGuardArea( pTrig, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doNamedHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedHunt(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface* aiUpdate = theUnit->getAIUpdateInterface();
	if (!aiUpdate) {
		return;
	}

	aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	aiUpdate->aiHunt( CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamHunt(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupHunt( CMD_FROM_SCRIPT );
}
//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamHuntWithCommandButton(const AsciiString& teamName, const AsciiString& ability)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}


	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}

		switch( commandButton->getCommandType() )
		{

			case GUI_COMMAND_SPECIAL_POWER:
				if( commandButton->getSpecialPowerTemplate() )
				{
					if (BitIsSet( commandButton->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ))
					{
						// OK, we can hunt with a power that targets an object.
						break;
					}
					AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
					msg.concat(ability);
					TheScriptEngine->AppendDebugMessage(msg, false);
					return;
				}
				return;
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUI_COMMAND_FIRE_WEAPON:
				{
					// ok, we can hunt with a weapon.
					break;
				}

			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
				//Various enter type hunts.
				break;

			case GUI_COMMAND_OBJECT_UPGRADE:
			case GUI_COMMAND_PLAYER_UPGRADE:
			case GUI_COMMAND_DOZER_CONSTRUCT:
			case GUI_COMMAND_DOZER_CONSTRUCT_CANCEL:
			case GUI_COMMAND_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UNIT_BUILD:
			case GUI_COMMAND_CANCEL_UPGRADE:
			case GUI_COMMAND_ATTACK_MOVE:
			case GUI_COMMAND_GUARD:
			case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
			case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
			case GUI_COMMAND_WAYPOINTS:
			case GUI_COMMAND_EXIT_CONTAINER:
			case GUI_COMMAND_EVACUATE:
			case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
			case GUI_COMMAND_BEACON_DELETE:
			case GUI_COMMAND_SET_RALLY_POINT:
			case GUI_COMMAND_SELL:
			case GUI_COMMAND_HACK_INTERNET:
			case GUI_COMMAND_TOGGLE_OVERCHARGE:
#ifdef ALLOW_SURRENDER
			case GUI_COMMAND_POW_RETURN_TO_PRISON:
#endif
#ifdef ALLOW_SURRENDER
			case GUICOMMANDMODE_PICK_UP_PRISONER:
#endif
			default:
				{
					AsciiString msg = "ERROR-Team hunt with command button - cannot hunt with ability ";
					msg.concat(ability);
					TheScriptEngine->AppendDebugMessage(msg, false);
					return;
				}
				break;
		}


	// Have all the members of the team do the command button.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Bool foundCommand = false;
		const CommandSet *commandSet = TheControlBar->findCommandSet( obj->getCommandSetString() );
		if( commandSet )
		{
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				const CommandButton *aCommandButton = commandSet->getCommandButton(i);
				if( commandButton == aCommandButton )
				{
					//We found the matching command button so now order the unit to do what the button wants.
					foundCommand = true;
					break;
				}
			}
		}
		if (!foundCommand) {
			AsciiString msg = "Error - Team hunt with command button - unit type '";
			msg.concat(obj->getTemplate()->getName().str());
			msg.concat("' is not valid for ability ");
			msg.concat(ability);
			TheScriptEngine->AppendDebugMessage(msg, false);
			continue;

		}

		switch( commandButton->getCommandType() )
		{

			case GUI_COMMAND_FIRE_WEAPON:
			case GUI_COMMAND_SWITCH_WEAPON:
			case GUI_COMMAND_SPECIAL_POWER:
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
			{
					static NameKeyType key_CommandButtonHuntUpdate = NAMEKEY("CommandButtonHuntUpdate");

					CommandButtonHuntUpdate* huntUpdate = (CommandButtonHuntUpdate*)obj->findUpdateModule(key_CommandButtonHuntUpdate);
					if( huntUpdate  )
					{
						huntUpdate->setCommandButton(ability);
					} else {
						AsciiString msg = "Error - Team hunt with command button - unit type '";
						msg.concat(obj->getTemplate()->getName().str());
						msg.concat("' requires CommandButtonHuntUpdate in .ini definition to hunt with ");
						msg.concat(ability);
						TheScriptEngine->AppendDebugMessage(msg, false);
					}
				}
				break;

		}

	}

}

//-------------------------------------------------------------------------------------------------
/** doTeamHunt */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerHunt(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setUnitsShouldHunt(true, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerSellEverything */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSellEverything(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->sellEverythingUnderTheSun();
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableBaseConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableBaseConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setCanBuildBase(false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableFactories */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableFactories(const AsciiString& playerName, const AsciiString& objectName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setObjectsEnabled(objectName, false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerDisableUnitConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerDisableUnitConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setCanBuildUnits(false);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableBaseConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableBaseConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}

	pPlayer->setCanBuildBase(true);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableFactories */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableFactories(const AsciiString& playerName, const AsciiString& objectName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->setObjectsEnabled(objectName, true);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerRepairStructure */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerRepairStructure(const AsciiString& playerName, const AsciiString& structureName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	Object *pStructure = TheScriptEngine->getUnitNamed(structureName);

	if (!pStructure) {
		return;
	}
	pPlayer->repairStructure(pStructure->getID());
}

//-------------------------------------------------------------------------------------------------
/** doPlayerEnableUnitConstruction */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerEnableUnitConstruction(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}

	pPlayer->setCanBuildUnits(true);
}

//-------------------------------------------------------------------------------------------------
/** doCameraMoveHome */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMoveHome()
{

}

//-------------------------------------------------------------------------------------------------
/** doBuildTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBuildTeam(const AsciiString& teamName)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( teamName );
	if (theTeamProto) {
		Player *player = theTeamProto->getControllingPlayer();
		if (player) {
			player->buildSpecificTeam(theTeamProto);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doRecruitTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRecruitTeam(const AsciiString& teamName, Real recruitRadius)
{
	TeamPrototype *theTeamProto = TheTeamFactory->findTeamPrototype( teamName );
	if (theTeamProto) {
		Player *player = theTeamProto->getControllingPlayer();
		if (player) {
			player->recruitSpecificTeam(theTeamProto, recruitRadius);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedDamage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedDamage(const AsciiString& unitName, Int damageAmt)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}
	DamageInfo damageInfo;
	damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
	damageInfo.in.m_deathType = DEATH_NORMAL;
	damageInfo.in.m_sourceID = INVALID_ID;
	damageInfo.in.m_amount = damageAmt;
	pUnit->attemptDamage( &damageInfo );
}

//-------------------------------------------------------------------------------------------------
/** doNamedDelete */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedDelete(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	TheGameLogic->destroyObject(theUnit);
}

//-------------------------------------------------------------------------------------------------
/** doTeamDelete */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamDelete(const AsciiString& teamName, Bool ignoreDead)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	team->deleteTeam(ignoreDead);
}

//-------------------------------------------------------------------------------------------------
/** doTeamWander */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamWander(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Have all the members of the team wander.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
		if (!way) {
			return;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
		ai->aiWander(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamIncreasePriority */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamIncreasePriority(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}
	const TeamPrototype *theTeamProto = team->getPrototype();

	if (!theTeamProto) {
		return;
	}
	theTeamProto->increaseAIPriorityForSuccess();
	AsciiString msg;
	msg.format("Team '%s' priority increased to %d for success.", teamName.str(), theTeamProto->getTemplateInfo()->m_productionPriority);
	TheScriptEngine->AppendDebugMessage(msg, false);

}

//-------------------------------------------------------------------------------------------------
/** doTeamDecreasePriority */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamDecreasePriority(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}
	const TeamPrototype *theTeamProto = team->getPrototype();

	if (!theTeamProto) {
		return;
	}
	theTeamProto->decreaseAIPriorityForFailure();
	AsciiString msg;
	msg.format("Team '%s' priority decreased to %d for failure.", teamName.str(), theTeamProto->getTemplateInfo()->m_productionPriority);
	TheScriptEngine->AppendDebugMessage(msg, false);

}

//-------------------------------------------------------------------------------------------------
/** doTeamWanderInPlace */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamWanderInPlace(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Have all the members of the team wander.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_WANDER);
		ai->aiWanderInPlace(CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamPanic */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamPanic(const AsciiString& teamName, const AsciiString& waypointPathLabel)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	// Get the center point for the team
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPathLabel );
		if (!way) {
			return;
		}
		ai->chooseLocomotorSet(LOCOMOTORSET_PANIC);
		ai->aiPanic(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedKill(const AsciiString& unitName)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}
	pUnit->kill();
}

//-------------------------------------------------------------------------------------------------
/** doTeamKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamKill(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	team->killTeam();
}

//-------------------------------------------------------------------------------------------------
/** doPlayerKill */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerKill(const AsciiString& playerName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!pPlayer) {
		return;
	}
	pPlayer->killPlayer();
}

//-------------------------------------------------------------------------------------------------
/** doDisplayText */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayText(const AsciiString& displayText)
{
	TheInGameUI->message(displayText);
}

//-------------------------------------------------------------------------------------------------
/** doInGamePopupMessage */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doInGamePopupMessage( const AsciiString& message, Int x, Int y, Int width, Bool pause )
{
	TheInGameUI->popupMessage(message, x,y,width, pause, FALSE);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCinematicText */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayCinematicText(const AsciiString& displayText, const AsciiString& fontType, Int timeInSeconds)
{
	// set the text
	UnicodeString uStr = TheGameText->fetch( displayText );
	AsciiString aStr;
	aStr.translate( uStr );
	TheDisplay->setCinematicText( aStr );

	// Gets the font info from parsing through fontType

	// get the font name
	AsciiString fontName = AsciiString::TheEmptyString;

	// TheSuperHackers @fix xezon 16/03/2025 Fixes potential buffer overrun via prior c!='\0' test.
	const char* c = fontType.str();
	for (; *c != '\0'; c++)
	{
		if( *c != ' ' && *c++ != '-' )
			fontName.concat(c);
		else
			break;
	}
	while( *c != ':' )
		c++;
	c++;  // eat through " - Size:"

	// get font size
	AsciiString fontSize = AsciiString::TheEmptyString;
	for( ; *c != '\0'; c++ )
	{
		if( *c != '\0' && *c != ' ' )
		{
			fontSize.concat( *c );
		}
		else
		{
			break;
		}
	}
	Int size = atoi( fontSize.str() );

	// get font fold
	Bool bold = FALSE;
	if( fontType.endsWith( "[Bold]" ) )
		bold = TRUE;

	// phew, now set as new font
	GameFont *font = TheFontLibrary->getFont( fontName,
		TheGlobalLanguageData->adjustFontSize(size), bold );
	TheDisplay->setCinematicFont( font );

	// set time
	Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
	TheDisplay->setCinematicTextFrames( frames );
}
//-------------------------------------------------------------------------------------------------
/** doCameoFlash */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameoFlash(const AsciiString& name, Int timeInSeconds)
{
	const CommandButton *button;

	//sanity
	button = TheControlBar->findCommandButton( name );
	if( button == nullptr )
	{
		DEBUG_CRASH(( "ScriptActions::doCameoFlash can't find AsciiString cameoflash" ));
		return;
	}

	Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
	// every time the framecount % 20 == 0,  controlbar:: update will do Cameo Flash
	Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
	// make sure count is even, so the cameo will return to its original state
	if( count % 2 == 1 )
		count++;

	button->setFlashCount(count);
	TheControlBar->setFlash( TRUE );

}

//-------------------------------------------------------------------------------------------------
/** doNamedCustomColor */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedCustomColor(const AsciiString& unitName, Color c)
{
	//sanity
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if ( !obj )
	{
		return;
	}
	obj->setCustomIndicatorColor(c);
}

//-------------------------------------------------------------------------------------------------
/** doNamedFlash */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFlash(const AsciiString& unitName, Int timeInSeconds, const RGBColor *color)
{
	/** This is called the first time this unit is told by the script to flash. timeInSeconds will tell the drawable
	how long to flash for.  Sets drawable to start flashing but only allows drawable's update to
	call the actual flash method */

	//sanity
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if ( !obj )
	{
		return;
	}
	Drawable *drawable = obj->getDrawable();
	if( !drawable )
	{
		return;
	}

	if( timeInSeconds > 0 )
	{
		// set count for drawable, but do not flash, allow drawable update to handle it

		/* The designer specifies how long he wants the unit to flash for.  We will
		convert this number into a count of how many times to call the doNamedFlash method */
		Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;
		// every time the framecount % 20 == 0, drawable::update will call doNamedFlash
		Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
		Color flashy = (color == nullptr) ? obj->getIndicatorColor() : color->getAsInt();
		drawable->setFlashColor( flashy );
		drawable->setFlashCount( count );
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamFlash */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFlash(const AsciiString& teamName, Int timeInSeconds, const RGBColor *color)
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if (team == nullptr || !team->hasAnyObjects())
		return;

	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();

	while( !iter.done() ) {
		Object *nextObj = iter.cur();
		Object *obj = nextObj;
		if (!obj) {
			break;
		}

		iter.advance();
		Drawable *draw = obj->getDrawable();
		if( !draw )
			break;
		Int frames = LOGICFRAMES_PER_SECOND * timeInSeconds;

		Int count = frames / DRAWABLE_FRAMES_PER_FLASH;
		Color flashy = (color == nullptr) ? obj->getIndicatorColor() : color->getAsInt();
		draw->setFlashColor( flashy );
		draw->setFlashCount( count );
	}

}

#define ARBITRARY_BUFFER_SIZE	128
//-------------------------------------------------------------------------------------------------
/** doMoviePlayFullScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoviePlayFullScreen(const AsciiString& movieName)
{
	TheDisplay->playMovie(movieName);
}

//-------------------------------------------------------------------------------------------------
/** doMoviePlayRadar */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoviePlayRadar(const AsciiString& movieName)
{
	TheInGameUI->playMovie(movieName);
}

//-------------------------------------------------------------------------------------------------
/** doSoundPlayFromNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundPlayFromNamed(const AsciiString& soundName, const AsciiString& unitName)
{
	Object *pUnit = TheScriptEngine->getUnitNamed(unitName);

	if (!pUnit) {
		return;
	}

	AudioEventRTS sfx(soundName, pUnit->getID());
	sfx.setIsLogicalAudio(true);
	TheAudio->addAudioEvent(&sfx);
}

//-------------------------------------------------------------------------------------------------
/** doSpeechPlay */
//-------------------------------------------------------------------------------------------------
enum
{
	SUBTITLE_DURATION = 8000
};
void ScriptActions::doSpeechPlay(const AsciiString& speechName, Bool allowOverlap)
{
	AudioEventRTS speech(speechName);
	speech.setIsLogicalAudio(true);
	speech.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	speech.setUninterruptible(!allowOverlap);
	TheAudio->addAudioEvent(&speech);


	AsciiString subtitleLabel("DIALOGEVENT:");
	subtitleLabel.concat(speechName);
	subtitleLabel.concat("Subtitle");

	// Found is important, because a failure will return a valid "Missing: 'Label'" string.
	Bool found = FALSE;
	UnicodeString subtitle = TheGameText->fetch(subtitleLabel, &found);
	if( found && !subtitle.isEmpty() && subtitle.getCharAt(0) != '*')
	{
		// Foreign versions can specify region specific subtitle strings if they want.
		// English will have strings with / for easy translation, but they don't want to display.
		TheInGameUI->militarySubtitle( subtitleLabel, SUBTITLE_DURATION );
	}
}

//-------------------------------------------------------------------------------------------------
/** doPlayerTransferAssetsToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerTransferAssetsToPlayer(const AsciiString& playerSrcName, const AsciiString& playerDstName)
{
	Player *pPlayerDest = TheScriptEngine->getPlayerFromAsciiString(playerDstName);
	Player *pPlayerSrc = TheScriptEngine->getPlayerFromAsciiString(playerSrcName);

	if (!pPlayerDest || !pPlayerSrc) {
		return;
	}

	pPlayerDest->transferAssetsFromThat(pPlayerSrc);
}

//-------------------------------------------------------------------------------------------------
/** doNamedTransferAssetsToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedTransferAssetsToPlayer(const AsciiString& unitName, const AsciiString& playerDstName)
{
	Object *pObj = TheScriptEngine->getUnitNamed(unitName);
	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerDstName);

	if (!pObj || !pPlayer) {
		return;
	}

	Team *playerTeam = pPlayer->getDefaultTeam();
	if (!playerTeam) {
		return;
	}

	pObj->setTeam(playerTeam);
	updateTeamAndPlayerStuff(pObj, nullptr);
}

//-------------------------------------------------------------------------------------------------
/** excludePlayerFromScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::excludePlayerFromScoreScreen(const AsciiString& playerName)
{
	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (pPlayer == nullptr) {
		return;
	}

	pPlayer->setListInScoreScreen(FALSE);
}

//-------------------------------------------------------------------------------------------------
/** excludePlayerFromScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScriptActions::enableScoring(Bool score)
{
	TheGameLogic->enableScoring(score);
}

//-------------------------------------------------------------------------------------------------
/** updatePlayerRelationTowardPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::updatePlayerRelationTowardPlayer(const AsciiString& playerSrcName, Int relationType, const AsciiString& playerDstName)
{
	Player *pPlayerDest = TheScriptEngine->getPlayerFromAsciiString(playerDstName);
	Player *pPlayerSrc = TheScriptEngine->getPlayerFromAsciiString(playerSrcName);

	if (!pPlayerDest || !pPlayerSrc) {
		return;
	}

	pPlayerSrc->setPlayerRelationship(pPlayerDest, (Relationship) relationType);
}

//-------------------------------------------------------------------------------------------------
/** doRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarCreateEvent(Coord3D *pos, Int eventType)
{
	TheRadar->createEvent(pos, (RadarEventType)eventType);
}

//-------------------------------------------------------------------------------------------------
/** doObjectRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doObjectRadarCreateEvent(const AsciiString& unitName, Int eventType)
{
	// get the building
	Object *theBuilding = TheScriptEngine->getUnitNamed( unitName );
	if (!theBuilding)
		return;

	// get building's position
	const Coord3D *pos = theBuilding->getPosition();
	if (!pos)
		return;

	// create event
	TheRadar->createEvent(pos, (RadarEventType)eventType);
}

//-------------------------------------------------------------------------------------------------
/** doTeamRadarCreateEvent */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRadarCreateEvent(const AsciiString& teamName, Int eventType)
{
	// get the team
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam)
		return;
	if (!theTeam->hasAnyUnits())
		return;

	// get team's position
	const Coord3D *pos = theTeam->getEstimateTeamPosition();
	if (!pos)
		return;

	// create event
	TheRadar->createEvent(pos, (RadarEventType)eventType);
}

//-------------------------------------------------------------------------------------------------
/** doRadarDisable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarDisable()
{
	TheRadar->hide(ThePlayerList->getLocalPlayer()->getPlayerIndex(), true);
}

//-------------------------------------------------------------------------------------------------
/** doRadarEnable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarEnable()
{
	TheRadar->hide(ThePlayerList->getLocalPlayer()->getPlayerIndex(), false);
}

//-------------------------------------------------------------------------------------------------
/** doCameraMotionBlurJump - zoom in at the current location, jump to waypoint, and zoom out.*/
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMotionBlurJump(const AsciiString& waypointName, Bool saturate)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	Bool passed = FALSE;	//assume all filters were applied correctly.
	Coord3D pos = *way->getLocation();
	if (TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER))
	{
		passed = TRUE;
		if (saturate) {
			if (!TheTacticalView->setViewFilterMode(FM_VIEW_MB_IN_AND_OUT_SATURATE))
			{	//failed to set filter so restore default state
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				passed = FALSE;
			}
		} else {
			if (!TheTacticalView->setViewFilterMode(FM_VIEW_MB_IN_AND_OUT_ALPHA))
			{	//failed to set filter so restore default state
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				passed = FALSE;
			};
		}
		if (passed)
		{
			TheTacticalView->setUserControlled(false);
			TheTacticalView->setViewFilterPos(&pos);
		}
	}
	if (!passed)
	{
		//if we failed to apply the filter, we still need to get the camera to the target
		//so do it another way:
		TheTacticalView->setUserControlled(false);
		TheTacticalView->lookAt(&pos);
	}
}

//-------------------------------------------------------------------------------------------------
/** doCameraMotionBlurJump - zoom in at the current location, jump to waypoint, and zoom out.*/
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraMotionBlur(Bool zoomIn, Bool saturate)
{
	if (TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER))
	{
		FilterModes mode;
		if (saturate) {
			if (zoomIn) {
				mode = FM_VIEW_MB_IN_SATURATE;
			} else {
				mode = FM_VIEW_MB_OUT_SATURATE;
			}
		} else {
			if (zoomIn) {
				mode = FM_VIEW_MB_IN_ALPHA;
			} else {
				mode = FM_VIEW_MB_OUT_ALPHA;
			}
		}
		if (!TheTacticalView->setViewFilterMode(mode))
		{	//failed to set the filter so restore everything to normal
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
		}
	}
}

static PlayerMaskType getHumanPlayerMask()
{
#if RETAIL_COMPATIBLE_CRC
	PlayerMaskType mask;
#else
	PlayerMaskType mask = 0;
#endif
	for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
	{
		const Player *player = ThePlayerList->getNthPlayer(i);
		if (player->getPlayerType() == PLAYER_HUMAN)
#if RETAIL_COMPATIBLE_CRC
			mask &= player->getPlayerMask();
#else
			mask |= player->getPlayerMask();
#endif
	}

	//DEBUG_LOG(("getHumanPlayerMask(): mask was %4.4X", mask));
	return mask;
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapAtWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapAtWaypoint(const AsciiString& waypointName, Real radiusToReveal, const AsciiString& playerName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	PlayerMaskType playerMask;
	if (player && playerName.isNotEmpty())
		playerMask = player->getPlayerMask();
	else
		playerMask = getHumanPlayerMask();

	Real positionX = way->getLocation()->x;
	Real positionY = way->getLocation()->y;

	// A reveal script is a quick look.  That way a Radar Jammer will still function correctly.
	ThePartitionManager->doShroudReveal(positionX, positionY, radiusToReveal, playerMask);
	ThePartitionManager->undoShroudReveal(positionX, positionY, radiusToReveal, playerMask);
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapAtWaypoint */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doShroudMapAtWaypoint(const AsciiString& waypointName, Real radiusToShroud, const AsciiString& playerName)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	if (!way) {
		return;
	}

	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	PlayerMaskType playerMask;
	if (player && playerName.isNotEmpty())
		playerMask = player->getPlayerMask();
	else
		playerMask = getHumanPlayerMask();

	Real positionX = way->getLocation()->x;
	Real positionY = way->getLocation()->y;

	// Likewise, this script does a dollop of shroud.  Not permanent active shroud
	ThePartitionManager->doShroudCover(positionX, positionY, radiusToShroud, playerMask);
	ThePartitionManager->undoShroudCover(positionX, positionY, radiusToShroud, playerMask);
}

//-------------------------------------------------------------------------------------------------
/** doRevealMapEntire */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapEntire(const AsciiString& playerName)
{
	DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player named '%s'", playerName.str()));
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player named '%ls' in position %d", player->getPlayerDisplayName().str(), player->getPlayerIndex()));
		ThePartitionManager->revealMapForPlayer( player->getPlayerIndex() );
	}
	else
	{
		DEBUG_LOG(("ScriptActions::doRevealMapEntire() - no player, so doing all human players"));
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doRevealMapEntire() for player %d", i));
				ThePartitionManager->revealMapForPlayer( i );
			}
		}
	}
}

void ScriptActions::doRevealMapEntirePermanently( Bool reveal, const AsciiString& playerName )
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		if( reveal )
			ThePartitionManager->revealMapForPlayerPermanently( player->getPlayerIndex() );
		else
			ThePartitionManager->undoRevealMapForPlayerPermanently( player->getPlayerIndex() );
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doRevealMapEntirePermanently() for player %d", i));
				if( reveal )
					ThePartitionManager->revealMapForPlayerPermanently( i );
				else
					ThePartitionManager->undoRevealMapForPlayerPermanently( i );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doShroudMapEntire */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doShroudMapEntire(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		ThePartitionManager->shroudMapForPlayer( player->getPlayerIndex() );
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doShroudMapEntire() for player %d", i));
				ThePartitionManager->shroudMapForPlayer( i );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamAvailableForRecruitment */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamAvailableForRecruitment(const AsciiString& teamName, Bool availability)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	theTeam->setRecruitable(availability);
}

//-------------------------------------------------------------------------------------------------
/** doCollectNearbyForTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCollectNearbyForTeam(const AsciiString& teamName)
{
	DEBUG_CRASH(("You would think this has been implemented, but you'd be wrong. (doCollectNearbyForTeam)"));
}

//-------------------------------------------------------------------------------------------------
/** doMergeTeamIntoTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMergeTeamIntoTeam(const AsciiString& teamSrcName, const AsciiString& teamDestName)
{
	Team *teamSrc = TheScriptEngine->getTeamNamed(teamSrcName);
	Team *teamDest = TheScriptEngine->getTeamNamed(teamDestName);
	if (teamDest==nullptr) {
		teamDest = TheTeamFactory->findTeam( teamDestName );
	}
	if (!teamSrc || !teamDest) {
		return;
	}

//	Bool done = FALSE;

	DLINK_ITERATOR<Object> iter = teamSrc->iterate_TeamMemberList();
	Object *nextObj = iter.cur();

	while (!iter.done()) {
		Object *obj = nextObj;
		if (!obj) {
			break;
		}

		// this has to be done here, setting the team will screw up the iterator. total bummer dude. jkmcd
		nextObj = iter.cur();
		iter.advance();
		obj->setTeam(teamDest);
		updateTeamAndPlayerStuff(obj, nullptr);
	}

	if (nextObj) {
		nextObj->setTeam(teamDest);
		updateTeamAndPlayerStuff(nextObj, nullptr);
	}

	teamSrc->deleteTeam();
	teamDest->setActive(); // in case we just created him.
}

//-------------------------------------------------------------------------------------------------
/** doDisableInput */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableInput()
{
#if defined(RTS_DEBUG)
	if (!TheGlobalData->m_disableScriptedInputDisabling)
#endif
	{
		TheInGameUI->setInputEnabled(false);
		TheMouse->setVisibility(false);
		TheInGameUI->deselectAllDrawables();
		TheInGameUI->clearAttackMoveToMode();
		TheInGameUI->setWaypointMode( FALSE );
		TheControlBar->deleteBuildTooltipLayout();
		TheLookAtTranslator->resetModes();
	}
}

//-------------------------------------------------------------------------------------------------
/** doEnableInput */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableInput()
{
	TheInGameUI->setInputEnabled(true);
	TheMouse->setVisibility(true);
}

//-------------------------------------------------------------------------------------------------
/** doSetBorderShroud */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetBorderShroud( Bool setting )
{
	if( setting )
		TheDisplay->setBorderShroudLevel(TheGlobalData->m_shroudAlpha);
	else
		TheDisplay->setBorderShroudLevel(TheGlobalData->m_clearAlpha);
}

//-------------------------------------------------------------------------------------------------
/** doIdleAllPlayerUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doIdleAllPlayerUnits(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		player->setUnitsShouldIdleOrResume(true);
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doIdleAllPlayerUnits() for player %d", i));
				player->setUnitsShouldIdleOrResume(true);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doResumeSupplyTruckingForIdleUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doResumeSupplyTruckingForIdleUnits(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (player && playerName.isNotEmpty())
	{
		player->setUnitsShouldIdleOrResume(false);
	}
	else
	{
		for (Int i=0; i<ThePlayerList->getPlayerCount(); ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player->getPlayerType() == PLAYER_HUMAN)
			{
				DEBUG_LOG(("ScriptActions::doResumeSupplyTruckingForIdleUnits() for player %d", i));
				player->setUnitsShouldIdleOrResume(false);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doAmbientSoundsPause */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAmbientSoundsPause(Bool pausing)	// if true, then pause, if false then resume.
{
	TheAudio->pauseAmbient(pausing);
}

//-------------------------------------------------------------------------------------------------
/** doMusicTrackChange */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMusicTrackChange(const AsciiString& newTrackName, Bool fadeout, Bool fadein)
{
	// Stop playing the music
	if (fadeout) {
		TheAudio->removeAudioEvent(AHSV_StopTheMusicFade);
	} else {
		TheAudio->removeAudioEvent(AHSV_StopTheMusic);
	}

	AudioEventRTS event(newTrackName);
	event.setShouldFade(fadein);
	event.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheAudio->addAudioEvent(&event);

	TheScriptEngine->setCurrentTrackName(newTrackName);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGarrisonSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGarrisonSpecificBuilding(const AsciiString& teamName, const AsciiString& buildingName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding) {
		return;
	}

	if( !theBuilding->getContain() )
	{
		DEBUG_CRASH( ("doTeamGarrisonSpecificBuilding script -- building doesn't have a container!" ) );
		return;
	}
	PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();

	if (!theBuilding->isKindOf(KINDOF_STRUCTURE) || (player != 0 && player != theTeam->getControllingPlayer()->getPlayerMask())) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupEnter(theBuilding, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doExitSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doExitSpecificBuilding(const AsciiString& buildingName)
{
	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding)
	{
		return;
	}

	if (!theBuilding->isKindOf(KINDOF_STRUCTURE))
	{
		return;
	}

	AIUpdateInterface *ai = theBuilding->getAIUpdateInterface();
	if (ai)
	{
		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiEvacuate( FALSE, CMD_FROM_SCRIPT );
		return;
	}

	ContainModuleInterface *contain = theBuilding->getContain();
	if (contain)
	{
		contain->removeAllContained( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamGarrisonNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGarrisonNearestBuilding(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	DLINK_ITERATOR<Object> diter = theTeam->iterate_TeamMemberList();
	Object *leader = diter.cur();
	if (!leader) {
		return;
	}


	PartitionFilter *filters[16];
	Int count = 0;

	PartitionFilterAcceptByKindOf f1( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );
	PartitionFilterGarrisonableByPlayer f2(theTeam->getControllingPlayer(), true, CMD_FROM_SCRIPT);

	if( leader->isKindOf( KINDOF_MONEY_HACKER ) )
	{
		//If the leader is a hacker, then look for an internet center instead of a normal building!
		filters[ count++ ] = &f1;
	}
	else
	{
		//If the leader ISN'T a hacker, then look for standard fare garrisonable buildings (internet centers won't show up)!
		filters[ count++ ] = &f2;
	}

	PartitionFilterSameMapStatus filterMapStatus(leader);
	filters[ count++ ] = &filterMapStatus;

	filters[count++] = nullptr;

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(leader, REALLY_FAR, FROM_CENTER_3D, filters, ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);


	// here's what we do. Find out how many slots each building has open, and tell each unit individually to
	// garrison a specific building. We won't use the partition solver because we've already done most of the work

	for (Object *theBuilding = iter->first(); theBuilding; theBuilding = iter->next()) {
		ContainModuleInterface *cmi = theBuilding->getContain();
		if (!cmi) {
			continue;
		}

		Int slotsAvailable = cmi->getContainMax() - cmi->getContainCount();
		for (int i = 0; i < slotsAvailable; ) {
			Object *obj = diter.cur();
			if (diter.done() || !obj) {
				return;
			}

			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (ai && obj->isKindOf(KINDOF_INFANTRY) && !obj->isKindOf(KINDOF_NO_GARRISON)) {
				ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
				++i;
			}
			diter.advance();
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamExitAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamExitAllBuildings(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (!theTeam) {
		return;
	}

	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}

		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}

		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiExit(nullptr, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doUnitGarrisonSpecificBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGarrisonSpecificBuilding(const AsciiString& unitName, const AsciiString& buildingName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	Object *theBuilding = TheScriptEngine->getUnitNamed(buildingName);
	if (!theBuilding) {
		return;
	}

	ContainModuleInterface *contain = theBuilding->getContain();
	if( !contain )
	{
		DEBUG_CRASH(("doUnitGarrisonSpecificBuilding script -- building doesn't have a container" ));
		return;
	}
	PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();

	if (!theBuilding->isKindOf(KINDOF_STRUCTURE) || (player != 0 && player != theUnit->getControllingPlayer()->getPlayerMask())) {
		return;
	}

	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doUnitGarrisonNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGarrisonNearestBuilding(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}


	PartitionFilter *filters[16];
	Int count = 0;

	PartitionFilterAcceptByKindOf f1(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	filters[ count++ ] = &f1;

	PartitionFilterSameMapStatus filterMapStatus(theUnit);
	filters[ count++ ] = &filterMapStatus;


	PartitionFilterAcceptByKindOf f2( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );
	PartitionFilterRejectByKindOf f3( MAKE_KINDOF_MASK( KINDOF_FS_INTERNET_CENTER ), KINDOFMASK_NONE );

	if( theUnit->isKindOf( KINDOF_MONEY_HACKER ) )
	{
		//If the unit is a hacker, then look for an internet center instead of a normal building!
		filters[ count++ ] = &f2;
	}
	else
	{
		//If the unit ISN'T a hacker, then ignore internet centers!
		filters[ count++ ] = &f3;
	}

	filters[count++] = nullptr;

	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(theUnit, REALLY_FAR, FROM_CENTER_3D, filters, ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);

	for (Object *theBuilding = iter->first(); theBuilding; theBuilding = iter->next())
	{
		ContainModuleInterface *contain = theBuilding->getContain();
		if( !contain )
		{
			DEBUG_CRASH( ("doUnitGarrisonNearestBuilding script -- building doesn't have a container.") );
			continue;
		}
		PlayerMaskType player = theBuilding->getContain()->getPlayerWhoEntered();
		if (!((player == 0) || (player == theUnit->getControllingPlayer()->getPlayerMask()))) {
			continue;
		}

		ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		ai->aiEnter(theBuilding, CMD_FROM_SCRIPT);
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedEnableStealth */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEnableStealth( const AsciiString& unitName, Bool enabled )
{
	Object *self = TheScriptEngine->getUnitNamed( unitName );
	if( self )
	{
		self->setScriptStatus( OBJECT_STATUS_SCRIPT_UNSTEALTHED, !enabled );
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamEnableStealth */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEnableStealth( const AsciiString& teamName, Bool enabled )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team )
	{
		return;
	}

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( obj )
		{
			obj->setScriptStatus( OBJECT_STATUS_SCRIPT_UNSTEALTHED, !enabled );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetUnmanned */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetUnmanned( const AsciiString& unitName )
{
	Object *self = TheScriptEngine->getUnitNamed( unitName );
	if( self )
	{
		self->setDisabled( DISABLED_UNMANNED );
		TheGameLogic->deselectObject( self, PLAYERMASK_ALL, TRUE );
		// Convert it to the neutral team so it renders gray giving visual representation that it is unmanned.
		self->setTeam( ThePlayerList->getNeutralPlayer()->getDefaultTeam() );
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetUnmanned */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetUnmanned( const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team )
	{
		return;
	}

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); )
	{
		Object *obj = iter.cur();

		//Advance the iterator NOW because when we unman the object, the team gets nuked, and
		//the iterator stops after the first iteration!
		iter.advance();
		//*************

		if( obj )
		{
			obj->setDisabled( DISABLED_UNMANNED );
			TheGameLogic->deselectObject( obj, PLAYERMASK_ALL, TRUE );
			// Convert it to the neutral team so it renders gray giving visual representation that it is unmanned.
			obj->setTeam( ThePlayerList->getNeutralPlayer()->getDefaultTeam() );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetBoobytrapped( const AsciiString& thingTemplateName, const AsciiString& unitName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
		const ThingTemplate *thing = TheThingFactory->findTemplate( thingTemplateName );
		if( thing )
		{
			Object *boobytrap = TheThingFactory->newObject( thing, obj->getTeam() );
			if( boobytrap )
			{
				static NameKeyType key_StickyBombUpdate = NAMEKEY( "StickyBombUpdate" );
				StickyBombUpdate *update = (StickyBombUpdate*)boobytrap->findUpdateModule( key_StickyBombUpdate );
				if( update )
				{
					//The charge gets positioned randomly on the outside of the perimeter of the victim.
					Coord3D pos;
					obj->getGeometryInfo().makeRandomOffsetOnPerimeter( pos );

					//Get the angle and transform matrix from the obj... then transform the calculated
					//position
					const Matrix3D *transform = obj->getTransformMatrix();
					transform->Transform_Vector( *transform, *(Vector3*)(&pos), (Vector3*)(&pos) );

					update->initStickyBomb( obj, nullptr, &pos );
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetBoobytrapped( const AsciiString& thingTemplateName, const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed( teamName );
	if( !team )
	{
		return;
	}

	for( DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();

		const ThingTemplate *thing = TheThingFactory->findTemplate( thingTemplateName );
		if( thing )
		{
			Object *boobytrap = TheThingFactory->newObject( thing, obj->getTeam() );
			if( boobytrap )
			{
				static NameKeyType key_StickyBombUpdate = NAMEKEY( "StickyBombUpdate" );
				StickyBombUpdate *update = (StickyBombUpdate*)boobytrap->findUpdateModule( key_StickyBombUpdate );
				if( update )
				{
					//The charge gets positioned randomly on the outside of the perimeter of the victim.
					Coord3D pos;
					obj->getGeometryInfo().makeRandomOffsetOnPerimeter( pos );

					//Get the angle and transform matrix from the obj... then transform the calculated
					//position
					const Matrix3D *transform = obj->getTransformMatrix();
					transform->Transform_Vector( *transform, *(Vector3*)(&pos), (Vector3*)(&pos) );

					update->initStickyBomb( obj, nullptr, &pos );
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doUnitExitBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitExitBuilding(const AsciiString& unitName)
{
	Object *theUnit = TheScriptEngine->getUnitNamed( unitName );
	if (!theUnit) {
		return;
	}

	AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiExit(nullptr, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerGarrisonAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerGarrisonAllBuildings(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	player->garrisonAllUnits(CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doPlayerExitAllBuildings */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerExitAllBuildings(const AsciiString& playerName)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	player->ungarrisonAllUnits(CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doLetterBoxMode */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doLetterBoxMode(Bool startLetterbox)
{
	if (startLetterbox)
	{
		HideControlBar(TRUE);
		TheDisplay->enableLetterBox(TRUE);
	}
	else
	{
		ShowControlBar(FALSE);
		TheDisplay->enableLetterBox(FALSE);
	}
}

//-------------------------------------------------------------------------------------------------
/** doBlackWhiteMode */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBlackWhiteMode(Bool startBWMode, Int frames)
{
	if (startBWMode)
	{
		TheTacticalView->setViewFilterMode(FM_VIEW_BW_BLACK_AND_WHITE);
		TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
		TheTacticalView->setFadeParameters(frames, 1);
	}
	else
	{
		if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
		{	//mode already set, turn it off
			TheTacticalView->setFadeParameters(frames, -1);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doSkyBox */
//-------------------------------------------------------------------------------------------------
extern void doSkyBoxSet(Bool startDraw);	//hack to avoid including globaldata here.

void ScriptActions::doSkyBox(Bool startDraw)
{
	if (startDraw)
	{
		doSkyBoxSet(1);
	}
	else
	{
		doSkyBoxSet(0);
	}
}

//-------------------------------------------------------------------------------------------------
/** doWeather */
//-------------------------------------------------------------------------------------------------

void ScriptActions::doWeather(Bool showWeather)
{
	TheSnowManager->setVisible(showWeather);
}

//-------------------------------------------------------------------------------------------------
/** Freeze time */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doFreezeTime()
{
	TheScriptEngine->doFreezeTime();
}

//-------------------------------------------------------------------------------------------------
/** Unfreeze time */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnfreezeTime()
{
	TheScriptEngine->doUnfreezeTime();
}

//-------------------------------------------------------------------------------------------------
/** Show a military briefing */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMilitaryCaption(const AsciiString& briefing, Int duration)
{
#if defined(RTS_DEBUG)
	if (TheGlobalData->m_disableMilitaryCaption)
		duration = 1;
#endif

	TheInGameUI->militarySubtitle(briefing, duration);
}

//-------------------------------------------------------------------------------------------------
/** Set the audible distance for shots in which the camera is up, and is therefore the center */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraSetAudibleDistance(Real audibleDistance)
{
	// No-op
}

//-------------------------------------------------------------------------------------------------
/** doSetStoppingDistance */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetStoppingDistance(const AsciiString& team, Real stoppingDistance)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );

	if (theTeam)
	{
		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
		{
			Object *obj = iter.cur();

			AIUpdateInterface *aiUpdate = obj->getAIUpdateInterface();
			if (!aiUpdate || !aiUpdate->getCurLocomotor()) {
				return;
			}

			if (stoppingDistance >= 0.5f)
			{
				aiUpdate->getCurLocomotor()->setCloseEnoughDist(stoppingDistance);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetHeld */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetHeld(const AsciiString& unit, Bool held)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		theObj->setDisabledUntil( DISABLED_HELD, held ? FOREVER : NEVER );
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetStoppingDistance */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetStoppingDistance(const AsciiString& unit, Real stoppingDistance)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		AIUpdateInterface *aiUpdate = theObj->getAIUpdateInterface();
		if (!aiUpdate || !aiUpdate->getCurLocomotor()) {
			return;
		}

		if (stoppingDistance >= 0.5f)
		{
			aiUpdate->getCurLocomotor()->setCloseEnoughDist(stoppingDistance);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doDisableSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableSpecialPowerDisplay()
{
	TheInGameUI->setSuperweaponDisplayEnabledByScript(false);
}

//-------------------------------------------------------------------------------------------------
/** doEnableSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableSpecialPowerDisplay()
{
	TheInGameUI->setSuperweaponDisplayEnabledByScript(true);
}

//-------------------------------------------------------------------------------------------------
/** doNamedHideSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedHideSpecialPowerDisplay(const AsciiString& unit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheInGameUI->hideObjectSuperweaponDisplayByScript(theObj);
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedShowSpecialPowerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedShowSpecialPowerDisplay(const AsciiString& unit)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheInGameUI->showObjectSuperweaponDisplayByScript(theObj);
	}
}

//-------------------------------------------------------------------------------------------------
/** doAudioSetVolume */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doAudioSetVolume(AudioAffect whichToAffect, Real newVolumeLevel)
{
	newVolumeLevel /= 100.0f;
	if (newVolumeLevel < 0.0f) {
		newVolumeLevel = 0.0f;
	} else if (newVolumeLevel > 1.0f) {
		newVolumeLevel = 1.0f;
	}

	TheAudio->setVolume(newVolumeLevel, whichToAffect);
}

//-------------------------------------------------------------------------------------------------
/** doTransferTeamToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTransferTeamToPlayer(const AsciiString& teamName, const AsciiString& playerName)
{

	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	Player* playerDest = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!(theTeam && playerDest)) {
		return;
	}

	theTeam->setControllingPlayer(playerDest);

	// srj sez: ensure that all these guys get the upgrades that belong to the new player
	theTeam->iterateObjects(updateTeamAndPlayerStuff, nullptr);
}

//-------------------------------------------------------------------------------------------------
/** doSetMoney */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetMoney(const AsciiString& playerName, Int money)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	Money *m = player->getMoney();
	if (!m)
		return;

	m->withdraw(m->countMoney());
	m->deposit(money, FALSE, FALSE);
}

//-------------------------------------------------------------------------------------------------
/** doGiveMoney */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doGiveMoney(const AsciiString& playerName, Int money)
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!player) {
		return;
	}

	Money *m = player->getMoney();
	if (!m)
		return;

	if (money < 0)
		m->withdraw(-money);
	else
		m->deposit(money);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCounter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayCounter(const AsciiString& counterName, const AsciiString& counterText)
{
	TheInGameUI->addNamedTimer(counterName, TheGameText->fetch(counterText), false);
}

//-------------------------------------------------------------------------------------------------
/** doHideCounter */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doHideCounter(const AsciiString& counterName)
{
	TheInGameUI->removeNamedTimer(counterName);
}

//-------------------------------------------------------------------------------------------------
/** doDisplayCountdownTimer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisplayCountdownTimer(const AsciiString& timerName, const AsciiString& timerText)
{
	TheInGameUI->addNamedTimer(timerName, TheGameText->fetch(timerText), true);
}

//-------------------------------------------------------------------------------------------------
/** doHideCountdownTimer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doHideCountdownTimer(const AsciiString& timerName)
{
	TheInGameUI->removeNamedTimer(timerName);
}

//-------------------------------------------------------------------------------------------------
/** doDisableCountdownTimerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDisableCountdownTimerDisplay()
{
	TheInGameUI->showNamedTimerDisplay(FALSE);
}

//-------------------------------------------------------------------------------------------------
/** doEnableCountdownTimerDisplay */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableCountdownTimerDisplay()
{
	TheInGameUI->showNamedTimerDisplay(TRUE);
}

//-------------------------------------------------------------------------------------------------
/** doNamedStopSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedStopSpecialPowerCountdown(const AsciiString& unit, const AsciiString& specialPower, Bool stop)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			mod->pauseCountdown(stop);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedSetSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetSpecialPowerCountdown( const AsciiString& unit, const AsciiString& specialPower, Int seconds )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Int frames = LOGICFRAMES_PER_SECOND * seconds;
			mod->setReadyFrame(TheGameLogic->getFrame() + frames);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedAddSpecialPowerCountdown */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedAddSpecialPowerCountdown( const AsciiString& unit, const AsciiString& specialPower, Int seconds )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Int frames = LOGICFRAMES_PER_SECOND * seconds;
			mod->setReadyFrame(mod->getReadyFrame() + frames);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtArea */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireSpecialPowerAtWaypoint( const AsciiString& unit, const AsciiString& specialPower, const AsciiString& waypoint )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			Waypoint *way = TheTerrainLogic->getWaypointByName(waypoint);
			if (!way) {
				return;
			}
			mod->doSpecialPowerAtLocation(way->getLocation(), INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtArea */
//-------------------------------------------------------------------------------------------------
// D-078 target choice, kept for comparison runs (scenario MunitionPicker = Legacy, D-110).
// A small munition should hit a unit it can destroy; a ballistic missile should remove a
// valuable building. The stock area search is designed for large superweapons and can aim
// a single-warhead strike between buildings. Check each launcher's range before choosing.
static Object *findIranMunitionTargetD078( Player *enemy, SpecialPowerModuleInterface *module,
	SpecialPowerType type )
{
	Object *best = nullptr;
	Real bestScore = 0.0f;
	for( Object *target = TheGameLogic->getFirstObject(); target; target = target->getNextObject() )
	{
		if( target->getControllingPlayer() != enemy || target->isEffectivelyDead()
			|| target->isKindOf( KINDOF_PROJECTILE ) || target->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| ( target->isKindOf( KINDOF_AIRCRAFT ) && target->isSignificantlyAboveTerrain() )
			|| !module->isValidTargetLocation( target->getPosition() ) )
			continue;

		BodyModuleInterface *body = target->getBodyModule();
		if( body == nullptr || body->getHealth() <= 0.0f )
			continue;
		Real cost = (Real)target->getTemplate()->calcCostToBuild( enemy );
		if( cost <= 0.0f )
			continue;
		Bool building = target->isKindOf( KINDOF_STRUCTURE );
		Bool vehicle = target->isKindOf( KINDOF_VEHICLE );
		if( !building && !vehicle )
			continue;

		Real damage = 500.0f;
		Real role = 1.0f;
		switch( type )
		{
			case SPECIAL_MUNITION_LAUNCH_1: // Shahed-136: vehicles and supply trucks
			case SPECIAL_MUNITION_LAUNCH_2: // Shahed-238
				role = building ? 0.55f : ( target->isKindOf( KINDOF_HARVESTER ) ? 2.0f : 1.5f );
				break;
			case SPECIAL_MUNITION_LAUNCH_3: // Arash: larger buildings
				damage = 1500.0f;
				role = building ? 2.0f : 0.35f;
				break;
			case SPECIAL_MUNITION_LAUNCH_4: // Fateh-110: defenses and production
				damage = 1640.0f;	// D-198 (was 2320)
				role = building ? ( target->isKindOf( KINDOF_FS_BASE_DEFENSE ) ? 2.0f : 1.5f ) : 0.25f;
				break;
			case SPECIAL_MUNITION_LAUNCH_5: // Kheibar Shekan: production and economy
				damage = 2050.0f;	// D-198 (was 2900)
				role = building ? ( target->isKindOf( KINDOF_FS_FACTORY ) || target->isKindOf( KINDOF_FS_SUPPLY_CENTER ) ? 2.0f : 1.5f ) : 0.25f;
				break;
			case SPECIAL_MUNITION_LAUNCH_6: // Fattah: enemy superweapons and tech
				damage = 2050.0f;	// D-198 (was 2320)
				role = building ? ( target->isKindOf( KINDOF_FS_SUPERWEAPON ) || target->isKindOf( KINDOF_FS_ADVANCED_TECH ) ? 3.0f : 1.5f ) : 0.25f;
				break;
			default:
				break;
		}
		// Finishing a nearly dead target with an expensive strike wastes its payload.
		Real useful = body->getHealth() < damage ? body->getHealth() / damage : 1.0f;
		Real score = cost * role * ( 0.25f + 0.75f * useful );
		if( score > bestScore )
		{
			bestScore = score;
			best = target;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Iran AI strike targeting (Generals Hypersonic D-110). The launch scripts fire a munition
// only at a target that the launch is expected to destroy: this munition alone, or together with the other
// loaded munitions of its group (missiles: Fateh-110, Kheibar Shekan, Fattah; drones: Shahed-136/238, Arash)
// fired at it as a salvo, after what is already on its way there. Expected damage per munition = armor-adjusted
// blast damage averaged over the uniform scatter disc (primary/secondary radius plus the target's bounding
// radius), times the chance it is not shot down: every enemy interceptor in range of the target gets
// IRAN_STRIKE_SHOTS_PER_DEFENSE shots per salvo, shared by the munitions of the salvo, each rolling the
// munition's hit chance (missiles: SAM-class weapons only; drones: anti-air weapons, lasers their laser chance).
// Among the killable targets the most valuable wins (cost x role); a salvo already under way is finished first.
// Nothing killable and worth it: hold the munition (no launch). Synced logic only (object scan in list order).
//-------------------------------------------------------------------------------------------------
static const Real IRAN_STRIKE_SHOTS_PER_DEFENSE = 4.0f;	///< interceptor shots per defense in range, per salvo
static const Real IRAN_STRIKE_KILL_MARGIN = 1.1f;				///< expected damage must exceed the target's health by this
static const Real IRAN_STRIKE_WORTH = 0.5f;							///< target value x role must be at least this x the munitions' cost
static const Real IRAN_STRIKE_AIM_REACH = 80.0f;				///< a munition in the air counts for targets this close to its aim point (+ radius)
enum { IRAN_STRIKE_MAX_POOL = 16 };											///< munitions considered for one salvo

struct IranMunitionInfo
{
	const ThingTemplate *m_munition;
	const WeaponTemplate *m_weapon;
	Real m_hitChance;				///< one interceptor shot's chance to bring it down (SAM or gun)
	Real m_laserHitChance;	///< ... a laser shot's
	Bool m_drone;
	Real m_cost;
	Real m_scatter;					///< scatter radius with the player's guidance upgrades
	Real m_damageScale;			///< Improved Warheads
};

struct IranStrikeDefense
{
	Coord3D m_pos;
	ObjectID m_id; // Hypersonic @feature (D-122) own target defense is allowed for slow drones
	Real m_samRange;		///< 0: none
	Real m_laserRange;
	Real m_gunRange;		///< any other anti-air weapon
};

struct IranStrikeCommitted
{
	ObjectID m_target;	///< a sortie ordered at this object; INVALID_ID: in the air, aimed at m_aim
	Coord3D m_aim;
	Int m_info;
	Bool m_inFlight; // Hypersonic @feature (D-122): a pending point aim has not scattered yet
};

static Int findIranMunitionInfo( const std::vector<IranMunitionInfo> &infos, const ThingTemplate *munition )
{
	if( munition == nullptr )
		return -1;
	for( size_t i = 0; i < infos.size(); ++i )
		if( munition->isEquivalentTo( infos[i].m_munition ) )
			return (Int)i;
	return -1;
}

static Int addIranMunitionInfo( std::vector<IranMunitionInfo> &infos, const ThingTemplate *munition,
	const MunitionMagazineInterface *magazine, const Object *facility, Bool drone )
{
	Int index = findIranMunitionInfo( infos, munition );
	if( index >= 0 )
		return index;
	const WeaponTemplate *weapon = magazine != nullptr ? magazine->getMunitionWeapon( munition ) : nullptr;
	if( weapon == nullptr )
		return -1;
	IranMunitionInfo info;
	info.m_munition = munition;
	info.m_weapon = weapon;
	info.m_hitChance = 0.0f;
	info.m_laserHitChance = 0.0f;
	info.m_drone = drone;
	info.m_cost = (Real)munition->calcCostToBuild( facility->getControllingPlayer() );
	info.m_scatter = weapon->getScatterRadiusForSource( facility );
	info.m_damageScale = 1.0f;
	const ModuleInfo &modules = munition->getBehaviorModuleInfo();
	for( Int i = 0; i < modules.getCount(); ++i )
	{
		if( !( modules.getNthName( i ) == "HitChanceBehavior" ) )
			continue;
		const HitChanceBehaviorModuleData *data = (const HitChanceBehaviorModuleData *)modules.getNthData( i );
		if( data == nullptr )
			continue;
		// Hypersonic @feature (D-122) include completed Countermeasures (D-196: I and II) in the purchase/launch estimate.
		const UpgradeTemplate *countermeasures = TheUpgradeCenter->findUpgrade( "Upgrade_IranCountermeasures" );
		const Bool upgraded = countermeasures != 0 && facility->getControllingPlayer()->hasUpgradeComplete( countermeasures );
		const Bool secondUpgraded = data->hasSecondUpgrade( facility->getControllingPlayer() );
		info.m_hitChance = data->getChance( FALSE, upgraded, secondUpgraded );
		info.m_laserHitChance = data->m_immuneToPointDefense ? 0.0f : data->getChance( TRUE, upgraded, secondUpgraded );
	}
	const UpgradeTemplate *warheads = TheUpgradeCenter->findUpgrade( "Upgrade_IranImprovedWarheads" );
	if( warheads != nullptr && facility->getControllingPlayer()->hasUpgradeComplete( warheads ) )
		info.m_damageScale = 1.20f;	// the missiles' and drones' PLAYER_UPGRADE DAMAGE 120% bonus (D-198; was 125%)
	infos.push_back( info );
	return (Int)infos.size() - 1;
}

// Damage one munition does to the target landing 'miss' from its centre (miss < 0: averaged over the scatter disc).
// Hypersonic @feature (D-122) 'second' (optional): the mean of the squared damage, for the kill test's spread.
static Real iranStrikeDamage( const IranMunitionInfo &info, Object *target, Real radius, Real miss, Real *second = 0 )
{
	WeaponBonus bonus;
	const WeaponTemplate *w = info.m_weapon;
	const Real primary = w->getPrimaryDamage( bonus );
	const Real secondary = w->getSecondaryDamage( bonus );
	const Real primaryRadius = w->getPrimaryDamageRadius( bonus ) + radius;
	Real secondaryRadius = w->getSecondaryDamageRadius( bonus ) + radius;
	if( secondaryRadius < primaryRadius )
		secondaryRadius = primaryRadius;
	// Hypersonic @feature third ring (Generals Hypersonic D-222): the outer ring, off (= secondary radius) when unset
	const Real tertiary = w->getTertiaryDamage( bonus );
	Real tertiaryRadius = w->getTertiaryDamageRadius( bonus ) > 0.0f ? w->getTertiaryDamageRadius( bonus ) + radius : 0.0f;
	if( tertiaryRadius < secondaryRadius )
		tertiaryRadius = secondaryRadius;
	// chance of the primary, the secondary and the tertiary blast
	Real pPrimary, pSecondary, pTertiary;
	if( miss >= 0.0f )
	{
		pPrimary = miss <= primaryRadius ? 1.0f : 0.0f;
		pSecondary = miss <= secondaryRadius ? 1.0f - pPrimary : 0.0f;
		pTertiary = miss <= tertiaryRadius ? 1.0f - pPrimary - pSecondary : 0.0f;
	}
	else if( info.m_scatter <= 0.0f )
	{
		pPrimary = 1.0f;
		pSecondary = 0.0f;
		pTertiary = 0.0f;
	}
	else
	{
		// uniform over the disc: P(miss <= r) = (r / scatter)^2
		pPrimary = sqr( primaryRadius / info.m_scatter );
		Real pWithin = sqr( secondaryRadius / info.m_scatter );
		Real pWithinOuter = sqr( tertiaryRadius / info.m_scatter );
		if( pPrimary > 1.0f ) pPrimary = 1.0f;
		if( pWithin > 1.0f ) pWithin = 1.0f;
		if( pWithinOuter > 1.0f ) pWithinOuter = 1.0f;
		pSecondary = pWithin - pPrimary;
		pTertiary = pWithinOuter - pWithin;
	}
	DamageInfoInput damage;
	damage.m_damageType = w->getDamageType();
	damage.m_amount = primary * info.m_damageScale;
	const Real primaryDamage = target->estimateDamage( damage );
	damage.m_amount = secondary * info.m_damageScale;
	const Real secondaryDamage = target->estimateDamage( damage );
	damage.m_amount = tertiary * info.m_damageScale;
	const Real tertiaryDamage = pTertiary > 0.0f ? target->estimateDamage( damage ) : 0.0f;
	if( second != 0 )
		*second = pPrimary * primaryDamage * primaryDamage + pSecondary * secondaryDamage * secondaryDamage
			+ pTertiary * tertiaryDamage * tertiaryDamage;
	return pPrimary * primaryDamage + pSecondary * secondaryDamage + pTertiary * tertiaryDamage;
}

// Chance that one munition of a salvo of 'salvo' gets through the defenses in reach of the target.
static Real iranStrikeSurvival( const IranMunitionInfo &info, Int sam, Int laser, Int gun, Int salvo )
{
	if( salvo < 1 )
		salvo = 1;
	const Int hitDefenses = info.m_drone ? sam + gun : sam;
	const Int laserDefenses = info.m_drone ? laser : 0;
	// shots at each munition, rounded up; integer powers (no pow()), so every machine gets the same result
	const Int perDefense = (Int)IRAN_STRIKE_SHOTS_PER_DEFENSE;
	const Int shots = ( perDefense * hitDefenses + salvo - 1 ) / salvo;
	const Int laserShots = ( perDefense * laserDefenses + salvo - 1 ) / salvo;
	Real survive = 1.0f;
	for( Int i = 0; i < shots && i < 64; ++i )
		survive *= 1.0f - info.m_hitChance;
	for( Int j = 0; j < laserShots && j < 64; ++j )
		survive *= 1.0f - info.m_laserHitChance;
	return survive;
}

// Hypersonic @feature (D-122) nearest distance to the entire straight cruise route (2D).
static Real iranStrikeRouteDistanceSqr( const Coord3D *start, const Coord3D *end, const Coord3D *point )
{
	const Real dx = end->x - start->x, dy = end->y - start->y;
	const Real lengthSqr = dx * dx + dy * dy;
	Real fraction = lengthSqr > 0.0f ? ( ( point->x - start->x ) * dx + ( point->y - start->y ) * dy ) / lengthSqr : 0.0f;
	if( fraction < 0.0f ) fraction = 0.0f;
	if( fraction > 1.0f ) fraction = 1.0f;
	return sqr( point->x - start->x - fraction * dx ) + sqr( point->y - start->y - fraction * dy );
}

// Hypersonic @feature (D-122) 16 fixed samples, two equal-area scatter rings. The centred case
// retains D-110's analytic disc average. No random draws; off-centre cluster aims include scatter too.
static Real iranStrikeDamageAtAim( const IranMunitionInfo &info, Object *target, Real radius, const Coord3D *aim,
	Real *second = 0 )
{
	const Real dx = aim->x - target->getPosition()->x, dy = aim->y - target->getPosition()->y;
	if( dx * dx + dy * dy < 0.01f ) return iranStrikeDamage( info, target, radius, -1.0f, second );
	if( info.m_scatter <= 0.0f ) return iranStrikeDamage( info, target, radius, sqrtf( dx * dx + dy * dy ), second );
	static const Real directions[8][2] = { {1,0}, {0.70710678f,0.70710678f}, {0,1}, {-0.70710678f,0.70710678f},
		{-1,0}, {-0.70710678f,-0.70710678f}, {0,-1}, {0.70710678f,-0.70710678f} };
	Real damage = 0.0f, squares = 0.0f;
	for( Int ring = 0; ring < 2; ++ring )
	{
		const Real distance = info.m_scatter * ( ring == 0 ? 0.5f : 0.8660254f );
		for( Int direction = 0; direction < 8; ++direction )
		{
			Real sampleSecond = 0.0f;
			damage += iranStrikeDamage( info, target, radius,
				sqrtf( sqr( dx + distance * directions[direction][0] ) + sqr( dy + distance * directions[direction][1] ) ),
				&sampleSecond );
			squares += sampleSecond;
		}
	}
	if( second != 0 )
		*second = squares / 16.0f;
	return damage / 16.0f;
}

// Hypersonic @feature (D-126 B) the enemy's grounded aircraft for the airfield rating below.
static Bool isIranStrikeGroundedAircraft( Object *obj, const Player *enemy )
{
	return !( obj->getControllingPlayer() != enemy || !obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_PROJECTILE )
		|| obj->isEffectivelyDead() || obj->isAboveTerrain() );
}

// Role weight of a structure as a missile target (D-110 rule 4); parked aircraft and hackers add to 'value'.
// Hypersonic @feature (D-126 B) 'grounded' (optional): the isIranStrikeGroundedAircraft objects, precomputed by the
// caller in object-list order (the same sum in the same order as the full walk over every object).
static Real iranStructureRole( Object *target, const Player *enemy, Real radius, Real *value,
	const std::vector<Object*> *grounded = 0 )
{
	if( target->isKindOf( KINDOF_FS_AIRFIELD ) )
	{
		// parked aircraft die with their airfield (ParkingPlaceBehavior::killAllParkedUnits)
		Real parked = 0.0f;
		const Coord3D *at = target->getPosition();
		std::vector<Object*> scanned;
		if( grounded == 0 )
		{
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
				if( isIranStrikeGroundedAircraft( obj, enemy ) )
					scanned.push_back( obj );
			grounded = &scanned;
		}
		for( size_t g = 0; g < grounded->size(); ++g )
		{
			const Object *obj = (*grounded)[g];
			const Coord3D *p = obj->getPosition();
			if( sqr( p->x - at->x ) + sqr( p->y - at->y ) <= sqr( radius + 30.0f ) )
				parked += (Real)obj->getTemplate()->calcCostToBuild( enemy );
		}
		*value += parked;
		return parked > 0.0f ? 2.5f : 1.5f;
	}
	if( target->isKindOf( KINDOF_FS_POWER ) )
		return target->getObjectCompletedUpgradeMask().any() ? 2.5f : 1.2f;
	if( target->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || target->isKindOf( KINDOF_FS_BLACK_MARKET )
		|| target->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || target->isKindOf( KINDOF_FS_INTERNET_CENTER )
		|| target->isKindOf( KINDOF_CASH_GENERATOR )
		|| target->getTemplate()->getName() == "TechOilDerrick" || target->getTemplate()->getName() == "TechOilRefinery" )
	{
		const ContainModuleInterface *contain = target->getContain();
		const ContainedItemsList *items = contain != nullptr ? contain->getContainedItemsList() : nullptr;
		if( target->isKindOf( KINDOF_FS_INTERNET_CENTER ) && items != nullptr )
		{
			for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				*value += (Real)(*it)->getTemplate()->calcCostToBuild( enemy );
		}
		return 2.0f;
	}
	if( target->isKindOf( KINDOF_FS_SUPERWEAPON ) )
		return 1.8f;
	if( target->isKindOf( KINDOF_FS_FACTORY ) || target->isKindOf( KINDOF_FS_BARRACKS ) || target->isKindOf( KINDOF_FS_WARFACTORY ) )
		return 1.5f;
	if( target->isKindOf( KINDOF_FS_ADVANCED_TECH ) || target->isKindOf( KINDOF_FS_TECHNOLOGY ) || target->isKindOf( KINDOF_FS_STRATEGY_CENTER ) )
		return 1.3f;
	if( target->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
		return 1.0f;
	return 0.8f;
}

// Hypersonic @feature (D-126 B) the blast neighbors iranStrikeClusterValue can count, independent of the aim point.
static Bool isIranStrikeClusterCandidate( Object *neighbor, const Player *enemy )
{
	if( neighbor->getControllingPlayer() != enemy || neighbor->isEffectivelyDead()
		|| neighbor->isKindOf( KINDOF_PROJECTILE ) || neighbor->isKindOf( KINDOF_FS_FAKE )
		|| neighbor->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || neighbor->isKindOf( KINDOF_AIRCRAFT )
		|| !( neighbor->isKindOf( KINDOF_STRUCTURE ) || neighbor->isKindOf( KINDOF_VEHICLE ) || neighbor->isKindOf( KINDOF_INFANTRY ) ) ) return FALSE;
	const AIUpdateInterface *ai = neighbor->getAI();
	if( !neighbor->isKindOf( KINDOF_STRUCTURE ) && ai != 0 && ai->isMoving() ) return FALSE;
	return TRUE;
}

// Hypersonic @feature (D-122) score blast neighbors only when this salvo alone can kill them.
// Only the anchor's airfield valuation includes parked aircraft, avoiding aircraft double-counts.
// Hypersonic @feature (D-126 B) 'candidates': the isIranStrikeClusterCandidate objects in object-list order, built once
// per target search (was: a walk over every object for every candidate target and aim point; same order, same sum).
static Real iranStrikeClusterValue( Player *enemy, Object *anchor, const Coord3D *aim,
	const std::vector<IranMunitionInfo> &infos, const Int *pool, Int salvo, Int sam,
	const std::vector<IranStrikeCommitted> &committed, const std::vector<Object*> &candidates )
{
	Real extra = 0.0f;
	for( size_t candidate = 0; candidate < candidates.size(); ++candidate )
	{
		Object *neighbor = candidates[candidate];
		if( neighbor == anchor ) continue;
		const Real radius = neighbor->getGeometryInfo().getBoundingSphereRadius();
		const Real distanceSqr = sqr( neighbor->getPosition()->x - aim->x ) + sqr( neighbor->getPosition()->y - aim->y );
		if( distanceSqr > sqr( radius + 200.0f ) ) continue; // maximum regular blast + scatter reach (Hypersonic D-222: third ring 110 + 80, was 160)
		const BodyModuleInterface *body = neighbor->getBodyModule();
		if( body == 0 || body->getHealth() <= 0.0f ) continue;
		// Do not count the value of a neighbor that a previous strike already covers.
		Real onTheWay = 0.0f;
		for( size_t pending = 0; pending < committed.size(); ++pending )
		{
			const IranStrikeCommitted &strike = committed[pending];
			Real raw = 0.0f;
			if( strike.m_target == neighbor->getID() ) raw = iranStrikeDamage( infos[strike.m_info], neighbor, radius, -1.0f );
			else if( strike.m_target == INVALID_ID )
			{
				const Real miss = sqrtf( sqr( strike.m_aim.x - neighbor->getPosition()->x ) + sqr( strike.m_aim.y - neighbor->getPosition()->y ) );
				raw = strike.m_inFlight ? iranStrikeDamage( infos[strike.m_info], neighbor, radius, miss )
					: iranStrikeDamageAtAim( infos[strike.m_info], neighbor, radius, &strike.m_aim );
			}
			// A single-strike survival estimate is conservative about already-doomed neighbors.
			onTheWay += raw * iranStrikeSurvival( infos[strike.m_info], sam, 0, 0, 1 );
		}
		if( onTheWay >= body->getHealth() ) continue;
		Real damage = 0.0f;
		for( Int shot = 0; shot < salvo; ++shot )
			damage += iranStrikeDamageAtAim( infos[pool[shot]], neighbor, radius, aim )
				* iranStrikeSurvival( infos[pool[shot]], sam, 0, 0, salvo );
		if( damage < body->getHealth() * IRAN_STRIKE_KILL_MARGIN ) continue;
		Real value = (Real)neighbor->getTemplate()->calcCostToBuild( enemy );
		// Captured tech buildings often have no purchase price but still deny continuing income.
		if( ( neighbor->isKindOf( KINDOF_CASH_GENERATOR ) || neighbor->isKindOf( KINDOF_TECH_BUILDING ) ) && value < 1000.0f ) value = 1000.0f;
		if( neighbor->isKindOf( KINDOF_STRUCTURE ) )
		{
			// Airfields scored without parked units here; those are included only at the anchor.
			const Real role = neighbor->isKindOf( KINDOF_FS_AIRFIELD ) ? 1.5f : iranStructureRole( neighbor, enemy, radius, &value );
			extra += value * role;
		}
		else extra += value;
	}
	return extra;
}

static Bool isIranStrikeFrontlineDefense( const Object *obj )
{
	return obj->isKindOf( KINDOF_STRUCTURE ) && obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) && !obj->isKindOf( KINDOF_FS_FAKE );
}

// Hypersonic @feature (D-189) step 2 of findIranMunitionTarget, shared with the D-189 strike doctrine (unchanged):
// Iran's munitions of 'infos' in the air (aim point known) and its sorties ordered but not yet fired (target known),
// in object-list order.
static void collectIranStrikeCommitted( Player *iran, const std::vector<IranMunitionInfo> &infos,
	std::vector<IranStrikeCommitted> &committed )
{
	for( Object *own = TheGameLogic->getFirstObject(); own; own = own->getNextObject() )
	{
		if( own->getControllingPlayer() != iran || own->isEffectivelyDead() )
			continue;
		IranStrikeCommitted c;
		c.m_target = INVALID_ID;
		c.m_aim.zero();
		c.m_inFlight = TRUE;
		c.m_info = findIranMunitionInfo( infos, own->getTemplate() );
		if( c.m_info >= 0 )
		{
			const CruiseFlightUpdate *flight = CruiseFlightUpdate::findFlight( own );
			const AIUpdateInterface *ai = own->getAI();
			const MissileAIUpdate *missile = ai != 0 ? ai->getMissileAIUpdate() : 0;
			if( flight != 0 )
			{
				if( flight->isWaitingOnLauncher() )
					continue;	// loaded, counted with its launcher
				c.m_aim = *flight->getImpactPoint();
			}
			else if( missile != 0 )
				c.m_aim = *missile->getOriginalTargetPos();
			else
				continue;
			committed.push_back( c );
			continue;
		}
		if( !own->isKindOf( KINDOF_STRUCTURE ) )
			continue;
		for( BehaviorModule **b = own->getBehaviorModules(); *b; ++b )
		{
			const MunitionMagazineInterface *mag = (*b)->getMunitionMagazineInterface();
			if( mag == 0 )
				continue;
			ObjectID targets[8];
			Coord3D positions[8];
			const ThingTemplate *munitions[8];
			const Int pending = mag->getPendingLaunches( targets, positions, munitions, 8 );
			for( Int i = 0; i < pending; ++i )
			{
				c.m_inFlight = FALSE;
				c.m_target = targets[i];
				c.m_aim = positions[i];
				c.m_info = findIranMunitionInfo( infos, munitions[i] );
				if( c.m_info >= 0 )
					committed.push_back( c );
			}
		}
	}
}

static Object *findIranMunitionTarget( Player *iran, Player *enemy, Object *site, SpecialPowerModuleInterface *module,
	SpecialPowerType type, Coord3D *chosenAim, Int prospectiveCount, Real *chosenScore = 0 )
{
	const Bool droneGroup = type <= SPECIAL_MUNITION_LAUNCH_3;
	if( iran == 0 || enemy == 0 || site == 0 )
		return 0;

	// 1. Iran's launch buttons of this group: munition data and loaded counts
	std::vector<IranMunitionInfo> infos;
	std::vector<SpecialPowerModuleInterface*> launchers;
	std::vector<Object*> launcherSites; // D-122: route-local drone salvos
	std::vector<Int> launcherInfo;
	Int current = -1;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getControllingPlayer() != iran || !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead()
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD )
			|| ( obj->isDisabled() && obj != site ) )	// a disabled facility cannot launch its part of a salvo
			continue;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
			if( sp == 0 || sp->getMunitionTemplate() == 0 || sp->getSpecialPowerTemplate() == 0 )
				continue;
			const SpecialPowerType spType = sp->getSpecialPowerTemplate()->getSpecialPowerType();
			if( spType < SPECIAL_MUNITION_LAUNCH_1 || spType > SPECIAL_MUNITION_LAUNCH_6
				|| ( spType <= SPECIAL_MUNITION_LAUNCH_3 ) != droneGroup )
				continue;
			const MunitionMagazineInterface *magazine = 0;
			for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b )
			{
				const MunitionMagazineInterface *mag = (*b)->getMunitionMagazineInterface();
				if( mag != 0 && mag->storesMunition( sp->getMunitionTemplate() ) )
					magazine = mag;
			}
			const Int index = addIranMunitionInfo( infos, sp->getMunitionTemplate(), magazine, obj, droneGroup );
			if( index < 0 )
				continue;
			launchers.push_back( sp );
			launcherSites.push_back( obj );
			launcherInfo.push_back( index );
			if( sp == module )
				current = index;
		}
	}
	if( current < 0 )
		return 0;

	// 2. what is already on its way: munitions in the air (aim point known), sorties not yet fired (target known)
	std::vector<IranStrikeCommitted> committed;
	collectIranStrikeCommitted( iran, infos, committed );	// Hypersonic @feature (D-189) shared helper

	// 3. enemy interceptors (disabled ones, e.g. unpowered, do not fire); drones: the nearest enemy base defense
	// Hypersonic @feature (D-126 B) one object walk builds every per-search list, each in object-list order as before:
	// interceptors, the enemy's structures (D-122 midpoints and the target candidates of step 4), blast neighbors
	// (iranStrikeClusterValue) and grounded aircraft (airfield rating).
	std::vector<IranStrikeDefense> defenses;
	Real frontline = -1.0f;
	const Coord3D *home = site->getPosition();
	// Hypersonic @feature (D-122) fixed-order structure list: used for cluster value and nearby
	// midpoint candidates. No pointer-keyed ordering and no mutable planner state.
	std::vector<Object*> structures;
	std::vector<Object*> clusterCandidates;
	std::vector<Object*> groundedAircraft;
	for( Object *foe = TheGameLogic->getFirstObject(); foe; foe = foe->getNextObject() )
	{
		if( foe->getControllingPlayer() == enemy && foe->isKindOf( KINDOF_STRUCTURE )
			&& !foe->isEffectivelyDead() && !foe->isKindOf( KINDOF_FS_FAKE )
			&& !foe->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ) structures.push_back( foe );
		if( !droneGroup && isIranStrikeClusterCandidate( foe, enemy ) ) clusterCandidates.push_back( foe );
		if( isIranStrikeGroundedAircraft( foe, enemy ) ) groundedAircraft.push_back( foe );

		if( foe->isEffectivelyDead() || foe->getTeam() == 0 || iran->getRelationship( foe->getTeam() ) != ENEMIES
			|| foe->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || foe->isKindOf( KINDOF_PROJECTILE ) )
			continue;
		if( droneGroup && foe->getControllingPlayer() == enemy && isIranStrikeFrontlineDefense( foe ) )
		{
			const Real d = sqrtf( sqr( foe->getPosition()->x - home->x ) + sqr( foe->getPosition()->y - home->y ) );
			if( frontline < 0.0f || d < frontline )
				frontline = d;
		}
		if( foe->isDisabled() )
			continue;
		IranStrikeDefense def;
		def.m_pos = *foe->getPosition();
		def.m_id = foe->getID();
		def.m_samRange = def.m_laserRange = def.m_gunRange = 0.0f;
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *w = foe->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( w == 0 )
				continue;
			const Int anti = w->getAntiMask();
			const InterceptorClassType cls = w->getTemplate()->getInterceptorClass();
			const Real range = w->getAttackRange( foe );
			if( cls == INTERCEPTOR_SAM && ( anti & ( WEAPON_ANTI_AIRBORNE_VEHICLE | WEAPON_ANTI_BALLISTIC_MISSILE | WEAPON_ANTI_SMALL_MISSILE ) ) )
				def.m_samRange = max( def.m_samRange, range );
			else if( cls == INTERCEPTOR_LASER )
				def.m_laserRange = max( def.m_laserRange, range );
			else if( anti & WEAPON_ANTI_AIRBORNE_VEHICLE )
				def.m_gunRange = max( def.m_gunRange, range );
		}
		if( def.m_samRange > 0.0f || ( droneGroup && ( def.m_laserRange > 0.0f || def.m_gunRange > 0.0f ) ) )
			defenses.push_back( def );
	}

	// 4. the most valuable target the launch is expected to destroy
	Object *best = 0;
	Real bestScore = 0.0f;
	// Hypersonic @feature (D-126 B) only the enemy's structures can pass the checks below (buildings only, D-122):
	// walk that list (object-list order) instead of every object; every check is kept, so the result is the same.
	for( size_t structureIndex = 0; structureIndex < structures.size(); ++structureIndex )
	{
		Object *target = structures[structureIndex];
		if( target->getControllingPlayer() != enemy || target->isEffectivelyDead()
			|| target->isKindOf( KINDOF_PROJECTILE ) || target->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| target->isKindOf( KINDOF_FS_FAKE )
			|| ( target->isKindOf( KINDOF_AIRCRAFT ) && target->isAboveTerrain() )
			 )
			continue;
		BodyModuleInterface *body = target->getBodyModule();
		if( body == 0 || body->getHealth() <= 0.0f )
			continue;
		Real value = (Real)target->getTemplate()->calcCostToBuild( enemy );
		// Hypersonic @feature (D-122) captured cash techs have economic value despite a zero build cost.
		if( ( target->isKindOf( KINDOF_CASH_GENERATOR ) || target->isKindOf( KINDOF_TECH_BUILDING ) ) && value < 1000.0f ) value = 1000.0f;
		if( value <= 0.0f )
			continue;
		const Bool building = target->isKindOf( KINDOF_STRUCTURE );
		if( !building )
			continue;	// D-122: facility drones and missiles aim at buildings, never moving trucks/dozers

		// Hypersonic @feature (D-122) at most one midpoint, toward the closest building within
		// 120 units. Also try the centre; the aimed building must still be expected to die.
		Coord3D aims[2];
		aims[0] = *target->getPosition();
		Int aimCount = 1;
		if( !droneGroup )
		{
			Real nearestPair = sqr( 120.0f );
			for( size_t pairIndex = 0; pairIndex < structures.size(); ++pairIndex )
			{
				Object *neighbor = structures[pairIndex];
				if( neighbor == target ) continue;
				const Real pairDistance = sqr( neighbor->getPosition()->x - aims[0].x ) + sqr( neighbor->getPosition()->y - aims[0].y );
				if( pairDistance < nearestPair )
				{
					nearestPair = pairDistance;
					aims[1] = aims[0];
					aims[1].x = ( aims[0].x + neighbor->getPosition()->x ) * 0.5f;
					aims[1].y = ( aims[0].y + neighbor->getPosition()->y ) * 0.5f;
					aims[1].z = TheTerrainLogic->getGroundHeight( aims[1].x, aims[1].y );
					aimCount = 2;
				}
			}
		}
		const Real baseValue = value;
		for( Int aimIndex = 0; aimIndex < aimCount; ++aimIndex )
		{
			value = baseValue; // airfield/hacker additions must not accumulate across point candidates
			const Coord3D *pos = &aims[aimIndex];
			if( !module->isValidTargetLocation( pos ) ) continue;
			if( !droneGroup && prospectiveCount > 0
				&& sqr( pos->x - home->x ) + sqr( pos->y - home->y ) < sqr( 240.0f ) ) continue;
			const Real radius = target->getGeometryInfo().getBoundingSphereRadius();
			Real role;
			const Real targetDistance = sqrtf( sqr( pos->x - home->x ) + sqr( pos->y - home->y ) );
			if( droneGroup && frontline >= 0.0f && targetDistance > frontline + 300.0f ) continue;
			Int maxSalvo = IRAN_STRIKE_MAX_POOL;
			if( !droneGroup )
				role = iranStructureRole( target, enemy, radius, &value, &groundedAircraft );
			else if( isIranStrikeFrontlineDefense( target ) )
			{
				// D-110 rule 2: the base defenses nearest Iran's base first
				const Real d = sqrtf( sqr( pos->x - home->x ) + sqr( pos->y - home->y ) );
				const Real front = frontline >= 0.0f ? ( frontline + 300.0f ) / ( d + 300.0f ) : 1.0f;
				role = 3.0f * front * front;
			}

			else
			{
				Real unused = value;
				role = iranStructureRole( target, enemy, radius, &unused, &groundedAircraft ) * 0.8f;
			}

			// defenses in reach of the target
			Int sam = 0, laser = 0, gun = 0;
			for( size_t d = 0; d < defenses.size(); ++d )
			{
				const Real distSqr = droneGroup ? iranStrikeRouteDistanceSqr( home, pos, &defenses[d].m_pos )
					: sqr( defenses[d].m_pos.x - pos->x ) + sqr( defenses[d].m_pos.y - pos->y );
				if( defenses[d].m_samRange > 0.0f && distSqr <= sqr( defenses[d].m_samRange + radius ) ) ++sam;
				if( defenses[d].m_laserRange > 0.0f && distSqr <= sqr( defenses[d].m_laserRange + radius ) ) ++laser;
				if( defenses[d].m_gunRange > 0.0f && distSqr <= sqr( defenses[d].m_gunRange + radius ) ) ++gun;
				// Hypersonic @feature (D-122) an anti-air target (Stinger Site, Gattling Cannon, ...) shoots at the
				// drones for the whole approach to itself: counted twice (8 shots), the most wasted drone target before
				if( droneGroup && defenses[d].m_id == target->getID() )
				{
					if( defenses[d].m_samRange > 0.0f ) ++sam;
					if( defenses[d].m_laserRange > 0.0f ) ++laser;
					if( defenses[d].m_gunRange > 0.0f ) ++gun;
				}
			}

			// Hypersonic @feature (D-122) slow drones may attack an isolated frontline defense,
			// or any undefended building. No other interceptor may lie on their route.
			Bool slowSafe = TRUE;
			if( droneGroup )
				for( size_t routeIndex = 0; routeIndex < defenses.size(); ++routeIndex )
				{
					const IranStrikeDefense &routeDefense = defenses[routeIndex];
					const Real routeRange = max( routeDefense.m_samRange, max( routeDefense.m_laserRange, routeDefense.m_gunRange ) );
					if( iranStrikeRouteDistanceSqr( home, pos, &routeDefense.m_pos ) <= sqr( routeRange + radius )
						&& routeDefense.m_id != target->getID() ) slowSafe = FALSE;
				}
			if( droneGroup && type != SPECIAL_MUNITION_LAUNCH_2 && !slowSafe ) continue;
			if( type == SPECIAL_MUNITION_LAUNCH_6 && !( sam >= 3
				&& ( target->isKindOf( KINDOF_FS_SUPERWEAPON ) || target->isKindOf( KINDOF_FS_AIRFIELD )
					|| value * role >= 4000.0f ) ) ) continue;

			// what is already on its way to it (only munitions expected to do it damage count); their raw damage is
			// kept so the salvo test below rates them with the same interception sharing as the new munitions
			enum { MAX_COMMITTED = 32 };
			Real committedRaw[MAX_COMMITTED];
			Real committedSecond[MAX_COMMITTED];	// D-122: mean squared damage (spread of the kill test)
			Int committedInfo[MAX_COMMITTED];
			Int committedCount = 0;
			for( size_t ci = 0; ci < committed.size() && committedCount < MAX_COMMITTED; ++ci )
			{
				const IranStrikeCommitted &c = committed[ci];
				Real dmg = 0.0f, dmgSecond = 0.0f;
				if( c.m_target == target->getID() )
					dmg = iranStrikeDamage( infos[c.m_info], target, radius, -1.0f, &dmgSecond );
				else if( c.m_target == INVALID_ID )
				{
					const Real missSqr = sqr( c.m_aim.x - target->getPosition()->x ) + sqr( c.m_aim.y - target->getPosition()->y );
					if( missSqr <= sqr( radius + IRAN_STRIKE_AIM_REACH ) )
						dmg = c.m_inFlight ? iranStrikeDamage( infos[c.m_info], target, radius, sqrtf( missSqr ), &dmgSecond )
							: iranStrikeDamageAtAim( infos[c.m_info], target, radius, &c.m_aim, &dmgSecond );
				}
				if( dmg <= 0.0f )
					continue;
				committedRaw[committedCount] = dmg;
				committedSecond[committedCount] = dmgSecond;
				committedInfo[committedCount] = c.m_info;
				++committedCount;
			}
			// continuing a salvo under way needs no extra margin (so rounding cannot strand it half-fired)
			const Real need = body->getHealth() * ( committedCount > 0 ? 1.0f : IRAN_STRIKE_KILL_MARGIN );
			Real committedDamage = 0.0f;
			for( Int cd = 0; cd < committedCount; ++cd )
				committedDamage += committedRaw[cd] * iranStrikeSurvival( infos[committedInfo[cd]], sam, laser, gun, committedCount );
			if( committedDamage >= need )
				continue;	// already doomed: nothing more to send

			// the salvo: this munition first, then the other loaded ones that can reach the target, strongest first
			Int pool[IRAN_STRIKE_MAX_POOL];
			Real poolDamage[IRAN_STRIKE_MAX_POOL];
			Real poolSecond[IRAN_STRIKE_MAX_POOL];	// D-122: mean squared damage
			Int poolSize = 0;
			pool[poolSize++] = current;
			Bool currentTaken = FALSE;
			if( prospectiveCount > 0 )
				for( Int preview = 1; preview < prospectiveCount && poolSize < IRAN_STRIKE_MAX_POOL; ++preview )
					pool[poolSize++] = current;
			for( size_t l = 0; l < launchers.size() && poolSize < IRAN_STRIKE_MAX_POOL; ++l )
			{
				// D-122: drone routes are rated from this facility; another facility's route may differ.
				if( droneGroup && launcherSites[l] != site ) continue;
				if( !launchers[l]->isValidTargetLocation( pos ) )
					continue;
				if( prospectiveCount > 0 && launchers[l] == module ) continue;
				if( droneGroup && launcherInfo[l] != current && !slowSafe
					&& infos[launcherInfo[l]].m_munition->getName() != "IranAirShahed238" ) continue;
				// The premium Fattah never subsidizes an ordinary TEL salvo.
				if( !droneGroup && type != SPECIAL_MUNITION_LAUNCH_6
					&& infos[launcherInfo[l]].m_munition->getName() == "IranMissileFattah" ) continue;
				Int loaded = launchers[l]->getShortcutReadyCount();
				if( launchers[l] == module && !currentTaken )
				{
					--loaded;	// that one is pool[0]
					currentTaken = TRUE;
				}
				for( Int k = 0; k < loaded && poolSize < IRAN_STRIKE_MAX_POOL; ++k )
					pool[poolSize++] = launcherInfo[l];
			}
			for( Int pd = 0; pd < poolSize; ++pd )
				poolDamage[pd] = iranStrikeDamageAtAim( infos[pool[pd]], target, radius, pos, &poolSecond[pd] );
			if( poolDamage[0] <= 0.0f )
				continue;
			// Hypersonic @feature (D-122) jets form the front-defense salvo before heavy/slow
			// supplements; elsewhere retain the D-110 strongest-first insertion order.
			const Bool jetsFirst = droneGroup && isIranStrikeFrontlineDefense( target );
			for( Int si = 2; si < poolSize; ++si )
				for( Int j = si; j > 1; --j )
				{
					const Bool jetHere = infos[pool[j]].m_munition->getName() == "IranAirShahed238";
					const Bool jetBefore = infos[pool[j - 1]].m_munition->getName() == "IranAirShahed238";
					const Bool stronger = jetsFirst && jetHere != jetBefore ? jetHere : poolDamage[j] > poolDamage[j - 1];
					if( !stronger ) break;
					const Real dt = poolDamage[j]; poolDamage[j] = poolDamage[j - 1]; poolDamage[j - 1] = dt;
					const Real ds = poolSecond[j]; poolSecond[j] = poolSecond[j - 1]; poolSecond[j - 1] = ds;
					const Int it = pool[j]; pool[j] = pool[j - 1]; pool[j - 1] = it;
				}

			Int salvo = 0;
			Real salvoCost = 0.0f;
			for( Int n = 1; n <= poolSize && n <= maxSalvo; ++n )
			{
				// Hypersonic @feature (D-122) the kill test uses the expected damage minus one standard deviation
				// (each munition: blast primary / secondary / none, then shot down or not), not the mean alone: one
				// big warhead whose primary blast often misses must not count as a kill (Kheibar at a midpoint).
				Real sum = 0.0f, spread = 0.0f, cost = 0.0f;
				for( Int cq = 0; cq < committedCount; ++cq )
				{
					const Real s = iranStrikeSurvival( infos[committedInfo[cq]], sam, laser, gun, n + committedCount );
					sum += committedRaw[cq] * s;
					spread += s * committedSecond[cq] - sqr( s * committedRaw[cq] );
				}
				for( Int q = 0; q < n; ++q )
				{
					const Real s = iranStrikeSurvival( infos[pool[q]], sam, laser, gun, n + committedCount );
					sum += poolDamage[q] * s;
					spread += s * poolSecond[q] - sqr( s * poolDamage[q] );
					cost += infos[pool[q]].m_cost;
				}
				if( spread > 0.0f )
					sum -= sqrtf( spread );
				Bool includesFateh = FALSE;
				for( Int cheap = 0; cheap < n; ++cheap )
					if( infos[pool[cheap]].m_munition->getName() == "IranMissileFateh110" ) includesFateh = TRUE;
				for( Int cheapCommitted = 0; cheapCommitted < committedCount; ++cheapCommitted )
					if( infos[committedInfo[cheapCommitted]].m_munition->getName() == "IranMissileFateh110" ) includesFateh = TRUE;
				const Int totalSalvo = n + committedCount;
				if( droneGroup && totalSalvo < 2 ) continue;
				if( includesFateh && totalSalvo < 3
					&& !( TheGameLogic->getFrame() < LOGICFRAMES_PER_SECOND * 360 && sam == 0 ) ) continue;
				if( sum >= need )
				{
					salvo = n;
					salvoCost = cost;
					break;
				}
			}
			if( salvo == 0 )
				continue;	// not expected to die: a launch would only damage it (D-110 rule 3)
			// Hypersonic @feature (D-122) count every additional expected kill in this blast,
			// including stationary ground units. Merely damaged neighbors contribute no value.
			Real clusterValue = value * role;
			if( !droneGroup )
				clusterValue += iranStrikeClusterValue( enemy, target, pos, infos, pool, salvo, sam, committed, clusterCandidates );
			if( type == SPECIAL_MUNITION_LAUNCH_6 && clusterValue < salvoCost ) continue;
			if( clusterValue < IRAN_STRIKE_WORTH * salvoCost )
				continue;	// not worth the munitions (rule 5)

			Real score = clusterValue / ( 1.0f + 0.15f * (Real)( salvo - 1 ) );
			if( committedCount > 0 )
				score *= 4.0f;	// finish a salvo under way
			if( score > bestScore )
			{
				bestScore = score;
				best = target;
				if( chosenAim != 0 ) *chosenAim = *pos;
				if( chosenScore != 0 ) *chosenScore = score;
			}
		}
	}
	return best;
}

// Hypersonic @feature (D-122) secondary purchases must have a killable target with the
// prospective pad/bay stock. This runs only from computer restock; no script-context enemy state.
Bool iranComputerMunitionHasTarget( Object *facility, const ThingTemplate *munition, Int prospectiveCount )
{
	Player *iran = facility != 0 ? facility->getControllingPlayer() : 0;
	if( iran == 0 || iran->getPlayerType() != PLAYER_COMPUTER || iran->getSide() != "Iran" || munition == 0 ) return FALSE;
	SpecialPowerModuleInterface *module = 0;
	for( BehaviorModule **behavior = facility->getBehaviorModules(); *behavior; ++behavior )
	{
		SpecialPowerModuleInterface *candidate = (*behavior)->getSpecialPower();
		if( candidate != 0 && candidate->getMunitionTemplate() != 0
			&& munition->isEquivalentTo( candidate->getMunitionTemplate() ) ) module = candidate;
	}
	if( module == 0 || module->getSpecialPowerTemplate() == 0 ) return FALSE;
	for( Int playerIndex = 0; playerIndex < ThePlayerList->getPlayerCount(); ++playerIndex )
	{
		Player *enemy = ThePlayerList->getNthPlayer( playerIndex );
		if( enemy == 0 || enemy->getDefaultTeam() == 0 || iran->getRelationship( enemy->getDefaultTeam() ) != ENEMIES ) continue;
		if( findIranMunitionTarget( iran, enemy, facility, module,
			module->getSpecialPowerTemplate()->getSpecialPowerType(), 0, prospectiveCount ) != 0 ) return TRUE;
	}
	return FALSE;
}

// Hypersonic @feature (D-122) strikes reach the whole map (D-027): rate the targets of every hostile player, not
// only the script's current enemy (team games: the far-corner enemy was never struck). Players in index order;
// ties keep the first, so every machine picks the same target.
static Object *findIranMunitionTargetAnyEnemy( Player *iran, Object *site, SpecialPowerModuleInterface *module,
	SpecialPowerType type, Coord3D *chosenAim )
{
	Object *best = 0;
	Real bestScore = 0.0f;
	for( Int playerIndex = 0; playerIndex < ThePlayerList->getPlayerCount(); ++playerIndex )
	{
		Player *enemy = ThePlayerList->getNthPlayer( playerIndex );
		if( enemy == 0 || enemy == iran || enemy->getDefaultTeam() == 0
			|| iran->getRelationship( enemy->getDefaultTeam() ) != ENEMIES )
			continue;
		Coord3D aim;
		Real score = 0.0f;
		Object *target = findIranMunitionTarget( iran, enemy, site, module, type, &aim, 0, &score );
		if( target != 0 && score > bestScore )
		{
			bestScore = score;
			best = target;
			*chosenAim = aim;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Iran AI strike doctrine (Generals Hypersonic D-189), for computer players of side Iran (not with
// the harness flags MunitionPicker = Legacy / D122). Strikes are planned with the ground war, not picked one munition
// at a time:
//  * Drone breach: when an Iran push reaches the enemy line (IRAN_BREACH_MIN_ARMY ground combat units within the
//    breach reach of an enemy base defense; the reach grows with the drones' flight time, so they land just before the
//    troops arrive), one Drone Facility fires every loaded drone at once (all three types, at least
//    IRAN_BREACH_MIN_DRONES) at up to IRAN_BREACH_TARGETS defenses of that line nearest the army, anti-air first: Arash,
//    then Shahed-238, then Shahed-136 go to the first target until their expected damage (scatter only, "if they
//    penetrate") reaches its health, then to the next; the rest spread over the targets. No defense near the army:
//    its nearest buildings. Otherwise the drones are held. A line of fewer than IRAN_BREACH_TARGETS targets is topped up
//    with the next defenses near the anchor, then buildings near it. The home guards (Base SAM, Home Defense teams) are
//    not the push, and an anchor within IRAN_BREACH_HOME_CLEAR of Iran's base centre never starts a volley.
//  * Missile salvo: the highest-priority target (superweapon > airfield with parked aircraft > vehicle factory > money
//    building; then value, then object-list order) that the fewest loaded missiles (at most IRAN_MISSILE_MAX_SALVO with
//    what is already on its way; superweapons at least 2) are expected to destroy, counting scatter but no
//    interception. A building that leaves a rebuild hole (GLA) needs the hole's health too, at least 2 missiles, and
//    the hole missiles start IRAN_HOLE_DELAY_SECONDS later so they land after the building died. Fattah only at a
//    superweapon, an airfield with aircraft or a target covered by 3+ SAM sites (D-122), never at a hole building.
//    Nothing killable: hold.
// Synced logic only: object-list order, stable sorts on game values, no random draws, no pointer keys.
//-------------------------------------------------------------------------------------------------
static const Real IRAN_BREACH_LINE = 400.0f;					///< the breach targets lie within this of the first one
static const Real IRAN_BREACH_ARMY_SPEED = 30.0f;			///< dist/sec, a mixed army's march speed
static const Real IRAN_BREACH_ENGAGE = 300.0f;				///< the army meets the line this far from a defense
static const Real IRAN_BREACH_LAUNCH_SECONDS = 3.0f;	///< drone prepare on the launcher + booster
static const Real IRAN_BREACH_REACH_MIN = 700.0f;		///< the breach reach (army to defense) stays within these
static const Real IRAN_BREACH_REACH_MAX = 1100.0f;
static const Real IRAN_HOLE_DELAY_SECONDS = 6.0f;		///< TEL hold spread 2 s + Fateh/Kheibar flight spread + margin (holes stay bare 20 s)
static const Real IRAN_BREACH_EXTRA_DEFENSES = 700.0f;	///< Hypersonic @feature (D-189) more defenses within this of the anchor
static const Real IRAN_BREACH_EXTRA_BUILDINGS = 400.0f;	///< Hypersonic @feature (D-189) then buildings within this of the anchor
static const Real IRAN_BREACH_HOME_CLEAR = 1200.0f;		///< Hypersonic @feature (D-189) no volley at an anchor this near Iran's base
enum { IRAN_BREACH_MIN_ARMY = 5, IRAN_BREACH_MIN_DRONES = 6, IRAN_BREACH_TARGETS = 3, IRAN_MISSILE_MAX_SALVO = 3 };
// Hypersonic (owner 2026-10-20, "6/6 wins is too much" -> weaker strikes): a breach volley launches at most this many
// drones (Arash first), the rest stay loaded for the next one; it launched every loaded drone (up to 10)
enum { IRAN_BREACH_MAX_DRONES = 6 };
// Hypersonic @feature (D-189 stale volley, owner: "the drone facility is full and drones are not launching") a facility
// that has held a full volley this long without a breach fires it anyway, at the enemy base-defense line nearest
// Iran's base (else the enemy buildings nearest it), with the same target and allocation rules
static const Real IRAN_BREACH_STALE_SECONDS = 120.0f;

struct IranStrikeLauncher
{
	SpecialPowerModuleInterface *m_module;
	MunitionMagazineInterface *m_magazine;
	Int m_info;
	Int m_loaded;		///< ready to launch now
};

// Hypersonic @feature (D-189) the launch buttons of one Iran facility for the drone or the missile group.
static void addIranStrikeLaunchers( Object *obj, Bool droneGroup, std::vector<IranMunitionInfo> &infos,
	std::vector<IranStrikeLauncher> &launchers )
{
	if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() || obj->isDisabled()
		|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
		if( sp == 0 || sp->getMunitionTemplate() == 0 || sp->getSpecialPowerTemplate() == 0 )
			continue;
		const SpecialPowerType spType = sp->getSpecialPowerTemplate()->getSpecialPowerType();
		if( spType < SPECIAL_MUNITION_LAUNCH_1 || spType > SPECIAL_MUNITION_LAUNCH_6
			|| ( spType <= SPECIAL_MUNITION_LAUNCH_3 ) != droneGroup )
			continue;
		MunitionMagazineInterface *magazine = 0;
		for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b )
		{
			MunitionMagazineInterface *mag = (*b)->getMunitionMagazineInterface();
			if( mag != 0 && mag->storesMunition( sp->getMunitionTemplate() ) )
				magazine = mag;
		}
		if( magazine == 0 )
			continue;
		const Int index = addIranMunitionInfo( infos, sp->getMunitionTemplate(), magazine, obj, droneGroup );
		if( index < 0 || !TheSpecialPowerStore->canUseSpecialPower( obj, sp->getSpecialPowerTemplate() ) )
			continue;
		IranStrikeLauncher launcher;
		launcher.m_module = sp;
		launcher.m_magazine = magazine;
		launcher.m_info = index;
		launcher.m_loaded = sp->isReady() ? sp->getShortcutReadyCount() : 0;
		launchers.push_back( launcher );
	}
}

// Hypersonic @feature (D-189) raw expected damage (no interception) of what is already on its way to 'target', and
// how many munitions that is (the committed rules of findIranMunitionTarget).
static Real iranStrikeCommittedDamage( const std::vector<IranMunitionInfo> &infos,
	const std::vector<IranStrikeCommitted> &committed, Object *target, Real radius, Int *count )
{
	Real total = 0.0f;
	Int n = 0;
	for( size_t ci = 0; ci < committed.size(); ++ci )
	{
		const IranStrikeCommitted &c = committed[ci];
		Real dmg = 0.0f;
		if( c.m_target == target->getID() )
			dmg = iranStrikeDamage( infos[c.m_info], target, radius, -1.0f );
		else if( c.m_target == INVALID_ID )
		{
			const Real missSqr = sqr( c.m_aim.x - target->getPosition()->x ) + sqr( c.m_aim.y - target->getPosition()->y );
			if( missSqr <= sqr( radius + IRAN_STRIKE_AIM_REACH ) )
				dmg = c.m_inFlight ? iranStrikeDamage( infos[c.m_info], target, radius, sqrtf( missSqr ) )
					: iranStrikeDamageAtAim( infos[c.m_info], target, radius, &c.m_aim );
		}
		if( dmg > 0.0f )
		{
			total += dmg;
			++n;
		}
	}
	if( count != 0 )
		*count = n;
	return total;
}

static Bool isIranStrikeHostile( const Player *iran, const Object *obj )
{
	return obj->getTeam() != 0 && iran->getRelationship( obj->getTeam() ) == ENEMIES;
}

// Hypersonic @feature (D-189) can shoot at drones or missiles (SAM / laser interceptors, anti-air guns)
static Bool hasIranStrikeAntiAirWeapon( Object *obj )
{
	for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const Weapon *w = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
		if( w == 0 )
			continue;
		const InterceptorClassType cls = w->getTemplate()->getInterceptorClass();
		if( cls == INTERCEPTOR_SAM || cls == INTERCEPTOR_LASER
			|| ( w->getAntiMask() & ( WEAPON_ANTI_AIRBORNE_VEHICLE | WEAPON_ANTI_BALLISTIC_MISSILE | WEAPON_ANTI_SMALL_MISSILE ) ) )
			return TRUE;
	}
	return FALSE;
}

// Hypersonic @feature (D-189) an Iran ground combat unit of the push (not a dozer, truck, aircraft, drone, a TEL or drone
// on its pad, nor a unit inside a building or transport)
static Bool isIranBreachArmyUnit( Object *obj, const Player *iran )
{
	if( obj->getControllingPlayer() != iran || obj->isEffectivelyDead() )
		return FALSE;
	if( !( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) ) )
		return FALSE;
	if( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_AIRCRAFT ) || obj->isKindOf( KINDOF_DOZER )
		|| obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_PROJECTILE ) || obj->isKindOf( KINDOF_DRONE ) )
		return FALSE;
	if( obj->testStatus( OBJECT_STATUS_UNSELECTABLE ) || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
		|| obj->getContainedBy() != 0 )
		return FALSE;
	// Hypersonic @feature (D-189) the home guards never count as the push: the inner-base Bavar-373 guards and the
	// home-defense pairs (team names from tools/iran_ai.py base_sam_guard / home_defense). Team names are synced data.
	const Team *team = obj->getTeam();
	if( team != 0 && ( team->getName().startsWith( "Iran Base SAM" ) || team->getName().startsWith( "Iran Home Defense" ) ) )
		return FALSE;
	return obj->isAbleToAttack();
}

// Hypersonic @feature (D-189) a drone's cruise speed in dist/sec (CruiseFlightUpdate Speed, or UpgradedSpeed once the
// player has its SpeedUpgrade, as CruiseFlightUpdate::getSpeed); 0: unknown
static Real iranDroneSpeed( const ThingTemplate *munition, const Player *player )
{
	const ModuleInfo &modules = munition->getBehaviorModuleInfo();
	for( Int i = 0; i < modules.getCount(); ++i )
	{
		if( !( modules.getNthName( i ) == "CruiseFlightUpdate" ) )
			continue;
		const CruiseFlightUpdateModuleData *data = (const CruiseFlightUpdateModuleData *)modules.getNthData( i );
		if( data == 0 )
			continue;
		if( data->m_upgradedSpeed > 0.0f && data->m_speedUpgradeName.isNotEmpty() && player != 0 )
		{
			const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( data->m_speedUpgradeName );
			if( upgrade != 0 && player->hasUpgradeComplete( upgrade ) )
				return data->m_upgradedSpeed * LOGICFRAMES_PER_SECOND;
		}
		return data->m_speed * LOGICFRAMES_PER_SECOND;
	}
	return 0.0f;
}

// Hypersonic @feature (D-189) the army distance to a defense at which the drones must leave to land just before the
// troops: the slowest drone's flight time from the facility (plus the launch), marched at the army's speed, plus the
// distance at which the army meets the line
static Real iranBreachReach( const Object *facility, const Object *defense, Real slowestSpeed )
{
	const Real flight = sqrtf( sqr( defense->getPosition()->x - facility->getPosition()->x )
		+ sqr( defense->getPosition()->y - facility->getPosition()->y ) ) / slowestSpeed;
	Real reach = IRAN_BREACH_ENGAGE + IRAN_BREACH_ARMY_SPEED * ( IRAN_BREACH_LAUNCH_SECONDS + flight );
	if( reach < IRAN_BREACH_REACH_MIN ) reach = IRAN_BREACH_REACH_MIN;
	if( reach > IRAN_BREACH_REACH_MAX ) reach = IRAN_BREACH_REACH_MAX;
	return reach;
}

static Real iranStrikeDistanceSqr( const Coord3D *a, const Coord3D *b )
{
	return sqr( a->x - b->x ) + sqr( a->y - b->y );
}

// Hypersonic @feature (D-189) the drone breach volley of one Drone Facility; TRUE when it launched
static Bool iranDroneBreach( Player *iran, Object *facility )
{
	std::vector<IranMunitionInfo> infos;
	std::vector<IranStrikeLauncher> launchers;
	addIranStrikeLaunchers( facility, TRUE, infos, launchers );
	Int loaded = 0;
	Real slowest = 0.0f;
	for( size_t li = 0; li < launchers.size(); ++li )
	{
		if( launchers[li].m_loaded <= 0 )
			continue;
		loaded += launchers[li].m_loaded;
		const Real speed = iranDroneSpeed( infos[launchers[li].m_info].m_munition, iran );	// Hypersonic @feature (D-189) upgraded speed
		if( speed > 0.0f && ( slowest <= 0.0f || speed < slowest ) )
			slowest = speed;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if( loaded < IRAN_BREACH_MIN_DRONES )
	{
		s_iranDroneFullSince.erase( facility->getID() );	// Hypersonic @feature (D-189 stale volley)
		return FALSE;	// no small salvos
	}
	// Hypersonic @feature (D-189 stale volley) how long this facility has held a full volley
	std::map<ObjectID, UnsignedInt>::iterator fullIt = s_iranDroneFullSince.find( facility->getID() );
	if( fullIt == s_iranDroneFullSince.end() )
		fullIt = s_iranDroneFullSince.insert( std::make_pair( facility->getID(), now ) ).first;
	const Bool stale = now >= fullIt->second + (UnsignedInt)( IRAN_BREACH_STALE_SECONDS * LOGICFRAMES_PER_SECOND );
	if( slowest <= 0.0f )
		slowest = 100.0f;

	// one walk: the push, the enemy's defenses and other buildings, enemy anti-air units (Stinger Site soldiers, ...)
	std::vector<Object*> army, defenses, buildings, antiAir;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( isIranBreachArmyUnit( obj, iran ) )
		{
			army.push_back( obj );
			continue;
		}
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || !isIranStrikeHostile( iran, obj ) )
			continue;
		if( !obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			if( hasIranStrikeAntiAirWeapon( obj ) )
				antiAir.push_back( obj );
			continue;
		}
		if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->isKindOf( KINDOF_FS_FAKE )
			|| obj->isKindOf( KINDOF_REBUILD_HOLE ) )
			continue;
		const BodyModuleInterface *body = obj->getBodyModule();
		if( body == 0 || body->getHealth() <= 0.0f )
			continue;
		if( isIranStrikeFrontlineDefense( obj ) )
			defenses.push_back( obj );
		else if( obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() ) > 0 || obj->isKindOf( KINDOF_TECH_BUILDING ) )
			buildings.push_back( obj );
	}
	if( (Int)army.size() < IRAN_BREACH_MIN_ARMY && !stale )
		return FALSE;

	// Hypersonic @feature (D-189) Iran's home: the AI base centre (centroid of its build list, fixed per game), else its
	// own Command Center; an anchor within IRAN_BREACH_HOME_CLEAR of it is the home guard's fight, never a breach
	Coord3D home;
	Bool hasHome = iran->getAiBaseCenter( &home );
	if( !hasHome )
	{
		Object *commandCenter = iran->findNaturalCommandCenter();
		if( commandCenter != 0 )
		{
			home = *commandCenter->getPosition();
			hasHome = TRUE;
		}
	}

	// the line the push has reached: the defense (else building) with the most of the army within its breach reach
	const std::vector<Object*> *line = 0;
	Object *anchor = 0;
	Int anchorCount = 0;
	Real anchorReach = 0.0f;
	for( Int pass = 0; pass < 2 && anchor == 0; ++pass )
	{
		const std::vector<Object*> &list = pass == 0 ? defenses : buildings;
		for( size_t si = 0; si < list.size(); ++si )
		{
			if( hasHome && iranStrikeDistanceSqr( list[si]->getPosition(), &home ) <= sqr( IRAN_BREACH_HOME_CLEAR ) )
				continue;	// Hypersonic @feature (D-189) too near Iran's base
			const Real reach = iranBreachReach( facility, list[si], slowest );
			Int count = 0;
			for( size_t au = 0; au < army.size(); ++au )
				if( iranStrikeDistanceSqr( army[au]->getPosition(), list[si]->getPosition() ) <= sqr( reach ) )
					++count;
			if( count >= IRAN_BREACH_MIN_ARMY && count > anchorCount )
			{
				anchor = list[si];
				anchorCount = count;
				anchorReach = reach;
			}
		}
		if( anchor != 0 )
			line = &list;
	}
	Bool staleVolley = FALSE;
	if( anchor == 0 && stale && hasHome )
	{
		// Hypersonic @feature (D-189 stale volley) no push at the enemy line, but the drones have waited long enough:
		// the enemy defense (else building) nearest Iran's base, outside the home guard's area
		for( Int pass = 0; pass < 2 && anchor == 0; ++pass )
		{
			const std::vector<Object*> &list = pass == 0 ? defenses : buildings;
			Real bestSqr = 0.0f;
			for( size_t si = 0; si < list.size(); ++si )
			{
				const Real dSqr = iranStrikeDistanceSqr( list[si]->getPosition(), &home );
				if( dSqr <= sqr( IRAN_BREACH_HOME_CLEAR ) )
					continue;
				if( anchor == 0 || dSqr < bestSqr )
				{
					anchor = list[si];
					bestSqr = dSqr;
				}
			}
			if( anchor != 0 )
				line = &list;
		}
		staleVolley = anchor != 0;
	}
	if( anchor == 0 )
		return FALSE;	// no push at the enemy line: hold the drones

	// the army group: its units within that reach; the line: around the structure nearest the group's centre (a stale
	// volley: nearest Iran's base)
	Coord3D centre;
	centre.zero();
	if( staleVolley )
		centre = home;
	else
	{
		for( size_t ac = 0; ac < army.size(); ++ac )
			if( iranStrikeDistanceSqr( army[ac]->getPosition(), anchor->getPosition() ) <= sqr( anchorReach ) )
			{
				centre.x += army[ac]->getPosition()->x;
				centre.y += army[ac]->getPosition()->y;
			}
		centre.x /= (Real)anchorCount;
		centre.y /= (Real)anchorCount;
	}
	// a stale volley starts at its anchor: the nearest structure to Iran's base can sit inside the home guard's area
	Object *first = staleVolley ? anchor : 0;
	Real firstSqr = 0.0f;
	for( size_t f = 0; f < line->size() && !staleVolley; ++f )
	{
		const Real dSqr = iranStrikeDistanceSqr( (*line)[f]->getPosition(), &centre );
		if( first == 0 || dSqr < firstSqr )
		{
			first = (*line)[f];
			firstSqr = dSqr;
		}
	}

	// candidates: the line around it, anti-air first (defenses only), then nearest the army; stable insertion order
	const Bool defenseLine = line == &defenses;
	std::vector<Object*> candidates;
	std::vector<Int> candidateClass;
	std::vector<Real> candidateDistance;
	for( size_t c = 0; c < line->size(); ++c )
	{
		Object *obj = (*line)[c];
		if( iranStrikeDistanceSqr( obj->getPosition(), first->getPosition() ) > sqr( IRAN_BREACH_LINE ) )
			continue;
		Bool aa = FALSE;
		if( defenseLine )
		{
			aa = hasIranStrikeAntiAirWeapon( obj );
			for( size_t sa = 0; sa < antiAir.size() && !aa; ++sa )	// its spawned crew (Stinger Site soldiers)
				if( antiAir[sa]->getProducerID() == obj->getID() || antiAir[sa]->getContainedBy() == obj )
					aa = TRUE;
		}
		const Int cls = aa ? 0 : 1;
		const Real dist = iranStrikeDistanceSqr( obj->getPosition(), &centre );
		size_t at = candidates.size();
		while( at > 0 && ( candidateClass[at - 1] > cls || ( candidateClass[at - 1] == cls && candidateDistance[at - 1] > dist ) ) )
			--at;
		candidates.insert( candidates.begin() + at, obj );
		candidateClass.insert( candidateClass.begin() + at, cls );
		candidateDistance.insert( candidateDistance.begin() + at, dist );
	}

	// targets: the first IRAN_BREACH_TARGETS candidates that what is already on its way does not destroy
	std::vector<IranStrikeCommitted> committed;
	collectIranStrikeCommitted( iran, infos, committed );
	Object *targets[IRAN_BREACH_TARGETS];
	Real remaining[IRAN_BREACH_TARGETS];
	Int targetCount = 0;
	for( size_t ti = 0; ti < candidates.size() && targetCount < IRAN_BREACH_TARGETS; ++ti )
	{
		Object *target = candidates[ti];
		const Real radius = target->getGeometryInfo().getBoundingSphereRadius();
		const Real left = target->getBodyModule()->getHealth() - iranStrikeCommittedDamage( infos, committed, target, radius, 0 );
		if( left <= 0.0f )
			continue;
		targets[targetCount] = target;
		remaining[targetCount] = left;
		++targetCount;
	}

	// Hypersonic @feature (D-189) a short line: spread the volley to the next defenses within IRAN_BREACH_EXTRA_DEFENSES of
	// the anchor, then the buildings within IRAN_BREACH_EXTRA_BUILDINGS of it (holes, fakes and buildings under
	// construction were never collected), each nearest the anchor first (stable: object-list order on ties)
	for( Int xe = 0; xe < 2 && targetCount < IRAN_BREACH_TARGETS; ++xe )
	{
		const std::vector<Object*> &xpool = xe == 0 ? defenses : buildings;
		const Real reachSqr = sqr( xe == 0 ? IRAN_BREACH_EXTRA_DEFENSES : IRAN_BREACH_EXTRA_BUILDINGS );
		std::vector<Object*> nearby;
		std::vector<Real> nearbyDistance;
		for( size_t xi = 0; xi < xpool.size(); ++xi )
		{
			Object *obj = xpool[xi];
			const Real dist = iranStrikeDistanceSqr( obj->getPosition(), anchor->getPosition() );
			Bool weighed = FALSE;	// a line candidate already weighed above
			for( size_t xc = 0; xc < candidates.size() && !weighed; ++xc )
				weighed = candidates[xc] == obj;
			if( dist > reachSqr || weighed )
				continue;
			size_t at = nearby.size();
			while( at > 0 && nearbyDistance[at - 1] > dist )
				--at;
			nearby.insert( nearby.begin() + at, obj );
			nearbyDistance.insert( nearbyDistance.begin() + at, dist );
		}
		for( size_t xn = 0; xn < nearby.size() && targetCount < IRAN_BREACH_TARGETS; ++xn )
		{
			Object *target = nearby[xn];
			const Real radius = target->getGeometryInfo().getBoundingSphereRadius();
			const Real left = target->getBodyModule()->getHealth() - iranStrikeCommittedDamage( infos, committed, target, radius, 0 );
			if( left <= 0.0f )
				continue;
			targets[targetCount] = target;
			remaining[targetCount] = left;
			++targetCount;
		}
		candidates.insert( candidates.end(), nearby.begin(), nearby.end() );	// the buildings pass skips these too
	}
	if( targetCount == 0 )
		return FALSE;	// the drones already on their way take this line

	// the volley: Arash, then Shahed-238, then Shahed-136 (then any other type), each in launcher order
	static const char *const volleyOrder[3] = { "IranAirArash", "IranAirShahed238", "IranAirShahed136" };
	std::vector<Int> pool;
	for( Int kind = 0; kind <= 3; ++kind )
		for( size_t lp = 0; lp < launchers.size(); ++lp )
		{
			const AsciiString &name = infos[launchers[lp].m_info].m_munition->getName();
			Int rank = 3;
			for( Int r = 0; r < 3; ++r )
				if( name == volleyOrder[r] )
					rank = r;
			if( rank != kind )
				continue;
			for( Int k = 0; k < launchers[lp].m_loaded && (Int)pool.size() < IRAN_BREACH_MAX_DRONES; ++k )
				pool.push_back( (Int)lp );
		}

	// allocation: fill target 1 until the expected damage (scatter only) reaches its health, then target 2, ...; the
	// rest round-robin over the targets
	std::vector<Int> assigned( pool.size(), 0 );
	size_t next = 0;
	for( Int ta = 0; ta < targetCount && next < pool.size(); ++ta )
	{
		const Real radius = targets[ta]->getGeometryInfo().getBoundingSphereRadius();
		Real dealt = 0.0f;
		while( next < pool.size() && dealt < remaining[ta] )
		{
			dealt += iranStrikeDamage( infos[launchers[pool[next]].m_info], targets[ta], radius, -1.0f );
			assigned[next] = ta;
			++next;
		}
	}
	for( Int spread = 0; next < pool.size(); ++next, ++spread )
		assigned[next] = spread % targetCount;

	// fire
	std::vector<Int> launched( infos.size(), 0 );
	Int total = 0;
	for( size_t v = 0; v < pool.size(); ++v )
	{
		IranStrikeLauncher &launcher = launchers[pool[v]];
		Object *target = targets[assigned[v]];
		if( !launcher.m_module->isReady() )
			continue;
		const Int before = launcher.m_module->getShortcutReadyCount();
		launcher.m_module->doSpecialPowerAtObject( target, COMMAND_FIRED_BY_SCRIPT );
		if( launcher.m_module->getShortcutReadyCount() >= before )
			continue;
		++launched[launcher.m_info];
		++total;
		if( TheHypersonicScenario != 0 )
			TheHypersonicScenario->noteMunitionLaunch( launcher.m_module->getSpecialPowerTemplate()->getName().str(), target );
	}
	if( TheHypersonicScenario != 0 && total > 0 )
	{
		AsciiString names, counts, part;
		for( Int tn = 0; tn < targetCount; ++tn )
		{
			part.format( "%s%s#%u", tn > 0 ? "," : "", targets[tn]->getTemplate()->getName().str(), (UnsignedInt)targets[tn]->getID() );
			names.concat( part );
		}
		for( size_t ni = 0; ni < infos.size(); ++ni )
		{
			part.format( "%s%s:%d", ni > 0 ? "," : "", infos[ni].m_munition->getName().str(), launched[ni] );
			counts.concat( part );
		}
		TheHypersonicScenario->noteAI( "BREACH%s army=%d reach=%.0f line=%s targets=%s munitions=%s",
			staleVolley ? " STALE" : "", anchorCount, anchorReach, defenseLine ? "defenses" : "buildings", names.str(),
			counts.str() );
	}
	if( total > 0 )
		s_iranDroneFullSince.erase( facility->getID() );	// Hypersonic @feature (D-189 stale volley) the wait starts over
	return total > 0;
}

// Hypersonic @feature (D-189) health of the rebuild hole a building leaves when it dies (GLA), else 0
static Real iranStrikeHoleHealth( const Object *target )
{
	const ModuleInfo &modules = target->getTemplate()->getBehaviorModuleInfo();
	for( Int i = 0; i < modules.getCount(); ++i )
	{
		if( !( modules.getNthName( i ) == "RebuildHoleExposeDie" ) )
			continue;
		const RebuildHoleExposeDieModuleData *data = (const RebuildHoleExposeDieModuleData *)modules.getNthData( i );
		if( data != 0 && data->m_holeName.isNotEmpty() )
			return data->m_holeMaxHealth;
	}
	return 0.0f;
}

// Hypersonic @feature (D-189) missile target class: 4 superweapon, 3 airfield (needs parked aircraft), 2 vehicle factory,
// 1 money building (supply center/stash, black market, Iran refinery, supply drop zone, captured cash tech), 0 none
static Int iranMissilePriority( const Object *obj )
{
	if( obj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
		return 4;
	if( obj->isKindOf( KINDOF_FS_AIRFIELD ) )
		return 3;
	if( obj->isKindOf( KINDOF_FS_WARFACTORY ) )
		return 2;
	if( obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || obj->isKindOf( KINDOF_FS_BLACK_MARKET )
		|| obj->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || obj->isKindOf( KINDOF_CASH_GENERATOR ) )
		return 1;
	if( obj->isKindOf( KINDOF_TECH_BUILDING ) )
	{
		// a captured oil derrick: pays its owner on a timer
		const ModuleInfo &modules = obj->getTemplate()->getBehaviorModuleInfo();
		for( Int i = 0; i < modules.getCount(); ++i )
			if( modules.getNthName( i ) == "AutoDepositUpdate" )
				return 1;
	}
	return 0;
}

struct IranMissileShot
{
	Int m_launcher;
	Real m_damage;
	Real m_cost;
	Bool m_fattah;
	Bool m_taken;
	Bool m_hole;		///< one of the delayed rebuild-hole missiles
};

// Hypersonic @feature (D-189) one planned missile salvo of all Iran's missile facilities; TRUE when it launched
static Bool iranMissileSalvo( Player *iran )
{
	std::vector<IranMunitionInfo> infos;
	std::vector<IranStrikeLauncher> launchers;
	for( Object *own = TheGameLogic->getFirstObject(); own; own = own->getNextObject() )
		if( own->getControllingPlayer() == iran && own->isKindOf( KINDOF_STRUCTURE ) )
			addIranStrikeLaunchers( own, FALSE, infos, launchers );
	Int loaded = 0;
	for( size_t ll = 0; ll < launchers.size(); ++ll )
		loaded += launchers[ll].m_loaded;
	if( loaded <= 0 )
		return FALSE;
	std::vector<IranStrikeCommitted> committed;
	collectIranStrikeCommitted( iran, infos, committed );

	// one walk: whitelisted targets, grounded aircraft (airfield value), working SAM interceptors (Fattah rule)
	std::vector<Object*> candidates;
	std::vector<Int> priorities;
	std::vector<Real> values;
	std::vector<Object*> grounded;
	std::vector<IranStrikeDefense> sams;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_PROJECTILE ) || !isIranStrikeHostile( iran, obj ) )
			continue;
		if( obj->isKindOf( KINDOF_AIRCRAFT ) )
		{
			if( !obj->isAboveTerrain() )
				grounded.push_back( obj );
			continue;
		}
		if( !obj->isDisabled() )
		{
			IranStrikeDefense def;
			def.m_pos = *obj->getPosition();
			def.m_id = obj->getID();
			def.m_samRange = def.m_laserRange = def.m_gunRange = 0.0f;
			for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
			{
				const Weapon *w = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
				if( w != 0 && w->getTemplate()->getInterceptorClass() == INTERCEPTOR_SAM
					&& ( w->getAntiMask() & ( WEAPON_ANTI_AIRBORNE_VEHICLE | WEAPON_ANTI_BALLISTIC_MISSILE | WEAPON_ANTI_SMALL_MISSILE ) ) )
					def.m_samRange = max( def.m_samRange, w->getAttackRange( obj ) );
			}
			if( def.m_samRange > 0.0f )
				sams.push_back( def );
		}
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| obj->isKindOf( KINDOF_FS_FAKE ) || obj->isKindOf( KINDOF_REBUILD_HOLE ) )
			continue;
		const Int priority = iranMissilePriority( obj );
		const BodyModuleInterface *body = obj->getBodyModule();
		if( priority <= 0 || body == 0 || body->getHealth() <= 0.0f )
			continue;
		Real value = (Real)obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() );
		if( ( obj->isKindOf( KINDOF_CASH_GENERATOR ) || obj->isKindOf( KINDOF_TECH_BUILDING ) ) && value < 1000.0f )
			value = 1000.0f;	// captured cash techs: income despite no build cost (D-122)
		candidates.push_back( obj );
		priorities.push_back( priority );
		values.push_back( value );
	}
	// airfields count only with parked aircraft, which die with them; their value is added
	for( size_t a = 0; a < candidates.size(); ++a )
	{
		if( priorities[a] != 3 )
			continue;
		const Real reach = candidates[a]->getGeometryInfo().getBoundingSphereRadius() + 30.0f;
		Real parked = 0.0f;
		for( size_t g = 0; g < grounded.size(); ++g )
			if( grounded[g]->getControllingPlayer() == candidates[a]->getControllingPlayer()
				&& iranStrikeDistanceSqr( grounded[g]->getPosition(), candidates[a]->getPosition() ) <= sqr( reach ) )
				parked += (Real)grounded[g]->getTemplate()->calcCostToBuild( grounded[g]->getControllingPlayer() );
		if( parked > 0.0f )
			values[a] += parked;
		else
			priorities[a] = 0;
	}
	// priority, then value, then object-list order (stable)
	std::vector<Int> order;
	for( size_t c = 0; c < candidates.size(); ++c )
	{
		if( priorities[c] <= 0 )
			continue;
		size_t at = order.size();
		while( at > 0 && ( priorities[order[at - 1]] < priorities[c]
			|| ( priorities[order[at - 1]] == priorities[c] && values[order[at - 1]] < values[c] ) ) )
			--at;
		order.insert( order.begin() + at, (Int)c );
	}

	for( size_t o = 0; o < order.size(); ++o )
	{
		Object *target = candidates[order[o]];
		const Int priority = priorities[order[o]];
		const Coord3D pos = *target->getPosition();
		const Real radius = target->getGeometryInfo().getBoundingSphereRadius();
		const Real health = target->getBodyModule()->getHealth();
		const Real hole = iranStrikeHoleHealth( target );
		Int sam = 0;
		for( size_t d = 0; d < sams.size(); ++d )
			if( iranStrikeDistanceSqr( &sams[d].m_pos, &pos ) <= sqr( sams[d].m_samRange + radius ) )
				++sam;
		// D-122: the rare premium missile. Never at a rebuild-hole building: its short silo sortie and speed land it about
		// 20 s before the TEL missiles, and a hole is rebuilt 20 s after its building died.
		const Bool fattahAllowed = ( priority >= 3 || sam >= 3 ) && hole <= 0.0f;
		Int committedCount = 0;
		const Real done = iranStrikeCommittedDamage( infos, committed, target, radius, &committedCount );
		if( done >= health + hole )
			continue;	// already doomed by what is on its way

		// the loaded missiles that can reach it: non-Fattah strongest first, then Fattah (stable)
		std::vector<IranMissileShot> shots;
		for( size_t sl = 0; sl < launchers.size(); ++sl )
		{
			if( launchers[sl].m_loaded <= 0 || !launchers[sl].m_module->isValidTargetLocation( &pos ) )
				continue;
			const IranMunitionInfo &info = infos[launchers[sl].m_info];
			IranMissileShot shot;
			shot.m_launcher = (Int)sl;
			shot.m_damage = iranStrikeDamage( info, target, radius, -1.0f );
			shot.m_cost = info.m_cost;
			shot.m_fattah = info.m_munition->getName() == "IranMissileFattah";
			shot.m_taken = FALSE;
			shot.m_hole = FALSE;
			if( shot.m_fattah && !fattahAllowed )
				continue;
			for( Int k = 0; k < launchers[sl].m_loaded; ++k )
			{
				size_t at = shots.size();
				while( at > 0 && ( ( shots[at - 1].m_fattah && !shot.m_fattah )
					|| ( shots[at - 1].m_fattah == shot.m_fattah && shots[at - 1].m_damage < shot.m_damage ) ) )
					--at;
				shots.insert( shots.begin() + at, shot );
			}
		}

		// the building: strongest first until the expected damage reaches its health
		Int picked = 0;
		Real sum = done;
		for( size_t sb = 0; sb < shots.size() && sum < health; ++sb )
		{
			shots[sb].m_taken = TRUE;
			sum += shots[sb].m_damage;
			++picked;
		}
		if( sum < health )
			continue;	// not killable with what is loaded now
		// its rebuild hole (exists only after the building died): cheapest non-Fattah first, launched later
		Int holePicked = 0;
		if( hole > 0.0f )
		{
			Real holeSum = done > health ? done - health : 0.0f;	// earlier D-189 hole missiles of this target
			while( holeSum < hole )
			{
				Int cheapest = -1;
				for( size_t sh = 0; sh < shots.size(); ++sh )
					if( !shots[sh].m_taken && !shots[sh].m_fattah && ( cheapest < 0 || shots[sh].m_cost < shots[cheapest].m_cost ) )
						cheapest = (Int)sh;
				if( cheapest < 0 )
					break;
				shots[cheapest].m_taken = TRUE;
				shots[cheapest].m_hole = TRUE;
				holeSum += shots[cheapest].m_damage;
				++holePicked;
			}
			if( holeSum < hole )
				continue;
		}
		// superweapons and rebuild-hole buildings: at least 2 missiles (with those on their way)
		const Int minimum = ( priority == 4 || hole > 0.0f ) ? 2 : 1;
		for( size_t sm = 0; sm < shots.size() && picked + holePicked + committedCount < minimum; ++sm )
			if( !shots[sm].m_taken )
			{
				shots[sm].m_taken = TRUE;
				++picked;
			}
		if( picked + holePicked + committedCount < minimum || picked + holePicked + committedCount > IRAN_MISSILE_MAX_SALVO
			|| picked + holePicked == 0 )
			continue;

		// fire: the building's missiles now, the hole's IRAN_HOLE_DELAY_SECONDS later (they wait at the exit)
		const UnsignedInt holeDelay = (UnsignedInt)( IRAN_HOLE_DELAY_SECONDS * LOGICFRAMES_PER_SECOND );
		Int launched = 0, holeLaunched = 0;
		for( Int pass = 0; pass < 2; ++pass )
			for( size_t sf = 0; sf < shots.size(); ++sf )
			{
				if( !shots[sf].m_taken || shots[sf].m_hole != ( pass == 1 ) )
					continue;
				IranStrikeLauncher &launcher = launchers[shots[sf].m_launcher];
				if( !launcher.m_module->isReady() )
					continue;
				const Int before = launcher.m_module->getShortcutReadyCount();
				if( shots[sf].m_hole )
					launcher.m_magazine->setNextLaunchDelay( holeDelay );
				launcher.m_module->doSpecialPowerAtObject( target, COMMAND_FIRED_BY_SCRIPT );
				launcher.m_magazine->setNextLaunchDelay( 0 );
				if( launcher.m_module->getShortcutReadyCount() >= before )
					continue;
				++launched;
				if( shots[sf].m_hole )
					++holeLaunched;
				if( TheHypersonicScenario != 0 )
					TheHypersonicScenario->noteMunitionLaunch( launcher.m_module->getSpecialPowerTemplate()->getName().str(), target );
			}
		if( TheHypersonicScenario != 0 && launched > 0 )
			TheHypersonicScenario->noteAI( "MISSILE PLAN %s#%u priority=%d value=%.0f hp=%.0f hole=%.0f sam=%d committed=%d launched=%d hole_missiles=%d",
				target->getTemplate()->getName().str(), (UnsignedInt)target->getID(), priority, values[order[o]], health, hole,
				sam, committedCount, launched, holeLaunched );
		return launched > 0;	// the best killable target only
	}
	return FALSE;	// nothing killable worth a missile: hold
}

//-------------------------------------------------------------------------------------------------
// Hypersonic @feature Qadr cluster missile, Iran computer player (Generals Hypersonic D-245). Kept apart from the
// D-110/D-189 building picker (SPECIAL_MUNITION_LAUNCH_7 is outside its 1-6 range): the Qadr's 10 small blasts over
// radius ~130 pay off on groups, not on one building. The aim is the centre of the enemy group worth the most: every
// enemy infantry, ground vehicle and base defense counts its build cost (moving units half: they may have left when
// the missile lands ~15-30 s later), summed over the group within IRAN_QADR_REACH of each candidate, aimed at that
// group's centre. Fired only when the group has at least IRAN_QADR_MIN_COUNT members worth IRAN_QADR_WORTH x the
// Qadr's cost, and no other Qadr of this player is already on its way (in the air or ordered) within IRAN_QADR_SPACING
// of the aim. One launch per call; synced logic only (object list order, no random draws).
//-------------------------------------------------------------------------------------------------
static const Real IRAN_QADR_REACH = 130.0f;		///< D-245: group scoring radius (submunition spread is now 136 + 60)
static const Real IRAN_QADR_SPACING = 250.0f;	///< no second Qadr aimed this close to one already on its way
static const Real IRAN_QADR_WORTH = 1.5f;			///< the group must be worth this x the Qadr's cost
enum { IRAN_QADR_MIN_COUNT = 3 };

static Bool isIranQadrTargetCandidate( Player *iran, Object *foe )
{
	if( foe->isEffectivelyDead() || foe->getTeam() == nullptr || foe->getControllingPlayer() == iran
		|| iran->getRelationship( foe->getTeam() ) != ENEMIES || foe->isKindOf( KINDOF_PROJECTILE )
		|| foe->isKindOf( KINDOF_FS_FAKE ) || foe->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
		|| foe->isKindOf( KINDOF_AIRCRAFT ) || foe->isAboveTerrain() || foe->getContainedBy() != nullptr )
		return FALSE;
	if( foe->isKindOf( KINDOF_STRUCTURE ) )
		return foe->isKindOf( KINDOF_FS_BASE_DEFENSE );
	return foe->isKindOf( KINDOF_INFANTRY ) || foe->isKindOf( KINDOF_VEHICLE );
}

static Bool iranQadrStrike( Player *iran, const SpecialPowerTemplate *power )
{
	if( iran == nullptr || power == nullptr )
		return FALSE;
	// a ready launcher of this power (an Iran Missile Facility with a loaded Qadr gate)
	SpecialPowerModuleInterface *module = nullptr;
	const ThingTemplate *qadr = nullptr;
	for( Object *own = TheGameLogic->getFirstObject(); own && module == nullptr; own = own->getNextObject() )
	{
		if( own->getControllingPlayer() != iran || !own->isKindOf( KINDOF_STRUCTURE ) || own->isEffectivelyDead()
			|| own->isDisabled() || own->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || own->testStatus( OBJECT_STATUS_SOLD ) )
			continue;
		SpecialPowerModuleInterface *sp = own->getSpecialPowerModule( power );
		if( sp == nullptr || !sp->isReady() || sp->getMunitionTemplate() == nullptr
			|| !TheSpecialPowerStore->canUseSpecialPower( own, power ) )
			continue;
		module = sp;
		qadr = sp->getMunitionTemplate();
	}
	if( module == nullptr )
		return FALSE;

	// Qadrs already on their way: in the air (aim point) or ordered at a gate (target position)
	std::vector<Coord3D> busy;
	for( Object *mine = TheGameLogic->getFirstObject(); mine; mine = mine->getNextObject() )
	{
		if( mine->getControllingPlayer() != iran || mine->isEffectivelyDead() )
			continue;
		if( mine->getTemplate()->isEquivalentTo( qadr ) )
		{
			const AIUpdateInterface *ai = mine->getAI();
			const MissileAIUpdate *missile = ai != nullptr ? ai->getMissileAIUpdate() : nullptr;
			if( missile != nullptr )
				busy.push_back( *missile->getOriginalTargetPos() );
			continue;
		}
		if( !mine->isKindOf( KINDOF_STRUCTURE ) )
			continue;
		for( BehaviorModule **b = mine->getBehaviorModules(); *b; ++b )
		{
			const MunitionMagazineInterface *mag = (*b)->getMunitionMagazineInterface();
			if( mag == nullptr )
				continue;
			ObjectID targets[8];
			Coord3D positions[8];
			const ThingTemplate *munitions[8];
			const Int pending = mag->getPendingLaunches( targets, positions, munitions, 8 );
			for( Int i = 0; i < pending; ++i )
				if( munitions[i] != nullptr && munitions[i]->isEquivalentTo( qadr ) )
					busy.push_back( positions[i] );
		}
	}

	// the enemy's infantry, ground vehicles and base defenses, with their value
	std::vector<Object*> foes;
	std::vector<Real> values;
	for( Object *foe = TheGameLogic->getFirstObject(); foe; foe = foe->getNextObject() )
	{
		if( !isIranQadrTargetCandidate( iran, foe ) )
			continue;
		Real value = (Real)foe->getTemplate()->calcCostToBuild( foe->getControllingPlayer() );
		if( value <= 0.0f )
			continue;
		const AIUpdateInterface *ai = foe->getAI();
		if( !foe->isKindOf( KINDOF_STRUCTURE ) && ai != nullptr && ai->isMoving() )
			value *= 0.5f;
		foes.push_back( foe );
		values.push_back( value );
	}

	const Real need = IRAN_QADR_WORTH * (Real)qadr->calcCostToBuild( iran );
	Real bestValue = 0.0f;
	Coord3D bestAim;
	bestAim.zero();
	for( size_t c = 0; c < foes.size(); ++c )
	{
		const Coord3D *at = foes[c]->getPosition();
		Real sum = 0.0f, sx = 0.0f, sy = 0.0f;
		Int count = 0;
		for( size_t m = 0; m < foes.size(); ++m )
		{
			const Coord3D *p = foes[m]->getPosition();
			if( sqr( p->x - at->x ) + sqr( p->y - at->y ) > sqr( IRAN_QADR_REACH ) )
				continue;
			sum += values[m];
			sx += values[m] * p->x;
			sy += values[m] * p->y;
			++count;
		}
		if( count < IRAN_QADR_MIN_COUNT || sum < need || sum <= bestValue )
			continue;
		Coord3D aim;
		aim.x = sx / sum;
		aim.y = sy / sum;
		aim.z = TheTerrainLogic->getGroundHeight( aim.x, aim.y );
		if( !module->isValidTargetLocation( &aim ) )
			continue;
		Bool taken = FALSE;
		for( size_t k = 0; k < busy.size() && !taken; ++k )
			if( sqr( busy[k].x - aim.x ) + sqr( busy[k].y - aim.y ) < sqr( IRAN_QADR_SPACING ) )
				taken = TRUE;
		if( taken )
			continue;
		bestValue = sum;
		bestAim = aim;
	}
	if( bestValue <= 0.0f )
		return FALSE;
	const Int loadedBefore = module->getShortcutReadyCount();
	module->doSpecialPowerAtLocation( &bestAim, 0.0f, COMMAND_FIRED_BY_SCRIPT );
	return module->getShortcutReadyCount() < loadedBefore;
}

void ScriptActions::doSkirmishFireSpecialPowerAtMostCost( const AsciiString &player, const AsciiString& specialPower )
{
	Int enemyNdx;
	Player *enemyPlayer = TheScriptEngine->getSkirmishEnemyPlayer();
	if (enemyPlayer == nullptr) return;
	enemyNdx = enemyPlayer->getPlayerIndex();

	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (power==nullptr)
		return;
	Real radius = 50.0f;
	if (power->getRadiusCursorRadius()>radius) {
		radius = power->getRadiusCursorRadius();
	}

	Player::PlayerTeamList::const_iterator it;

	Player *pPlayer = TheScriptEngine->getPlayerFromAsciiString(player);
	if (pPlayer==nullptr)
		return;

	SpecialPowerType type = power->getSpecialPowerType();
	// Hypersonic @feature (D-245) the Qadr: its own group picker (iranQadrStrike), for the Iran computer player only
	if( type == SPECIAL_MUNITION_LAUNCH_7 )
	{
		if( pPlayer->getPlayerType() == PLAYER_COMPUTER && pPlayer->getSide() == "Iran" )
			iranQadrStrike( pPlayer, power );
		return;
	}
	if( type >= SPECIAL_MUNITION_LAUNCH_1 && type <= SPECIAL_MUNITION_LAUNCH_6 )
	{
		// Hypersonic @feature (D-189) the Iran computer player's strike doctrine; the harness flags MunitionPicker = Legacy
		// (D-078) and D122 (D-110/D-122 kill-only picker) keep the older pickers below for comparison runs
		const Bool doctrine = pPlayer->getPlayerType() == PLAYER_COMPUTER && pPlayer->getSide() == "Iran"
			&& !( TheHypersonicScenario != 0 && ( TheHypersonicScenario->isLegacyMunitionPicker() || TheHypersonicScenario->isD122MunitionPicker() ) );
		if( doctrine )
		{
			// Hypersonic @feature (D-189) the scripts call this once per launch power (3 drone, 3 missile powers); a run
			// covers its whole group, so after a run that launched nothing the group's other calls in this frame are
			// skipped. After a launch the next call runs again (the next facility / the next salvo), as before.
			const Int group = type > SPECIAL_MUNITION_LAUNCH_3 ? 1 : 0;
			const Int playerIndex = pPlayer->getPlayerIndex();
			const Bool tracked = playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT;
			const UnsignedInt mark = TheGameLogic->getFrame() + 1;
			if( tracked && s_iranDoctrineIdleFrame[playerIndex][group] == mark )
				return;
			Bool launched = FALSE;
			if( group == 1 )
				launched = iranMissileSalvo( pPlayer );	// one salvo of every missile facility
			else
			{
				// the drone breach volley of the first Drone Facility that has one (all its loaded drones), else held
				for( it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end() && !launched; ++it )
					for( DLINK_ITERATOR<Team> teamIt = (*it)->iterate_TeamInstanceList(); !teamIt.done() && !launched; teamIt.advance() )
					{
						Team *team = teamIt.cur();
						if( team == nullptr ) continue;
						for( DLINK_ITERATOR<Object> objIt = team->iterate_TeamMemberList(); !objIt.done() && !launched; objIt.advance() )
						{
							Object *site = objIt.cur();
							if( site != nullptr && site->isKindOf( KINDOF_STRUCTURE ) )
								launched = iranDroneBreach( pPlayer, site );
						}
					}
			}
			if( tracked && !launched )
				s_iranDoctrineIdleFrame[playerIndex][group] = mark;
			return;
		}
		for( it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it )
		{
			for( DLINK_ITERATOR<Team> teamIt = (*it)->iterate_TeamInstanceList(); !teamIt.done(); teamIt.advance() )
			{
				Team *team = teamIt.cur();
				if( team == nullptr ) continue;
				for( DLINK_ITERATOR<Object> objIt = team->iterate_TeamMemberList(); !objIt.done(); objIt.advance() )
				{
				Object *site = objIt.cur();
					if( site == nullptr || site->isDisabled() || site->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ) continue;
					SpecialPowerModuleInterface *module = site->getSpecialPowerModule( power );
					if( module == nullptr || !module->isReady() || !TheSpecialPowerStore->canUseSpecialPower( site, power ) ) continue;
					// Hypersonic @feature (D-110) only at a target the launch is expected to destroy; else hold it
					const Bool legacy = pPlayer->getPlayerType() != PLAYER_COMPUTER || pPlayer->getSide() != "Iran"
						|| ( TheHypersonicScenario != 0 && TheHypersonicScenario->isLegacyMunitionPicker() );
					// Hypersonic @feature (D-122) issue the ready portion together, rechecking committed
					// damage after every order. The next typed power completes mixed salvos in script order.
					ObjectID volleyTarget = INVALID_ID;
					for( Int volley = 0; volley < ( legacy ? 1 : IRAN_STRIKE_MAX_POOL ); ++volley )
					{
						if( !module->isReady() ) break;
						Coord3D aim;
						Object *target = legacy ? findIranMunitionTargetD078( enemyPlayer, module, type )
							: findIranMunitionTargetAnyEnemy( pPlayer, site, module, type, &aim );
						if( target == 0 || ( volleyTarget != INVALID_ID && volleyTarget != target->getID() ) ) break;
						volleyTarget = target->getID();
						if( target != 0 )
						{
							const Int loadedBefore = module->getShortcutReadyCount();
							// Hypersonic @feature (D-122) midpoint strikes use the point, centre strikes keep object semantics.
							if( !legacy && ( sqr( aim.x - target->getPosition()->x ) + sqr( aim.y - target->getPosition()->y ) > 0.01f ) )
								module->doSpecialPowerAtLocation( &aim, 0.0f, COMMAND_FIRED_BY_SCRIPT );
							else module->doSpecialPowerAtObject( target, COMMAND_FIRED_BY_SCRIPT );
							if( TheHypersonicScenario != 0 && module->getShortcutReadyCount() < loadedBefore )
								TheHypersonicScenario->noteMunitionLaunch( power->getName().str(), target );
							if( module->getShortcutReadyCount() >= loadedBefore ) break;
						}
					}
					if( volleyTarget != INVALID_ID ) return;
				}
			}
		}
		return;
	}


	for (it = pPlayer->getPlayerTeams()->begin(); it != pPlayer->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (!team)
				continue;

			for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
			{
				Object *pObj = iter.cur();
				if (!pObj)
					continue;

				SpecialPowerModuleInterface *mod = pObj->getSpecialPowerModule(power);
				if (mod)
				{
					if( !mod->isReady() )
						continue;


	        Coord3D location;
          Bool locationFound = FALSE;

					// Hypersonic @feature (D-172) a power with a MinimumRange (Khorramshahr: 1250) searches only
					// spots at least that far from this source, so the AI fires at the best spot it can hit
					if( power->getMinimumRange() > 0.0f )
						locationFound = pPlayer->computeSuperweaponTarget(power, &location, enemyNdx, radius, pObj->getPosition(), power->getMinimumRange());
					else
						locationFound = pPlayer->computeSuperweaponTarget(power, &location, enemyNdx, radius);

					if( locationFound && power->getSpecialPowerType() == SPECIAL_SNEAK_ATTACK )
					{
						//We need to modify the location. We're already calculated the sweet spot, but we need to modify that
						//position if we can't place it in the current location.
						const ThingTemplate *sneakAttackTemplate = mod->getReferenceThingTemplate();
						if( sneakAttackTemplate )
						{
							locationFound = pPlayer->calcClosestConstructionZoneLocation( sneakAttackTemplate, &location );
						}
					}

          // Hypersonic @feature (D-172) with a MinimumRange no legal valuable spot is an expected hold
          DEBUG_ASSERTCRASH( locationFound || power->getMinimumRange() > 0.0f, ("ScriptActions::doSkirmishFireSpecialPowerAtMostCost() could not find a valid (costly) location.") );

					// Hypersonic @feature (D-163) a spot inside the power's MinimumRange (Khorramshahr: 1250) is
					// refused by the power: try the player's next ready source, else hold the power
					if( locationFound && !mod->isValidTargetLocation( &location ) )
						continue;

					if( locationFound && location.lengthSqr() > 0.0f )
					{
						mod->doSpecialPowerAtLocation( &location, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
					}
					break;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** doNamedFireSpecialPowerAtNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireSpecialPowerAtNamed( const AsciiString& unit, const AsciiString& specialPower, const AsciiString& target )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Object *theTarget = TheScriptEngine->getUnitNamed( target );
	const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate(specialPower);
	if (theObj && power && theTarget)
	{
		SpecialPowerModuleInterface *mod = theObj->getSpecialPowerModule(power);
		if (mod)
		{
			mod->doSpecialPowerAtObject(theTarget, COMMAND_FIRED_BY_SCRIPT );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbility( const AsciiString& unit, const AsciiString& ability )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );

	//Sanity check
	if( !theObj )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButton( commandButton, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityOnNamed( const AsciiString& unit, const AsciiString& ability, const AsciiString& target )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Object *theTarget = TheScriptEngine->getUnitNamed( target );

	//Sanity check
	if( !theObj || !theTarget )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonAtObject( commandButton, theTarget, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityAtWaypoint( const AsciiString& unit, const AsciiString& ability, const AsciiString& waypoint )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	Waypoint *pWaypoint = TheTerrainLogic->getWaypointByName( waypoint );

	//Sanity check
	if( !theObj || !pWaypoint )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonAtPosition( commandButton, pWaypoint->getLocation(), CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedUseCommandButtonAbilityUsingWaypointPath( const AsciiString& unit, const AsciiString& ability, const AsciiString& waypointPath )
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if( !theObj )
	{
		return;
	}

	Waypoint *pWaypoint = TheTerrainLogic->getClosestWaypointOnPath( theObj->getPosition(), waypointPath );

	//Sanity check
	if( !pWaypoint )
	{
		return;
	}

	const CommandSet *commandSet = TheControlBar->findCommandSet( theObj->getCommandSetString() );
	if( commandSet )
	{
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
			//Get the command button.
			const CommandButton *commandButton = commandSet->getCommandButton(i);

			if( commandButton )
			{
				if( !commandButton->getName().isEmpty() )
				{
					if( commandButton->getName() == ability )
					{
						theObj->doCommandButtonUsingWaypoints( commandButton, pWaypoint, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbility( const AsciiString& team, const AsciiString& ability )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam )
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroupPtr theGroup = TheAI->createGroup();
	if( !theGroup )
	{
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupDoCommandButton( commandButton, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityOnNamed( const AsciiString& team, const AsciiString& ability, const AsciiString& target )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam )
	{
		return;
	}

	Object *theObj = TheScriptEngine->getUnitNamed( target );
	if( !theObj )
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroupPtr theGroup = TheAI->createGroup();
	if( !theGroup )
	{
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupDoCommandButtonAtObject( commandButton, theObj, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonAbilityAtWaypoint( const AsciiString& team, const AsciiString& ability, const AsciiString& waypoint )
{
	Team *theTeam = TheScriptEngine->getTeamNamed( team );
	// The team is the team based on the name, and the calling team (if any) and the team that
	// triggered the condition.  jba. :)
	if( !theTeam )
	{
		return;
	}

	Waypoint *pWaypoint = TheTerrainLogic->getWaypointByName( waypoint );
	if( !pWaypoint )
	{
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton )
	{
		return;
	}


	AIGroupPtr theGroup = TheAI->createGroup();
	if( !theGroup )
	{
		return;
	}

#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupDoCommandButtonAtPosition( commandButton, pWaypoint->getLocation(), CMD_FROM_SCRIPT );
}





//-------------------------------------------------------------------------------------------------
/** doRadarRefresh */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarRefresh()
{
	TheRadar->refreshTerrain( TheTerrainLogic );
}


//-------------------------------------------------------------------------------------------------
/** doCameraTetherNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraTetherNamed(const AsciiString& unit, Bool snapToUnit, Real play)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unit );
	if (theObj)
	{
		TheTacticalView->setCameraLock(theObj->getID());
		if (snapToUnit)
			TheTacticalView->snapToCameraLock();

		TheTacticalView->setSnapMode( View::LOCK_TETHER, play );
	}
}

//-------------------------------------------------------------------------------------------------
/** doCameraStopTetherNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraStopTetherNamed()
{
	TheTacticalView->setCameraLock(INVALID_ID);
}

//-------------------------------------------------------------------------------------------------
/** doCameraSetDefault */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCameraSetDefault(Real pitch, Real angle, Real maxHeight)
{
#if PRESERVE_RETAIL_SCRIPTED_CAMERA
	// TheSuperHackers @tweak To preserve the original scripted camera values, offset them by default ones.
	pitch = -pitch + ViewDefaultPitchRadians;
	angle = angle + ViewDefaultYawRadians;
#endif

	TheTacticalView->setDefaultView(pitch, angle, maxHeight);
}

//-------------------------------------------------------------------------------------------------
/** doNamedStop */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedStop(const AsciiString& unitName)
{
	Object *theObj = TheScriptEngine->getUnitNamed( unitName );
	if (!theObj) {
		return;
	}

	AIUpdateInterface *ai = theObj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->aiIdle(CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamStop */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStop(const AsciiString& teamName, Bool shouldDisband)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupIdle(CMD_FROM_SCRIPT);

	if (shouldDisband) {
		Team *playerDefaultTeam = theTeam->getControllingPlayer()->getDefaultTeam();

		for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
			Object *obj = iter.cur();

			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if (!ai) {
				continue;
			}

			ai->setIsRecruitable(TRUE);
		}

		doMergeTeamIntoTeam(teamName, playerDefaultTeam->getName());
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetOverrideRelationToTeam(const AsciiString& teamName, const AsciiString& otherTeam, Int relation)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (theTeam && theOtherTeam) {
		theTeam->setOverrideTeamRelationship(theOtherTeam->getID(), (Relationship)relation);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveOverrideRelationToTeam(const AsciiString& teamName, const AsciiString& otherTeam)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (theTeam && theOtherTeam) {
		theTeam->removeOverrideTeamRelationship(theOtherTeam->getID());
	}
}

//-------------------------------------------------------------------------------------------------
/** doPlayerSetOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetOverrideRelationToTeam(const AsciiString& playerName, const AsciiString& otherTeam, Int relation)
{
	Player *thePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (thePlayer && theOtherTeam) {
		thePlayer->setTeamRelationship(theOtherTeam, (Relationship)relation);
	}
}

//-------------------------------------------------------------------------------------------------
/** doPlayerRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerRemoveOverrideRelationToTeam(const AsciiString& playerName, const AsciiString& otherTeam)
{
	Player *thePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
	Team *theOtherTeam = TheScriptEngine->getTeamNamed( otherTeam );
	if (thePlayer && theOtherTeam) {
		thePlayer->removeTeamRelationship(theOtherTeam);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSetOverrideRelationToPlayer */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSetOverrideRelationToPlayer(const AsciiString& teamName, const AsciiString& otherPlayer, Int relation)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Player *theOtherPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(otherPlayer));
	if (theTeam && theOtherPlayer) {
		theTeam->setOverridePlayerRelationship(theOtherPlayer->getPlayerIndex(), (Relationship)relation);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveOverrideRelationToTeam */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveOverrideRelationToPlayer(const AsciiString& teamName, const AsciiString& otherPlayer)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	Player *theOtherPlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY(otherPlayer));
	if (theTeam && theOtherPlayer) {
		theTeam->removeOverridePlayerRelationship(theOtherPlayer->getPlayerIndex());
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamRemoveAllOverrideRelations */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamRemoveAllOverrideRelations(const AsciiString& teamName)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if (theTeam) {
		// invalid ID is OK -- it removes all relationships
		theTeam->removeOverrideTeamRelationship( TEAM_ID_INVALID );
		theTeam->removeOverridePlayerRelationship( PLAYER_INDEX_INVALID );
	}
}
//-------------------------------------------------------------------------------------------------
/** doUnitStartSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitStartSequentialScript(const AsciiString& unitName, const AsciiString& scriptName, Int loopVal)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	Script *script = const_cast<Script*>(TheScriptEngine->findScriptByName(scriptName));
	if (!script) {
		return;
	}

	SequentialScript* seqScript = newInstance(SequentialScript);
	seqScript->m_objectID = obj->getID();
	seqScript->m_scriptToExecuteSequentially = script;
	seqScript->m_timesToLoop = loopVal;

	TheScriptEngine->appendSequentialScript(seqScript);

	deleteInstance(seqScript);
}

//-------------------------------------------------------------------------------------------------
/** doUnitStopSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitStopSequentialScript(const AsciiString& unitName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	TheScriptEngine->removeAllSequentialScripts(obj);
}

//-------------------------------------------------------------------------------------------------
/** doTeamStartSequentialScript */
//-------------------------------------------------------------------------------------------------
/** doNamedFireWeaponFollowingWaypointPath -- Kris
		Orders unit to fire a waypoint following capable weapon to follow a waypoint and attack the
		final waypoint position. */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFireWeaponFollowingWaypointPath( const AsciiString& unit, const AsciiString& waypointPath )
{
	//Get the unit... if it fails, abort.
	Object *theUnit = TheScriptEngine->getUnitNamed( unit );
	if( !theUnit )
	{
		return;
	}

	Coord3D pos = *theUnit->getPosition();


	//Find the closest waypoint on the path.
	Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &pos, waypointPath );
	if( !way )
	{
		return;
	}
	//We have to do special checking to make sure our unit even has a waypoint following capable weapon.
	Weapon *weapon = theUnit->findWaypointFollowingCapableWeapon();
	if( !weapon )
	{
		return;
	}

	Object *projectile = weapon->forceFireWeapon( theUnit, &pos );
	if( projectile )
	{
		//Get the AIUpdateInterface... if it fails, abort.
		AIUpdateInterface* aiUpdate = projectile->getAIUpdateInterface();
		if( !aiUpdate )
		{
			return;
		}
		DEBUG_ASSERTLOG(TheTerrainLogic->isPurposeOfPath(way, waypointPath), ("***Wrong waypoint purpose. Make jba fix this."));

		projectile->leaveGroup();
		aiUpdate->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		aiUpdate->aiFollowWaypointPath(way, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamStartSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStartSequentialScript(const AsciiString& teamName, const AsciiString& scriptName, Int loopVal)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	Script *script = const_cast<Script*>(TheScriptEngine->findScriptByName(scriptName));
	if (!script) {
		return;
	}

	// Idle the team so the seq script will start executing. jba.
	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif
	theGroup->groupIdle(CMD_FROM_SCRIPT);


	SequentialScript* seqScript = newInstance(SequentialScript);
	seqScript->m_teamToExecOn = team;
	seqScript->m_scriptToExecuteSequentially = script;
	seqScript->m_timesToLoop = loopVal;

	TheScriptEngine->appendSequentialScript(seqScript);

	deleteInstance(seqScript);
}

//-------------------------------------------------------------------------------------------------
/** doTeamStopSequentialScript */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamStopSequentialScript(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	TheScriptEngine->removeAllSequentialScripts(team);
}


//-------------------------------------------------------------------------------------------------
/** doUnitGuardForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitGuardForFramecount(const AsciiString& unitName, Int framecount)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	Coord3D pos = *obj->getPosition();
	ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(obj, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doUnitIdleForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitIdleForFramecount(const AsciiString& unitName, Int framecount)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) {
		return;
	}

	ai->aiIdle(CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(obj, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doTeamGuardForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardForFramecount(const AsciiString& teamName, Int framecount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	// Have all the members of the team guard at their current pos.
	for (DLINK_ITERATOR<Object> iter = theTeam->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		Coord3D pos = *obj->getPosition();
		ai->aiGuardPosition(&pos, GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
	TheScriptEngine->setSequentialTimer(theTeam, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doTeamIdleForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamIdleForFramecount(const AsciiString& teamName, Int framecount)
{
	Team *theTeam = TheScriptEngine->getTeamNamed(teamName);
	if (!theTeam) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if (!theGroup) {
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif
	Coord3D center;
	theGroup->getCenter(&center);

	theGroup->groupIdle(CMD_FROM_SCRIPT);
	TheScriptEngine->setSequentialTimer(theTeam, framecount);
}

//-------------------------------------------------------------------------------------------------
/** doWaterChangeHeight */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doWaterChangeHeight(const AsciiString& waterName, Real newHeight)
{
	const WaterHandle *water = TheTerrainLogic->getWaterHandleByName(waterName);
	if (!water) {
		return;
	}

	TheTerrainLogic->setWaterHeight(water, newHeight, 999999.9f, TRUE );
}

//-------------------------------------------------------------------------------------------------
/** doWaterChangeHeightOverTime */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doWaterChangeHeightOverTime( const AsciiString& waterName, Real newHeight, Real time, Real damage )
{
	const WaterHandle *water = TheTerrainLogic->getWaterHandleByName( waterName );
	if( water )
	{
		TheTerrainLogic->changeWaterHeightOverTime( water, newHeight, time, damage );
	}
}

//-------------------------------------------------------------------------------------------------
/** doBorderSwitch */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doBorderSwitch(Int borderToUse)
{
	/*
	 *	The observer player always has to have the map completely revealed for him.
	 *	Border switching doesn't play nice with the permanent map reveal so for the
	 *	observer player we need to undo the old permanent reveal, switch map borders,
	 *	and re-reveal the map. BGC
	*/
	Int observerPlayerIndex = -1;
	if (ThePlayerList != nullptr)
	{
		Player *observer = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"));

		if (observer != nullptr) {
			observerPlayerIndex = observer->getPlayerIndex();
		}
	}

	if (observerPlayerIndex != -1)
	{
		ThePartitionManager->undoRevealMapForPlayerPermanently( observerPlayerIndex );
	}

	TheTerrainLogic->setActiveBoundary(borderToUse);

	if (observerPlayerIndex != -1)
	{
		ThePartitionManager->revealMapForPlayerPermanently( observerPlayerIndex );
	}

	ThePartitionManager->refreshShroudForLocalPlayer();
}

//-------------------------------------------------------------------------------------------------
/** doForceObjectSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doForceObjectSelection(const AsciiString& teamName, const AsciiString& objectType, Bool centerInView, const AsciiString& audioToPlay)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);

	if (!team) {
		return;
	}

	Object *bestGuess = nullptr;

	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (!obj) {
			continue;
		}

		if (obj->getTemplate() && obj->getTemplate()->getName() == objectType) {
			if (bestGuess == nullptr || obj->getID() < bestGuess->getID()) { // lower ID means its newer
				bestGuess = obj;
			}
		}
	}

	if (!(bestGuess && bestGuess->getDrawable())) {
		return;
	}

	TheInGameUI->deselectAllDrawables();
	TheInGameUI->selectDrawable(bestGuess->getDrawable());
	// play the sound
	AudioEventRTS audioEvent(audioToPlay);
	audioEvent.setPlayerIndex(ThePlayerList->getLocalPlayer()->getPlayerIndex());
	TheAudio->addAudioEvent(&audioEvent);

	if (centerInView) {
		Coord3D pos = *bestGuess->getPosition();
		TheTacticalView->moveCameraTo(&pos, 0, 0, FALSE, 0.0f, 0.0f);
	}
}

void* __cdecl killTheObject( Object *obj, void* userObj )
{
	userObj;
	if (obj)
		obj->kill();
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doForceObjectSelection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doDestroyAllContained(const AsciiString& unitName, Int damageType )
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	ContainModuleInterface *cmi = obj->getContain();
	if( !cmi || cmi->getContainCount() == 0 )
	{
		return;
	}

	cmi->iterateContained((ContainIterateFunc)killTheObject, nullptr, false);
}

//-------------------------------------------------------------------------------------------------
/** doRadarForceEnable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarForceEnable()
{
	TheRadar->forceOn(ThePlayerList->getLocalPlayer()->getPlayerIndex(), true);
}

//-------------------------------------------------------------------------------------------------
/** doRadarRevertNormal */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doRadarRevertNormal()
{
	TheRadar->forceOn(ThePlayerList->getLocalPlayer()->getPlayerIndex(), false);
}

//-------------------------------------------------------------------------------------------------
/** doScreenShake */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doScreenShake( UnsignedInt intensity )
{
	Coord3D pos = TheTacticalView->getPosition();
	TheTacticalView->shake( &pos, (View::CameraShakeType)intensity );
}

//-------------------------------------------------------------------------------------------------
/** doModifyBuildableStatus */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doModifyBuildableStatus( const AsciiString& objectType, Int buildableStatus )
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ)
	{
		return;
	}

	TheGameLogic->setBuildableStatusOverride(templ, (BuildableStatus) buildableStatus);
}

//-------------------------------------------------------------------------------------------------
static CaveInterface* findCave(Object* obj)
{
	for (BehaviorModule** i = obj->getBehaviorModules(); *i; ++i)
	{
		CaveInterface* c = (*i)->getCaveInterface();
		if (c != nullptr)
			return c;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** doSetCaveIndex */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetCaveIndex( const AsciiString& caveName, Int caveIndex )
{
	Object *obj = TheScriptEngine->getUnitNamed(caveName);
	if (!obj)
	{
		return;
	}

	CaveInterface *caveModule = findCave(obj);
	if( caveModule == nullptr )
		return;

	caveModule->tryToSetCaveIndex( caveIndex );
}

//-------------------------------------------------------------------------------------------------
/** doSetWarehouseValue */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetWarehouseValue( const AsciiString& warehouseName, Int cashValue )
{
	Object *obj = TheScriptEngine->getUnitNamed(warehouseName);
	if (!obj)
	{
		return;
	}

	static const NameKeyType warehouseModuleKey = TheNameKeyGenerator->nameToKey( "SupplyWarehouseDockUpdate" );
	SupplyWarehouseDockUpdate *warehouseModule = (SupplyWarehouseDockUpdate *)obj->findUpdateModule( warehouseModuleKey );
	if( warehouseModule == nullptr )
		return;

	warehouseModule->setCashValue( cashValue );
}

//-------------------------------------------------------------------------------------------------
/** doSoundEnableType */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundEnableType( const AsciiString& soundEventName, Bool enable )
{
	TheAudio->setAudioEventEnabled(soundEventName, enable);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundRemoveAllDisabled()
{
	TheAudio->removeDisabledEvents();
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundRemoveType( const AsciiString& soundEventName )
{
	TheAudio->removeAudioEvent( soundEventName );
}

//-------------------------------------------------------------------------------------------------
/** doSoundOverrideVolume */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSoundOverrideVolume( const AsciiString& soundEventName, Real newVolume )
{
	TheAudio->setAudioEventVolumeOverride(soundEventName, newVolume / 100.0f);
}

//-------------------------------------------------------------------------------------------------
/** doSetToppleDirection */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetToppleDirection( const AsciiString& unitName, const Coord3D *dir )
{
	TheScriptEngine->setToppleDirection(unitName, dir);
}

//-------------------------------------------------------------------------------------------------
/** doMoveTeamTowardsNearest */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveUnitTowardsNearest( const AsciiString& unitName, const AsciiString& objectType, AsciiString triggerName)
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj)
	{
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}

	PolygonTrigger *trig = TheScriptEngine->getQualifiedTriggerAreaByName(triggerName);
	if (!trig)
	{
		return;
	}

	Object *bestObj = nullptr;

	const ThingTemplate *templ = TheThingFactory->findTemplate( objectType, FALSE );
	if( templ )
	{
		PartitionFilterThing thingsToAccept(templ, true);
		PartitionFilterPolygonTrigger acceptWithin(trig);
		PartitionFilterSameMapStatus filterMapStatus(obj);

		PartitionFilter *filters[] = { &thingsToAccept, &acceptWithin, &filterMapStatus, nullptr };

		bestObj = ThePartitionManager->getClosestObject( obj->getPosition(), REALLY_FAR, FROM_CENTER_2D, filters );
		if( !bestObj )
		{
			return;
		}
	}
	else
	{
		ObjectTypes *objectTypes = TheScriptEngine->getObjectTypes( objectType );
		if( objectTypes )
		{
			PartitionFilterPolygonTrigger acceptWithin( trig );
			PartitionFilterSameMapStatus filterMapStatus( obj );

			Coord3D pos = *obj->getPosition();
			Real closestDist;
			Real dist;

			for( size_t typeIndex = 0; typeIndex < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing f2( thisType, true );
					PartitionFilter *filters[] = { &f2, &acceptWithin, &filterMapStatus, nullptr };

					Object *obj = ThePartitionManager->getClosestObject( &pos, REALLY_FAR, FROM_CENTER_2D, filters, &dist );
					if( obj )
					{
						if( !bestObj || dist < closestDist )
						{
							bestObj = obj;
							closestDist = dist;
						}
					}
				}
			}
		}
	}

	ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
	ai->aiMoveToObject( bestObj, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doMoveTeamTowardsNearest */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doMoveTeamTowardsNearest( const AsciiString& teamName, const AsciiString& objectType, AsciiString triggerName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team)
	{
		return;
	}

	PolygonTrigger *trig = TheScriptEngine->getQualifiedTriggerAreaByName(triggerName);
	if (!trig)
	{
		return;
	}

	//Get the first object (to use in the partition filter checks).
	Object *teamObj = nullptr;
	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
	for (; !iter.done(); iter.advance())
	{
		teamObj = iter.cur();
		if( teamObj )
		{
			AIUpdateInterface *ai = teamObj->getAIUpdateInterface();
			if( ai )
			{
				break;
			}
		}
	}
	if( !teamObj )
	{
		return;
	}

	Coord3D teamPos = *team->getEstimateTeamPosition();
	PartitionFilterSameMapStatus filterMapStatus( teamObj );
	PartitionFilterPolygonTrigger acceptWithin( trig );
	Object *bestObj = nullptr;

	const ThingTemplate *templ = TheThingFactory->findTemplate( objectType, FALSE );
	if( templ )
	{
		//Find the closest specified template.
		PartitionFilterThing thingsToAccept( templ, true );
		PartitionFilter *filters[] = { &thingsToAccept, &acceptWithin, &filterMapStatus, nullptr };
		bestObj = ThePartitionManager->getClosestObject( &teamPos, REALLY_FAR, FROM_CENTER_2D, filters );
		if (!bestObj)
		{
			return;
		}
	}
	else
	{
		//Find the closest object within the object template list.
		ObjectTypes *objectTypes = TheScriptEngine->getObjectTypes( objectType );
		if( objectTypes )
		{
			Real closestDist;
			Real dist;
			for( size_t typeIndex = 0; typeIndex < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing thingToAccept( thisType, true );
					PartitionFilter *filters[] = { &thingToAccept, &acceptWithin, &filterMapStatus, nullptr };

					Object *obj = ThePartitionManager->getClosestObject( &teamPos, REALLY_FAR, FROM_CENTER_2D, filters, &dist );
					if( obj )
					{
						if( !bestObj || dist < closestDist )
						{
							bestObj = obj;
							closestDist = dist;
						}
					}
				}
			}
		}
	}
	for( iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
	{
		Object *obj = iter.cur();
		if( !obj )
		{
			return;
		}
		AIUpdateInterface *ai = obj->getAI();
		if( !ai )
		{
			return;
		}
		ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		ai->aiMoveToObject( bestObj, CMD_FROM_SCRIPT );
	}
}

//-------------------------------------------------------------------------------------------------
/** doUnitReceiveUpgrade */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doUnitReceiveUpgrade( const AsciiString& unitName, const AsciiString& upgradeName )
{
	Object *obj = TheScriptEngine->getUnitNamed(unitName);
	if (!obj) {
		return;
	}

	const UpgradeTemplate *templ = TheUpgradeCenter->findUpgrade(upgradeName);
	if (!templ) {
		return;
	}

	DEBUG_ASSERTCRASH(obj->affectedByUpgrade(templ), ("Design bug: Unit '%s' was given upgrade '%s', but he is unaffected.", unitName.str(), upgradeName.str()));

	obj->giveUpgrade(templ);
}

//-------------------------------------------------------------------------------------------------
/** doSkirmishAttackNearestGroupWithValue */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishAttackNearestGroupWithValue( const AsciiString& teamName, Int comparison, Int value )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	Player *player = team->getControllingPlayer();

	if (!player)
		return;

	Coord3D loc;
	Coord3D groupLoc;
	theGroup->getCenter(&groupLoc);
	if (comparison == Parameter::GREATER_EQUAL || comparison == Parameter::GREATER) {
		ThePartitionManager->getNearestGroupWithValue(player->getPlayerIndex(), ALLOW_ENEMIES, VOT_CashValue,
			&groupLoc, value, true, &loc);
	}

	theGroup->groupAttackMoveToPosition( &loc, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/** doSkirmishCommandButtonOnMostValuable */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doSkirmishCommandButtonOnMostValuable( const AsciiString& teamName, const AsciiString& ability, Real range, Bool allTeamMembers)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	Player *player = team->getControllingPlayer();
	if (!player)
		return;

	const CommandButton *commandButton = TheControlBar->findCommandButton( ability );
	if( !commandButton ) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if ( !srcObj ) {
		return;
	}

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterValidCommandButtonTarget f2(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, nullptr };
	// @todo: Should we add the group's radius to the range? Seems like a possibility.
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&pos, range, FROM_CENTER_2D, filters, ITER_SORTED_EXPENSIVE_TO_CHEAP);
	MemoryPoolObjectHolder hold(iter);

	if (iter && iter->first()) {
		theGroup->groupDoCommandButtonAtObject(commandButton, iter->first(), CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamSpinForFramecount */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamSpinForFramecount( const AsciiString& teamName, Int waitForFrames )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	TheScriptEngine->setSequentialTimer(team, waitForFrames);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNamed */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNamed( const AsciiString& teamName, const AsciiString& commandAbility, const AsciiString& unitName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if (!obj) {
		return;
	}

	if (commandButton->isValidToUseOn(srcObj, obj, nullptr, CMD_FROM_SCRIPT)) {
		theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestEnemy */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestEnemy( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterValidCommandButtonTarget f2(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestGarrisonedBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestGarrisonedBuilding( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if(!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterGarrisonable f3(true);
	PartitionFilterValidCommandButtonTarget f4(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestKindof */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestKindof( const AsciiString& teamName, const AsciiString& commandAbility, Int kindofBit )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(kindofBit), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestBuilding */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestBuilding( const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestBuildingClass */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestBuildingClass( const AsciiString& teamName, const AsciiString& commandAbility, Int kindofBit )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES, true);
	PartitionFilterAcceptByKindOf f2(MAKE_KINDOF_MASK(KINDOF_STRUCTURE), KINDOFMASK_NONE);
	PartitionFilterAcceptByKindOf f3(MAKE_KINDOF_MASK(kindofBit), KINDOFMASK_NONE);
	PartitionFilterValidCommandButtonTarget f4(srcObj, commandButton, true, CMD_FROM_SCRIPT);
	PartitionFilterSameMapStatus filterMapStatus(srcObj);

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamUseCommandButtonOnNearestObjectType */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamUseCommandButtonOnNearestObjectType( const AsciiString& teamName, const AsciiString& commandAbility, const AsciiString& objectType )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	Object *srcObj = nullptr;
	if (commandButton->getSpecialPowerTemplate()) {
		srcObj = theGroup->getSpecialPowerSourceObject(commandButton->getSpecialPowerTemplate()->getID());
	} else {
		srcObj = theGroup->getCommandButtonSourceObject(commandButton->getCommandType());
	}

	if (!srcObj) {
		return;
	}

	Object *bestObj = nullptr;

	//First look for a specific object type (object lists will fail)
	const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( objectType, FALSE );
	if( thingTemplate )
	{
		PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
		PartitionFilterThing f2(thingTemplate, true);
		PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
		PartitionFilterSameMapStatus filterMapStatus(srcObj);
		PartitionFilter *filters[] = { &f1, &f2, &f3, &filterMapStatus, nullptr };

		Coord3D pos;
		theGroup->getCenter(&pos);

		bestObj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
#if RETAIL_COMPATIBLE_CRC
		if (!bestObj)
			return;
#endif
	}
	else
	{
		ObjectTypes *objectTypes = TheScriptEngine->getObjectTypes( objectType );
		if( objectTypes )
		{
			PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
			PartitionFilterValidCommandButtonTarget f3(srcObj, commandButton, true, CMD_FROM_SCRIPT);
			PartitionFilterSameMapStatus f4(srcObj);

			Coord3D pos;
			theGroup->getCenter(&pos);
			Real closestDist;
			Real dist;

			for( size_t typeIndex = 0; typeIndex < objectTypes->getListSize(); typeIndex++ )
			{
				AsciiString thisTypeName = objectTypes->getNthInList( typeIndex );
				const ThingTemplate *thisType = TheThingFactory->findTemplate( thisTypeName );
				if( thisType )
				{
					PartitionFilterThing f2( thisType, true );
					PartitionFilter *filters[] = { &f1, &f2, &f3, &f4, nullptr };

					Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters, &dist );
					if( obj )
					{
						if( !bestObj || dist < closestDist )
						{
							bestObj = obj;
							closestDist = dist;
						}
					}
				}
			}
		}
	}

#if !RETAIL_COMPATIBLE_CRC
	if (!bestObj)
		return;
#endif

	// D-244 Phase 2: passive dev-harness evidence of the actual valid script target.
	// The order below remains stock; a target/order is not proof of a launched power.
	if( TheHypersonicScenario != nullptr )
		TheHypersonicScenario->noteChallengePowerTarget( teamName, commandAbility, srcObj, bestObj );
	// already been checked for validity
	theGroup->groupDoCommandButtonAtObject(commandButton, bestObj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doTeamPartialUseCommandButton */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamPartialUseCommandButton( Real percentage, const AsciiString& teamName, const AsciiString& commandAbility )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton(commandAbility);
	if (!commandButton) {
		return;
	}

	std::vector<Object *> objList;
	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();

	for (iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		if (commandButton->isValidToUseOn(obj, nullptr, nullptr, CMD_FROM_SCRIPT)) {
			objList.push_back(obj);
		}
	}

	Int numObjs = /*REAL_TO_INT_CEIL*/(percentage / 100.0f * objList.size());
	Int count = 0;
	for (std::vector<Object*>::const_iterator it = objList.begin(); it != objList.end(); ++it)
	{
		Object *obj = (*it);

		if (count >= numObjs)
			return;

		obj->doCommandButton(commandButton, CMD_FROM_SCRIPT);
		++count;
	}
}

//-------------------------------------------------------------------------------------------------
/** doTeamCaptureNearestUnownedFactionUnit */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamCaptureNearestUnownedFactionUnit( const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
	team->getTeamAsAIGroup(theGroup);
#else
	team->getTeamAsAIGroup(theGroup.Peek());
#endif

	PartitionFilterPlayerAffiliation f1(team->getControllingPlayer(), ALLOW_ENEMIES | ALLOW_NEUTRAL, true);
	PartitionFilterUnmannedObject f2(true);
	PartitionFilterOnMap filterMapStatus;

	Coord3D pos;
	theGroup->getCenter(&pos);

	PartitionFilter *filters[] = { &f1, &f2, &filterMapStatus, nullptr };
	Object *obj = ThePartitionManager->getClosestObject(&pos, REALLY_FAR, FROM_CENTER_2D, filters);
	if (!obj) {
		return;
	}

	theGroup->groupEnter(obj, CMD_FROM_SCRIPT);
}

//-------------------------------------------------------------------------------------------------
/** doCreateTeamFromCapturedUnits */
//-------------------------------------------------------------------------------------------------
void ScriptActions::doCreateTeamFromCapturedUnits( const AsciiString& playerName, const AsciiString& teamName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerAddSkillPoints(const AsciiString& playerName, Int delta)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->addSkillPoints(delta);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerAddRankLevels(const AsciiString& playerName, Int delta)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->setRankLevel(pPlayer->getRankLevel() + delta);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetRankLevel(const AsciiString& playerName, Int level)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	pPlayer->setRankLevel(level);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doMapSetRankLevelLimit(Int level)
{
	if (TheGameLogic)
		TheGameLogic->setRankLevelLimit(level);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerGrantScience(const AsciiString& playerName, const AsciiString& scienceName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	ScienceType science = TheScienceStore->getScienceFromInternalName(scienceName);
	if (science == SCIENCE_INVALID)
		return;
	pPlayer->grantScience(science);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerPurchaseScience(const AsciiString& playerName, const AsciiString& scienceName)
{
	Player* pPlayer = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!pPlayer)
		return;
	ScienceType science = TheScienceStore->getScienceFromInternalName(scienceName);
	if (science == SCIENCE_INVALID)
		return;
	pPlayer->attemptToPurchaseScience(science);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doPlayerSetScienceAvailability( const AsciiString& playerName, const AsciiString& scienceName, const AsciiString& scienceAvailability )
{
	Player* player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if( player )
	{
		ScienceAvailabilityType saType = player->getScienceAvailabilityTypeFromString( scienceAvailability );
		if( saType != SCIENCE_AVAILABILITY_INVALID )
		{
			ScienceType science = TheScienceStore->getScienceFromInternalName( scienceName );
			if( science != SCIENCE_INVALID )
			{
				player->setScienceAvailability( science, saType );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamEmoticon(const AsciiString& teamName, const AsciiString& emoticonName, Real duration)
{
	Team *theTeam = TheScriptEngine->getTeamNamed( teamName );
	if( !theTeam )
	{
		return;
	}

	AIGroupPtr theGroup = TheAI->createGroup();
	if( !theGroup )
	{
		return;
	}
#if RETAIL_COMPATIBLE_AIGROUP
	theTeam->getTeamAsAIGroup(theGroup);
#else
	theTeam->getTeamAsAIGroup(theGroup.Peek());
#endif

	Int frames = (Int)( duration * LOGICFRAMES_PER_SECOND );
	theGroup->groupSetEmoticon( emoticonName, frames );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedEmoticon(const AsciiString& unitName, const AsciiString& emoticonName, Real duration)
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
		Drawable *draw = obj->getDrawable();
		if( draw )
		{
			Int frames = (Int)( duration * LOGICFRAMES_PER_SECOND );
			draw->setEmoticon( emoticonName, frames );
		}
	}
}

//-------------------------------------------------------------------------------------------------
// if addObject, we're adding an object to a list. If not addObject, we're removing the associated
// object.
//-------------------------------------------------------------------------------------------------
void ScriptActions::doObjectTypeListMaintenance(const AsciiString& objectList, const AsciiString& objectType, Bool addObject)
{
	TheScriptEngine->doObjectTypeListMaintenance( objectList, objectType, addObject );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doRevealMapAtWaypointPermanent(const AsciiString& waypointName, Real radiusToReveal, const AsciiString& playerName, const AsciiString& lookName)
{
	TheScriptEngine->createNamedMapReveal(lookName, waypointName, radiusToReveal, playerName);
	TheScriptEngine->doNamedMapReveal(lookName);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doUndoRevealMapAtWaypointPermanent(const AsciiString& lookName)
{
	TheScriptEngine->undoNamedMapReveal(lookName);
	TheScriptEngine->removeNamedMapReveal(lookName);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doEvaEnabledDisabled(Bool setEnabled)
{
	TheEva->setEvaEnabled(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetOcclusionMode(Bool setEnabled)
{
	TheGameLogic->setShowBehindBuildingMarkers(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetDrawIconUIMode(Bool setEnabled)
{
	TheGameLogic->setDrawIconUI(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doSetDynamicLODMode(Bool setEnabled)
{
	TheGameLogic->setShowDynamicLOD(setEnabled);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doOverrideHulkLifetime( Real seconds )
{
	if( seconds < 0.0f )
	{
		// Turn it off.
		TheGameLogic->setHulkMaxLifetimeOverride(-1);
	}
	else
	{
		// Convert real seconds into frames.
		Int frames = (Int)(seconds * LOGICFRAMES_PER_SECOND);
		TheGameLogic->setHulkMaxLifetimeOverride(frames);
	}
}

//-------------------------------------------------------------------------------------------------
// MBL CNC3 INCURSION - This is to Have Max camera animation playback
//
void ScriptActions::doC3CameraEnableSlaveMode
(
	const AsciiString& thingTemplateName,
	const AsciiString& boneName
)
{
	TheTacticalView->cameraEnableSlaveMode( thingTemplateName, boneName );
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doC3CameraDisableSlaveMode()
{
	TheTacticalView->cameraDisableSlaveMode();
}

//-------------------------------------------------------------------------------------------------
// WST 11.12.2002 CNC3 INCURSION - This is for New Camera Shake
//
void ScriptActions::doC3CameraShake
(
	const AsciiString &waypointName,
	const Real amplitude,
	const Real duration_seconds,
	const Real radius
)
{
	Waypoint *way = TheTerrainLogic->getWaypointByName(waypointName);
	DEBUG_ASSERTLOG( (way != nullptr), ("Camera shake with No Valid Waypoint") );
	Coord3D pos = *way->getLocation();

	TheTacticalView->Add_Camera_Shake(pos, radius, duration_seconds, amplitude);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFaceNamed( const AsciiString &unitName, const AsciiString &faceUnitName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
		Object *faceObj = TheScriptEngine->getUnitNamed( faceUnitName );
		if( faceObj )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->clearWaypointQueue();
				obj->leaveGroup();
				ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
				ai->aiFaceObject( faceObj, CMD_FROM_SCRIPT );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedFaceWaypoint( const AsciiString &unitName, const AsciiString &faceWaypointName )
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if( obj )
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName( faceWaypointName );
		if( way )
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				ai->clearWaypointQueue();
				obj->leaveGroup();
				ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
				ai->aiFacePosition( way->getLocation(), CMD_FROM_SCRIPT );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFaceNamed( const AsciiString &teamName, const AsciiString &faceUnitName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if( team )
	{
		Object *faceObj = TheScriptEngine->getUnitNamed( faceUnitName );
		if( faceObj )
		{
			DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
			for( iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
			{
				Object *obj = iter.cur();
				if( obj )
				{
					AIUpdateInterface *ai = obj->getAI();
					if( ai )
					{
						ai->clearWaypointQueue();
						obj->leaveGroup();
						ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
						ai->aiFaceObject( faceObj, CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamFaceWaypoint( const AsciiString &teamName, const AsciiString &faceWaypointName )
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if( team )
	{
		Waypoint *way = TheTerrainLogic->getWaypointByName( faceWaypointName );
		if( way )
		{
			DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
			for( iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance() )
			{
				Object *obj = iter.cur();
				if( obj )
				{
					AIUpdateInterface *ai = obj->getAI();
					if( ai )
					{
						ai->clearWaypointQueue();
						obj->leaveGroup();
						ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
						ai->aiFacePosition( way->getLocation(), CMD_FROM_SCRIPT );
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectObjectPanelFlagsUnit(const AsciiString& unitName, const AsciiString& flagName, Bool enable)
{
	Object *obj = TheScriptEngine->getUnitNamed( unitName );
	if (!obj) {
		return;
	}

	changeObjectPanelFlagForSingleObject(obj, flagName, enable);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectObjectPanelFlagsTeam(const AsciiString& teamName, const AsciiString& flagName, Bool enable)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}

	DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList();
	for (iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance()) {
		Object *obj = iter.cur();
		changeObjectPanelFlagForSingleObject(obj, flagName, enable);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectPlayerSkillset(const AsciiString& playerName, Int skillset)
{
	Player *player = TheScriptEngine->getPlayerFromAsciiString(playerName);
	if (!player) {
		return;
	}

	--skillset;
	player->friend_setSkillset(skillset);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doGuardSupplyCenter(const AsciiString& teamName, Int supplies)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}
	Player *player = team->getControllingPlayer();
	if (!player) {
		return;
	}

	player->guardSupplyCenter(team, supplies);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doTeamGuardInTunnelNetwork(const AsciiString& teamName)
{
	Team *team = TheScriptEngine->getTeamNamed(teamName);
	if (!team) {
		return;
	}
	// Have all the members of the team guard at a tunnel network.
	for (DLINK_ITERATOR<Object> iter = team->iterate_TeamMemberList(); !iter.done(); iter.advance())
	{
		Object *obj = iter.cur();
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}
		ai->aiGuardTunnelNetwork(GUARDMODE_NORMAL, CMD_FROM_SCRIPT);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doRemoveCommandBarButton(const AsciiString& buttonName, const AsciiString& objectType)
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ) {
		return;
	}
	const CommandSet *cs = TheControlBar->findCommandSet(templ->friend_getCommandSetString());
	if (!cs) {
		return;
	}

	Int slotNum = -1;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
	{
		if (cs->getCommandButton(i) && (cs->getCommandButton(i)->getName() == buttonName))
		{
			slotNum = i;
			break;
		}
	}

	if (slotNum >= 0)
	{
		TheGameLogic->setControlBarOverride(templ->friend_getCommandSetString(), slotNum, nullptr);
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAddCommandBarButton(const AsciiString& buttonName, const AsciiString& objectType, Int slotNum)
{
	const ThingTemplate *templ = TheThingFactory->findTemplate(objectType);
	if (!templ) {
		return;
	}

	const CommandButton *commandButton = TheControlBar->findCommandButton( buttonName );
	if (commandButton == nullptr)
	{
		// not here. use doRemoveCommandBarButton to remove one.
		return;
	}

	// decrement it, so we can present it as 1-12 for the designers.
	--slotNum;

	if (slotNum < 0 || slotNum >= MAX_COMMANDS_PER_SET)
		return;

	TheGameLogic->setControlBarOverride(templ->friend_getCommandSetString(), slotNum, commandButton);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doAffectSkillPointsModifier(const AsciiString& playerName, Real newModifier)
{
	Player *playerDst = TheScriptEngine->getPlayerFromAsciiString(playerName);

	if (!playerDst) {
		return;
	}

	playerDst->setSkillPointsModifier(newModifier);

}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doResizeViewGuardband(const Real gbx, const Real gby)
{

	Coord2D newGuardBand = { gbx, gby };

	TheTacticalView->setGuardBandBias( &newGuardBand );

}

//-------------------------------------------------------------------------------------------------
void ScriptActions::deleteAllUnmanned()
{
	Object *obj = TheGameLogic->getFirstObject();
	while (obj) {
		if (obj->isDisabledByType(DISABLED_UNMANNED))
			TheGameLogic->destroyObject(obj);
		obj = obj->getNextObject();
	}
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doEnableOrDisableObjectDifficultyBonuses(Bool enableBonuses)
{
	// Loops over every object in the game, applying bonuses or not.
	Object *obj = TheGameLogic->getFirstObject();
	while (obj) {
		obj->setReceivingDifficultyBonus(enableBonuses);
		obj = obj->getNextObject();
	}

	// notify the script engine of our decision
	TheScriptEngine->setObjectsShouldReceiveDifficultyBonus(enableBonuses);
}

//-------------------------------------------------------------------------------------------------
void ScriptActions::doChooseVictimAlwaysUsesNormal(Bool enable)
{
	TheScriptEngine->setChooseVictimAlwaysUsesNormal(enable);
}



//-------------------------------------------------------------------------------------------------
void ScriptActions::doNamedSetTrainHeld( const AsciiString &locoName, const Bool set )
{
	Object *obj = TheScriptEngine->getUnitNamed( locoName );
	if( obj )
	{

		static const NameKeyType rrkey = NAMEKEY( "RailroadBehavior" );
		RailroadBehavior *rBehavior = (RailroadBehavior*)obj->findUpdateModule( rrkey );

    if ( rBehavior )
    {
      rBehavior->RailroadBehavior::setHeld( set );
    }

	}
}


//-------------------------------------------------------------------------------------------------
/** Execute an action */
//-------------------------------------------------------------------------------------------------
void ScriptActions::executeAction( ScriptAction *pAction )
{
	switch (pAction->getActionType()) {
		default:
			DEBUG_CRASH(("Unknown ScriptAction type %d", pAction->getActionType())); return;
		case ScriptAction::DEBUG_MESSAGE_BOX:
			doDebugMessage(pAction->getParameter(0)->getString(), true);
			return;
		case ScriptAction::DEBUG_STRING:
			doDebugMessage(pAction->getParameter(0)->getString(), false);
			return;
		case ScriptAction::DAMAGE_MEMBERS_OF_TEAM:
			doDamageTeamMembers(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::MOVE_TEAM_TO:
			doMoveToWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::MOVE_NAMED_UNIT_TO:
			doNamedMoveToWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_SET_STATE:
			doSetTeamState(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_FOLLOW_WAYPOINTS:
			doTeamFollowWaypoints(pAction->getParameter(0)->getString(),
				pAction->getParameter(1)->getString(),
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_FOLLOW_WAYPOINTS_EXACT:
			doTeamFollowWaypointsExact(pAction->getParameter(0)->getString(),
				pAction->getParameter(1)->getString(),
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::NAMED_FOLLOW_WAYPOINTS_EXACT:
			doNamedFollowWaypointsExact(pAction->getParameter(0)->getString(),
				pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_FOLLOW_APPROACH_PATH:
			doTeamFollowSkirmishApproachPath(pAction->getParameter(0)->getString(),
				pAction->getParameter(1)->getString(),
				pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::SKIRMISH_MOVE_TO_APPROACH_PATH:
			doTeamMoveToSkirmishApproachPath(pAction->getParameter(0)->getString(),
				pAction->getParameter(1)->getString());
			return;
		case ScriptAction::CREATE_REINFORCEMENT_TEAM:
			doCreateReinforcements(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_BUILD_BUILDING:
			doBuildBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::AI_PLAYER_BUILD_SUPPLY_CENTER:
			doBuildSupplyCenter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::AI_PLAYER_BUILD_TYPE_NEAREST_TEAM:
			doBuildObjectNearestTeam( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;
		case ScriptAction::AI_PLAYER_BUILD_UPGRADE:
			doBuildUpgrade(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FRONT:
			doBuildBaseDefense(false);
			return;
		case ScriptAction::SKIRMISH_BUILD_BASE_DEFENSE_FLANK:
			doBuildBaseDefense(true);
			return;
		case ScriptAction::SKIRMISH_BUILD_STRUCTURE_FRONT:
			doBuildBaseStructure(pAction->getParameter(0)->getString(), false);
			return;
		case ScriptAction::SKIRMISH_BUILD_STRUCTURE_FLANK:
			doBuildBaseStructure(pAction->getParameter(0)->getString(), true);
			return;
		case ScriptAction::RECRUIT_TEAM:
			doRecruitTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::PLAY_SOUND_EFFECT:
			doPlaySoundEffect(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVE_CAMERA_TO:
			doMoveCameraTo(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::SETUP_CAMERA:
			doSetupCamera(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::ZOOM_CAMERA:
			doZoomCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::PITCH_CAMERA:
			doPitchCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::CAMERA_FOLLOW_NAMED:
			doCameraFollowNamed(pAction->getParameter(0)->getString(), (pAction->getParameter(1) && pAction->getParameter(1)->getInt() != 0));
			return;
		case ScriptAction::CAMERA_STOP_FOLLOW:
			doStopCameraFollowUnit();
			return;
		case ScriptAction::OVERSIZE_TERRAIN:
			doOversizeTheTerrain(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_MOD_LOOK_TOWARD:
			doModCameraLookToward(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_MOD_FINAL_LOOK_TOWARD:
			doModCameraFinalLookToward(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVE_CAMERA_ALONG_WAYPOINT_PATH:
			doMoveCameraAlongWaypointPath(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::ROTATE_CAMERA:
			doRotateCamera(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::CAMERA_LOOK_TOWARD_OBJECT:
			doRotateCameraTowardObject(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getReal());
			return;
		case ScriptAction::CAMERA_LOOK_TOWARD_WAYPOINT:
			doRotateCameraTowardWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal(), pAction->getParameter(4)->getInt());
			return;
		case ScriptAction::RESET_CAMERA:
			doResetCamera(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal());
			return;
		case ScriptAction::MOVE_CAMERA_TO_SELECTION:
			doModCameraMoveToSelection();
			return;
		case ScriptAction::CAMERA_MOD_FREEZE_TIME:
			TheTacticalView->cameraModFreezeTime();
			return;
		case ScriptAction::CAMERA_MOD_FREEZE_ANGLE:
			TheTacticalView->cameraModFreezeAngle();
			return;
		case ScriptAction::CAMERA_MOD_SET_FINAL_ZOOM:
			TheTacticalView->cameraModFinalZoom(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal());
			return;
		case ScriptAction::CAMERA_MOD_SET_FINAL_PITCH:
			TheTacticalView->cameraModFinalPitch(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal());
			return;
		case ScriptAction::CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER:
			TheTacticalView->cameraModFinalTimeMultiplier(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_MOD_SET_ROLLING_AVERAGE:
			TheTacticalView->cameraModRollingAverage(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::SET_VISUAL_SPEED_MULTIPLIER:
			TheTacticalView->setTimeMultiplier(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::SUSPEND_BACKGROUND_SOUNDS:
			TheAudio->pauseAudio(AudioAffect_Sound);
			return;
		case ScriptAction::RESUME_BACKGROUND_SOUNDS:
			TheAudio->resumeAudio(AudioAffect_Sound);
			return;
		case ScriptAction::PLAY_SOUND_EFFECT_AT:
			doPlaySoundEffectAt(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SET_INFANTRY_LIGHTING_OVERRIDE:
			doSetInfantryLightingOverride(pAction->getParameter(0)->getReal());
			return;
		case ScriptAction::RESET_INFANTRY_LIGHTING_OVERRIDE:
			doSetInfantryLightingOverride(-1.0f);
			return;
		case ScriptAction::QUICKVICTORY:
			doQuickVictory();
			return;
		case ScriptAction::VICTORY:
			doVictory();
			return;
		case ScriptAction::DEFEAT:
			doDefeat();
			return;
		case ScriptAction::LOCALDEFEAT:
			doLocalDefeat();
			return;
		case ScriptAction::CREATE_OBJECT:
		{
			Coord3D pos;
			pAction->getParameter(2)->getCoord3D(&pos);
			doCreateObject( m_unnamedUnit, pAction->getParameter(0)->getString(),  pAction->getParameter(1)->getString(), &pos, pAction->getParameter(3)->getReal() );
			return;
		}

		case ScriptAction::TEAM_ATTACK_TEAM:
			doAttack( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::NAMED_ATTACK_NAMED:
			doNamedAttack( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::CREATE_NAMED_ON_TEAM_AT_WAYPOINT:
			createUnitOnTeamAt( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT:
			createUnitOnTeamAt( AsciiString::TheEmptyString, pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case ScriptAction::NAMED_APPLY_ATTACK_PRIORITY_SET:
			updateNamedAttackPrioritySet(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_APPLY_ATTACK_PRIORITY_SET:
			updateTeamAttackPrioritySet(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SET_BASE_CONSTRUCTION_SPEED:
			updateBaseConstructionSpeed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_ATTITUDE:
			updateNamedSetAttitude(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_ATTITUDE:
			updateTeamSetAttitude(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_REPULSOR:
			doNamedSetRepulsor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_REPULSOR:
			doTeamSetRepulsor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_ATTACK_AREA:
			doNamedAttackArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_ATTACK_TEAM:
			doNamedAttackTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ATTACK_AREA:
			doTeamAttackArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ATTACK_NAMED:
			doTeamAttackNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_LOAD_TRANSPORTS:
			doLoadAllTransports(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_ENTER_NAMED:
			doNamedEnterNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ENTER_NAMED:
			doTeamEnterNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_EXIT_ALL:
			doNamedExitAll(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_EXIT_ALL:
			doTeamExitAll(pAction->getParameter(0)->getString());
			return;
    case ScriptAction::NAMED_SET_EVAC_LEFT_OR_RIGHT:
      doNamedSetGarrisonEvacDisposition(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
      return;
		case ScriptAction::NAMED_FOLLOW_WAYPOINTS:
			doNamedFollowWaypoints(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_GUARD:
			doNamedGuard(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GUARD:
			doTeamGuard(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GUARD_POSITION:
			doTeamGuardPosition(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_GUARD_OBJECT:
			doTeamGuardObject(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_GUARD_AREA:
			doTeamGuardArea(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_HUNT:
			doNamedHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_HUNT:
			doTeamHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_HUNT_WITH_COMMAND_BUTTON:
			doTeamHuntWithCommandButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_HUNT:
			doPlayerHunt(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_SELL_EVERYTHING:
			doPlayerSellEverything(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_BASE_CONSTRUCTION:
			doPlayerDisableBaseConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_FACTORIES:
			doPlayerDisableFactories(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_DISABLE_UNIT_CONSTRUCTION:
			doPlayerDisableUnitConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_ENABLE_BASE_CONSTRUCTION:
			doPlayerEnableBaseConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_ENABLE_FACTORIES:
			doPlayerEnableFactories(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::PLAYER_REPAIR_NAMED_STRUCTURE:
			doPlayerRepairStructure(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::PLAYER_ENABLE_UNIT_CONSTRUCTION:
			doPlayerEnableUnitConstruction(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_MOVE_HOME:
			doCameraMoveHome();
			return;
		case ScriptAction::BUILD_TEAM:
			doBuildTeam(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_DAMAGE:
			doNamedDamage(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_DELETE:
			doNamedDelete(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_DELETE:
			doTeamDelete(pAction->getParameter(0)->getString(), FALSE);
			return;
		case ScriptAction::TEAM_DELETE_LIVING:
			doTeamDelete(pAction->getParameter(0)->getString(), TRUE);
			return;
		case ScriptAction::TEAM_WANDER:
			doTeamWander(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_WANDER_IN_PLACE:
			doTeamWanderInPlace(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_INCREASE_PRIORITY:
			doTeamIncreasePriority(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_DECREASE_PRIORITY:
			doTeamDecreasePriority(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_PANIC:
			doTeamPanic(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_KILL:
			doNamedKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_KILL:
			doTeamKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_KILL:
			doPlayerKill(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::DISPLAY_TEXT:
			doDisplayText(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::DISPLAY_CINEMATIC_TEXT:
			doDisplayCinematicText(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(),
				                     pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::DEBUG_CRASH_BOX:
#ifdef DEBUG_CRASHING
			{
				const char* MSG = "Your Script requested the following message be displayed:\n\n";
				const char* MSG2 = "\n\nTHIS IS NOT A BUG. DO NOT REPORT IT.";
				DEBUG_CRASH(("%s%s%s",MSG,pAction->getParameter(0)->getString().str(),MSG2));
			}
#endif
			return;
		case ScriptAction::INGAME_POPUP_MESSAGE:
			doInGamePopupMessage(pAction->getParameter(0)->getString(),pAction->getParameter(1)->getInt(),
													pAction->getParameter(2)->getInt(), pAction->getParameter(3)->getInt(),
													pAction->getParameter(4)->getInt() );
			return;
		case ScriptAction::CAMEO_FLASH:
			doCameoFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_FLASH:
			doNamedFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), nullptr);
			return;
		case ScriptAction::TEAM_FLASH:
			doTeamFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), nullptr);
			return;
		case ScriptAction::NAMED_FLASH_WHITE:
			{
				RGBColor c;
				c.red = c.green = c.blue = 1.0f;
				doNamedFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), &c);
			}
			return;
		case ScriptAction::NAMED_CUSTOM_COLOR:
			{
				doNamedCustomColor(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			}
			return;
		case ScriptAction::TEAM_FLASH_WHITE:
			{
				RGBColor c;
				c.red = c.green = c.blue = 1.0f;
				doTeamFlash(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), &c);
			}
			return;
		case ScriptAction::MOVIE_PLAY_FULLSCREEN:
			doMoviePlayFullScreen(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MOVIE_PLAY_RADAR:
			doMoviePlayRadar(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::SOUND_PLAY_NAMED:
			doSoundPlayFromNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::SPEECH_PLAY:
			doSpeechPlay(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_TRANSFER_OWNERSHIP_PLAYER:
			doPlayerTransferAssetsToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_TRANSFER_OWNERSHIP_PLAYER:
			doNamedTransferAssetsToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_EXCLUDE_FROM_SCORE_SCREEN:
			excludePlayerFromScoreScreen(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::ENABLE_SCORING:
			enableScoring(TRUE);
			return;
		case ScriptAction::DISABLE_SCORING:
			enableScoring(FALSE);
			return;
		case ScriptAction::PLAYER_RELATES_PLAYER:
			updatePlayerRelationTowardPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(2)->getInt(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::RADAR_CREATE_EVENT:
		{
			Coord3D pos;
			pAction->getParameter(0)->getCoord3D(&pos);
			doRadarCreateEvent(&pos, pAction->getParameter(1)->getInt());
			return;
		}
		case ScriptAction::OBJECT_CREATE_RADAR_EVENT:
			doObjectRadarCreateEvent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_CREATE_RADAR_EVENT:
			doTeamRadarCreateEvent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::RADAR_DISABLE:
			doRadarDisable();
			return;
		case ScriptAction::RADAR_ENABLE:
			doRadarEnable();
			return;
		case ScriptAction::NAMED_SET_STEALTH_ENABLED:
			doNamedEnableStealth(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_SET_STEALTH_ENABLED:
			doTeamEnableStealth(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::NAMED_SET_UNMANNED_STATUS:
			doNamedSetUnmanned(pAction->getParameter(0)->getString() );
			return;
		case ScriptAction::TEAM_SET_UNMANNED_STATUS:
			doTeamSetUnmanned(pAction->getParameter(0)->getString() );
			return;
		case ScriptAction::NAMED_SET_BOOBYTRAPPED:
			doNamedSetBoobytrapped( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::TEAM_SET_BOOBYTRAPPED:
			doTeamSetBoobytrapped( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;
		case ScriptAction::MAP_REVEAL_AT_WAYPOINT:
			doRevealMapAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::MAP_SHROUD_AT_WAYPOINT:
			doShroudMapAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL:
			doRevealMapEntire(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL_PERM:
			doRevealMapEntirePermanently(TRUE, pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_REVEAL_ALL_UNDO_PERM:
			doRevealMapEntirePermanently(FALSE, pAction->getParameter(0)->getString());
			return;
		case ScriptAction::MAP_SHROUD_ALL:
			doShroudMapEntire(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_AVAILABLE_FOR_RECRUITMENT:
			doTeamAvailableForRecruitment(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::TEAM_COLLECT_NEARBY_FOR_TEAM:
			doCollectNearbyForTeam(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_MERGE_INTO_TEAM:
			doMergeTeamIntoTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::IDLE_ALL_UNITS:
			doIdleAllPlayerUnits(AsciiString::TheEmptyString);
			return;
		case ScriptAction::RESUME_SUPPLY_TRUCKING:
			doResumeSupplyTruckingForIdleUnits(AsciiString::TheEmptyString);
			return;
		case ScriptAction::DISABLE_INPUT:
			doDisableInput();
			return;
		case ScriptAction::ENABLE_INPUT:
			doEnableInput();
			return;
		case ScriptAction::DISABLE_BORDER_SHROUD:
			doSetBorderShroud(FALSE);
			return;
		case ScriptAction::ENABLE_BORDER_SHROUD:
			doSetBorderShroud(TRUE);
			return;
		case ScriptAction::SOUND_AMBIENT_PAUSE:
			doAmbientSoundsPause(true);
			return;
		case ScriptAction::SOUND_AMBIENT_RESUME:
			doAmbientSoundsPause(false);
			return;
		case ScriptAction::MUSIC_SET_TRACK:
			doMusicTrackChange(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_GARRISON_SPECIFIC_BUILDING:
			doTeamGarrisonSpecificBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::EXIT_SPECIFIC_BUILDING:
			doExitSpecificBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_GARRISON_NEAREST_BUILDING:
			doTeamGarrisonNearestBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::TEAM_EXIT_ALL_BUILDINGS:
			doTeamExitAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_GARRISON_SPECIFIC_BUILDING:
			doUnitGarrisonSpecificBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::NAMED_GARRISON_NEAREST_BUILDING:
			doUnitGarrisonNearestBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::NAMED_EXIT_BUILDING:
			doUnitExitBuilding(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_GARRISON_ALL_BUILDINGS:
			doPlayerGarrisonAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_EXIT_ALL_BUILDINGS:
			doPlayerExitAllBuildings(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::CAMERA_LETTERBOX_BEGIN:
			doLetterBoxMode(true);
			return;
		case ScriptAction::CAMERA_LETTERBOX_END:
			doLetterBoxMode(false);
			return;
		case ScriptAction::CAMERA_BW_MODE_BEGIN:
			doBlackWhiteMode(true, pAction->getParameter(0) ? pAction->getParameter(0)->getInt() : 0);
			return;
		case ScriptAction::CAMERA_BW_MODE_END:
			doBlackWhiteMode(false, pAction->getParameter(0) ? pAction->getParameter(0)->getInt() : 0);
			return;
		case ScriptAction::DRAW_SKYBOX_BEGIN:
			doSkyBox(true);
			return;
		case ScriptAction::DRAW_SKYBOX_END:
			doSkyBox(false);
			return;
		case ScriptAction::SHOW_WEATHER:
			doWeather(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR:
			doCameraMotionBlur(pAction->getParameter(0)->getInt(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR_JUMP:
			doCameraMotionBlurJump(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_MOTION_BLUR_FOLLOW:
			TheTacticalView->setViewFilterMode((FilterModes)(FM_VIEW_MB_PAN_ALPHA+pAction->getParameter(0)->getInt()));
			TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			return;
		case ScriptAction::CAMERA_MOTION_BLUR_END_FOLLOW:
			TheTacticalView->setViewFilterMode(FM_VIEW_MB_END_PAN_ALPHA);
			TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			return;
		case ScriptAction::FREEZE_TIME:
			doFreezeTime();
			return;
		case ScriptAction::UNFREEZE_TIME:
			doUnfreezeTime();
			return;
		case ScriptAction::SHOW_MILITARY_CAPTION:
			doMilitaryCaption(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::CAMERA_SET_AUDIBLE_DISTANCE:
			doCameraSetAudibleDistance(pAction->getParameter(0)->getReal());
			return;
		case ScriptAction::SET_STOPPING_DISTANCE:
			doSetStoppingDistance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::SET_FPS_LIMIT:
			if (!pAction->getParameter(0)->getInt())
			{
				TheFramePacer->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);
			}
			else
			{
				TheFramePacer->setFramesPerSecondLimit(pAction->getParameter(0)->getInt());
			}
			// Setting the fps limit doesn't do much good if we don't use it.  jba.
			TheWritableGlobalData->m_useFpsLimit = true;
			return;

		case ScriptAction::DISABLE_SPECIAL_POWER_DISPLAY:
			doDisableSpecialPowerDisplay();
			return;

		case ScriptAction::ENABLE_SPECIAL_POWER_DISPLAY:
			doEnableSpecialPowerDisplay();
			return;

		case ScriptAction::NAMED_HIDE_SPECIAL_POWER_DISPLAY:
			doNamedHideSpecialPowerDisplay(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::NAMED_SHOW_SPECIAL_POWER_DISPLAY:
			doNamedShowSpecialPowerDisplay(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::NAMED_SET_STOPPING_DISTANCE:
			doNamedSetStoppingDistance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::NAMED_SET_HELD:
			doNamedSetHeld(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::MUSIC_SET_VOLUME:
			doAudioSetVolume(AudioAffect_Music, pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::SOUND_SET_VOLUME:
			doAudioSetVolume((AudioAffect)(AudioAffect_Sound | AudioAffect_Sound3D), pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::SPEECH_SET_VOLUME:
			doAudioSetVolume(AudioAffect_Speech, pAction->getParameter(0)->getReal());
			return;

		case ScriptAction::TEAM_TRANSFER_TO_PLAYER:
			doTransferTeamToPlayer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_SET_MONEY:
			doSetMoney(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_GIVE_MONEY:
			doGiveMoney(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::DISPLAY_COUNTDOWN_TIMER:
			doDisplayCountdownTimer(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::HIDE_COUNTDOWN_TIMER:
			doHideCountdownTimer(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::DISPLAY_COUNTER:
			doDisplayCounter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::HIDE_COUNTER:
			doHideCounter(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::DISABLE_COUNTDOWN_TIMER_DISPLAY:
			doDisableCountdownTimerDisplay();
			return;

		case ScriptAction::ENABLE_COUNTDOWN_TIMER_DISPLAY:
			doEnableCountdownTimerDisplay();
			return;

		case ScriptAction::NAMED_STOP_SPECIAL_POWER_COUNTDOWN:
			doNamedStopSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), TRUE);
			return;

		case ScriptAction::NAMED_START_SPECIAL_POWER_COUNTDOWN:
			doNamedStopSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), FALSE);
			return;

		case ScriptAction::NAMED_SET_SPECIAL_POWER_COUNTDOWN:
			doNamedSetSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::NAMED_ADD_SPECIAL_POWER_COUNTDOWN:
			doNamedAddSpecialPowerCountdown(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT:
			doNamedFireSpecialPowerAtWaypoint(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST:
			doSkirmishFireSpecialPowerAtMostCost(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::NAMED_FIRE_SPECIAL_POWER_AT_NAMED:
			doNamedFireSpecialPowerAtNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::REFRESH_RADAR:
			doRadarRefresh();
			return;

		case ScriptAction::NAMED_STOP:
			doNamedStop(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::TEAM_STOP:
			doTeamStop(pAction->getParameter(0)->getString(), FALSE);
			return;

		case ScriptAction::TEAM_STOP_AND_DISBAND:
			doTeamStop(pAction->getParameter(0)->getString(), TRUE);
			return;

		case ScriptAction::CAMERA_TETHER_NAMED:
			doCameraTetherNamed(pAction->getParameter(0)->getString(), (Bool)(pAction->getParameter(1)->getInt()), pAction->getParameter(2)->getReal());
			return;

		case ScriptAction::CAMERA_STOP_TETHER_NAMED:
			doCameraStopTetherNamed();
			return;

		case ScriptAction::CAMERA_SET_DEFAULT:
			doCameraSetDefault(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getReal());
			return;

		case ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_TEAM:
			doTeamSetOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString(),		// second team
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc)
			return;

		case ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM:
			doTeamRemoveOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString());		// second team
			return;

		case ScriptAction::TEAM_REMOVE_ALL_OVERRIDE_RELATIONS:
			doTeamRemoveAllOverrideRelations(pAction->getParameter(0)->getString());		// first team
			return;

		case ScriptAction::TEAM_SET_OVERRIDE_RELATION_TO_PLAYER:
			doTeamSetOverrideRelationToPlayer(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString(),			// second player
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc)
			return;

		case ScriptAction::TEAM_REMOVE_OVERRIDE_RELATION_TO_PLAYER:
			doTeamRemoveOverrideRelationToPlayer(pAction->getParameter(0)->getString(),		// first team
									pAction->getParameter(1)->getString());		// second player
			return;

		case ScriptAction::PLAYER_SET_OVERRIDE_RELATION_TO_TEAM:
			doPlayerSetOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first player
									pAction->getParameter(1)->getString(),		// second team
									pAction->getParameter(2)->getInt());				// relation (ENEMIES, etc)
			return;

		case ScriptAction::PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM:
			doPlayerRemoveOverrideRelationToTeam(pAction->getParameter(0)->getString(),		// first player
									pAction->getParameter(1)->getString());		// second team
			return;

		case ScriptAction::NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH:
			doNamedFireWeaponFollowingWaypointPath(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY:
			doNamedUseCommandButtonAbility( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED:
			doNamedUseCommandButtonAbilityOnNamed( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT:
			doNamedUseCommandButtonAbilityAtWaypoint( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::NAMED_USE_COMMANDBUTTON_ABILITY_USING_WAYPOINT_PATH:
			doNamedUseCommandButtonAbilityUsingWaypointPath( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY:
			doTeamUseCommandButtonAbility( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString() );
			return;

		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED:
			doTeamUseCommandButtonAbilityOnNamed( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT:
			doTeamUseCommandButtonAbilityAtWaypoint( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString() );
			return;

		case ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT:
			doUnitStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), 0);
			return;

		case ScriptAction::UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING:
			doUnitStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt() - 1);
			return;

		case ScriptAction::UNIT_STOP_SEQUENTIAL_SCRIPT:
			doUnitStopSequentialScript(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT:
			doTeamStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), 0);
			return;

		case ScriptAction::TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING:
			doTeamStartSequentialScript(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt() - 1);
			return;

		case ScriptAction::TEAM_STOP_SEQUENTIAL_SCRIPT:
			doTeamStopSequentialScript(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::UNIT_GUARD_FOR_FRAMECOUNT:
			doUnitGuardForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::UNIT_IDLE_FOR_FRAMECOUNT:
			doUnitIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::TEAM_GUARD_FOR_FRAMECOUNT:
			doTeamIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::TEAM_IDLE_FOR_FRAMECOUNT:
			doTeamIdleForFramecount(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::WATER_CHANGE_HEIGHT:
			doWaterChangeHeight(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::WATER_CHANGE_HEIGHT_OVER_TIME:
			doWaterChangeHeightOverTime( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(),
																	 pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getReal() );
			return;

		case ScriptAction::MAP_SWITCH_BORDER:
			doBorderSwitch(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::OBJECT_FORCE_SELECT:
			doForceObjectSelection(pAction->getParameter(0)->getString(),
														 pAction->getParameter(1)->getString(),
														 pAction->getParameter(2)->getInt(),
														 pAction->getParameter(3)->getString());
			return;
		case ScriptAction::UNIT_DESTROY_ALL_CONTAINED:
			doDestroyAllContained(pAction->getParameter(0)->getString(), 0);
			return;

		case ScriptAction::RADAR_FORCE_ENABLE:
			doRadarForceEnable();
			return;

		case ScriptAction::RADAR_REVERT_TO_NORMAL:
			doRadarRevertNormal();
			return;

		case ScriptAction::SCREEN_SHAKE:
			doScreenShake( (View::CameraShakeType)pAction->getParameter( 0 )->getInt() );
			return;

		case ScriptAction::TECHTREE_MODIFY_BUILDABILITY_OBJECT:
			doModifyBuildableStatus(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;


		case ScriptAction::SET_CAVE_INDEX:
			doSetCaveIndex(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::WAREHOUSE_SET_VALUE:
			doSetWarehouseValue(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::SOUND_DISABLE_TYPE:
			doSoundEnableType(pAction->getParameter(0)->getString(), false);
			return;

		case ScriptAction::SOUND_ENABLE_TYPE:
			doSoundEnableType(pAction->getParameter(0)->getString(), true);
			return;

		case ScriptAction::SOUND_ENABLE_ALL:
			doSoundEnableType(AsciiString::TheEmptyString, true);
			return;

		case ScriptAction::SOUND_REMOVE_ALL_DISABLED:
			doSoundRemoveAllDisabled();
			return;

		case ScriptAction::SOUND_REMOVE_TYPE:
			doSoundRemoveType(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::AUDIO_OVERRIDE_VOLUME_TYPE:
			doSoundOverrideVolume(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;
		case ScriptAction::AUDIO_RESTORE_VOLUME_TYPE:
			doSoundOverrideVolume(pAction->getParameter(0)->getString(), -100.0);
			return;
		case ScriptAction::AUDIO_RESTORE_VOLUME_ALL_TYPE:
			doSoundOverrideVolume(AsciiString::TheEmptyString, -100.0);
			return;

		case ScriptAction::NAMED_SET_TOPPLE_DIRECTION:
		{
			Coord3D dir;
			pAction->getParameter(1)->getCoord3D(&dir);

			doSetToppleDirection(pAction->getParameter(0)->getString(), &dir);
			return;
		}

		case ScriptAction::UNIT_MOVE_TOWARDS_NEAREST_OBJECT_TYPE:
			doMoveUnitTowardsNearest(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE:
			doMoveTeamTowardsNearest(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;

		case ScriptAction::NAMED_RECEIVE_UPGRADE:
			doUnitReceiveUpgrade(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE:
			doSkirmishAttackNearestGroupWithValue(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT:
			doSkirmishCommandButtonOnMostValuable(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal(), pAction->getParameter(3)->getInt());
			return;

		case ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL:
			// We should never get here.
			DEBUG_CRASH(("\"[Skirmish] Wait for command button available - all\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL:
			// We should never get here.
			DEBUG_CRASH(("\"[Skirmish] Wait for command button available - partial\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_ALL:
			// We should never get here.
			DEBUG_CRASH(("\"[Team] Wait for team no longer contained - all\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL:
			// We should never get here.
			DEBUG_CRASH(("\"[Team] Wait for team no longer contained - partial\" should never be used outside of Sequential scripts. - jkmcd"));
			return;

		case ScriptAction::TEAM_SPIN_FOR_FRAMECOUNT:
			doTeamSpinForFramecount( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt() );
			return;

		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NAMED:
			doTeamUseCommandButtonOnNamed(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_UNIT:
			doTeamUseCommandButtonOnNearestEnemy(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING:
			doTeamUseCommandButtonOnNearestGarrisonedBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_KINDOF:
			doTeamUseCommandButtonOnNearestKindof(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING:
			doTeamUseCommandButtonOnNearestBuilding(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_ENEMY_BUILDING_CLASS:
			doTeamUseCommandButtonOnNearestBuildingClass(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;
		case ScriptAction::TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_OBJECTTYPE:
			doTeamUseCommandButtonOnNearestObjectType(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_PARTIAL_USE_COMMANDBUTTON:
			doTeamPartialUseCommandButton(pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT:
			doTeamCaptureNearestUnownedFactionUnit(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS:
			doCreateTeamFromCapturedUnits(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_ADD_SKILLPOINTS:
			doPlayerAddSkillPoints(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_ADD_RANKLEVEL:
			doPlayerAddRankLevels(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_SET_RANKLEVEL:
			doPlayerSetRankLevel(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;
		case ScriptAction::PLAYER_SET_RANKLEVELLIMIT:
			doMapSetRankLevelLimit(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::PLAYER_GRANT_SCIENCE:
			doPlayerGrantScience(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_PURCHASE_SCIENCE:
			doPlayerPurchaseScience(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;
		case ScriptAction::PLAYER_SCIENCE_AVAILABILITY:
			doPlayerSetScienceAvailability(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString());
			return;
		case ScriptAction::TEAM_SET_EMOTICON:
			doTeamEmoticon( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal() );
			return;
		case ScriptAction::NAMED_SET_EMOTICON:
			doNamedEmoticon( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getReal() );
			return;
		case ScriptAction::OBJECTLIST_ADDOBJECTTYPE:
			doObjectTypeListMaintenance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), TRUE);
			return;
		case ScriptAction::OBJECTLIST_REMOVEOBJECTTYPE:
			doObjectTypeListMaintenance(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), FALSE);
			return;
		case ScriptAction::MAP_REVEAL_PERMANENTLY_AT_WAYPOINT:
			doRevealMapAtWaypointPermanent(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal(), pAction->getParameter(2)->getString(), pAction->getParameter(3)->getString());
			return;
		case ScriptAction::MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT:
			doUndoRevealMapAtWaypointPermanent(pAction->getParameter(0)->getString());
			return;
		case ScriptAction::EVA_SET_ENABLED_DISABLED:
			doEvaEnabledDisabled(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::OPTIONS_SET_OCCLUSION_MODE:
			doSetOcclusionMode(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::OPTIONS_SET_DRAWICON_UI_MODE:
			doSetDrawIconUIMode(pAction->getParameter(0)->getInt());
			return;
		case ScriptAction::CAMERA_ENABLE_SLAVE_MODE:
			{
				doC3CameraEnableSlaveMode
				(
					pAction->getParameter(0)->getString(),
					pAction->getParameter(1)->getString()
				);
			}
			return;
		case ScriptAction::CAMERA_DISABLE_SLAVE_MODE:
			{
				doC3CameraDisableSlaveMode();
			}
			return;
		case ScriptAction::CAMERA_ADD_SHAKER_AT: // WST 11.12.2002 (MBL)
			{
				doC3CameraShake
				(
					pAction->getParameter(0)->getString(),	// Waypoint name
					pAction->getParameter(1)->getReal(),	// Amplitude
					pAction->getParameter(2)->getReal(),	// Duration in seconds
					pAction->getParameter(3)->getReal()		// Radius
				);
			}
			return;
		case ScriptAction::OPTIONS_SET_PARTICLE_CAP_MODE:
			doSetDynamicLODMode(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::SCRIPTING_OVERRIDE_HULK_LIFETIME:
			doOverrideHulkLifetime( pAction->getParameter( 0 )->getReal() );
			return;

		case ScriptAction::NAMED_FACE_NAMED:
			doNamedFaceNamed( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::NAMED_FACE_WAYPOINT:
			doNamedFaceWaypoint( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::TEAM_FACE_NAMED:
			doTeamFaceNamed( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::TEAM_FACE_WAYPOINT:
			doTeamFaceWaypoint( pAction->getParameter( 0 )->getString(), pAction->getParameter( 1 )->getString() );
			return;

		case ScriptAction::UNIT_AFFECT_OBJECT_PANEL_FLAGS:
			doAffectObjectPanelFlagsUnit(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::TEAM_AFFECT_OBJECT_PANEL_FLAGS:
			doAffectObjectPanelFlagsTeam(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::PLAYER_SELECT_SKILLSET:
			doAffectPlayerSkillset(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE:
			doRemoveCommandBarButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString());
			return;

		case ScriptAction::COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT:
			doAddCommandBarButton(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getInt());
			return;

		case ScriptAction::UNIT_SPAWN_NAMED_LOCATION_ORIENTATION:
		{
			Coord3D pos;
			pAction->getParameter(3)->getCoord3D(&pos);
			doCreateObject( pAction->getParameter(0)->getString(), pAction->getParameter(1)->getString(), pAction->getParameter(2)->getString(), &pos, pAction->getParameter(4)->getReal() );
			return;
		}

		case ScriptAction::PLAYER_AFFECT_RECEIVING_EXPERIENCE:
			doAffectSkillPointsModifier(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getReal());
			return;

		case ScriptAction::TEAM_GUARD_SUPPLY_CENTER:
			doGuardSupplyCenter(pAction->getParameter(0)->getString(), pAction->getParameter(1)->getInt());
			return;

		case ScriptAction::OBJECT_ALLOW_BONUSES:
			doEnableOrDisableObjectDifficultyBonuses(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::TEAM_GUARD_IN_TUNNEL_NETWORK:
			doTeamGuardInTunnelNetwork(pAction->getParameter(0)->getString());
			return;

		case ScriptAction::RESIZE_VIEW_GUARDBAND:
			doResizeViewGuardband( pAction->getParameter(0)->getReal(), pAction->getParameter(1)->getReal() );
			return;

		case ScriptAction::DELETE_ALL_UNMANNED:
			deleteAllUnmanned();
			return;

		case ScriptAction::CHOOSE_VICTIM_ALWAYS_USES_NORMAL:
			doChooseVictimAlwaysUsesNormal(pAction->getParameter(0)->getInt());
			return;

		case ScriptAction::SET_TRAIN_HELD:
			doNamedSetTrainHeld( pAction->getParameter( 0 )->getString(), (Bool)pAction->getParameter( 1 )->getInt() );
			return;

  	case ScriptAction::ENABLE_OBJECT_SOUND:
 			doEnableObjectSound(pAction->getParameter(0)->getString(), true);
			return;

		case ScriptAction::DISABLE_OBJECT_SOUND:
			doEnableObjectSound(pAction->getParameter(0)->getString(), false);
			return;

	}
}
