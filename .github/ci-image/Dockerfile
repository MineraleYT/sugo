# SPDX-License-Identifier: GPL-3.0-or-later
# The system the Linux jobs of ci.yml build and test in: Ubuntu with every build dependency already
# installed, so a run downloads one image from ghcr.io instead of ~110 MB of packages from the
# Ubuntu mirrors (usually 30 s, but now and then 7 to 35 min). ci.yml builds it again, tagged with a
# hash of this file, whenever this file changes; nothing else rebuilds it.
#
# ubuntu:26.04, pinned like the actions.
FROM ubuntu@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c

LABEL org.opencontainers.image.source=https://github.com/MineraleYT/sugo
LABEL org.opencontainers.image.description="Build and test environment of Sugo's Linux CI jobs"

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# The build (GCC and Clang, libsodium for Sugo Sync), the metadata checks, the tests (D-Bus, python3 for the platform parity
# check, a font for the offscreen QML runs), and what the actions need inside a container (git for
# actions/checkout, zstd for actions/cache).
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        cmake ninja-build g++ clang ccache \
        qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools \
        qt6-svg-plugins qt6-multimedia-dev libqt6sql6-sqlite extra-cmake-modules \
        libsodium-dev pkgconf \
        appstream desktop-file-utils \
        dbus-daemon dbus-bin python3 fonts-dejavu-core \
        ca-certificates git zstd \
 && rm -rf /var/lib/apt/lists/*

# The tests run as an ordinary user, with the user id of the runner (the owner of the workspace the
# runner mounts): as root, the tests that take away write access to a folder would still write.
RUN useradd --create-home --uid 1001 runner
# Docker sets no USER; the runner does, and Diagnostics::redact() falls back on the name of the home
# folder without it, which here (/github/home) is "home".
ENV USER=runner
