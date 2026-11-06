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

// Hypersonic @feature (D-274) Hang watchdog and diagnostics log, see HangWatch.h.
//
// The watchdog must keep working during the very deadlock it reports. So its thread:
// - takes no game lock and makes no Miles call,
// - uses no memory pool, heap, CRT file or engine string: only raw Win32 calls on static buffers,
// - never allocates or writes while another thread is suspended. It formats only after the resume,
// - opens files only while the main thread is healthy, because CreateFile may use the process heap,
// - does not call functions that take the loader lock (GetModuleFileName, the Toolhelp module list, ...),
//   and the Psapi wide functions it uses instead read memory directly,
// - reports text only, and makes no dump: MiniDumpWriteDump in-process can block for good on a lock held by a thread it suspended.
//
// Only this thread writes the file while it runs. The main thread queues its few lines and touches no disk.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/HangWatch.h"

#ifdef RTS_HANGWATCH

#include <stdarg.h>
#include <tlhelp32.h>
#include "gitinfo.h"

// Device events reach us through this hook, since the WW3D2 library cannot see the game engine. Defined in dx8wrapper.cpp.
extern void (*DX8Wrapper_DiagEvent)(const char* text);

// All limits, in one place.
static const DWORD WAKE_INTERVAL_MS = 500;
static const DWORD STALL_SAMPLE_MS = 3000; ///< No heartbeat this long: one stack sample of the main thread.
static const DWORD HANG_REPORT_MS = 15000; ///< No heartbeat this long: full report.
static const DWORD LONG_WORK_SAMPLE_MS = 60000; ///< The same two limits while loading (see beginLongWork).
static const DWORD LONG_WORK_REPORT_MS = 300000;
static const DWORD WATCHDOG_GAP_MS = 5000; ///< The watchdog itself slept this long: PC sleep or everything suspended, so not a hang.
static const DWORD MINUTE_MS = 60000; ///< Once-a-minute line and file flush.
static const DWORD SHUTDOWN_WAIT_MS = 3000;
static const DWORD ROTATE_BYTES = 2 * 1024 * 1024;
static const DWORD ROTATE_RETRY_BYTES = 256 * 1024;
static const int LOG_FILES_KEPT = 5; ///< Diagnostics.txt, Diagnostics.1.txt ... Diagnostics.4.txt
static const int MAX_SAMPLES_PER_SESSION = 100; ///< After this a stall gets one line without a stack.
static const int MAX_REPORTS_PER_SESSION = 3;
static const unsigned int MAIN_STACK_WORDS = 128; ///< Resolved stack words written per thread.
static const unsigned int OTHER_STACK_WORDS = 48;
static const unsigned int STACK_COPY_BYTES = 64 * 1024;
static const unsigned int MAX_MODULES = 256;
static const unsigned int MAX_THREADS = 128;
static const unsigned int QUEUE_SLOTS = 32; ///< Power of two.
static const unsigned int QUEUE_LINE_BYTES = 512;
static const unsigned int CRASH_SLOTS = 8; ///< Exceptions waiting for the watchdog to write them.
static const unsigned int CRASH_STACK_WORDS = 1024; ///< Stack copied per exception, 4 KB up from the faulting ESP.
static const unsigned int CRASH_FRAMES_WRITTEN = 32; ///< Resolved stack lines written per exception.
static const int MAX_CRASH_RECORDS_PER_SESSION = 64; ///< Some drivers fault and recover on purpose: keep this bounded.

static const char* const s_stageNames[HangWatch::Stage_Count] =
{
	"Startup",
	"Radar",
	"Audio",
	"AudioRequests",
	"AudioPlaying",
	"AudioFading",
	"AudioRelease",
	"Client",
	"ClientDraw",
	"Present",
	"ClientUi",
	"Messages",
	"Network",
	"Logic",
	"ClientStep",
	"FramePacer",
	"WindowsMessages",
	"Minimized",
};

volatile long HangWatch::s_stage = HangWatch::Stage_Startup;
volatile long HangWatch::s_statsRequested = 0;

// Written by the main thread, read by the watchdog.
static volatile long s_heartbeat = 0;
static volatile long s_longWork = 1; ///< Engine startup counts as long work until the first heartbeat.
static volatile long s_crashing = 0;
static volatile long s_running = 0;
static volatile DWORD s_logicFrame = 0;
static volatile DWORD s_audioUsedBytes = 0;
static volatile DWORD s_audioMaxBytes = 0;
static volatile long s_inUseEvictions = 0;

static volatile long s_statsReady = 0;
static volatile DWORD s_statsObjects = 0;
static volatile DWORD s_statsDrawables = 0;
static volatile long s_statsInMatch = 0;
static volatile long s_droppedLines = 0;

// Main thread to watchdog line queue: one writer, one reader, both indexes only ever count up.
static char s_queue[QUEUE_SLOTS][QUEUE_LINE_BYTES];
static volatile long s_queueWrite = 0;
static volatile long s_queueRead = 0;

// Owned by the watchdog thread while it runs, by the main thread before and after.
static HANDLE s_file = INVALID_HANDLE_VALUE;
static DWORD s_fileBytes = 0;
static DWORD s_rotateAtBytes = ROTATE_BYTES;
static HANDLE s_thread = nullptr;
static DWORD s_threadId = 0;
static HANDLE s_quit = nullptr;
static HANDLE s_mainThread = nullptr;
static DWORD s_mainThreadId = 0;
static DWORD s_mainCpuAtSample = 0;
static int s_samples = 0;
static int s_reports = 0;
static char s_logDir[MAX_PATH];
static char s_line[1024];
static char s_stackCopy[STACK_COPY_BYTES];

