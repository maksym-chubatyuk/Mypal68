/* d3d9.dll shim for Mypal on Windows XP with One-Core-API.
   One-Core-API adds Direct3DCreate9Ex to the native d3d9.dll, forwarding it to a
   Wine-based D3D9Ex (d3d9ex.dll -> d3dwine.dll) that runs on top of OpenGL. Gecko prefers
   Direct3DCreate9Ex when present, which routes compositing through Wine. This shim
   forwards every export of the native system d3d9.dll except Direct3DCreate9Ex, so Gecko
   falls back to the real Direct3D 9 driver. */
#include <windows.h>
static const char* const names[] = {"CheckFullscreen","D3DPERF_BeginEvent","D3DPERF_EndEvent","D3DPERF_GetStatus",
  "D3DPERF_QueryRepeatFrame","D3DPERF_SetMarker","D3DPERF_SetOptions","D3DPERF_SetRegion","DebugSetLevel",
  "DebugSetMute","Direct3DCreate9","Direct3DShaderValidatorCreate9","PSGPError","PSGPSampleTexture"};
void* g_ptrs[14];
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
  if (reason == DLL_PROCESS_ATTACH) {
    char path[MAX_PATH]; HMODULE real; int i;
    DisableThreadLibraryCalls(inst);
    GetSystemDirectoryA(path, MAX_PATH); lstrcatA(path, "\\d3d9.dll");
    real = LoadLibraryA(path); if (!real) return FALSE;
    for (i = 0; i < 14; i++) g_ptrs[i] = (void*)GetProcAddress(real, names[i]);
  }
  return TRUE;
}
