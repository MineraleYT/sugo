#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Which CI jobs a change needs: reads the files changed between two commits (GitHub's compare API,
# no checkout) and writes linux=, clang= and macos= (true/false) to $GITHUB_OUTPUT.
#   linux  the GCC build with the tests and the metadata checks (ubuntu)
#   clang  the Clang build, warnings as errors (only the compiled sources matter)
#   macos  the macOS build with the tests
# It fails open: when the changed files cannot be known (a manual run, a new branch, a force push
# the API cannot compare, too many files), every job runs.
#
# usage: ci-changes.sh <base commit> <head commit>   (GH_TOKEN, GITHUB_REPOSITORY, GITHUB_OUTPUT)
set -uo pipefail

base=${1:-}
head=${2:-}
linux=false clang=false macos=false

all() {
    echo "Running every job: $1"
    linux=true clang=true macos=true
}

# What a file needs. Anything not listed below goes to every job.
classify() {
    case $1 in
        # Read by no job of ci.yml (release.yml builds the Flatpak, appimage.yml and macos.yml the packages).
        *.md | docs/* | LICENSE | LICENSES/* | *.license | .gitignore | po/* | \
        .github/ISSUE_TEMPLATE/* | .github/dependabot.yml | \
        .github/workflows/release.yml | .github/workflows/nightly.yml | \
        .github/workflows/appimage.yml | .github/workflows/macos.yml | \
        io.github.mineraleyt.sugo.yml | packaging/appimage/*)
            ;;

        # Linux metadata: validated (and installed) by the Linux build only.
        data/io.github.mineraleyt.sugo.desktop | data/io.github.mineraleyt.sugo.metainfo.xml)
            linux=true ;;

        # macOS only: packaging, the app icon, and the sources and tests built under if(APPLE).
        packaging/macos/* | data/icons/macos/* | src/platform/*.mm | \
        src/platform/MacApp.h | src/platform/MacNotifier.h | src/platform/MacPowerAssertion.* | \
        src/platform/MacSessionEvents.* | src/platform/MacKeychainStore.* | \
        tests/tst_macpowerassertion.cpp | tests/tst_mackeychainstore.cpp)
            macos=true ;;

        # Linux only: the D-Bus services and their tests (the if(NOT APPLE) blocks).
        src/platform/DBusTimerService.* | src/platform/FreedesktopInhibitor.* | \
        src/platform/FreedesktopNotifier.* | src/platform/KRunnerService.* | \
        src/platform/PortalAutostart.* | src/platform/SecretServiceStore.* | \
        src/platform/SingleInstance.* | tests/PrivateDBus.h | \
        tests/tst_powerinhibitor.cpp | tests/tst_freedesktopinhibitor.cpp | \
        tests/tst_singleinstance.cpp | tests/tst_krunnerservice.cpp | \
        tests/tst_freedesktopnotifier.cpp | tests/tst_dbustimerservice.cpp | tests/tst_secretstore.cpp)
            linux=true clang=true ;;
        tests/shutdown_app.sh | tests/check_qml_colours.sh | tests/check_qml_sizes.sh)
            linux=true ;;

        # Run by the tests on both systems, compiled by neither.
        tests/*.sh | tests/*.py | tests/*.json | data/*.in)
            linux=true macos=true ;;

        *)
            linux=true clang=true macos=true ;;
    esac
}

if [[ -z $base || -z $head || $base =~ ^0+$ ]]; then
    all "no previous commit to compare with"
elif ! files=$(gh api "repos/$GITHUB_REPOSITORY/compare/$base...$head?per_page=100" --paginate \
        --jq '.files[].filename' 2>&1); then
    all "the comparison failed ($files)"
elif [[ -z $files ]]; then
    all "no changed file reported"
elif (($(wc -l <<<"$files") >= 300)); then
    all "300 changed files or more (the API lists no more)"
else
    echo "Changed files:"
    while IFS= read -r file; do
        echo "  $file"
        classify "$file"
    done <<<"$files"
fi

echo "linux=$linux clang=$clang macos=$macos"
{
    echo "linux=$linux"
    echo "clang=$clang"
    echo "macos=$macos"
} >>"${GITHUB_OUTPUT:-/dev/stdout}"