struct ModuleEntry
{
	DWORD base;
	DWORD size;
	char name[48];
};
static ModuleEntry s_modules[MAX_MODULES];
static unsigned int s_moduleCount = 0;
static HMODULE s_moduleHandles[MAX_MODULES];

// Loaded once at start, because loading a library at hang time could wait on the loader lock.
struct HwModuleInfo { LPVOID base; DWORD size; LPVOID entry; };
struct HwMemCounters
{
	DWORD cb;
	DWORD pageFaultCount;
	SIZE_T peakWorkingSet;
	SIZE_T workingSet;
	SIZE_T quotaPeakPagedPool;
	SIZE_T quotaPagedPool;
	SIZE_T quotaPeakNonPagedPool;
	SIZE_T quotaNonPagedPool;
	SIZE_T pagefileUsage;
	SIZE_T peakPagefileUsage;
	SIZE_T privateUsage;
};
typedef BOOL (WINAPI *EnumProcessModules_t)(HANDLE, HMODULE*, DWORD, LPDWORD);
typedef DWORD (WINAPI *GetModuleBaseNameW_t)(HANDLE, HMODULE, LPWSTR, DWORD);
typedef BOOL (WINAPI *GetModuleInformation_t)(HANDLE, HMODULE, HwModuleInfo*, DWORD);
typedef BOOL (WINAPI *GetProcessMemoryInfo_t)(HANDLE, HwMemCounters*, DWORD);
typedef HANDLE (WINAPI *OpenThread_t)(DWORD, BOOL, DWORD);
typedef LONG (WINAPI *NtQueryInformationThread_t)(HANDLE, int, PVOID, ULONG, PULONG);
static EnumProcessModules_t s_enumProcessModules = nullptr;
static GetModuleBaseNameW_t s_getModuleBaseNameW = nullptr;
static GetModuleInformation_t s_getModuleInformation = nullptr;
static GetProcessMemoryInfo_t s_getProcessMemoryInfo = nullptr;
static OpenThread_t s_openThread = nullptr;
static NtQueryInformationThread_t s_ntQueryInformationThread = nullptr;

//-------------------------------------------------------------------------------------------------
// A small formatter, because the CRT's takes locks and wsprintf has its own limits.
// %s string, %u unsigned, %x 8 hex digits, %T local date and time, %% percent.

static void putChar(char* out, unsigned int cap, unsigned int& n, char c)
{
	if (n < cap)
		out[n++] = c;
}

static void putString(char* out, unsigned int cap, unsigned int& n, const char* s)
{
	if (s == nullptr)
		s = "?";
	while (*s)
		putChar(out, cap, n, *s++);
}

