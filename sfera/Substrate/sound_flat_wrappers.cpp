// sound_flat_wrappers.cpp
// C ABI thunks to sound.dll. Resolution is isolated in SoundLibrary; gameplay code should not call GetProcAddress directly.
// Build this file as C++ and add it to the Win32 project.

#include <windows.h>
#include <stdint.h>

struct _D3DVECTOR
{
    float x;
    float y;
    float z;
};
typedef struct _D3DVECTOR D3DVECTOR;

struct _DS3DBUFFER;
typedef struct _DS3DBUFFER DS3DBUFFER;

namespace sfera::substrate::sound
{
    class SoundLibrary final
    {
    public:
        SoundLibrary() = default;
        SoundLibrary(const SoundLibrary&) = delete;
        SoundLibrary& operator=(const SoundLibrary&) = delete;
        ~SoundLibrary()
        {
            if (ownsModule_ && module_)
                FreeLibrary(module_);
        }

        FARPROC resolve(const char* name)
        {
            ensureLoaded();
            return module_ ? GetProcAddress(module_, name) : nullptr;
        }

        static SoundLibrary& instance()
        {
            static SoundLibrary library;
            return library;
        }

    private:
        void ensureLoaded()
        {
            if (module_)
                return;
            module_ = GetModuleHandleA("sound.dll");
            if (!module_)
            {
                module_ = LoadLibraryA("sound.dll");
                ownsModule_ = module_ != nullptr;
            }
        }

        HMODULE module_ = nullptr;
        bool ownsModule_ = false;
    };

    template <typename Function>
    Function resolveSoundProcedure(const char* decoratedName)
    {
        return reinterpret_cast<Function>(SoundLibrary::instance().resolve(decoratedName));
    }
}

