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

// FILE: ReplaceObjectUpgrade.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, July 2003
// Desc:	 UpgradeModule that creates a new Object in our exact location and then deletes our object
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/Module/ReplaceObjectUpgrade.h"

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AutoDepositUpdate.h"	// Hypersonic @bugfix (D-141)
#include "GameLogic/Module/BodyModule.h"	// Hypersonic @bugfix (D-141)
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"
#include "GameClient/InGameUI.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ReplaceObjectUpgradeModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpgradeModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "ReplaceObject",	INI::parseAsciiString,	nullptr, offsetof( ReplaceObjectUpgradeModuleData, m_replaceObjectName ) },
		// Hypersonic @feature (D-141) opt-in, default No = stock behaviour (stock factions unchanged, IRAN_DESIGN 13)
		{ "KeepHealthAndProgress",	INI::parseBool,	nullptr, offsetof( ReplaceObjectUpgradeModuleData, m_keepHealthAndProgress ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ReplaceObjectUpgrade::ReplaceObjectUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ReplaceObjectUpgrade::~ReplaceObjectUpgrade()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ReplaceObjectUpgrade::upgradeImplementation()
{
	const ReplaceObjectUpgradeModuleData *data = getReplaceObjectUpgradeModuleData();
	const ThingTemplate* replacementTemplate = TheThingFactory->findTemplate(data->m_replaceObjectName);

	Bool oldObjectSelected;
	Int oldObjectSquadNumber;
	ObjectID oldObjectID;	// Hypersonic @bugfix (D-100)
	Real oldHealthFraction = 1.0f;	// Hypersonic @bugfix (D-141)
	const AutoDepositUpdate* oldAutoDeposit = nullptr;	// Hypersonic @bugfix (D-141)
	static const NameKeyType key_AutoDepositUpdate = NAMEKEY("AutoDepositUpdate");	// Hypersonic @bugfix (D-141)
	Player* oldOwner;
	Matrix3D myMatrix;
	Team* myTeam;

	{
		Object* me = getObject();

		myMatrix = *me->getTransformMatrix();
		myTeam = me->getTeam();// Team implies player.  It is a subset.
		oldObjectID = me->getID();
		oldOwner = me->getControllingPlayer();

		// Hypersonic @bugfix (Generals Hypersonic D-141) remember the old object's damage and income timer (destroyObject
		// only queues the deletion, so the old modules stay valid until the end of this function).
		const BodyModuleInterface* oldBody = me->getBodyModule();
		if( oldBody && oldBody->getMaxHealth() > 0.0f )
			oldHealthFraction = oldBody->getHealth() / oldBody->getMaxHealth();
		oldAutoDeposit = (const AutoDepositUpdate*)me->findUpdateModule(key_AutoDepositUpdate);

		if (replacementTemplate == nullptr)
		{
			DEBUG_ASSERTCRASH(replacementTemplate != nullptr, ("No such object '%s' in ReplaceObjectUpgrade.", data->m_replaceObjectName.str()));
			return;
		}

		Drawable* selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
		oldObjectSelected = selectedDrawable && selectedDrawable->getID() == me->getDrawable()->getID();
		oldObjectSquadNumber = me->getControllingPlayer()->getSquadNumberForObject(me);

		// Remove us first since occupation of cells is apparently not a refcount, but a flag.  If I don't remove, then the new
		// thing will be placed, and then on deletion I will remove "his" marks.
		TheAI->pathfinder()->removeObjectFromPathfindMap(me);
		TheGameLogic->destroyObject(me);
	}

	Object *replacementObject = TheThingFactory->newObject(replacementTemplate, myTeam);
	replacementObject->setTransformMatrix(&myMatrix);
	TheAI->pathfinder()->addObjectToPathfindMap( replacementObject );

	// Hypersonic @bugfix (Generals Hypersonic D-141) an upgrade is not a repair: the replacement keeps the old object's
	// health fraction (Export Contract no longer fully repairs a damaged Abadan Refinery) ...
	// Only with KeepHealthAndProgress = Yes (Iran's Export Contract); stock modules keep the stock behaviour.
	if( data->m_keepHealthAndProgress )
	{
		BodyModuleInterface* newBody = replacementObject->getBodyModule();
		if( newBody && oldHealthFraction > 0.0f && oldHealthFraction < 1.0f )
			newBody->internalChangeHealth( oldHealthFraction * newBody->getMaxHealth() - newBody->getHealth() );

		// ... and its progress toward the next income deposit (the 20 s refinery timer does not restart).
		AutoDepositUpdate* newAutoDeposit = (AutoDepositUpdate*)replacementObject->findUpdateModule(key_AutoDepositUpdate);
		if( newAutoDeposit && oldAutoDeposit )
			newAutoDeposit->inheritDepositProgress( oldAutoDeposit );
	}

	// Hypersonic @bugfix (Generals Hypersonic D-100) an AI build list entry that tracked the old object now tracks its
	// replacement. Otherwise the AI sees the old object as destroyed and rebuilds it next to the replacement: every
	// Export Contract on an AI Abadan Refinery added another refinery.
	if( oldOwner )
	{
		for( BuildListInfo *info = oldOwner->getBuildList(); info; info = info->getNext() )
		{
			if( info->getObjectID() == oldObjectID )
				info->setObjectID( replacementObject->getID() );
		}
	}

	// Now onCreates were called at the constructor.  This magically created
	// thing needs to be considered as Built for Game specific stuff.
	for (BehaviorModule** m = replacementObject->getBehaviorModules(); *m; ++m)
	{
		CreateModuleInterface* create = (*m)->getCreate();
		if (!create)
			continue;
		create->onBuildComplete();
	}

	if( replacementObject->getControllingPlayer() )
	{
		// Hypersonic @bugfix (Generals Hypersonic D-141) the replacement was created finished, so joining its team
		// (Object constructor -> setTeam -> Player::becomingTeamMember) already counted its power;
		// onStructureConstructionComplete counts it again. Take it out once so it is counted once (Export Contract: the
		// Abadan Refinery used 4 power, then 8, and selling it gave back only 4). Generic: stock replacements use no power.
		// With KeepHealthAndProgress, isRebuild = TRUE: the score screen does not count the replacement as another
		// building built and its full cost spent.
		replacementObject->friend_adjustPowerForPlayer(FALSE);
		replacementObject->getControllingPlayer()->onStructureConstructionComplete(nullptr, replacementObject, data->m_keepHealthAndProgress);

		// TheSuperHackers @bugfix Stubbjax 26/05/2025 If the old object was selected, select the new one.
		if (oldObjectSelected)
		{
			GameMessage* msg = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND);
			msg->appendBooleanArgument(TRUE);
			msg->appendObjectIDArgument(replacementObject->getID());
			TheInGameUI->selectDrawable(replacementObject->getDrawable());
		}

		// TheSuperHackers @bugfix Stubbjax 26/05/2025 If the old object was grouped, group the new one.
		if (oldObjectSquadNumber != NO_HOTKEY_SQUAD)
		{
			if (replacementObject->isLocallyControlled())
			{
				GameMessage* msg = TheMessageStream->appendMessage((GameMessage::Type)(GameMessage::MSG_CREATE_TEAM0 + oldObjectSquadNumber));
				msg->appendObjectIDArgument(replacementObject->getID());
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ReplaceObjectUpgrade::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ReplaceObjectUpgrade::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ReplaceObjectUpgrade::loadPostProcess()
{

	// extend base class
	UpgradeModule::loadPostProcess();

}
