#!/usr/bin/env python3
"""Copy into the bundle every shared library its programs and plugins need, except the ones every
Linux desktop already has (HOST below), until nothing is missing.

Each copied library goes into its own folder, BUNDLE/lib/fallback/<soname>/. The launcher
(lib/fallback/paths.sh) puts a folder on the library path only when the user's system doesn't have
that library: a system's own copy is newer, and the system's other libraries may need it (Debian 13's
GnuTLS crashed on Ubuntu 24.04's older nettle when ours came first). ALWAYS_OURS go into BUNDLE/lib.

It follows each file's own list of needed libraries (its DT_NEEDED entries), and only through
libraries it copies. It never looks inside a HOST library: their own dependencies (curl's Kerberos
and LDAP, glib's libmount) must come from the user's system too, never an older copy from here.

usage: bundle-libs.py BUNDLE [extra library folder ...]
Exits 1 if a library can't be found at all.
"""
import fnmatch
import os
import shutil
import subprocess
import sys

# Libraries taken from the user's system, never copied: the C library and compiler runtime, the
# graphics, X11 and sound libraries that must match the user's drivers and servers, and the base
# desktop libraries (glib, cairo, pango, fonts), which every desktop has in a newer, compatible version.
HOST = """
ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libutil.so.1
libresolv.so.2 libanl.so.1 libmvec.so.1
libstdc++.so.6 libgcc_s.so.1
libGL.so.1 libGLX.so.0 libGLdispatch.so.0 libEGL.so.1 libOpenGL.so.0 libGLESv2.so.2 libdrm.so.2 libgbm.so.1
libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libxcb-*.so.* libXau.so.6 libXdmcp.so.6 libXext.so.6
libXrender.so.1 libXrandr.so.2 libXinerama.so.1 libXi.so.6 libXcursor.so.1 libXfixes.so.3
libXdamage.so.1 libXcomposite.so.1 libXxf86vm.so.1 libSM.so.6 libICE.so.6 libXft.so.2
libxshmfence.so.1 libxkbcommon.so.0 libxkbcommon-x11.so.0 libwayland-*.so.0
libasound.so.2 libjack.so.0 libpulse.so.0 libpulse-simple.so.0 libpulse-mainloop-glib.so.0
libpipewire-0.3.so.0
libglib-2.0.so.0 libgobject-2.0.so.0 libgio-2.0.so.0 libgmodule-2.0.so.0 libgthread-2.0.so.0
libcairo.so.2 libcairo-gobject.so.2 libpango-1.0.so.0 libpangocairo-1.0.so.0 libpangoft2-1.0.so.0
libpangoxft-1.0.so.0 libharfbuzz.so.0 libfribidi.so.0 libthai.so.0 libdatrie.so.1 libpixman-1.so.0
libgraphite2.so.3 libfontconfig.so.1 libfreetype.so.6 libpng16.so.16 libz.so.1 libexpat.so.1
libbrotlidec.so.1 libbrotlicommon.so.1 libbrotlienc.so.1 libuuid.so.1
libdbus-1.so.3 libudev.so.1 libsystemd.so.0 libcap.so.2 libgcrypt.so.20 libgpg-error.so.0
liblz4.so.1 libzstd.so.1 liblzma.so.5 libusb-1.0.so.0 libcurl.so.4
""".split()

# Built here differently from the distributions' copies (aubio without FFmpeg): always this one
ALWAYS_OURS = {"libaubio.so.5"}


def is_host(soname):
    return any(fnmatch.fnmatchcase(soname, pattern) for pattern in HOST)


def needed(path):
    """The libraries this file names as needed (DT_NEEDED)"""
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    names = []
    for line in out.splitlines():
        if "(NEEDED)" in line and "[" in line:
            names.append(line.split("[", 1)[1].split("]", 1)[0])
    return names


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def main():
    bundle = os.path.abspath(sys.argv[1])
    libdir = os.path.join(bundle, "lib")
    fallback = os.path.join(libdir, "fallback")
    extra = [os.path.abspath(p) for p in sys.argv[2:]]
    checked, copied, missing, host_used = set(), [], {}, set()

    while True:
        new = 0
        folders = sorted(os.path.join(fallback, d) for d in os.listdir(fallback)) if os.path.isdir(fallback) else []
        env = dict(os.environ, LD_LIBRARY_PATH=":".join([libdir] + folders + extra))
        for root, _, files in os.walk(bundle):
            for name in files:
                path = os.path.join(root, name)
                if os.path.islink(path) or path in checked or not is_elf(path):
                    continue
                checked.add(path)
                # Where the loader would find each library, then only the ones this file names itself
                out = subprocess.run(["ldd", path], env=env, capture_output=True, text=True).stdout
                found = {}
                for line in out.splitlines():
                    line = line.strip()
                    if "=>" in line:
                        soname, _, target = (part.strip() for part in line.partition("=>"))
                        found[soname] = target
                for soname in needed(path):
                    target = found.get(soname, "not found")
                    if is_host(soname):
                        host_used.add(soname)
                        continue
                    if target.startswith("not found"):
                        missing.setdefault(soname, []).append(os.path.relpath(path, bundle))
                        continue
                    target = target.split(" (")[0]
                    if target.startswith(bundle + "/"):
                        continue
                    if soname in ALWAYS_OURS:
                        dest = os.path.join(libdir, soname)
                    else:
                        os.makedirs(os.path.join(fallback, soname), exist_ok=True)
                        dest = os.path.join(fallback, soname, soname)
                    if not os.path.exists(dest):
                        shutil.copy2(os.path.realpath(target), dest)
                        os.chmod(dest, 0o755)
                        copied.append(f"{soname} <- {target}")
                        new += 1
        if not new:
            break

    print(f"bundle-libs: checked {len(checked)} files, copied {len(copied)} libraries")
    for line in sorted(copied):
        print("  copied", line)
    print("  from the user's system:", " ".join(sorted(host_used)))
    if missing:
        for soname, users in sorted(missing.items()):
            print(f"  MISSING {soname}, needed by {', '.join(users[:3])}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
