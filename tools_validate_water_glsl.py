#!/usr/bin/env python3
"""Extract the water GLSL string literals from the C header, assemble each
stage exactly as the engine will, and run glslangValidator over it."""
import re, subprocess, sys, os, tempfile

HDR = "/home/ruben/BennuGD2/modules/libmod_3d/libmod_3d_water_shaders.h"

def strip_comments(src):
    """Remove C block comments, but never inside a string literal.

    This has to run over the WHOLE file before anything looks for the shader
    definitions. A comment ending a line with ';' (perfectly normal English
    punctuation in a list) otherwise terminates the naive `... ;` match early and
    silently truncates the shader, which then 'fails' validation while the real C
    compiler is perfectly happy. Diagnosing that from a GLSL syntax error is
    thoroughly misleading, so remove the possibility instead.
    """
    out = []
    i, n = 0, len(src)
    in_str = False
    while i < n:
        c = src[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(src[i + 1]); i += 2; continue
            if c == '"':
                in_str = False
            i += 1
        elif c == '"':
            in_str = True; out.append(c); i += 1
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
        else:
            out.append(c); i += 1
    return ''.join(out)


def extract(src):
    """name -> shader text, for every `static const char *name = "..." ...;`"""
    src = strip_comments(src)
    out = {}
    for m in re.finditer(r'static const char \*(\w+)\s*=\s*(.*?);\s*\n', src, re.S):
        name, body = m.group(1), m.group(2)
        text = ''.join(
            lit.encode().decode('unicode_escape')
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        )
        out[name] = text
    return out

def preamble(version, tier, tess, ibl, ssr, spectrum):
    return (f"#version {version}\n"
            f"#define G3D_TIER {tier}\n"
            f"#define WATER_TESS {tess}\n"
            f"#define WATER_IBL {ibl}\n"
            f"#define WATER_SSR {ssr}\n"
            f"#define WATER_SPECTRUM {spectrum}\n")

def check(label, stage_ext, source):
    with tempfile.NamedTemporaryFile('w', suffix=stage_ext, delete=False) as f:
        f.write(source)
        path = f.name
    try:
        r = subprocess.run(["glslangValidator", path],
                           capture_output=True, text=True)
        ok = r.returncode == 0
        print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
        if not ok:
            for line in (r.stdout + r.stderr).splitlines():
                if line.strip() and not line.startswith(path):
                    print(f"        {line}")
            # show numbered source around the first error for context
            m = re.search(r':(\d+):', r.stdout)
            if m:
                n = int(m.group(1))
                lines = source.splitlines()
                for i in range(max(0, n - 4), min(len(lines), n + 3)):
                    mark = ">>" if i + 1 == n else "  "
                    print(f"        {mark}{i+1:4d}| {lines[i]}")
        return ok
    finally:
        os.unlink(path)

def main():
    src = open(HDR).read()
    s = extract(src)
    missing = [k for k in ("g3d_water_glsl_common", "g3d_water_glsl_vert",
                           "g3d_water_glsl_tcs", "g3d_water_glsl_tes",
                           "g3d_water_glsl_frag") if k not in s]
    if missing:
        print("could not extract:", missing); return 1

    common = s["g3d_water_glsl_common"]
    ok = True

    print("ULTRA / HIGH  (GL 4.3, tessellation + IBL + SSR)")
    pre = preamble(430, 3, 1, 1, 1, 0)
    ok &= check("vertex (pass-through)", ".vert", pre + common + s["g3d_water_glsl_vert"])
    ok &= check("tess control",          ".tesc", pre + common + s["g3d_water_glsl_tcs"])
    ok &= check("tess evaluation",       ".tese", pre + common + s["g3d_water_glsl_tes"])
    ok &= check("fragment",              ".frag", pre + common + s["g3d_water_glsl_frag"])
    # The foam pass degrades silently at runtime if it fails to build, so it has
    # to be checked here or a break in it goes unnoticed.
    ok &= check("foam (compute)",        ".comp",
                preamble(430, 3, 0, 1, 1, 0) + common + s["g3d_water_glsl_foam_comp"])

    print("MID  (GL 3.3, no tessellation, IBL, no SSR)")
    pre = preamble(330, 1, 0, 1, 0, 0)
    ok &= check("vertex (displacing)", ".vert", pre + common + s["g3d_water_glsl_vert"])
    ok &= check("fragment",            ".frag", pre + common + s["g3d_water_glsl_frag"])

    print("LOW  (GL 3.3 baseline, no IBL, no SSR)")
    pre = preamble(330, 0, 0, 0, 0, 0)
    ok &= check("vertex",   ".vert", pre + common + s["g3d_water_glsl_vert"])
    ok &= check("fragment", ".frag", pre + common + s["g3d_water_glsl_frag"])

    print("\n" + ("ALL SHADERS COMPILE" if ok else "SHADER ERRORS"))
    return 0 if ok else 1

sys.exit(main())