static void putDecimal(char* out, unsigned int cap, unsigned int& n, unsigned int value, unsigned int minDigits)
{
	char digits[12];
	unsigned int count = 0;
	do
	{
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	while (count < minDigits && count < sizeof(digits))
		digits[count++] = '0';
	while (count > 0)
		putChar(out, cap, n, digits[--count]);
}

static void putHex(char* out, unsigned int cap, unsigned int& n, unsigned int value)
{
	static const char hex[] = "0123456789abcdef";
	for (int shift = 28; shift >= 0; shift -= 4)
		putChar(out, cap, n, hex[(value >> shift) & 0xF]);
}

static void putStamp(char* out, unsigned int cap, unsigned int& n)
{
	SYSTEMTIME st;
	::GetLocalTime(&st);
	putDecimal(out, cap, n, st.wYear, 4);
	putChar(out, cap, n, '-');
	putDecimal(out, cap, n, st.wMonth, 2);
	putChar(out, cap, n, '-');
	putDecimal(out, cap, n, st.wDay, 2);
	putChar(out, cap, n, ' ');
	putDecimal(out, cap, n, st.wHour, 2);
	putChar(out, cap, n, ':');
	putDecimal(out, cap, n, st.wMinute, 2);
	putChar(out, cap, n, ':');
	putDecimal(out, cap, n, st.wSecond, 2);
}

/// Returns the length. The result always ends with CR LF, and is not null terminated.
static unsigned int formatLine(char* out, unsigned int cap, const char* format, va_list args)
{
	const unsigned int textCap = cap - 2;
	unsigned int n = 0;
	for (; *format; ++format)
	{
		if (*format != '%')
		{
			putChar(out, textCap, n, *format);
			continue;
		}
		++format;
		switch (*format)
		{
		case 's': putString(out, textCap, n, va_arg(args, const char*)); break;
		case 'u': putDecimal(out, textCap, n, va_arg(args, unsigned int), 1); break;
		case 'x': putHex(out, textCap, n, va_arg(args, unsigned int)); break;
		case 'T': putStamp(out, textCap, n); break;
		case '%': putChar(out, textCap, n, '%'); break;
		case 0: --format; break;
		default: break;
		}
	}
	out[n++] = '\r';
	out[n++] = '\n';
	return n;
}

//-------------------------------------------------------------------------------------------------
// The log file

static void writeLine(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const unsigned int length = formatLine(s_line, sizeof(s_line), format, args);
	va_end(args);

	if (s_file != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		::WriteFile(s_file, s_line, length, &written, nullptr);
		s_fileBytes += written;
	}
}

static void flushLog()
{
	if (s_file != INVALID_HANDLE_VALUE)
		::FlushFileBuffers(s_file);
}

/// path = <Logs dir>Diagnostics[.N].txt
static void logFilePath(char* path, int index)
{
	unsigned int n = 0;
	putString(path, MAX_PATH - 1, n, s_logDir);
	putString(path, MAX_PATH - 1, n, "Diagnostics");
	if (index > 0)
	{
		putChar(path, MAX_PATH - 1, n, '.');
		putDecimal(path, MAX_PATH - 1, n, (unsigned int)index, 1);
	}
	putString(path, MAX_PATH - 1, n, ".txt");
	path[n] = 0;
}

static void openLog()
{
	char path[MAX_PATH];
	logFilePath(path, 0);
	// Appending keeps lines whole if a second copy of the game writes here too.
	s_file = ::CreateFileA(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	s_fileBytes = (s_file != INVALID_HANDLE_VALUE) ? ::GetFileSize(s_file, nullptr) : 0;
}

/// Only while the main thread is healthy, see the top of this file.
static void rotateLogIfNeeded()
{
	if (s_fileBytes < s_rotateAtBytes)
		return;

	if (s_file != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(s_file);
		s_file = INVALID_HANDLE_VALUE;
	}

	char from[MAX_PATH];
	char to[MAX_PATH];
	logFilePath(to, LOG_FILES_KEPT - 1);
	::DeleteFileA(to);
	for (int i = LOG_FILES_KEPT - 2; i >= 0; --i)
	{
		logFilePath(from, i);
		logFilePath(to, i + 1);
		::MoveFileA(from, to);
	}
	openLog();

	// If a program holds the file open it cannot be renamed. Try again later, not on every wake.
	s_rotateAtBytes = (s_fileBytes < ROTATE_BYTES) ? ROTATE_BYTES : s_fileBytes + ROTATE_RETRY_BYTES;
}

/// Writes a note if the last session in the log has no end line: it froze and was killed, or the PC was reset.
static void noteUncleanPreviousSession()
{
	char path[MAX_PATH];
	logFilePath(path, 0);
	HANDLE file = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;

	static const char endMark[] = "SESSION END";
	char tail[96];
	DWORD got = 0;
	const DWORD size = ::GetFileSize(file, nullptr);
	if (size > 0 && size != (DWORD)-1)
	{
		const DWORD want = size < sizeof(tail) ? size : (DWORD)sizeof(tail);
		::SetFilePointer(file, -(LONG)want, nullptr, FILE_END);
		::ReadFile(file, tail, want, &got, nullptr);
	}
	::CloseHandle(file);

	if (got == 0)
		return;

	const DWORD markLength = sizeof(endMark) - 1;
	for (DWORD i = 0; i + markLength <= got; ++i)
	{
		if (memcmp(tail + i, endMark, markLength) == 0)
			return;
	}
	writeLine("%T PREVIOUS SESSION DID NOT END: no end line, so it froze and was killed, or the PC was reset");
}

//-------------------------------------------------------------------------------------------------
// Modules and stacks

static void refreshModules()
{
	if (s_enumProcessModules == nullptr || s_getModuleBaseNameW == nullptr || s_getModuleInformation == nullptr)
		return;

	const HANDLE self = ::GetCurrentProcess();
	DWORD neededBytes = 0;
	if (!s_enumProcessModules(self, s_moduleHandles, sizeof(s_moduleHandles), &neededBytes))
		return;

	unsigned int count = neededBytes / sizeof(HMODULE);
	if (count > MAX_MODULES)
		count = MAX_MODULES;

	unsigned int kept = 0;
	for (unsigned int i = 0; i < count; ++i)
	{
		HwModuleInfo info;
		WCHAR wideName[48];
		if (!s_getModuleInformation(self, s_moduleHandles[i], &info, sizeof(info)))
			continue;
		const DWORD length = s_getModuleBaseNameW(self, s_moduleHandles[i], wideName, ARRAY_SIZE(wideName) - 1);

		ModuleEntry& entry = s_modules[kept++];
		entry.base = (DWORD)info.base;
		entry.size = info.size;
		// Only the base name: a full path can hold the Windows user name.
		DWORD c = 0;
		for (; c < length && c < sizeof(entry.name) - 1; ++c)
			entry.name[c] = (wideName[c] < 128) ? (char)wideName[c] : '_';
		entry.name[c] = 0;
	}
	s_moduleCount = kept;
}

static const ModuleEntry* findModule(DWORD address)
{
	for (unsigned int i = 0; i < s_moduleCount; ++i)
	{
		if (address - s_modules[i].base < s_modules[i].size)
			return &s_modules[i];
	}
	return nullptr;
}

/// True if the bytes before this address look like a CALL, which removes most of the data pointers on a stack.
static bool looksLikeReturnAddress(DWORD address)
{
	unsigned char code[7];
	SIZE_T got = 0;
	if (address < sizeof(code)
		|| !::ReadProcessMemory(::GetCurrentProcess(), (LPCVOID)(address - sizeof(code)), code, sizeof(code), &got)
		|| got != sizeof(code))
	{
		return false;
	}

	if (code[2] == 0xE8) // call rel32
		return true;

	static const int indirectCallPositions[] = { 0, 1, 3, 4, 5 }; // FF /2 with 5, 4, 2, 1 or 0 bytes after the ModRM byte
	for (int i = 0; i < 5; ++i)
	{
		const int position = indirectCallPositions[i];
		if (code[position] == 0xFF && (code[position + 1] & 0x38) == 0x10)
			return true;
	}
	return false;
}

static DWORD threadCpuMs(HANDLE thread)
{
	FILETIME created, exited, kernel, user;
	if (!::GetThreadTimes(thread, &created, &exited, &kernel, &user))
		return 0;

	ULARGE_INTEGER k, u;
	k.LowPart = kernel.dwLowDateTime;
	k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime;
	u.HighPart = user.dwHighDateTime;
	return (DWORD)((k.QuadPart + u.QuadPart) / 10000);
}

static void writeAddressField(const char* label, DWORD address)
{
	const ModuleEntry* module = findModule(address);
	if (module != nullptr)
		writeLine("    %s %s+%x", label, module->name, address - module->base);
	else
		writeLine("    %s ?%x", label, address);
}

/// Suspends the thread only to copy its registers and stack, then formats after the resume.
/// The suspend counts up, so a thread that someone else suspended stays suspended.
static void writeThread(HANDLE thread, DWORD threadId, const char* tag, unsigned int maxWords)
{
	CONTEXT context;
	memset(&context, 0, sizeof(context));
	context.ContextFlags = CONTEXT_FULL;

	SIZE_T stackBytes = 0;
	bool gotContext = false;
	const DWORD previousSuspendCount = ::SuspendThread(thread);
	if (previousSuspendCount != (DWORD)-1)
	{
		gotContext = ::GetThreadContext(thread, &context) != FALSE;
		if (gotContext)
		{
			// Only from the stack pointer up, in the committed region around it: reading into the guard page
			// below it would take that page away from the thread and break its stack growth.
			MEMORY_BASIC_INFORMATION region;
			if (::VirtualQuery((LPCVOID)context.Esp, &region, sizeof(region)) != 0
				&& region.State == MEM_COMMIT
				&& (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0)
			{
				SIZE_T available = (DWORD)region.BaseAddress + region.RegionSize - context.Esp;
				if (available > STACK_COPY_BYTES)
					available = STACK_COPY_BYTES;
				::ReadProcessMemory(::GetCurrentProcess(), (LPCVOID)context.Esp, s_stackCopy, available, &stackBytes);
			}
		}
		::ResumeThread(thread);
	}

	DWORD startAddress = 0;
	if (s_ntQueryInformationThread != nullptr)
		s_ntQueryInformationThread(thread, 9 /* ThreadQuerySetWin32StartAddress */, &startAddress, sizeof(startAddress), nullptr);

	writeLine("  THREAD tid=%u%s suspendCountBefore=%u cpuMs=%u", threadId, tag, previousSuspendCount, threadCpuMs(thread));
	writeAddressField("start", startAddress);
	if (!gotContext)
	{
		writeLine("    no context");
		return;
	}

	writeLine("    eax=%x ebx=%x ecx=%x edx=%x esi=%x edi=%x", context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi);
	writeLine("    ebp=%x esp=%x eip=%x efl=%x stackCopied=%u", context.Ebp, context.Esp, context.Eip, context.EFlags, (unsigned int)stackBytes);
	writeAddressField("eip", context.Eip);

	unsigned int written = 0;
	const DWORD* words = (const DWORD*)s_stackCopy;
	const unsigned int wordCount = (unsigned int)(stackBytes / sizeof(DWORD));
	for (unsigned int i = 0; i < wordCount && written < maxWords; ++i)
	{
		const ModuleEntry* module = findModule(words[i]);
		if (module != nullptr && looksLikeReturnAddress(words[i]))
		{
			writeLine("    +%x %s+%x", i * (unsigned int)sizeof(DWORD), module->name, words[i] - module->base);
			++written;
		}
	}
}

static void memoryMegabytes(unsigned int& privateMb, unsigned int& workingSetMb)
{
	privateMb = 0;
	workingSetMb = 0;
	if (s_getProcessMemoryInfo != nullptr)
	{
		HwMemCounters counters;
		memset(&counters, 0, sizeof(counters));
		counters.cb = sizeof(counters);
		if (s_getProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters)))
		{
			privateMb = (unsigned int)(counters.privateUsage / (1024 * 1024));
			workingSetMb = (unsigned int)(counters.workingSet / (1024 * 1024));
		}
	}
}

static const char* currentStageName()
{
	const long stage = HangWatch::s_stage;
	return (stage >= 0 && stage < HangWatch::Stage_Count) ? s_stageNames[stage] : "?";
}

static void writeState(unsigned int stalledMs)
{
	unsigned int privateMb, workingSetMb;
	memoryMegabytes(privateMb, workingSetMb);
	writeLine("  noHeartbeatMs=%u stage=%s phase=%s frame=%u beats=%u", stalledMs, currentStageName(),
		s_longWork ? "long-work" : "normal", (unsigned int)s_logicFrame, (unsigned int)s_heartbeat);
	writeLine("  privMB=%u wsMB=%u audioKB=%u/%u inUseEvict=%u lastCounts obj=%u draw=%u inMatch=%u", privateMb, workingSetMb,
		(unsigned int)(s_audioUsedBytes / 1024), (unsigned int)(s_audioMaxBytes / 1024), (unsigned int)s_inUseEvictions,
		(unsigned int)s_statsObjects, (unsigned int)s_statsDrawables, (unsigned int)s_statsInMatch);
}

/// A stall: this also catches stalls that recover.
static void writeStallSample(DWORD stalledMs)
{
	++s_samples;
	if (s_samples > MAX_SAMPLES_PER_SESSION)
	{
		writeLine("%T STALL %u ms stage=%s (no stack, session limit reached)", stalledMs, currentStageName());
		return;
	}

	writeLine("%T STALL sample %u, main thread stopped sending heartbeats", (unsigned int)s_samples);
	writeState(stalledMs);
	refreshModules();
	writeThread(s_mainThread, s_mainThreadId, " MAIN", MAIN_STACK_WORDS);
	s_mainCpuAtSample = threadCpuMs(s_mainThread);
	flushLog();
}

static void writeAllOtherThreads()
{
	const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		writeLine("  no thread list");
		return;
	}

	const DWORD selfProcess = ::GetCurrentProcessId();
	const DWORD selfThread = ::GetCurrentThreadId();
	unsigned int count = 0;
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	for (BOOL more = ::Thread32First(snapshot, &entry); more && count < MAX_THREADS; more = ::Thread32Next(snapshot, &entry))
	{
		if (entry.th32OwnerProcessID != selfProcess || entry.th32ThreadID == selfThread || entry.th32ThreadID == s_mainThreadId)
			continue;

		++count;
		if (s_openThread == nullptr)
			continue;

		HANDLE thread = s_openThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
		if (thread != nullptr)
		{
			writeThread(thread, entry.th32ThreadID, "", OTHER_STACK_WORDS);
			::CloseHandle(thread);
		}
	}
	::CloseHandle(snapshot);
	writeLine("  other threads listed=%u", count);
}

