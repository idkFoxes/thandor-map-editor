# Test15 gameplay and the editor's headless platform adapter.
set(adapter "${PROJECT_SOURCE_DIR}/tools/native_headless")
file(GLOB_RECURSE engine_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
# The worker includes the tick implementation to call its private simulation step.
list(REMOVE_ITEM engine_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay/session/tick.cpp")
add_library(thandor_jandrenator_engine OBJECT
    ${engine_sources} "${adapter}/driver.cpp" "${adapter}/posix_platform.cpp")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native_simulation_tick.cpp"
    CONTENT "#include \"gameplay/session/tick.cpp\"\n")
target_include_directories(thandor_jandrenator_engine PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${adapter}/compat" include src)
target_compile_definitions(thandor_jandrenator_engine PRIVATE
    THANDOR_JANDRENATOR_HEADLESS THANDOR_JANDRENATOR_EMBEDDED
    HeapCreate=Jandrenator_HeapCreate HeapAlloc=Jandrenator_HeapAlloc
    HeapFree=Jandrenator_HeapFree HeapDestroy=Jandrenator_HeapDestroy
    __forceinline=inline __cdecl= __stdcall= __fastcall=)
target_compile_options(thandor_jandrenator_engine PRIVATE
    "-D__declspec(x)=" -O1 -fwrapv -fno-strict-aliasing -ffp-contract=off
    -mms-bitfields -ffunction-sections -fdata-sections)
if(MINGW)
    target_compile_definitions(thandor_jandrenator_engine PRIVATE THANDOR_WINDOWS_HEADLESS)
    target_compile_options(thandor_jandrenator_engine PRIVATE -include string.h -flto)
else()
    target_compile_definitions(thandor_jandrenator_engine PRIVATE _strnicmp=strncasecmp)
    target_compile_options(thandor_jandrenator_engine PRIVATE -include strings.h -fshort-wchar -fno-pie)
endif()
