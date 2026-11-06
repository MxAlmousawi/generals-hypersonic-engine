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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/WorkingDirectory.h"

namespace rts
{

Bool WorkingDirectory::s_hasSetWorkingDirectory = FALSE;
Char WorkingDirectory::s_startupWorkingDirectory[_MAX_PATH] = "";

struct WorkingDirectoryInitializer
{
	WorkingDirectoryInitializer()
	{
		WorkingDirectory::saveStartupWorkingDirectory();
	}
};

// Request capture at startup, even if no setter is called.
static WorkingDirectoryInitializer s_workingDirectoryInitializer;

Bool WorkingDirectory::saveStartupWorkingDirectory()
{
	// An earlier static constructor may call a setter before our initializer runs.
	// Cache the first capture, including failure, without logging or allocating.
	static const DWORD len = ::GetCurrentDirectory(ARRAY_SIZE(s_startupWorkingDirectory), s_startupWorkingDirectory);
	return len > 0 && len < ARRAY_SIZE(s_startupWorkingDirectory);
}

Bool WorkingDirectory::setWorkingDirectory(const char *path)
{
	if (path == nullptr || path[0] == '\0')
	{
		DEBUG_LOG(("Cannot set an empty working directory"));
		return FALSE;
	}

	if (::SetCurrentDirectory(path) == 0)
	{
		DEBUG_LOG(("Failed to set working directory to '%s' (error %d)", path, GetLastError()));
		return FALSE;
	}

	s_hasSetWorkingDirectory = TRUE;
	return TRUE;
}

Bool WorkingDirectory::setStartupWorkingDirectory()
{
	if (!saveStartupWorkingDirectory())
	{
		DEBUG_LOG(("Startup working directory is unavailable"));
		return FALSE;
	}
	return setWorkingDirectory(s_startupWorkingDirectory);
}

Bool WorkingDirectory::setExecutableWorkingDirectory()
{
	saveStartupWorkingDirectory();

	Char buffer[_MAX_PATH];
	const DWORD len = GetModuleFileName(nullptr, buffer, ARRAY_SIZE(buffer));
	if (len == 0)
	{
		DEBUG_LOG(("Failed to get executable path for working directory (error %d)", GetLastError()));
		return FALSE;
	}
	if (len >= ARRAY_SIZE(buffer))
	{
		DEBUG_LOG(("Executable path exceeds the working directory path buffer"));
		return FALSE;
	}

	Char *pEnd = strrchr(buffer, '\\');
	if (pEnd == nullptr)
	{
		DEBUG_LOG(("Executable path has no directory: '%s'", buffer));
		return FALSE;
	}
	// TheSuperHackers @bugfix For "C:\game.exe", retain "C:\" as the directory.
	// Removing the backslash leaves "C:", which refers to that drive's current directory.
	pEnd[1] = '\0';

	return setWorkingDirectory(buffer);
}

// Hypersonic @feature (D-134) separate mod folder ------------------------------------------------------------
#if RTS_ZEROHOUR
static const char *const s_gameDataProbe = "INIZH.big";
static const char *const s_retailRegistryKey = "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour";
#else
static const char *const s_gameDataProbe = "INI.big";
static const char *const s_retailRegistryKey = "SOFTWARE\\Electronic Arts\\EA Games\\Generals";
#endif

// TRUE when dir (with or without a trailing slash) contains the retail game data.
static Bool hasGameData(const char *dir)
{
	if (dir == nullptr || dir[0] == '\0')
		return FALSE;
	Char path[_MAX_PATH];
	const size_t len = strlen(dir);
	const char *sep = (dir[len - 1] == '\\' || dir[len - 1] == '/') ? "" : "\\";
	const int n = snprintf(path, ARRAY_SIZE(path), "%s%s%s", dir, sep, s_gameDataProbe);
	if (n <= 0 || n >= (int)ARRAY_SIZE(path))
		return FALSE;
	const DWORD attr = ::GetFileAttributesA(path);
	return attr != (DWORD)-1 && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static Bool readRegistryString(HKEY root, const char *key, const char *value, Char *buffer, DWORD size)
{
	HKEY handle;
	if (::RegOpenKeyExA(root, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
		return FALSE;
	DWORD type = 0;
	DWORD bytes = size - 1;
	const LONG result = ::RegQueryValueExA(handle, value, nullptr, &type, (LPBYTE)buffer, &bytes);
	::RegCloseKey(handle);
	if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0)
		return FALSE;
	buffer[bytes < size ? bytes : size - 1] = '\0';
	return buffer[0] != '\0';
}

// "GameDir=<path>" from HypersonicZH.ini next to the exe (written by the installer).
static Bool readGameDirFromIni(const char *exeDir, Char *buffer, size_t size)
{
	Char path[_MAX_PATH];
	const int n = snprintf(path, ARRAY_SIZE(path), "%sHypersonicZH.ini", exeDir);
	if (n <= 0 || n >= (int)ARRAY_SIZE(path))
		return FALSE;
	FILE *fp = fopen(path, "r");
	if (fp == nullptr)
		return FALSE;
	Bool found = FALSE;
	Char line[_MAX_PATH + 32];
	while (!found && fgets(line, ARRAY_SIZE(line), fp) != nullptr)
	{
		char *text = line;
		if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
			text += 3;	// UTF-8 byte order mark
		if (strnicmp(text, "GameDir=", 8) != 0)
			continue;
		text += 8;
		size_t len = strlen(text);
		while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == ' ' || text[len - 1] == '"'))
			text[--len] = '\0';
		while (*text == ' ' || *text == '"')
			++text;
		if (*text != '\0' && strlen(text) < size)
		{
			strcpy(buffer, text);
			found = TRUE;
		}
	}
	fclose(fp);
	return found;
}

Bool WorkingDirectory::getExecutableDirectory(Char *buffer, size_t size)
{
	const DWORD len = ::GetModuleFileName(nullptr, buffer, (DWORD)size);
	if (len == 0 || len >= size)
		return FALSE;
	Char *pEnd = strrchr(buffer, '\\');
	if (pEnd == nullptr)
		return FALSE;
	pEnd[1] = '\0';
	return TRUE;
}

Bool WorkingDirectory::setGameDataWorkingDirectory()
{
	saveStartupWorkingDirectory();

	Char exeDir[_MAX_PATH];
	if (!getExecutableDirectory(exeDir, ARRAY_SIZE(exeDir)) || hasGameData(exeDir))
		return FALSE;	// classic layout: the exe sits in the game folder

	Char candidate[_MAX_PATH];
	const char *gameDir = nullptr;
	if (hasGameData(s_startupWorkingDirectory))
		gameDir = s_startupWorkingDirectory;	// the shortcut's "Start in"
	else if (readGameDirFromIni(exeDir, candidate, ARRAY_SIZE(candidate)) && hasGameData(candidate))
		gameDir = candidate;
	else if (readRegistryString(HKEY_CURRENT_USER, "Software\\Generals Hypersonic", "GameDir", candidate, ARRAY_SIZE(candidate))
		&& hasGameData(candidate))
		gameDir = candidate;
	else if (readRegistryString(HKEY_LOCAL_MACHINE, s_retailRegistryKey, "InstallPath", candidate, ARRAY_SIZE(candidate))
		&& hasGameData(candidate))
		gameDir = candidate;

	// mss32.dll and binkw32.dll are loaded with LoadLibrary from the game folder (the mod never ships them)
	if (gameDir != nullptr && !setWorkingDirectory(gameDir))
		gameDir = nullptr;	// found but not usable: same message as not found

	if (gameDir == nullptr)
	{
		::MessageBoxA(nullptr,
			"Command & Conquer Generals Zero Hour 1.04 was not found.\n\n"
			"Start the game from its shortcut, or name the Zero Hour folder in HypersonicZH.ini next to the exe:\n"
			"GameDir=C:\\Path\\To\\Zero Hour",
			"Generals Hypersonic", MB_OK | MB_ICONERROR);
		exit(1);	// without the game data nothing can load
	}

	typedef BOOL (WINAPI *SetDllDirectoryAFunc)(LPCSTR);
	HMODULE kernel = ::GetModuleHandleA("kernel32.dll");
	SetDllDirectoryAFunc setDllDirectory = kernel ? (SetDllDirectoryAFunc)::GetProcAddress(kernel, "SetDllDirectoryA") : nullptr;
	if (setDllDirectory != nullptr)
		setDllDirectory(gameDir);

	return TRUE;
}
// Hypersonic end ---------------------------------------------------------------------------------------------

Bool WorkingDirectory::setCustomWorkingDirectory(const char *path)
{
	saveStartupWorkingDirectory();
	return setWorkingDirectory(path);
}

Bool WorkingDirectory::hasSetWorkingDirectory()
{
	return s_hasSetWorkingDirectory;
}

} // namespace rts
