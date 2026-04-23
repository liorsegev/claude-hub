#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <optional>

namespace ch {

// Opens the Windows "Pick a folder" common dialog (IFileDialog + FOS_PICKFOLDERS)
// modal to `owner`. Returns the chosen path, or nullopt if the user cancelled.
// `initial` may be empty — in that case the dialog opens at whatever Windows
// considers the default location.
std::optional<std::filesystem::path>
pick_folder(HWND owner, const std::filesystem::path& initial);

}
