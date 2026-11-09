#ifndef THANDOR_WIN32_MIN_H
#define THANDOR_WIN32_MIN_H
#ifdef __cplusplus
extern "C" {
#endif
#define WINAPI __stdcall
#define CALLBACK __stdcall
#define DLLEXPORT __declspec(dllexport)
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned int UINT; typedef signed int s32; typedef unsigned long ULONG; typedef long LONG; typedef int BOOL; typedef void* HANDLE; typedef HANDLE HMODULE; typedef HANDLE HINSTANCE; typedef HANDLE HWND; typedef void* LPVOID; typedef const char* LPCSTR; typedef char* LPSTR; typedef const u16* LPCWSTR; typedef u16* LPWSTR; typedef unsigned long DWORD; typedef unsigned short WORD; typedef unsigned char BYTE; typedef unsigned long SIZE_T; typedef void (*FARPROC)(void);
#define TRUE 1
#define FALSE 0
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define PAGE_EXECUTE_READWRITE 0x40u
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define HEAP_ZERO_MEMORY 0x00000008u
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define DM_BITSPERPEL 0x00040000u
#define DM_PELSWIDTH  0x00080000u
#define DM_PELSHEIGHT 0x00100000u
#define MB_OK 0u
#define MB_ICONERROR 0x10u
#define SW_SHOW 5
#define PM_REMOVE 0x0001u
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096u

typedef struct FLOATING_SAVE_AREA {
 DWORD ControlWord,StatusWord,TagWord,ErrorOffset,ErrorSelector,DataOffset,DataSelector;
 BYTE RegisterArea[80]; DWORD Cr0NpxState;
} FLOATING_SAVE_AREA;
typedef struct CONTEXT {
 DWORD ContextFlags; DWORD Dr0,Dr1,Dr2,Dr3,Dr6,Dr7; FLOATING_SAVE_AREA FloatSave;
 DWORD SegGs,SegFs,SegEs,SegDs; DWORD Edi,Esi,Ebx,Edx,Ecx,Eax;
 DWORD Ebp,Eip,SegCs,EFlags,Esp,SegSs; BYTE ExtendedRegisters[512];
} CONTEXT;
typedef struct EXCEPTION_RECORD {
 DWORD ExceptionCode,ExceptionFlags; struct EXCEPTION_RECORD *ExceptionRecord; LPVOID ExceptionAddress;
 DWORD NumberParameters; SIZE_T ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD;
typedef struct EXCEPTION_POINTERS { EXCEPTION_RECORD *ExceptionRecord; CONTEXT *ContextRecord; } EXCEPTION_POINTERS;
typedef LONG (WINAPI *LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS*);

typedef struct RECT { LONG left, top, right, bottom; } RECT;
typedef struct SYSTEMTIME { WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } SYSTEMTIME;
typedef struct FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef struct POINTL { LONG x; LONG y; } POINTL;
typedef struct POINT { LONG x; LONG y; } POINT;
typedef struct MSG { HWND hwnd; UINT message; u32 wParam; s32 lParam; DWORD time; POINT pt; } MSG;
typedef struct DEVMODEA {
 char dmDeviceName[32]; WORD dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; DWORD dmFields;
 union { struct { short dmOrientation, dmPaperSize, dmPaperLength, dmPaperWidth, dmScale, dmCopies, dmDefaultSource, dmPrintQuality; } p; struct { POINTL dmPosition; DWORD dmDisplayOrientation, dmDisplayFixedOutput; } d; } u1;
 short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate; char dmFormName[32]; WORD dmLogPixels; DWORD dmBitsPerPel, dmPelsWidth, dmPelsHeight; union { DWORD dmDisplayFlags; DWORD dmNup; } u2; DWORD dmDisplayFrequency, dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
} DEVMODEA;

HMODULE WINAPI LoadLibraryA(LPCSTR);
BOOL WINAPI FreeLibrary(HMODULE);
FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR);
HMODULE WINAPI GetModuleHandleA(LPCSTR);
DWORD WINAPI GetModuleFileNameA(HMODULE,LPSTR,DWORD);
DWORD WINAPI GetModuleFileNameW(HMODULE,LPWSTR,DWORD);
LPWSTR WINAPI GetCommandLineW(void);
DWORD WINAPI GetFileAttributesW(LPCWSTR);
DWORD WINAPI GetLastError(void);
HANDLE WINAPI CreateFileW(LPCWSTR,DWORD,DWORD,LPVOID,DWORD,DWORD,HANDLE);
BOOL WINAPI ReadFile(HANDLE,LPVOID,DWORD,DWORD*,LPVOID);
BOOL WINAPI WriteFile(HANDLE,const void*,DWORD,DWORD*,LPVOID);
BOOL WINAPI CloseHandle(HANDLE);
BOOL WINAPI DeleteFileW(LPCWSTR);
BOOL WINAPI MoveFileW(LPCWSTR,LPCWSTR);
BOOL WINAPI MoveFileExW(LPCWSTR,LPCWSTR,DWORD);
BOOL WINAPI CreateDirectoryW(LPCWSTR,LPVOID);
DWORD WINAPI GetFileSize(HANDLE,DWORD*);
BOOL WINAPI GetFileTime(HANDLE,FILETIME*,FILETIME*,FILETIME*);
BOOL WINAPI SetFileTime(HANDLE,const FILETIME*,const FILETIME*,const FILETIME*);
void WINAPI GetLocalTime(SYSTEMTIME*);
void WINAPI ExitProcess(unsigned int);
BOOL WINAPI GetComputerNameW(LPWSTR,DWORD*);
s32 WINAPI GetPrivateProfileIntW(LPCWSTR,LPCWSTR,s32,LPCWSTR);
DWORD WINAPI GetPrivateProfileStringW(LPCWSTR,LPCWSTR,LPCWSTR,LPWSTR,DWORD,LPCWSTR);
BOOL WINAPI WritePrivateProfileStringW(LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR);
BOOL WINAPI VirtualProtect(LPVOID,SIZE_T,DWORD,DWORD*);
BOOL WINAPI FlushInstructionCache(HANDLE,LPCSTR,SIZE_T);
HANDLE WINAPI GetCurrentProcess(void);

DWORD WINAPI GetCurrentProcessId(void);
DWORD WINAPI GetCurrentThreadId(void);
DWORD WINAPI GetTickCount(void);
BOOL WINAPI FlushFileBuffers(HANDLE);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER);
BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
LONG WINAPI InterlockedExchange(volatile LONG*,LONG);
HANDLE WINAPI GetProcessHeap(void);
LPVOID WINAPI HeapAlloc(HANDLE,DWORD,SIZE_T);
BOOL WINAPI HeapFree(HANDLE,DWORD,LPVOID);
LPVOID WINAPI VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);
BOOL WINAPI VirtualFree(LPVOID,SIZE_T,DWORD);
BOOL WINAPI EnumDisplaySettingsA(LPCSTR,DWORD,DEVMODEA*);
BOOL WINAPI GetClientRect(HWND,RECT*);
int WINAPI MessageBoxA(HWND,LPCSTR,LPCSTR,unsigned int);
HWND WINAPI CreateWindowExW(DWORD,LPCWSTR,LPCWSTR,DWORD,s32,s32,s32,s32,HWND,HANDLE,HINSTANCE,LPVOID);
BOOL WINAPI DestroyWindow(HWND);
BOOL WINAPI ShowWindow(HWND,s32);
BOOL WINAPI UpdateWindow(HWND);
BOOL WINAPI SetWindowTextW(HWND,LPCWSTR);
LONG WINAPI SendMessageW(HWND,UINT,u32,s32);
BOOL WINAPI PeekMessageW(MSG*,HWND,UINT,UINT,UINT);
BOOL WINAPI TranslateMessage(const MSG*);
LONG WINAPI DispatchMessageW(const MSG*);
s32 WINAPI GetSystemMetrics(s32);
#ifdef __cplusplus
}
#endif
#endif
