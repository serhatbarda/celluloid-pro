// Celluloid Pro - small platform helpers for the OFX shell (paths, folders, file names).
// All strings are UTF-8; on Windows they are converted to UTF-16 at the OS boundary.
#pragma once
#include <string>

namespace cel { namespace ofxp {

// The user's documents folder (macOS/Linux: $HOME/Documents or XDG_DOCUMENTS_DIR;
// Windows: the Known Folder "Documents"). Never empty (falls back to the home/current folder).
std::string documentsDirectory();

// "<Documents>/Celluloid LUTs/" with the platform's separator and a trailing separator.
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

}} // namespace cel::ofxp
