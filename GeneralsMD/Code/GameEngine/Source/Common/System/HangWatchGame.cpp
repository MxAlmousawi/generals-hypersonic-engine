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

// Hypersonic @feature (D-274) The side of the diagnostics log that reads game state, see HangWatch.h.
// Everything here runs on the main thread and only reads. No player names, addresses or user paths are written.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/HangWatch.h"

#ifdef RTS_HANGWATCH

#include "Common/FramePacer.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/GameLogic.h"

void HangWatch_mainLoopTick()
{
	HangWatch::heartbeat(TheGameLogic->getFrame());

	if (HangWatch::statsRequested())
	{
		const Bool inMatch = TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
		UnsignedInt objects = 0;
		UnsignedInt drawables = 0;
		if (inMatch)
		{
			objects = TheGameLogic->getObjectCount();
			for (Drawable* draw = TheGameClient->firstDrawable(); draw != nullptr; draw = draw->getNextDrawable())
				++drawables;
		}
		HangWatch::publishStats(objects, drawables, inMatch != FALSE);
	}
}

static const char* gameModeName()
{
	if (TheGameLogic->isInReplayGame()) return "replay";
	if (TheGameLogic->isInLanGame()) return "lan";
	if (TheGameLogic->isInInternetGame()) return "internet";
	if (TheGameLogic->isInSkirmishGame()) return "skirmish";
	if (TheGameLogic->isInSinglePlayerGame()) return "campaign";
	return "other";
}

static const char* difficultyName(GameDifficulty difficulty)
{
	switch (difficulty)
	{
	case DIFFICULTY_EASY: return "Easy";
	case DIFFICULTY_NORMAL: return "Normal";
	case DIFFICULTY_HARD: return "Hard";
	default: return "?";
	}
}

void HangWatch_logMatchHeader(int loadingSaveGame)
{
	if (TheGameLogic->isInShellGame())
		return;

	// Only the file name: a custom map's path holds the Windows user name.
	const char* mapName = TheGlobalData->m_mapName.str();
	for (const char* c = mapName; *c; ++c)
	{
		if (*c == '\\' || *c == '/')
			mapName = c + 1;
	}

	AsciiString line;
	line.format("MATCH map=%s mode=%s save=%d fpsLimit=%d logicFps=%d %s %dx%d exe=%s %s slots:",
		mapName, gameModeName(), loadingSaveGame ? 1 : 0,
		TheFramePacer->getFramesPerSecondLimit(), TheFramePacer->getActualLogicTimeScaleFps(),
		TheGlobalData->m_windowed ? "windowed" : "fullscreen",
		TheGlobalData->m_xResolution, TheGlobalData->m_yResolution,
		__DATE__, __TIME__);

	// The player's key is the internal "player<slot>" in a skirmish, never the display name.
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		const Player* player = ThePlayerList->getNthPlayer(i);
		const PlayerTemplate* playerTemplate = player != nullptr ? player->getPlayerTemplate() : nullptr;
		if (playerTemplate == nullptr || !(playerTemplate->isPlayableSide() || playerTemplate->isObserver()))
			continue;

		AsciiString entry;
		entry.format(" %s=%s/%s",
			TheNameKeyGenerator->keyToName(player->getPlayerNameKey()).str(),
			playerTemplate->getName().str(),
			player->getPlayerType() == PLAYER_HUMAN ? "Human" : difficultyName(player->getPlayerDifficulty()));
		line.concat(entry);
	}

	HangWatch::logEvent(line.str());
}

#endif // RTS_HANGWATCH
