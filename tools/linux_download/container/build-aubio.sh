#!/bin/bash
# Runs inside the build container, called by build.sh. Builds aubio 0.4.9 as a shared library into
# /work/deps, using fftw3f and nothing else.
# Ubuntu's own aubio links FFmpeg, which would drag dozens of libraries into the download.
# aubio's source archive doesn't include its build tool (its build first downloads waf 2.0.14), so
# this compiles its 60 C files directly, with the defines aubio's own build would set.
set -euo pipefail
PREFIX=/work/deps
if [ -f "$PREFIX/lib/pkgconfig/aubio.pc" ]; then echo "aubio: already built"; exit 0; fi

SRC=/work/build/aubio/aubio-0.4.9
if [ ! -d "$SRC" ]; then
	mkdir -p /work/build/aubio
	curl -fsSL -o /work/build/aubio/aubio-0.4.9.tar.gz https://github.com/aubio/aubio/archive/refs/tags/0.4.9.tar.gz
	tar -xzf /work/build/aubio/aubio-0.4.9.tar.gz -C /work/build/aubio
fi
cd "$SRC"

cat > src/config.h <<'H'
/* written by build-aubio.sh: what aubio's waf build defines with fftw3f, without
   sndfile, samplerate or libav */
#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_COMPLEX_H 1
#define HAVE_MATH_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDARG_H 1
#define HAVE_GETOPT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_C99_VARARGS_MACROS 1
#define HAVE_FFTW3F 1
#define HAVE_FFTW3 1
#define HAVE_WAVREAD 1
#define HAVE_WAVWRITE 1
#define HAVE_MEMCPY_HACKS 1
H

rm -rf obj && mkdir obj
n=0
while IFS= read -r f; do
	n=$((n + 1))
	gcc -O2 -fPIC -std=gnu99 -DHAVE_CONFIG_H -DNDEBUG -Isrc $(pkg-config --cflags fftw3f) -c "$f" -o "obj/$n.o"
done < <(find src -name '*.c' | sort)

mkdir -p "$PREFIX/lib/pkgconfig" "$PREFIX/include/aubio"
gcc -shared -Wl,-soname,libaubio.so.5 -o "$PREFIX/lib/libaubio.so.5.4.8" obj/*.o $(pkg-config --libs fftw3f) -lm
ln -sf libaubio.so.5.4.8 "$PREFIX/lib/libaubio.so.5"
ln -sf libaubio.so.5 "$PREFIX/lib/libaubio.so"

# Public headers, keeping their folders, as aubio's install does
(cd src && find . -name '*.h' ! -name '*_priv.h' ! -name config.h | while IFS= read -r h; do
	mkdir -p "$PREFIX/include/aubio/$(dirname "$h")"
	cp "$h" "$PREFIX/include/aubio/$h"
done)

sed -e "s|@prefix@|$PREFIX|" -e "s|@exec_prefix@|$PREFIX|" -e "s|@libdir@|$PREFIX/lib|" \
	-e "s|@includedir@|$PREFIX/include|" -e "s|@VERSION@|0.4.9|" aubio.pc.in > "$PREFIX/lib/pkgconfig/aubio.pc"
echo "aubio: built $n files into $PREFIX"
