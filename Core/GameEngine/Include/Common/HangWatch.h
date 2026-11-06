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

// Hypersonic @feature (D-274) Hang watchdog and diagnostics log, written to <user data>\Logs\.
//
// A separate thread watches a heartbeat from the main loop. It reports a stopped heartbeat and never ends the game.
// Nothing here is read by game logic, so CRC, replays and LAN sync are not affected.
//
// On anything but 32-bit Windows (the Android engine) every call below is an empty inline function,
// so a port needs only this header and the one-line calls.

#pragma once

#if defined(_WIN32) && !defined(__ANDROID__) && (defined(_M_IX86) || defined(__i386__))
#define RTS_HANGWATCH 1
#endif

class HangWatch
{
public:
	/// Where the main thread is in a frame. Keep in step with s_stageNames in HangWatch.cpp.
	enum Stage
	{
		Stage_Startup,
		Stage_Radar,
		Stage_Audio,
		Stage_AudioRequests,
		Stage_AudioPlaying,
		Stage_AudioFading,
		Stage_AudioRelease,
		Stage_Client,
		Stage_ClientDraw,
		Stage_Present,
		Stage_ClientUi,
		Stage_Messages,
		Stage_Network,
		Stage_Logic,
		Stage_ClientStep,
		Stage_FramePacer,
		Stage_WindowsMessages,
		Stage_Minimized,

		Stage_Count
	};

#ifdef RTS_HANGWATCH
	static void init(const char* userDataDir); ///< Main thread, before the main loop.
	static void shutdown(const char* reason); ///< Any thread. Only the first call does anything.

	static void heartbeat(unsigned int logicFrame); ///< Main thread, once per main loop.
	static void idleHeartbeat(); ///< Main thread, in the loop that waits while the game is minimized.
	static void setStage(Stage stage) { s_stage = stage; }

	/// Main thread, before work that blocks the loop for long by design: loading a map or a save, ending a match, listing maps.
	/// The stall limits are longer until the next heartbeat.
	static void beginLongWork();

	/// Main thread only. Queues one timestamped line, the watchdog thread writes it. Takes no lock and touches no disk.
	static void logEvent(const char* text);

	static bool statsRequested() { return s_statsRequested != 0; }
	static void publishStats(unsigned int objects, unsigned int drawables, bool inMatch); ///< Main thread.

	static void noteAudioCache(unsigned int usedBytes, unsigned int maxBytes); ///< Main thread.
	static void countInUseEviction(); ///< Any thread, including the Miles timer thread: only an interlocked increment.

	static void noteCrash(); ///< A crash report is being made: start no new hang report.

	static volatile long s_stage;
	static volatile long s_statsRequested;
#else
	static void init(const char*) {}
	static void shutdown(const char*) {}
	static void heartbeat(unsigned int) {}
	static void idleHeartbeat() {}
	static void setStage(Stage) {}
	static void beginLongWork() {}
	static void logEvent(const char*) {}
	static bool statsRequested() { return false; }
	static void publishStats(unsigned int, unsigned int, bool) {}
	static void noteAudioCache(unsigned int, unsigned int) {}
	static void countInUseEviction() {}
	static void noteCrash() {}
#endif
};

// The parts that read game state, in GeneralsMD/Code/GameEngine/Source/Common/System/HangWatchGame.cpp.
#ifdef RTS_HANGWATCH
void HangWatch_mainLoopTick(); ///< Main thread, once per main loop: heartbeat, and the counts for the once-a-minute line.
void HangWatch_logMatchHeader(int loadingSaveGame); ///< Main thread, when a match has finished loading.
#else
inline void HangWatch_mainLoopTick() {}
inline void HangWatch_logMatchHeader(int) {}
#endif