extern "C" void* __cdecl CSound_CSound(void* self)
{
    typedef void (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("??0CSound@@QAE@XZ");
    if (fn)
        fn(self);
    return self;
}

extern "C" void __cdecl CSound_dtor_CSound(void* self)
{
    typedef void (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("??1CSound@@QAE@XZ");
    if (fn)
        fn(self);
}

extern "C" int __cdecl CSound_SetAllParameters(void* self, const DS3DBUFFER* params, int apply)
{
    typedef int (__thiscall *Fn)(void*, const DS3DBUFFER*, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetAllParameters@CSound@@QAEHPBU_DS3DBUFFER@@H@Z");
    return fn ? fn(self, params, apply) : 0;
}

extern "C" int __cdecl CSound_LoadSound(void* self, const char* fileName, unsigned int flags)
{
    typedef int (__thiscall *Fn)(void*, const char*, unsigned int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?LoadSound@CSound@@QAEHPBDK@Z");
    return fn ? fn(self, fileName, flags) : 0;
}

extern "C" int __cdecl CSound_SetVolume(void* self, float volume)
{
    typedef int (__thiscall *Fn)(void*, float);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetVolume@CSound@@QAEHM@Z");
    return fn ? fn(self, volume) : 0;
}

extern "C" int __cdecl CSound_SetPosition(void* self, float x, float y, float z, int apply)
{
    typedef int (__thiscall *Fn)(void*, float, float, float, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetPosition@CSound@@QAEHMMMH@Z");
    return fn ? fn(self, x, y, z, apply) : 0;
}

extern "C" int __cdecl CSound_SetVelocity(void* self, float x, float y, float z, int apply)
{
    typedef int (__thiscall *Fn)(void*, float, float, float, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetVelocity@CSound@@QAEHMMMH@Z");
    return fn ? fn(self, x, y, z, apply) : 0;
}

extern "C" float __cdecl CSound_GetPlayTimepos(void* self)
{
    typedef float (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?GetPlayTimepos@CSound@@QBEMXZ");
    return fn ? fn(self) : 0.0f;
}

extern "C" int __cdecl CSound_IsSoundPlaying(void* self)
{
    typedef int (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?IsSoundPlaying@CSound@@QBEHXZ");
    return fn ? fn(self) : 0;
}

extern "C" void __cdecl CSound_SetPlayTimepos(void* self, float timePos)
{
    typedef void (__thiscall *Fn)(void*, float);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetPlayTimepos@CSound@@QAEXM@Z");
    if (fn)
        fn(self, timePos);
}

extern "C" int __cdecl CSoundInterface_UpdateSettings(void* self)
{
    typedef int (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?UpdateSettings@CSoundInterface@@QAEHXZ");
    return fn ? fn(self) : 0;
}

extern "C" void __cdecl CSoundListener_GetOrientation(void* self, D3DVECTOR* front, D3DVECTOR* top)
{
    typedef void (__thiscall *Fn)(void*, D3DVECTOR*, D3DVECTOR*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?GetOrientation@CSoundListener@@QBEXPAU_D3DVECTOR@@0@Z");
    if (fn)
        fn(self, front, top);
}

extern "C" int __cdecl CSoundListener_SetPosition(void* self, float x, float y, float z, int apply)
{
    typedef int (__thiscall *Fn)(void*, float, float, float, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetPosition@CSoundListener@@QAEHMMMH@Z");
    return fn ? fn(self, x, y, z, apply) : 0;
}

extern "C" int __cdecl CSoundListener_SetVelocity(void* self, float x, float y, float z, int apply)
{
    typedef int (__thiscall *Fn)(void*, float, float, float, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetVelocity@CSoundListener@@QAEHMMMH@Z");
    return fn ? fn(self, x, y, z, apply) : 0;
}

extern "C" int __cdecl CSoundListener_SetOrientation(void* self, const D3DVECTOR* front, const D3DVECTOR* top, int apply)
{
    typedef int (__thiscall *Fn)(void*, const D3DVECTOR*, const D3DVECTOR*, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetOrientation@CSoundListener@@QAEHABU_D3DVECTOR@@0H@Z");
    return fn ? fn(self, front, top, apply) : 0;
}

extern "C" void __cdecl CSoundStream_SetDecodeSignal(void* self, float signal)
{
    typedef void (__thiscall *Fn)(void*, float);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetDecodeSignal@CSoundStream@@QAEXM@Z");
    if (fn)
        fn(self, signal);
}

extern "C" int __cdecl CSoundStream_SeekToTime(void* self, float timePos)
{
    typedef int (__thiscall *Fn)(void*, float);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SeekToTime@CSoundStream@@QAEHM@Z");
    return fn ? fn(self, timePos) : 0;
}

extern "C" void __cdecl CSoundStream_SetPlaySignal(void* self, float signal)
{
    typedef void (__thiscall *Fn)(void*, float);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SetPlaySignal@CSoundStream@@QAEXM@Z");
    if (fn)
        fn(self, signal);
}

extern "C" void __cdecl CSoundStream_Stop(void* self)
{
    typedef void (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?Stop@CSoundStream@@QAEXXZ");
    if (fn)
        fn(self);
}

extern "C" int __cdecl CSoundStream_IsStreamPlaying(void* self)
{
    typedef int (__thiscall *Fn)(void*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?IsStreamPlaying@CSoundStream@@QBEHXZ");
    return fn ? fn(self) : 0;
}

extern "C" int __cdecl CSoundStream_PlayEx(void* self, float fadeTime, int flags)
{
    typedef int (__thiscall *Fn)(void*, float, int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?PlayEx@CSoundStream@@QAEHMH@Z");
    return fn ? fn(self, fadeTime, flags) : 0;
}

extern "C" void __cdecl SI_SetHardwareMixing(int enabled)
{
    typedef void (__cdecl *Fn)(bool);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_SetHardwareMixing@@YAX_N@Z");
    if (fn)
        fn(enabled != 0);
}

extern "C" int __cdecl SI_GetHardwareMixing(void)
{
    typedef bool (__cdecl *Fn)(void);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_GetHardwareMixing@@YA_NXZ");
    return fn ? (fn() ? 1 : 0) : 0;
}

extern "C" void* __cdecl SI_GetInterface(void)
{
    typedef void* (__cdecl *Fn)(void);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_GetInterface@@YAPAVCSoundInterface@@XZ");
    return fn ? fn() : NULL;
}

extern "C" void* __cdecl SI_CreateInterface(HWND hwnd, int device, unsigned int samplesPerSec, unsigned int flags)
{
    typedef void* (__cdecl *Fn)(HWND, int, unsigned int, unsigned int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_CreateInterface@@YAPAVCSoundInterface@@PAUHWND__@@HKK@Z");
    return fn ? fn(hwnd, device, samplesPerSec, flags) : NULL;
}

extern "C" void __cdecl SI_Close(void)
{
    typedef void (__cdecl *Fn)(void);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_Close@@YAXXZ");
    if (fn)
        fn();
}

extern "C" void __cdecl SI_SetLogFile(const char* fileName)
{
    typedef void (__cdecl *Fn)(const char*);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_SetLogFile@@YAXPBD@Z");
    if (fn)
        fn(fileName);
}

extern "C" int __cdecl SI_GetStreamVolume(void)
{
    typedef int (__cdecl *Fn)(void);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_GetStreamVolume@@YAHXZ");
    return fn ? fn() : 0;
}

extern "C" void __cdecl SI_SetStreamVolume(int volume)
{
    typedef void (__cdecl *Fn)(int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_SetStreamVolume@@YAXH@Z");
    if (fn)
        fn(volume);
}

extern "C" unsigned int __cdecl SI_StreamCreateFile(const char* fileName, unsigned int flags)
{
    typedef unsigned int (__cdecl *Fn)(const char*, unsigned int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_StreamCreateFile@@YAKPBDK@Z");
    return fn ? fn(fileName, flags) : 0;
}

extern "C" void __cdecl SI_StreamFree(unsigned int streamId)
{
    typedef void (__cdecl *Fn)(unsigned int);
    Fn fn = sfera::substrate::sound::resolveSoundProcedure<Fn>("?SI_StreamFree@@YAXK@Z");
    if (fn)
        fn(streamId);
}

