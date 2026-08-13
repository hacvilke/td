#include "asset_browser.h"
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace td {
void AssetBrowser::render(GuiContext& gui) {
    (void)gui;
    if (!visible) return;

    // Enumerate files in currentPath using Win32 API
    WIN32_FIND_DATAA findData;
    char searchPath[600];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", currentPath);

    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (findData.cFileName[0] == '.') continue;
        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // Filter by type
        if (!isDir && filterType > 0) {
            const char* ext = strrchr(findData.cFileName, '.');
            if (ext) {
                bool match = false;
                if (filterType == 1 && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0)) match = true;
                if (filterType == 2 && strcmp(ext, ".obj") == 0) match = true;
                if (filterType == 3 && strcmp(ext, ".wav") == 0) match = true;
                if (filterType == 4 && strcmp(ext, ".td") == 0) match = true;
                if (filterType == 5 && (strcmp(ext, ".vert") == 0 || strcmp(ext, ".frag") == 0)) match = true;
                if (!match) continue;
            }
        }

        // Search filter
        if (filterText[0] != '\0' && !strstr(findData.cFileName, filterText)) continue;

        // In real implementation: draw file icon + name using GuiContext
        (void)isDir;

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}
} // namespace td
