/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#pragma once

#include "Lib/BaseType.h"

namespace rts
{

// TheSuperHackers @feature CryoTheRenegade 14/08/2026
// Saves and restores the process working directory.
class WorkingDirectory
{
public:
	static Bool setStartupWorkingDirectory();
	static Bool setExecutableWorkingDirectory();
	// Relative paths are resolved from the current working directory.
	static Bool setCustomWorkingDirectory(const char *path);
	// Returns true after a setter successfully changes the working directory.
	static Bool hasSetWorkingDirectory();

	// Hypersonic @feature (D-134) The mod's exe may live in its own folder, outside the game folder. When the
	// executable directory holds no game data, find the game folder (startup working directory, HypersonicZH.ini
	// "GameDir=" next to the exe, HKCU "Software\Generals Hypersonic" GameDir, the retail InstallPath), make it
	// the working directory and add it to the DLL search path (mss32.dll, binkw32.dll). FALSE when the executable
	// directory is the game folder (the caller then uses it); without any game folder it shows an error and exits.
	static Bool setGameDataWorkingDirectory();
	// Directory of the running executable with a trailing backslash; FALSE on failure.
	static Bool getExecutableDirectory(Char *buffer, size_t size);

private:
	friend struct WorkingDirectoryInitializer;

	static Bool saveStartupWorkingDirectory();
	static Bool setWorkingDirectory(const char *path);

	static Bool s_hasSetWorkingDirectory;
	static Char s_startupWorkingDirectory[];
};

} // namespace rts
