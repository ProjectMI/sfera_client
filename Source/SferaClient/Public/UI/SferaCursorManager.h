#pragma once
#include "SferaBase.h"

class SferaResourceManager;

class SferaCursorManager
{
public:
    bool Initialize(const SferaResourceManager& Resources, bool bPreferHardwareCursor);
    void Shutdown();
    void SetActiveCursor(HCURSOR Cursor);
    HCURSOR GetActiveCursor() const;
    const std::string& GetActiveCursorName() const { return ActiveCursorName; }

private:
    HCURSOR LoadCursorFromResources(const SferaResourceManager& Resources);

private:
    // Original error observed: CCursorManager::GetActiveCursor() when current cursor is not set.
    HCURSOR ActiveCursor = nullptr;
    std::string ActiveCursorName = "system-arrow";
    bool bHardwareCursor = true;
};