/// The hang report: text only. An in-process dump could turn a stall that would recover into a permanent freeze.
static void writeHangReport(DWORD stalledMs)
{
	++s_reports;
	if (s_reports > MAX_REPORTS_PER_SESSION)
	{
		writeLine("%T HANG %u ms stage=%s (no report, session limit reached)", stalledMs, currentStageName());
		return;
	}

	writeLine("%T HANG report %u", (unsigned int)s_reports);
	writeLine("  build=%s %s git=%s", __DATE__, __TIME__, GitShortSHA1);
	writeState(stalledMs);
	refreshModules();
	const DWORD mainCpu = threadCpuMs(s_mainThread);
	writeLine("  mainCpuMsSinceStallSample=%u (near 0 means blocked, near the elapsed time means spinning)", mainCpu - s_mainCpuAtSample);

	// The main thread first: it needs neither a snapshot nor a handle opened now.
	writeThread(s_mainThread, s_mainThreadId, " MAIN", MAIN_STACK_WORDS);
	flushLog();

	writeLine("  MODULES count=%u", s_moduleCount);
	for (unsigned int i = 0; i < s_moduleCount; ++i)
		writeLine("    %x %x %s", s_modules[i].base, s_modules[i].size, s_modules[i].name);

	writeAllOtherThreads();
	writeLine("%T HANG report %u text complete", (unsigned int)s_reports);
	flushLog();

}

