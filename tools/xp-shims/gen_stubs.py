#!/usr/bin/env python3
"""Generate the export forwarding stubs (.def, .S, names.c) for the shim DLLs.

usage: gen_stubs.py opengl32 path/to/system32/opengl32.dll out_dir
       gen_stubs.py d3d9     path/to/system32/d3d9.dll     out_dir
Needs `pefile`. Exports listed in OVERRIDES are implemented in C (h_<name>);
exports listed in DROP are not exported at all; everything else jumps to the
real system DLL through g_ptrs[], filled in DllMain from g_names[].
"""
import os
import sys

import pefile

OVERRIDES = {
    "opengl32": {"glTexImage2D", "glTexSubImage2D", "wglGetProcAddress", "wglMakeCurrent"},
    "d3d9": set(),
}
DROP = {
    "opengl32": set(),
    "d3d9": {"Direct3DCreate9Ex"},
}

lib, dll, out = sys.argv[1], sys.argv[2], sys.argv[3]
pe = pefile.PE(dll)
exports = [(e.ordinal, e.name.decode()) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name]
defs, asm, names = ["LIBRARY %s" % lib, "EXPORTS"], [".intel_syntax noprefix", ".text"], []
for ordinal, name in exports:
    if name in DROP[lib]:
        continue
    if name in OVERRIDES[lib]:
        defs.append("%s=h_%s @%d" % (name, name, ordinal))
        continue
    defs.append("%s=s_%s @%d" % (name, name, ordinal))
    asm += [".globl _s_%s" % name, "_s_%s:" % name, "  jmp dword ptr [_g_ptrs+%d]" % (4 * len(names))]
    names.append(name)
with open(os.path.join(out, lib + ".def"), "w") as f:
    f.write("\n".join(defs) + "\n")
with open(os.path.join(out, lib + "_stubs.S"), "w") as f:
    f.write("\n".join(asm) + "\n")
with open(os.path.join(out, lib + "_names.c"), "w") as f:
    f.write("const char* const g_names[] = {\n%s  0};\nvoid* g_ptrs[%d];\nconst int g_count = %d;\n"
            % ("".join('  "%s",\n' % n for n in names), len(names), len(names)))
