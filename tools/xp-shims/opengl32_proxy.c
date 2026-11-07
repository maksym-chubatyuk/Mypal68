/* opengl32.dll proxy for Mypal on old ATI drivers.
   The ATI R300 XP driver (Catalyst 10.2) takes a per-texel float conversion path for
   glTex(Sub)Image2D whenever source rows are not tightly packed (UNPACK_ROW_LENGTH or
   UNPACK_ALIGNMENT padding) and for BGRA + UNSIGNED_INT_8_8_8_8_REV. Gecko uploads every
   video plane with a padded stride, costing ~18ms per 480p luma plane. Repack such
   uploads into a tight buffer (and use UNSIGNED_BYTE for 8888_REV) before calling the
   real driver. Everything else is forwarded untouched. */
#include <windows.h>
#include <string.h>
typedef unsigned int GLenum; typedef int GLint; typedef int GLsizei; typedef void GLvoid;
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
extern const char* const g_names[]; extern void* g_ptrs[]; extern const int g_count;
typedef void (WINAPI *PFTEX)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const GLvoid*);
typedef void (WINAPI *PFSUB)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const GLvoid*);
typedef void (WINAPI *PFPS)(GLenum,GLint);
typedef void (WINAPI *PFGI)(GLenum,GLint*);
typedef PROC (WINAPI *PFGPA)(LPCSTR);
static PFTEX r_TexImage2D; static PFSUB r_TexSubImage2D; static PFPS r_PixelStorei; static PFGI r_GetIntegerv; static PFGPA r_GetProcAddress;
typedef BOOL (WINAPI *PFMC)(HDC,HGLRC); typedef BOOL (WINAPI *PFSI)(int);
static PFMC r_MakeCurrent; static int g_swapInterval = -1; static HGLRC g_lastCtx[16];
static HMODULE g_real; static DWORD g_tls = TLS_OUT_OF_INDEXES; static int g_disabled;

static int Bpp(GLenum fmt, GLenum type) {
  if (type == GL_UNSIGNED_INT_8_8_8_8_REV) return (fmt == 0x80E1 || fmt == 0x1908) ? 4 : 0;
  if (type != GL_UNSIGNED_BYTE) return 0;
  switch (fmt) {
    case 0x1909: case 0x1906: case 0x1903: case 0x8049: return 1; /* LUMINANCE ALPHA RED INTENSITY */
    case 0x190A: return 2;                                          /* LUMINANCE_ALPHA */
    case 0x1907: case 0x80E0: return 3;                             /* RGB BGR */
    case 0x1908: case 0x80E1: return 4;                             /* RGBA BGRA */
  }
  return 0;
}
static unsigned char* Scratch(size_t n) {
  struct { size_t cap; unsigned char* p; } *s = TlsGetValue(g_tls);
  if (!s) { s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s); if (!s) return NULL; TlsSetValue(g_tls, s); }
  if (s->cap < n) { if (s->p) VirtualFree(s->p, 0, MEM_RELEASE); s->p = VirtualAlloc(NULL, n, MEM_COMMIT, PAGE_READWRITE); s->cap = s->p ? n : 0; }
  return s->p;
}
/* Returns the pointer to upload from, or NULL to pass through unchanged. *restore set when unpack state was changed. */
static const GLvoid* Prepare(GLsizei w, GLsizei h, GLenum fmt, GLenum* type, const GLvoid* px, GLint st[4]) {
  int bpp; GLint pbo = 0; size_t tight, stride; const unsigned char* src; unsigned char* dst; int y;
  if (g_disabled || !px || w <= 0 || h <= 0) return NULL;
  bpp = Bpp(fmt, *type); if (!bpp) return NULL;
  r_GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo); if (pbo) return NULL;
  r_GetIntegerv(GL_UNPACK_ROW_LENGTH, &st[0]); r_GetIntegerv(GL_UNPACK_SKIP_ROWS, &st[1]);
  r_GetIntegerv(GL_UNPACK_SKIP_PIXELS, &st[2]); r_GetIntegerv(GL_UNPACK_ALIGNMENT, &st[3]);
  tight = (size_t)w * bpp;
  stride = (size_t)(st[0] > 0 ? st[0] : w) * bpp;
  if (st[3] > 1) stride = (stride + st[3] - 1) / st[3] * st[3];
  src = (const unsigned char*)px + (size_t)st[1] * stride + (size_t)st[2] * bpp;
  if (*type == GL_UNSIGNED_INT_8_8_8_8_REV) *type = GL_UNSIGNED_BYTE; /* same bytes on little endian */
  if (stride == tight) {
    dst = (unsigned char*)src;                 /* already tight: only reset unpack state */
  } else {
    dst = Scratch(tight * h); if (!dst) return NULL;
    for (y = 0; y < h; y++) memcpy(dst + y * tight, src + y * stride, tight);
  }
  r_PixelStorei(GL_UNPACK_ROW_LENGTH, 0); r_PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  r_PixelStorei(GL_UNPACK_SKIP_PIXELS, 0); r_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return dst;
}
static void Restore(const GLint st[4]) {
  r_PixelStorei(GL_UNPACK_ROW_LENGTH, st[0]); r_PixelStorei(GL_UNPACK_SKIP_ROWS, st[1]);
  r_PixelStorei(GL_UNPACK_SKIP_PIXELS, st[2]); r_PixelStorei(GL_UNPACK_ALIGNMENT, st[3]);
}
void WINAPI h_glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const GLvoid* p) {
  GLint st[4]; const GLvoid* q = Prepare(w, h, f, &ty, p, st);
  if (!q) { r_TexImage2D(t, l, ifmt, w, h, b, f, ty, p); return; }
  r_TexImage2D(t, l, ifmt, w, h, b, f, ty, q); Restore(st);
}
void WINAPI h_glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const GLvoid* p) {
  GLint st[4]; const GLvoid* q = Prepare(w, h, f, &ty, p, st);
  if (!q) { r_TexSubImage2D(t, l, x, y, w, h, f, ty, p); return; }
  r_TexSubImage2D(t, l, x, y, w, h, f, ty, q); Restore(st);
}
/* Optionally force a swap interval on every context the first time it is made current
   (MYPAL_GL_SWAPINTERVAL=n). */
