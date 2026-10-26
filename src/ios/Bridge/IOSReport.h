#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// EspressU-Report.txt: the one file to send for a bug, a crash or a performance comparison.
//
// One file in Documents/Logs for every app session, appended to until it is deleted. Each session holds
// the device and settings, every log line, memory samples, crash reports with backtraces, a performance
// summary per game with a one-line COMPARE entry, and at its top how the session before it ended.
//
// Writes are plain write() calls on one descriptor, so the crash handler can use it too.

struct IOSReportPerfWindow
{
	double seconds = 0;
	double hostFps = 0;
	double guestFps = 0;
	double speedPct = 0;
	double ppcRunPct = 0;
	double gpuThreadBusyPct = 0;
	double metalGpuMs = 0;
	double metalGpuBusyPct = 0;
	int thermal = 0;   // 0 nominal .. 3 critical
	int bottleneck = 0; // PerfTelemetry verdict
	uint64_t availableMB = 0;
	uint64_t footprintMB = 0;
};

// Rotates the files of earlier sessions and opens this session's report. Called once, very early.
void IOSReport_OpenEarly();
// The descriptor the crash handler writes to as well; -1 when the report could not be opened.
int IOSReport_Fd();

// Raw text, written as is (log tee and crash handler).
void IOSReport_Write(const char* text, size_t length);
// One timestamped line.
void IOSReport_Line(const std::string& line);
void IOSReport_Section(const std::string& title);

// Free text blocks the front end and the bridge provide.
void IOSReport_SetDevice(const std::string& text);
void IOSReport_SetSettings(const std::string& text);

void IOSReport_TitleStarted(uint64_t titleId, const std::string& name);
void IOSReport_NotePerfWindow(const IOSReportPerfWindow& window);
void IOSReport_TitleStopped(const char* reason);

std::string IOSReport_Path();
std::string IOSReport_PreviousPath(); // empty: there is only one report file
// Empties the report; the running session goes on writing into it.
void IOSReport_Clear();
