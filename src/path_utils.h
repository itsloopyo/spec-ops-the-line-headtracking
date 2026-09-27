#pragma once

#include <string>

namespace SpecOpsTheLineHeadTracking {

// Directory this DLL was loaded from, with a trailing separator, or an empty
// string when the module path could not be resolved. Wide throughout: the real
// path is UTF-16, and GetModuleFileNameA would replace anything outside the
// active ANSI codepage with '?' before we ever saw it.
std::wstring GetModuleDirectoryW();

// Wide path to a file beside this DLL. Empty when the directory is unknown.
std::wstring GetModulePathW(const char* filename);

// The ANSI path the pre-canonical builds opened @p path by, for the legacy import: the path
// itself where the active ANSI codepage holds every character of its folder, else with the
// folder's 8.3 short name. Empty where neither gives a path; those builds did not start there.
// Never a bare filename, which GetPrivateProfileString would resolve against the Windows
// directory.
std::string LegacyAnsiPath(const std::wstring& path);

}