BOOL WINAPI h_wglMakeCurrent(HDC dc, HGLRC rc) {
  BOOL ok = r_MakeCurrent(dc, rc); int i;
  if (ok && rc && g_swapInterval >= 0) {
    for (i = 0; i < 16; i++) { if (g_lastCtx[i] == rc) return ok; if (!g_lastCtx[i]) break; }
    if (i < 16) { PFSI si = (PFSI)r_GetProcAddress("wglSwapIntervalEXT"); g_lastCtx[i] = rc; if (si) si(g_swapInterval); }
  }
  return ok;
}
PROC WINAPI h_wglGetProcAddress(LPCSTR n) {
  if (n && !strcmp(n, "glTexImage2D")) return (PROC)h_glTexImage2D;
  if (n && !strcmp(n, "glTexSubImage2D")) return (PROC)h_glTexSubImage2D;
  return r_GetProcAddress(n);
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
  if (reason == DLL_PROCESS_ATTACH) {
    char path[MAX_PATH]; int i;
    DisableThreadLibraryCalls(inst);
    GetSystemDirectoryA(path, MAX_PATH); lstrcatA(path, "\\opengl32.dll");
    g_real = LoadLibraryA(path); if (!g_real) return FALSE;
    for (i = 0; i < g_count; i++) g_ptrs[i] = (void*)GetProcAddress(g_real, g_names[i]);
    r_TexImage2D = (PFTEX)GetProcAddress(g_real, "glTexImage2D");
    r_TexSubImage2D = (PFSUB)GetProcAddress(g_real, "glTexSubImage2D");
    r_PixelStorei = (PFPS)GetProcAddress(g_real, "glPixelStorei");
    r_GetIntegerv = (PFGI)GetProcAddress(g_real, "glGetIntegerv");
    r_GetProcAddress = (PFGPA)GetProcAddress(g_real, "wglGetProcAddress");
    r_MakeCurrent = (PFMC)GetProcAddress(g_real, "wglMakeCurrent");
    g_tls = TlsAlloc();
    g_disabled = GetEnvironmentVariableA("MYPAL_GL_PROXY_OFF", path, sizeof path) > 0;
    if (GetEnvironmentVariableA("MYPAL_GL_SWAPINTERVAL", path, sizeof path) > 0) g_swapInterval = path[0] - '0';
  }
  return TRUE;
}
