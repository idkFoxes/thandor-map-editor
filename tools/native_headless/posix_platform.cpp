// OS/presentation boundary. Original arena, package decoder and gameplay remain intact.
#include <thandor/thandor.h>
#include <thandor/platform/bootstrap/image.h>
#ifndef THANDOR_WINDOWS_HEADLESS
#include <sys/mman.h>
#include <execinfo.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>
static std::string path8(const uint16_t *p){std::string s;while(*p){s+=*p=='\\'?'/':char(*p);++p;}return s;}
extern "C" HANDLE HeapCreate(DWORD,SIZE_T,SIZE_T){return reinterpret_cast<HANDLE>(1);}
#ifdef THANDOR_WINDOWS_HEADLESS
extern "C" void* VirtualAlloc(void*, SIZE_T, DWORD, DWORD);
extern "C" BOOL VirtualFree(void*, SIZE_T, DWORD);
extern "C" BOOL MoveFileExA(const char*, const char*, DWORD);
extern "C" DWORD GetLastError();
extern "C" void Sleep(DWORD);
extern "C" LPVOID HeapAlloc(HANDLE, DWORD, SIZE_T bytes) {
    for (uintptr_t address = 0x10000000; address + bytes < 0x80000000; address += 0x08000000) {
        if (auto* region = VirtualAlloc(reinterpret_cast<void*>(address), bytes, 0x3000, 0x04)) return region;
    }
    return nullptr;
}
extern "C" BOOL HeapFree(HANDLE, DWORD, LPVOID region) { return VirtualFree(region, 0, 0x8000); }
bool replace_jandrenator_file(const char* from, const char* to) {
    // An open reader can briefly deny replacement on Windows.
    for (unsigned attempt = 0; attempt < 25; ++attempt) {
        if (MoveFileExA(from, to, 1)) return true;
        const auto error = GetLastError();
        if (error != 5 && error != 32) return false;
        Sleep(2);
    }
    return false;
}
#else
extern "C" LPVOID HeapAlloc(HANDLE,DWORD,SIZE_T n){void *p=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);if(p==MAP_FAILED||reinterpret_cast<uintptr_t>(p)+n>0x80000000ULL)std::abort();return p;}
extern "C" BOOL HeapFree(HANDLE,DWORD,LPVOID p){return munmap(p,ARENA_HEAP_RESERVE_BYTES)==0;}
#endif
extern "C" BOOL HeapDestroy(HANDLE){return 1;}
[[noreturn]] void FatalError_ShowAndExit(uintptr_t error) {
    std::fprintf(stderr, "FATAL native error 0x%llx\n", static_cast<unsigned long long>(error));
#ifndef THANDOR_WINDOWS_HEADLESS
    void* frames[32];
    const int count = backtrace(frames, 32);
    backtrace_symbols_fd(frames, count, 2);
#endif
    std::exit(3);
}
// Narrow fallback for UTF16 diagnostics, avoiding libc's incompatible 32-bit wchar_t.
void Thandor_Log(const char *fmt,...){std::string f=fmt;for(size_t p=0;(p=f.find("%ls",p))!=std::string::npos;)f.replace(p,3,"%s");va_list args;va_start(args,fmt);std::fputs("[native] ",stderr);std::vfprintf(stderr,f.c_str(),args);std::fputc(10,stderr);va_end(args);}
void Thandor_LogStack(const char *message,uint32_t code){std::fprintf(stderr,"[stack] %s %x\n",message,code);}
static uint32_t Open(FileSystemOpenFlags,uint16_t *p,void **out){auto s=path8(p);FILE *h=std::fopen(s.c_str(),"rb");if(!h)return FATAL_ERROR_FILE_ACCESS_FAILED;*out=h;return 0;}
static void Close(void *h){std::fclose(static_cast<FILE*>(h));}
static uint32_t Read(uint32_t n,void *dst,void *h){return std::fread(dst,1,n,static_cast<FILE*>(h))==n?0:FATAL_ERROR_FILE_READ_FAILED;}
static uint32_t Seek(FileSystemSeekOrigin origin,uint32_t n,void *h){return std::fseek(static_cast<FILE*>(h),int32_t(n),int(origin))==0?0:FATAL_ERROR_FILE_SEEK_FAILED;}
static bool Size(void *h,uint32_t *n){auto f=static_cast<FILE*>(h);auto pos=std::ftell(f);std::fseek(f,0,SEEK_END);auto size=std::ftell(f);std::fseek(f,pos,SEEK_SET);*n=size;return size>=0;}
static bool Position(void *h,uint32_t *n){long p=std::ftell(static_cast<FILE*>(h));*n=p;return p>=0;}
FileSystemOpenProc *g_FileSystemOpen=Open;FileSystemCloseProc *g_FileSystemClose=Close;FileSystemReadExactProc *g_FileSystemReadExact=Read;FileSystemSeekProc *g_FileSystemSeek=Seek;FileSystemGetSizeProc *g_FileSystemGetSize=Size;FileSystemGetPositionProc *g_FileSystemGetPosition=Position;
uint16_t g_FileSystemCombinedPathScratchUtf16[THANDOR_PATH_CAPACITY]={};uint16_t g_ExecutableDirectoryUtf16[256]={};
bool FileSystem_LoadWholeFileNearExecutable(uint16_t *path,void **out,uint32_t *n,uint32_t *err){void *h=nullptr;uint32_t e=Open(FileSystemOpenFlags::FILESYSTEM_OPEN_NONE,path,&h);if(e){if(err)*err=e;return false;}uint32_t bytes=0;Size(h,&bytes);void *p=nullptr;e=g_MemoryApi.alloc(bytes,&p);if(!e)e=Read(bytes,p,h);Close(h);if(e){if(p)g_MemoryApi.free(p);if(err)*err=e;return false;}*out=p;if(n)*n=bytes;return true;}
[[noreturn]] void Thandor_Ptr32Overflow(uintptr_t p){std::fprintf(stderr,"Ptr32 overflow %llx\n",static_cast<unsigned long long>(p));std::abort();}
void MoviePlayback_AdvanceScheduledFrameAndTick(){++g_MoviePlaybackScheduleCounter;}
void InGameHud_UpdateCurrentFactionMetricCache(){}void InGameBuildCatalog_RebuildGrid(UiNodeBase*){}void InGameSpecialBuildCatalog_RebuildGrid(UiNodeBase*){}void InGameArmyStock_RebuildGrid(UiNodeBase*){}void InGameOtherPlayerCommand_RebuildTargetEntries(UiNodeBase*){}void InGameSelectionDetailPanel_Rebuild(){}
static uint32_t Enumerate(FileSystemEnumerationMode,uint32_t,uint32_t,uint8_t*,uint8_t*){return 0;}
FileSystemEnumerateDirectoryOrVolumeEntriesProc *g_FileSystemEnumerateDirectoryOrVolumeEntries=Enumerate;
UiRootCallbacks g_InGameUiRootCallbacks={};SoundVoice *g_InGameActiveEffectVoice=nullptr;uint32_t g_InGameEffectsEnabled=0;SoundVoice *g_InGameActiveMusicVoice=nullptr;uint32_t g_InGameMusicNextTrackCountdown=0;
void InGameTechnologyPanel_Rebuild(UiRootNode*){}
GraphicsTextureResource *ArmyRuntime_RenderPreviewTexture(GraphicsPixelDimension,GraphicsPixelDimension,FactionRuntimeIndex,PckArmyAssetIdCatalog,WorldRuntimeContext*){return nullptr;}
// Verbatim constant from omitted preview.cpp; also used by native aircraft hangars.
const uint32_t g_InGamePointerModePreviewArmyIds[8]={0,240,242,240,244,240,242,240};
static_assert(sizeof(ArmyRuntimeSlot)==0x120);static_assert(sizeof(ModelRuntimeSlot)==0x200);static_assert(sizeof(GameFactionRuntimeRecord)==0x740);static_assert(sizeof(FieldGridCell)==0x80);static_assert(sizeof(WorldObjectRecord)==0x100);static_assert(sizeof(wchar_t)==2);

#ifdef THANDOR_JANDRENATOR_HEADLESS
uint32_t g_LocalPlayerRuntimeId = 0;

uint32_t FileSystem_WriteBufferToPath(FileIoByteCount bytes, void* source, uint16_t* path) {
    const auto name = path8(path);
    auto* file = std::fopen(name.c_str(), "wb");
    if (!file) return FATAL_ERROR_FILE_ACCESS_FAILED;
    const bool complete = std::fwrite(source, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && complete ? 0 : FATAL_ERROR_FILE_ACCESS_FAILED;
}

void Thandor_LogPlayer(const char* format, ...) {
    auto* file = std::fopen("player.log", "a");
    if (!file) return;
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fclose(file);
}
#endif
