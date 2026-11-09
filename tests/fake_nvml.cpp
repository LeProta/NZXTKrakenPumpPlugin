// Fausse nvml.dll pour tester NvmlFan sans GPU NVIDIA (1 GPU, 2 ventilateurs).
#include <windows.h>
#include <cstring>

extern "C" {
typedef int R;
struct Info { unsigned version, fan, speed; };
static unsigned g_pct[2] = { 40, 40 };
static bool     g_manual = false;
static unsigned g_temp   = 40;
static int      g_inits  = 0;

__declspec(dllexport) R nvmlInit_v2()     { ++g_inits; return 0; }
__declspec(dllexport) R nvmlShutdown()    { --g_inits; return 0; }
__declspec(dllexport) R nvmlDeviceGetCount_v2(unsigned* c) { *c = 1; return 0; }
__declspec(dllexport) R nvmlDeviceGetHandleByIndex_v2(unsigned i, void** d) { *d = (void*)1; return i == 0 ? 0 : 2; }
__declspec(dllexport) R nvmlDeviceGetName(void*, char* n, unsigned l) { strncpy_s(n, l, "Fake RTX 9999", _TRUNCATE); return 0; }
__declspec(dllexport) R nvmlDeviceGetNumFans(void*, unsigned* n) { *n = 2; return 0; }
__declspec(dllexport) R nvmlDeviceGetFanSpeed_v2(void*, unsigned f, unsigned* s) { if (f > 1) return 2; *s = g_pct[f]; return 0; }
__declspec(dllexport) R nvmlDeviceSetFanSpeed_v2(void*, unsigned f, unsigned s) { if (f > 1 || s > 100) return 2; g_pct[f] = s; g_manual = true; return 0; }
__declspec(dllexport) R nvmlDeviceSetDefaultFanSpeed_v2(void*, unsigned f) { if (f > 1) return 2; g_manual = false; g_pct[f] = 40; return 0; }
__declspec(dllexport) R nvmlDeviceGetMinMaxFanSpeed(void*, unsigned* mn, unsigned* mx) { *mn = 30; *mx = 100; return 0; }
__declspec(dllexport) R nvmlDeviceGetTemperature(void*, unsigned, unsigned* t) { *t = g_temp; return 0; }
__declspec(dllexport) R nvmlDeviceGetFanSpeedRPM(void*, Info* i) { if (i->fan > 1) return 2; i->speed = g_pct[i->fan] * 20; return 0; }

// Pilotage / inspection depuis le test
__declspec(dllexport) void     fake_set_temp(unsigned t) { g_temp = t; }
__declspec(dllexport) unsigned fake_pct(unsigned f)      { return g_pct[f]; }
__declspec(dllexport) int      fake_manual()             { return g_manual ? 1 : 0; }
__declspec(dllexport) int      fake_inits()              { return g_inits; }
}
