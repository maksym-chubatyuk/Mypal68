# Mypal68 for low-end Windows XP machines

This is a fork of [Feodor2/Mypal68](https://github.com/Feodor2/Mypal68), a
browser for Windows XP based on Firefox 68 (currently at 78.1.0). Mypal itself
is Feodor2's work. This fork adds a few changes to make it usable on the
weakest hardware Mypal still runs on: a single-core Pentium 4 with 1 GB of RAM.

The goal was one YouTube tab with uBlock Origin and h264ify, playing 480p
without running out of memory, with **total system memory under 1 GB**. Past
1 GB, XP starts swapping and becomes unusable.

The original Mypal README is in [README.upstream.md](README.upstream.md).

## Results

Measured on Windows XP SP3 with One-Core-API, Pentium 4 2.8 GHz (single core),
1 GB RAM and an ATI Radeon 9600. The test was YouTube in a maximized window,
with uBlock Origin and h264ify enabled.

|                     | Stock Mypal 78.1.0            | With this fork's changes        |
|---------------------|-------------------------------|---------------------------------|
| Processes           | 4 × `mypal.exe`               | 1                               |
| Total system commit | 950–1150 MB, *with uBlock off* | 660–690 MB, with uBlock on     |
| Playback            | 240p, ~80% CPU                | 480p, 1.6% dropped frames over 6 minutes, ~35% CPU spare |

Memory peaks at about 685–760 MB while the page loads, then stays flat during
playback.

> [!IMPORTANT]
> These numbers come from the stock Mypal 78.1.0 binary with the equivalent
> changes applied at runtime: a prefs file, the `d3d9.dll` shim and
> `MOZ_FORCE_DISABLE_E10S=1`. The C++ changes in this repository have **not
> been compiled yet**. See [Status](#status).

## What's different from upstream

### 1. Native Direct3D 9 instead of a Wine wrapper (biggest CPU win)

[One-Core-API](https://github.com/shorthorn-project/One-Core-API-Binaries)
adds a `Direct3DCreate9Ex` export to XP's `d3d9.dll`. XP has no real D3D9Ex,
so that export forwards to a Wine-based Direct3D 9 running on top of OpenGL.
Gecko prefers the Ex entry point whenever it exists, so it ended up on that
wrapper instead of the native driver. On the Radeon 9600 the wrapper took
about 18 ms to upload each 480p video frame, against about 1.7 ms through
native D3D9. That alone used about 65% of the CPU, and the wrapper also
crashed Mypal on shutdown.

- `gfx/layers/d3d9/DeviceManagerD3D9.cpp`: use D3D9Ex only on Vista and
  later. On plain XP without One-Core-API there's no Ex export, so nothing
  changes there.
- `tools/xp-shims/d3d9_shim.c`: a drop-in `d3d9.dll` for the Mypal folder
  that hides `Direct3DCreate9Ex` and forwards everything else to the system
  DLL. It's still needed for ANGLE (`libGLESv2.dll`, used by WebGL), which
  makes its own D3D9Ex choice.

### 2. Faster texture uploads on old OpenGL drivers

Old XP OpenGL drivers (ATI R300 here) convert every texel in software when
rows are padded (`GL_UNPACK_ROW_LENGTH`) or the type is
`GL_UNSIGNED_INT_8_8_8_8_REV`. That makes uploads 7–10× slower.

- `gfx/gl/GLUploadHelpers.cpp`: repack padded rows to tight ones before
  upload, and use `GL_UNSIGNED_BYTE` for BGRA on Windows. On little-endian x86
  the byte order is identical.
- `tools/xp-shims/opengl32_proxy.c`: the same fix as a drop-in
  `opengl32.dll` for existing builds.

This only matters when Mypal is drawing with OpenGL (the OpenGL compositor, or
WebGL without ANGLE). Once fix 1 puts Mypal on native D3D9, the shim does
practically nothing.

### 3. Single process by default (~250 MB saved)

Firefox has run in several processes since version 57: the browser window,
web content, extensions, and preloaded or privileged `about:` pages. Each one
loads its own copy of the engine and the JavaScript runtime. On a 1 GB machine
that cost about 250 MB.

- `toolkit/xre/nsAppRunner.cpp`: official builds ignored
  `browser.tabs.remote.autostart=false` outside test automation. They now
  respect it.
- `browser/app/profile/mypal.js`: e10s, remote extensions, the privileged
  content process, process prelaunch and tab warmup are off by default, and
  `dom.ipc.processCount` is 1.

The trade-off: a crashing page takes the whole browser down with it, and web
content is no longer isolated in its own process. With an ad blocker cutting
most third-party scripts, that's a reasonable trade on this hardware.

### 4. Memory and background-work defaults

These are all in `browser/app/profile/mypal.js`:

- **Garbage collection is tighter.** Upstream lets the JS heap grow to about 3×
  its live size before collecting. On a playing YouTube tab, memory grew about
  0.7 MB/s until a tab switch forced a collection. It now collects at about
  1.2–1.5× and stays flat.
- **Smaller caches.** MSE buffers are capped at 30 MB of video and 8 MB of
  audio. The memory, image and media caches are smaller, and the Back button
  no longer keeps rendered copies of previous pages.
- **30 Hz refresh.** XP has no DWM, so Gecko runs its own vsync timer. 30 Hz
  halves the compositor and refresh-driver work and is enough for 24/30 fps
  video.
- **Background services off:** Safe Browsing, telemetry and health reports,
  the Activity Stream new-tab page and page thumbnails, link and DNS prefetch,
  the network predictor and speculative connections, and accessibility.
- **Session store** saves every 60 s instead of every 15 s.

> [!NOTE]
> These defaults apply to every build made from this fork, on any Windows
> version. They're tuned for 1 GB machines. On a machine with plenty of RAM
> you may want upstream's defaults back, especially multiprocess.

## Using it with an existing Mypal install

You can get the same results without rebuilding.

1. Build the shims (see [`tools/xp-shims/README.md`](tools/xp-shims/README.md))
   and put `d3d9.dll` in the Mypal folder, next to `mypal.exe`. You only need
   it on One-Core-API systems. Add `opengl32.dll` only if Mypal is on OpenGL.
2. Copy the `pref(...)` lines this fork changes or adds in
   `browser/app/profile/mypal.js` into a new file, such as
   `defaults\pref\xp-lowmem.js`, in the Mypal folder.
3. Set the user environment variable `MOZ_FORCE_DISABLE_E10S=1`. Stock
   official builds ignore the single-process pref, so this is the only way to
   turn it on there.
4. Use h264ify. The Radeon 9600 and other GPUs of that era can't decode
   H.264 or VP9, so decoding runs on the CPU, and H.264 is much cheaper than
   YouTube's default VP9 on a Pentium 4.

To undo any of it, delete the files or the variable.

## Status

- The prefs, the shims and the single-process setting were tested on the
  hardware above.
- The C++ changes (`DeviceManagerD3D9.cpp`, `GLUploadHelpers.cpp`,
  `nsAppRunner.cpp`) were written but **not compiled**. They do the same
  things as the tested runtime fixes. Build them with the usual Mypal
  toolchain; see the
  [upstream wiki](https://github.com/Feodor2/Mypal68/wiki).
- WebGL through ANGLE still picks D3D9Ex without the `d3d9.dll` shim.

## Staying in sync with upstream

```sh
git remote add upstream https://github.com/Feodor2/Mypal68.git
git fetch upstream
git merge upstream/main
```

The changes are kept small and contained so that merges stay easy.

## Reporting problems

Report problems caused by this fork's changes here. Report general Mypal bugs
and site compatibility problems
[upstream](https://github.com/Feodor2/Mypal68/issues), and check first that
they also happen in stock Mypal.

## License

Same as upstream: MPL 2.0 and the other licenses in [LICENSE](LICENSE) and
`other-licenses/`.