//-------------------------------------------------------------------------------------------------
// The watchdog thread

//-------------------------------------------------------------------------------------------------
// Exception capture. The catch (...) around GameEngine::update unwinds the stack before the crash report is made, so the
// report no longer shows the failing code (the Alt-Tab report, D-274). A vectored handler sees the exception first, before
// any unwinding. It runs on the faulting thread, possibly with locks held or almost no stack left, so it only copies
// numbers into a static slot and never changes the outcome. The watchdog formats and writes the slot later.

struct CrashRecord
{
	volatile long claim; ///< The first InterlockedIncrement to read 1 owns the slot.
	volatile long ready; ///< Set once the slot is filled, for the watchdog.
	volatile DWORD ownerThread; ///< The thread filling this slot, 0 otherwise.
	DWORD code;
	DWORD address;
	DWORD threadId;
	DWORD infoCount;
	DWORD info0;
	DWORD info1;
	DWORD eax, ebx, ecx, edx, esi, edi, ebp, esp;
	DWORD wordCount;
	DWORD words[CRASH_STACK_WORDS];
};

static CrashRecord s_crashRecords[CRASH_SLOTS];
static volatile long s_crashDropped = 0;
static int s_crashWritten = 0;
static void* s_crashHandler = nullptr;
typedef ULONG (WINAPI *RemoveVectoredExceptionHandler_t)(void*);
static RemoveVectoredExceptionHandler_t s_removeCrashHandler = nullptr;

