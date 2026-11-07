# Windows XP shims for Mypal

Drop-in DLLs placed next to `mypal.exe` on Windows XP machines. Gecko loads
`d3d9.dll` and `opengl32.dll` by bare name, so the application directory wins
over `system32`. Both forward everything else to the real system DLL.

* `d3d9.dll` (`d3d9_shim.c`): hides `Direct3DCreate9Ex`. One-Core-API adds that
  export to XP's native `d3d9.dll`, forwarding it to a Wine-based D3D9 on top of
  OpenGL. Gecko and ANGLE prefer the Ex entry point and end up on the slow
  wrapper, which also crashes Mypal on shutdown. Builds that include the
  `DeviceManagerD3D9.cpp` change skip the Ex path on XP for compositing; the
  shim still helps ANGLE (WebGL).
* `opengl32.dll` (`opengl32_proxy.c`): repacks `glTex(Sub)Image2D` uploads with
  padded rows and turns BGRA `UNSIGNED_INT_8_8_8_8_REV` into `UNSIGNED_BYTE`.
  The ATI R300 XP driver converts every texel in software for those, roughly 10x
  slower (18ms per 480p luma plane). Only matters when GL is in use (the OpenGL
  compositor, WebGL without ANGLE); builds with the `GLUploadHelpers.cpp`
  change do the same inside Gecko. Environment knobs: `MYPAL_GL_PROXY_OFF=1`
  disables the repacking, `MYPAL_GL_SWAPINTERVAL=n` forces a swap interval.

Build with mingw-w64 (i686):

    ./gen_stubs.py opengl32 /path/to/xp/system32/opengl32.dll .
    i686-w64-mingw32-gcc -O2 -shared -o opengl32.dll opengl32_proxy.c \
        opengl32_names.c opengl32_stubs.S opengl32.def -nostartfiles \
        -Wl,-e,_DllMain@12 -Wl,--enable-stdcall-fixup -static-libgcc \
        -Wl,--major-subsystem-version=5,--minor-subsystem-version=1 -lkernel32

    ./gen_stubs.py d3d9 /path/to/xp/system32/d3d9.dll .
    i686-w64-mingw32-gcc -O2 -shared -o d3d9.dll d3d9_shim.c d3d9_stubs.S \
        d3d9.def -nostartfiles -Wl,-e,_DllMain@12 -static-libgcc \
        -Wl,--major-subsystem-version=5,--minor-subsystem-version=1 -lkernel32

`d3d9_shim.c` carries its own name table, so it does not use `d3d9_names.c`.
