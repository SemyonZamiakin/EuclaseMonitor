// Small shared helpers: UTF-8 <-> UTF-16, file IO, well-known folders, log file. No iostream (keeps RAM and startup small).
#pragma once
#include <string>
#include <vector>

namespace qm {

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

bool readFile(const std::wstring& path, std::string& out, size_t maxBytes = 8u * 1024 * 1024);
bool writeFileAtomic(const std::wstring& path, const std::string& data);
bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);

std::wstring exeDir();
std::wstring appDataDir();                          // %APPDATA%\QuasarMonitor (created on demand)
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::vector<std::wstring> listSubdirs(const std::wstring& dir);

// Appends a line to %APPDATA%\QuasarMonitor\monitor.log (capped at ~256 KB, then truncated).
void logLine(const std::string& msg);

}  // namespace qm
