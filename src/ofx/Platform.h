// Reafilm Pro - small platform helpers for the OFX shell (paths, folders, file names).
// All strings are UTF-8; on Windows they are converted to UTF-16 at the OS boundary.
#pragma once
#include <string>
#include <vector>

namespace cel { namespace ofxp {

// The user's documents folder (macOS/Linux: $HOME/Documents or XDG_DOCUMENTS_DIR;
// Windows: the Known Folder "Documents"). Never empty (falls back to the home/current folder).
std::string documentsDirectory();

// "<Documents>/Reafilm LUTs/" with the platform's separator and a trailing separator.
std::string defaultLutDirectory();

// Expands a leading "~" to the home folder and trims surrounding white space and quotes
// (users often paste quoted paths). Empty input stays empty.
std::string normalizeUserPath(const std::string& path);

// Joins a directory and a file name with exactly one separator.
std::string joinPath(const std::string& dir, const std::string& file);

// Creates the directory and all parents. Returns false + err if it does not exist afterwards.
bool ensureDirectory(const std::string& dir, std::string* err);

// Turns a UI label into a safe file-name component: runs of anything but [A-Za-z0-9] become a
// single '-', leading/trailing '-' removed ("CN 200T Studio Tungsten" -> "CN-200T-Studio-Tungsten").
std::string fileComponent(const std::string& label);

// Value of an environment variable ("" if unset).
std::string envVar(const char* name);

// Product setting from the environment: REAFILM_<suffix>, or the pre-rename CELLULOID_<suffix>
// when the new name is unset or empty (e.g. productEnv("LOG")).
std::string productEnv(const char* suffix);

// v1.3 P1 user presets: the per-user preset folder (not created here). macOS: ~/Library/Application
// Support/Reafilm/Presets, Windows: %APPDATA%\Reafilm\Presets, Linux: $XDG_CONFIG_HOME (or ~/.config)
// /reafilm/presets. REAFILM_PRESET_DIR (or CELLULOID_PRESET_DIR) overrides it.
std::string userPresetDirectory();
// The same folder under the pre-rename product name (Celluloid / celluloid), read as a fallback;
// "" when the override is set (an explicit folder has no legacy twin).
std::string legacyUserPresetDirectory();

// Whole-file text IO (UTF-8 paths). readTextFile returns false when the file cannot be opened;
// writeTextFile writes a temporary file next to the target and renames it over the target.
bool fileExists(const std::string& path);
bool readTextFile(const std::string& path, std::string& out);
bool writeTextFile(const std::string& path, const std::string& text, std::string* err);
// File names (not paths) of the regular files in dir ending in `ext` (case-insensitive), sorted.
std::vector<std::string> listFiles(const std::string& dir, const std::string& ext);

}} // namespace cel::ofxp
