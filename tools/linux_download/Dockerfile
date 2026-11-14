# Build environment for Spite's Linux download (see README.md). Ubuntu 24.04 is the oldest system
# the download supports (glibc 2.39), so the result runs there and on anything newer.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential git python3 pkgconf ca-certificates curl file xz-utils patchelf \
      libboost-dev libglib2.0-dev libglibmm-2.4-dev libsigc++-2.0-dev libcairo2-dev libcairomm-1.0-dev \
      libpango1.0-dev libpangomm-1.4-dev libfontconfig-dev libpng-dev libjpeg-dev libx11-dev libxext-dev \
      libxrandr-dev libxinerama-dev libxi-dev libsndfile1-dev libflac-dev libogg-dev libsamplerate0-dev \
      libfftw3-dev libcurl4-openssl-dev libarchive-dev liblo-dev libtag1-dev vamp-plugin-sdk \
      librubberband-dev libxml2-dev lv2-dev libserd-dev libsord-dev libsratom-dev liblilv-dev \
      libasound2-dev libpulse-dev libjack-jackd2-dev libusb-1.0-0-dev libudev-dev libwebsockets-dev \
      libdbus-1-dev libreadline-dev \
      cmake ninja-build libfreetype-dev libxcomposite-dev libxcursor-dev libxkbcommon-dev \
      libgl1-mesa-dev libegl1-mesa-dev libwayland-dev libdecor-0-dev \
    && rm -rf /var/lib/apt/lists/*
# Left out on purpose:
# - libaubio-dev: Ubuntu's aubio links FFmpeg. container/build-aubio.sh builds aubio without it.
# - liblrdf0-dev: lrdf needs a libcurl that only Debian and Ubuntu have. Spite is configured with --no-lrdf.
