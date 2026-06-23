#pragma once

template <typename T>
inline void SafeRelease(T*& value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}