/// Copies what it can. The limits in the TIB do not promise every page is readable, so a failed read only shortens the copy.
static DWORD copyStackWords(DWORD* to, const DWORD* from, DWORD count)
{
	DWORD copied = 0;
#ifdef _MSC_VER
	__try
	{
		for (; copied < count; ++copied)
			to[copied] = from[copied];
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
#endif
	return copied;
}

static bool isReportedException(DWORD code)
{
	switch (code)
	{
		case 0xC0000005: // access violation
		case 0xC0000006: // in-page error
		case 0xC000001D: // illegal instruction
		case 0xC0000096: // privileged instruction
		case 0xC00000FD: // stack overflow
		case 0xC0000094: // integer divide by zero
		case 0xC0000095: // integer overflow
		case 0xC000008C: // array bounds exceeded
		case 0x80000002: // datatype misalignment
		case 0xC0000025: // noncontinuable
		case 0xC0000374: // heap corruption
			return true;
	}
	return false;
}

static LONG WINAPI crashCaptureHandler(EXCEPTION_POINTERS* info)
{
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	const bool cppThrow = code == 0xE06D7363;
	if (s_running == 0 || (!cppThrow && !isReportedException(code)))
		return EXCEPTION_CONTINUE_SEARCH;
	if (cppThrow && s_longWork != 0) // INI and map parsing throw and catch while loading.
		return EXCEPTION_CONTINUE_SEARCH;
	const DWORD threadId = ::GetCurrentThreadId();
	if (threadId == s_threadId)
		return EXCEPTION_CONTINUE_SEARCH;

	// A fault inside this handler, in the guarded stack copy, goes straight on to that copy's own __except.
	for (unsigned int active = 0; active < CRASH_SLOTS; ++active)
	{
		if (s_crashRecords[active].ownerThread == threadId)
			return EXCEPTION_CONTINUE_SEARCH;
	}

	CrashRecord* record = nullptr;
	for (unsigned int i = 0; i < CRASH_SLOTS && record == nullptr; ++i)
	{
		if (::InterlockedIncrement((LONG*)&s_crashRecords[i].claim) == 1)
			record = &s_crashRecords[i];
		else
			::InterlockedDecrement((LONG*)&s_crashRecords[i].claim);
	}
	if (record == nullptr)
	{
		::InterlockedIncrement((LONG*)&s_crashDropped);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	record->ownerThread = threadId;
	const CONTEXT* context = info->ContextRecord;
	record->code = code;
	record->address = (DWORD)info->ExceptionRecord->ExceptionAddress;
	record->threadId = threadId;
	record->infoCount = info->ExceptionRecord->NumberParameters;
	record->info0 = record->infoCount > 0 ? (DWORD)info->ExceptionRecord->ExceptionInformation[0] : 0;
	record->info1 = record->infoCount > 1 ? (DWORD)info->ExceptionRecord->ExceptionInformation[1] : 0;
	record->eax = context->Eax;
	record->ebx = context->Ebx;
	record->ecx = context->Ecx;
	record->edx = context->Edx;
	record->esi = context->Esi;
	record->edi = context->Edi;
	record->ebp = context->Ebp;
	record->esp = context->Esp;

	// Only the part of the stack that is in use, read from the limits of this thread in its TIB.
	DWORD stackBase = 0;
	DWORD stackLimit = 0;
#ifdef _MSC_VER
	__asm
	{
		mov eax, fs:[4]
		mov stackBase, eax
		mov eax, fs:[8]
		mov stackLimit, eax
	}
#endif
	DWORD count = 0;
	const DWORD esp = record->esp & ~(DWORD)3;
	if (code != 0xC00000FD && esp >= stackLimit && esp < stackBase) // A stack overflow leaves no room to spare here.
	{
		count = (stackBase - esp) / sizeof(DWORD);
		if (count > CRASH_STACK_WORDS)
			count = CRASH_STACK_WORDS;
		count = copyStackWords(record->words, (const DWORD*)esp, count);
	}
	record->wordCount = count;

	record->ownerThread = 0;
	::InterlockedExchange((LONG*)&record->ready, 1);
	return EXCEPTION_CONTINUE_SEARCH;
}

static void writeCrashRecords()
{
	bool wroteAny = false;
	for (unsigned int i = 0; i < CRASH_SLOTS; ++i)
	{
		CrashRecord& record = s_crashRecords[i];
		if (record.ready == 0)
			continue;

		if (s_crashWritten < MAX_CRASH_RECORDS_PER_SESSION)
		{
			++s_crashWritten;
			if (!wroteAny)
				refreshModules();
			wroteAny = true;
			writeLine("%T EXCEPTION %u code=%x tid=%u%s stageNow=%s frame=%u (seen first: the game may still handle it)",
				(unsigned int)s_crashWritten, record.code, record.threadId, record.threadId == s_mainThreadId ? " MAIN" : "",
				currentStageName(), (unsigned int)s_logicFrame);
			writeAddressField("at", record.address);
			if (record.infoCount >= 2)
				writeLine("    info0=%x info1=%x", record.info0, record.info1);
			writeLine("    eax=%x ebx=%x ecx=%x edx=%x esi=%x edi=%x", record.eax, record.ebx, record.ecx, record.edx, record.esi, record.edi);
			writeLine("    ebp=%x esp=%x stackCopied=%u", record.ebp, record.esp, (unsigned int)(record.wordCount * sizeof(DWORD)));
			unsigned int written = 0;
			for (DWORD w = 0; w < record.wordCount && written < CRASH_FRAMES_WRITTEN; ++w)
			{
				const ModuleEntry* module = findModule(record.words[w]);
				if (module != nullptr && looksLikeReturnAddress(record.words[w]))
				{
					writeLine("    +%x %s+%x", (unsigned int)(w * sizeof(DWORD)), module->name, record.words[w] - module->base);
					++written;
				}
			}
		}
		::InterlockedExchange((LONG*)&record.ready, 0);
		::InterlockedDecrement((LONG*)&record.claim);
	}

	const long dropped = ::InterlockedExchange((LONG*)&s_crashDropped, 0);
	if (dropped != 0)
		writeLine("%T %u exceptions not recorded, all slots busy", (unsigned int)dropped);
	if (wroteAny)
		flushLog();
}

static void drainQueue()
{
	writeCrashRecords();

	long read = s_queueRead;
	const long write = s_queueWrite;
	while (read != write)
	{
		const char* text = s_queue[(unsigned long)read & (QUEUE_SLOTS - 1)];
		writeLine("%s", text);
		++read;
		::InterlockedExchange((LONG*)&s_queueRead, read);
	}

	const long dropped = ::InterlockedExchange((LONG*)&s_droppedLines, 0);
	if (dropped != 0)
		writeLine("%T %u log lines dropped, queue full", (unsigned int)dropped);
}

static void writeMinuteLine()
{
	unsigned int privateMb, workingSetMb;
	memoryMegabytes(privateMb, workingSetMb);
	writeLine("%T MINUTE frame=%u obj=%u draw=%u privMB=%u wsMB=%u audioKB=%u/%u inUseEvict=%u",
		(unsigned int)s_logicFrame, (unsigned int)s_statsObjects, (unsigned int)s_statsDrawables, privateMb, workingSetMb,
		(unsigned int)(s_audioUsedBytes / 1024), (unsigned int)(s_audioMaxBytes / 1024), (unsigned int)s_inUseEvictions);
}

static void watchLoop()
{
	enum { STATE_HEALTHY, STATE_SAMPLED, STATE_REPORTED };

	int state = STATE_HEALTHY;
	DWORD lastWake = ::GetTickCount();
	DWORD lastBeatTick = lastWake;
	DWORD lastMinuteTick = lastWake;
	long lastBeat = s_heartbeat;

	while (::WaitForSingleObject(s_quit, WAKE_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		const DWORD now = ::GetTickCount();
		const DWORD slept = now - lastWake;
		lastWake = now;

		if (slept > WATCHDOG_GAP_MS)
		{
			// This thread did not run either, so a stopped heartbeat says nothing about the main thread.
			writeLine("%T WATCHDOG GAP %u ms: PC sleep, or the whole process was suspended", slept);
			lastBeatTick = now;
		}

		const long beat = s_heartbeat;
		if (beat != lastBeat)
		{
			if (state != STATE_HEALTHY)
			{
				writeLine("%T RECOVERED after %u ms, stage now %s frame=%u", now - lastBeatTick, currentStageName(), (unsigned int)s_logicFrame);
				flushLog();
				state = STATE_HEALTHY;
			}
			lastBeat = beat;
			lastBeatTick = now;
		}
		else if (s_crashing == 0)
		{
			const DWORD stalled = now - lastBeatTick;
			const bool longWork = s_longWork != 0;
			if (state == STATE_HEALTHY && stalled >= (longWork ? LONG_WORK_SAMPLE_MS : STALL_SAMPLE_MS))
			{
				writeStallSample(stalled);
				state = STATE_SAMPLED;
			}
			else if (state == STATE_SAMPLED && stalled >= (longWork ? LONG_WORK_REPORT_MS : HANG_REPORT_MS))
			{
				writeHangReport(stalled);
				state = STATE_REPORTED;
			}
		}

		drainQueue();

		if (s_statsReady != 0)
		{
			s_statsReady = 0;
			if (s_statsInMatch != 0)
				writeMinuteLine();
		}

		if (now - lastMinuteTick >= MINUTE_MS)
		{
			lastMinuteTick = now;
			HangWatch::s_statsRequested = 1;
			flushLog();
		}

		// Only just after a heartbeat, see the top of this file.
		if (state == STATE_HEALTHY && now == lastBeatTick)
			rotateLogIfNeeded();
	}
}

static const char* volatile s_endReason = "clean";

static DWORD WINAPI watchThreadProc(LPVOID)
{
#ifdef _MSC_VER
	// A bug in this diagnostic code must end only this thread, never the player's game.
	__try
	{
		watchLoop();
		drainQueue();
		writeLine("%T SESSION END reason=%s", s_endReason);
		flushLog();
		::CloseHandle(s_file);
		s_file = INVALID_HANDLE_VALUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
#else
	watchLoop();
#endif
	return 0;
}

//-------------------------------------------------------------------------------------------------
// The public side

void HangWatch::init(const char* userDataDir)
{
	if (s_running != 0 || userDataDir == nullptr)
		return;

	// The user data folder comes with a trailing backslash (GlobalData.cpp).
	unsigned int n = 0;
	putString(s_logDir, MAX_PATH - 32, n, userDataDir);
	putString(s_logDir, MAX_PATH - 32, n, "Logs\\");
	s_logDir[n] = 0;
	::CreateDirectoryA(s_logDir, nullptr);

	const HMODULE psapi = ::LoadLibraryA("psapi.dll");
	if (psapi != nullptr)
	{
		s_enumProcessModules = (EnumProcessModules_t)::GetProcAddress(psapi, "EnumProcessModules");
		s_getModuleBaseNameW = (GetModuleBaseNameW_t)::GetProcAddress(psapi, "GetModuleBaseNameW");
		s_getModuleInformation = (GetModuleInformation_t)::GetProcAddress(psapi, "GetModuleInformation");
		s_getProcessMemoryInfo = (GetProcessMemoryInfo_t)::GetProcAddress(psapi, "GetProcessMemoryInfo");
	}
	s_openThread = (OpenThread_t)::GetProcAddress(::GetModuleHandleA("kernel32.dll"), "OpenThread");
	s_ntQueryInformationThread = (NtQueryInformationThread_t)::GetProcAddress(::GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");

	openLog();
	if (s_file == INVALID_HANDLE_VALUE)
		return;
	noteUncleanPreviousSession();
	rotateLogIfNeeded();
	if (s_file == INVALID_HANDLE_VALUE)
		return;
	refreshModules();

	s_mainThreadId = ::GetCurrentThreadId();
	if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &s_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS))
		s_mainThread = nullptr;

	const char* exeBase = (const char*)::GetModuleHandleA(nullptr);
	const IMAGE_NT_HEADERS* ntHeaders = (const IMAGE_NT_HEADERS*)(exeBase + ((const IMAGE_DOS_HEADER*)exeBase)->e_lfanew);
	// RtlGetVersion, since GetVersionEx reports an old Windows version to a program without a matching manifest.
	OSVERSIONINFOW version;
	memset(&version, 0, sizeof(version));
	version.dwOSVersionInfoSize = sizeof(version);
	typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW*);
	const RtlGetVersion_t rtlGetVersion = (RtlGetVersion_t)::GetProcAddress(::GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
	if (rtlGetVersion != nullptr)
		rtlGetVersion(&version);
	SYSTEM_INFO systemInfo;
	::GetSystemInfo(&systemInfo);

	writeLine("%T SESSION START build=%s %s git=%s exeStamp=%x pid=%u mainTid=%u windows=%u.%u.%u cpus=%u",
		__DATE__, __TIME__, GitShortSHA1, (unsigned int)ntHeaders->FileHeader.TimeDateStamp, (unsigned int)::GetCurrentProcessId(),
		(unsigned int)s_mainThreadId, (unsigned int)version.dwMajorVersion, (unsigned int)version.dwMinorVersion,
		(unsigned int)version.dwBuildNumber, (unsigned int)systemInfo.dwNumberOfProcessors);

	s_quit = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
	if (s_quit == nullptr || s_mainThread == nullptr)
		return;

	s_running = 1;
	DX8Wrapper_DiagEvent = HangWatch::logEvent;

	s_thread = ::CreateThread(nullptr, 0, watchThreadProc, nullptr, 0, &s_threadId);
	if (s_thread == nullptr)
	{
		s_running = 0;
		DX8Wrapper_DiagEvent = nullptr;
		return;
	}
	// Exception capture (see crashCaptureHandler). Looked up by name: the VC6 headers do not declare it.
	typedef void* (WINAPI *AddVectoredExceptionHandler_t)(ULONG, LONG (WINAPI *)(EXCEPTION_POINTERS*));
	const AddVectoredExceptionHandler_t addHandler = (AddVectoredExceptionHandler_t)::GetProcAddress(::GetModuleHandleA("kernel32.dll"), "AddVectoredExceptionHandler");
	// Resolved here, since the lookup takes the loader lock, which a crash must not wait for.
	s_removeCrashHandler = (RemoveVectoredExceptionHandler_t)::GetProcAddress(::GetModuleHandleA("kernel32.dll"), "RemoveVectoredExceptionHandler");
	if (addHandler != nullptr)
		s_crashHandler = addHandler(1, crashCaptureHandler);

	// Above normal, so a main thread spinning at normal priority cannot keep it from running. It sleeps almost all the time.
	::SetThreadPriority(s_thread, THREAD_PRIORITY_ABOVE_NORMAL);
}

void HangWatch::shutdown(const char* reason)
{
	if (::InterlockedExchange((LONG*)&s_running, 0) == 0)
		return;

	DX8Wrapper_DiagEvent = nullptr;

	// s_running is now 0, so the handler does nothing. On a crash it simply stays installed until the process ends.
	if (s_crashHandler != nullptr && s_crashing == 0 && s_removeCrashHandler != nullptr)
	{
		s_removeCrashHandler(s_crashHandler);
		s_crashHandler = nullptr;
	}

	// A crash on the watchdog thread itself comes through here too, it cannot wait for itself.
	if (::GetCurrentThreadId() == s_threadId)
		return;

	// The watchdog writes the last lines itself, so a hung disk can only cost this wait, not hold up a crash report.
	// If it is stuck, the next session reports that this one had no end line.
	s_endReason = reason;
	::SetEvent(s_quit);
	::WaitForSingleObject(s_thread, SHUTDOWN_WAIT_MS);
}

void HangWatch::heartbeat(unsigned int logicFrame)
{
	s_logicFrame = logicFrame;
	s_longWork = 0;
	::InterlockedIncrement((LONG*)&s_heartbeat);
}

void HangWatch::idleHeartbeat()
{
	s_stage = Stage_Minimized;
	s_longWork = 0;
	::InterlockedIncrement((LONG*)&s_heartbeat);
}

void HangWatch::beginLongWork()
{
	s_longWork = 1;
}

void HangWatch::logEvent(const char* text)
{
	if (s_running == 0)
		return;

	const long write = s_queueWrite;
	if ((unsigned long)(write - s_queueRead) >= QUEUE_SLOTS)
	{
		::InterlockedIncrement((LONG*)&s_droppedLines);
		return;
	}

	unsigned int n = 0;
	char* slot = s_queue[(unsigned long)write & (QUEUE_SLOTS - 1)];
	putStamp(slot, QUEUE_LINE_BYTES - 1, n);
	putChar(slot, QUEUE_LINE_BYTES - 1, n, ' ');
	putString(slot, QUEUE_LINE_BYTES - 1, n, text);
	slot[n] = 0;
	::InterlockedExchange((LONG*)&s_queueWrite, write + 1);
}

void HangWatch::publishStats(unsigned int objects, unsigned int drawables, bool inMatch)
{
	s_statsObjects = objects;
	s_statsDrawables = drawables;
	s_statsInMatch = inMatch ? 1 : 0;
	s_statsRequested = 0;
	::InterlockedExchange((LONG*)&s_statsReady, 1);
}

void HangWatch::noteAudioCache(unsigned int usedBytes, unsigned int maxBytes)
{
	s_audioUsedBytes = usedBytes;
	s_audioMaxBytes = maxBytes;
}

void HangWatch::countInUseEviction()
{
	::InterlockedIncrement((LONG*)&s_inUseEvictions);
}

void HangWatch::noteCrash()
{
	s_crashing = 1;
}

#endif // RTS_HANGWATCH
