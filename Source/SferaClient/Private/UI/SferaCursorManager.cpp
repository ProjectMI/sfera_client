#include "SferaCursorManager.h"
#include "SferaResourceManager.h"

bool SferaCursorManager::Initialize(const SferaResourceManager& Resources, bool bPreferHardwareCursor)
{
    bHardwareCursor = bPreferHardwareCursor;
    ActiveCursor = bHardwareCursor ? LoadCursorFromResources(Resources) : nullptr;
    if (!ActiveCursor)
    {
        ActiveCursor = LoadCursorA(nullptr, IDC_ARROW);
        ActiveCursorName = "system-arrow";
    }
    SetCursor(ActiveCursor);
    return ActiveCursor != nullptr;
}

void SferaCursorManager::Shutdown()
{
    ActiveCursor = nullptr;
    ActiveCursorName.clear();
}

void SferaCursorManager::SetActiveCursor(HCURSOR Cursor)
{
    ActiveCursor = Cursor ? Cursor : LoadCursorA(nullptr, IDC_ARROW);
    SetCursor(ActiveCursor);
}

HCURSOR SferaCursorManager::GetActiveCursor() const
{
    return ActiveCursor;
}

HCURSOR SferaCursorManager::LoadCursorFromResources(const SferaResourceManager& Resources)
{
    std::vector<const SferaResourceRecord*> CursorImages = Resources.FindByKind(ESferaResourceKind::CursorImage);
    for (const SferaResourceRecord* Record : CursorImages)
    {
        if (!Record || !Record->bExists)
        {
            continue;
        }

        HANDLE Image = LoadImageA(nullptr, Record->DiskPath.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
        if (Image)
        {
            ActiveCursorName = Record->LogicalPath;
            return reinterpret_cast<HCURSOR>(Image);
        }
    }

    return nullptr;
}
