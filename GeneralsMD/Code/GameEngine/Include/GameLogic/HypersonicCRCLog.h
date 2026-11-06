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

// FILE: HypersonicCRCLog.h ///////////////////////////////////////////////////////////////////////
// Desc:   Hypersonic @feature Network CRC diagnostics (Generals Hypersonic D-091).
//
//         When the file "hypersonic_crc_log.txt" exists in the working directory (the game folder),
//         the first 6 network logic CRCs of each game are written to the user data folder as
//         HypersonicCRC_<frame>.txt: every section and object in the order it is hashed, readable
//         key values (floats also as hex bits), the exact bytes hashed and the running CRC after
//         each record. A CRC mismatch writes HypersonicCRC_mismatch_<frame>.txt. The same file is
//         used, unchanged, in the desktop engine and the Android port so the logs line up;
//         compare two folders with tools/crc_diff.py.
//
//         HypersonicCRCLog is an XferCRC that also records what it hashes. It computes the same
//         CRC as XferCRC (it only adds recording after calling XferCRC), so game logic is
//         unchanged. When the marker is absent GameLogic uses a plain XferCRC as before.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/XferCRC.h"
#include <string>

class Object;

class HypersonicCRCLog : public XferCRC
{
public:
	/// Called once per network logic CRC; TRUE when this one should be logged (marker present,
	/// fewer than 6 logged in this game). Checks the marker file once per game.
	static Bool wantFrame( UnsignedInt frame );
	/// Records a CRC we computed (kept for the mismatch report); cheap, only when enabled.
	static void noteLocalCRC( UnsignedInt frame, UnsignedInt crc );
	/// Writes HypersonicCRC_mismatch_<frame>.txt when logging is enabled. playerIndex/crc pairs.
	static void logMismatch( UnsignedInt frame, Int count, const Int *playerIndex, const UnsignedInt *crc,
		Int numConnectedPlayers );

	HypersonicCRCLog( UnsignedInt frame );
	virtual ~HypersonicCRCLog() override;

	virtual void xferSnapshot( Snapshot *snapshot ) override;

	/// Emit a record for the bytes hashed since the previous record: "<tag> <text> run=<crc>".
	void record( const char *tag, const char *text );
	/// Emit the record for one object just hashed.
	void recordObject( Object *obj );
	/// Records one line per top-level child snapshot (players of ThePlayerList) until endPlayers().
	void beginPlayers();
	void endPlayers();
	/// Split the following bytes into chunk records of ~1 KB until endChunks() (large sections).
	void beginChunks( const char *sectionName );
	void endChunks();
	/// Write the file. finalCRC is the value GameLogic returns.
	void finish( UnsignedInt finalCRC, UnsignedInt seedCRC );

protected:
	virtual void xferImplementation( void *data, Int dataSize ) override;

	void emit( const char *tag, const char *text );
	void emitPlayer( Int index );

	UnsignedInt m_frame;
	std::string m_out;			///< records so far
	std::string m_pending;		///< hex of the bytes hashed since the last record
	Int m_pendingBytes;
	Int m_depth;				///< nesting of xferSnapshot calls
	Bool m_players;				///< emit a player record when a depth-1 child snapshot ends
	Int m_playerIndex;
	Bool m_chunks;
	std::string m_chunkSection;
	Int m_chunkIndex;
	Int m_recordIndex;
};
