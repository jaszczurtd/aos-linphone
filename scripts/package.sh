#!/bin/bash
# Packs a build of the phone into LHA and ZIP archives, ready to unpack on
# the system it was built for (AROS x86_64 or MorphOS; the script reads it
# from the build directory). Each build gives two variants: the release
# archives hold the programs stripped (strip --strip-unneeded -R .comment,
# as AROS and the MorphOS SDK strip their own), the -debug archives the same
# programs as built, with their symbols and debug information, for reading
# crash addresses. Each archive holds the drawer aos-linphone and its icon:
#
#   aos-linphone.info                   the drawer icon
#   aos-linphone/aos-phone(.info)       the phone and its icon
#   aos-linphone/aos-phone-cmd          the ARexx command tool
#   aos-linphone/aos-register           with -r: the registration test tool
#   aos-linphone/share/...              belr grammars, ring tones, still picture
#   aos-linphone/*.md, SOURCE.txt       documentation, licence, where the
#                                       source of this build is
#
# With -d, more archives hold the drawer aos-linphone-drivers with the given
# drivers under the paths they take below SYS: (on MorphOS *.class in
# MorphOS/Classes/USB, where AddUSBClasses looks for classes, and *.device
# in Devs/USBHardware), drivers/README.md, their licence and SOURCE.txt, in
# the same two variants.
#
# usage: scripts/package.sh [-o <out dir>] [-r <aos-register>] [-d <driver>]...
#                           [--no-lha] <phone build dir> <prefix>
#
#   phone build dir  the build directory of apps/aos-phone, holding
#                    aos-phone, aos-phone-cmd, aos-phone.info and
#                    aos-linphone.info (the icons need ilbmtoicon)
#   prefix           the SDK prefix of scripts/build-sdk.sh
#   -o <out dir>     where the archives go, default <repository>/builds
#   -r <file>        also pack this aos-register executable
#   -d <file>        pack this driver (uvc.class, pcixhci.device) into the
#                    drivers archives; repeat for each driver
#   --no-lha         make the ZIP archives only
#
# The archives are named aos-linphone-<version>-<target>.lha and .zip, the
# drivers aos-linphone-drivers-<version>-<target>.lha and .zip, with
# <target> aros-x86_64 or morphos-ppc; the debug variant adds -debug to the
# name. The strip tool is the CMAKE_STRIP of the phone build. <version> is
# VERSION when set, else `git describe` of the release tags without the v:
# 0.1 on the commit tagged v0.1, 0.1-3-g1234abc three commits later, the
# short commit before the first tag; -dirty is added when files other than
# the submodule differ from the last commit. A commit tagged v* must carry
# that version in cmake/version.cmake, else the script stops. The LHA archive needs an lha
# archiver: LHA names it, else lha (LHa for UNIX) or jlha (jlha-utils) from
# the PATH. The source URL in SOURCE.txt comes from the remote origin, or
# from AOS_SOURCE_URL when set.

set -euo pipefail

usage() {
    echo "usage: $0 [-o <out dir>] [-r <aos-register>] [-d <driver>]... [--no-lha] <phone build dir> <prefix>" >&2
    exit 2
}

repo=$(cd "$(dirname "$0")/.." && pwd)
out="$repo/builds"
register=""
drivers=()
make_lha=1
while [ $# -gt 0 ]; do
    case "$1" in
    -o) [ $# -ge 2 ] || usage; out=$2; shift 2 ;;
    -r) [ $# -ge 2 ] || usage; register=$2; shift 2 ;;
    -d) [ $# -ge 2 ] || usage; drivers+=("$2"); shift 2 ;;
    --no-lha) make_lha=0; shift ;;
    -*) usage ;;
    *) break ;;
    esac
done
[ $# -eq 2 ] || usage
build=$(cd "$1" && pwd)
prefix=$(cd "$2" && pwd)
sounds="$repo/third_party/linphone-sdk/liblinphone/share"

# The system the phone was configured for, as CMake recorded it.
system=$(sed -n 's/^set(CMAKE_SYSTEM_NAME "\(.*\)")$/\1/p' "$build"/CMakeFiles/*/CMakeSystem.cmake 2>/dev/null | head -1 || true)
case "$system" in
AROS) target=aros-x86_64; system_label="AROS x86_64"; class_dir=Classes/USB ;;
MorphOS) target=morphos-ppc; system_label="MorphOS (PowerPC)"; class_dir=MorphOS/Classes/USB ;;
*) echo "error: $build is no AROS or MorphOS build of aos-phone" >&2; exit 1 ;;
esac

# The strip of the toolchain the phone was built with.
strip=$(sed -n 's/^CMAKE_STRIP:FILEPATH=//p' "$build/CMakeCache.txt" 2>/dev/null || true)
[ -x "$strip" ] || { echo "error: no CMAKE_STRIP in $build/CMakeCache.txt" >&2; exit 1; }

lha=${LHA:-}
if [ -z "$lha" ]; then
    for candidate in lha jlha; do
        if command -v "$candidate" >/dev/null 2>&1; then
            lha=$candidate
            break
        fi
    done
fi
if [ $make_lha -eq 1 ] && ! command -v "${lha:-lha}" >/dev/null 2>&1; then
    echo "error: no lha found; install LHa for UNIX or jlha-utils, set LHA, or use --no-lha" >&2
    exit 1
fi
command -v zip >/dev/null 2>&1 || { echo "error: zip not found" >&2; exit 1; }

files=("$build/aos-phone" "$build/aos-phone-cmd" "$build/aos-phone.info" "$build/aos-linphone.info"
       "$sounds/rings/oldphone-mono.wav" "$sounds/ringback.wav" "$prefix/share/images/nowebcamCIF.jpg")
[ -z "$register" ] || files+=("$register")
for driver in "${drivers[@]}"; do
    case "$driver" in
    *.class | *.device) ;;
    *) echo "error: $driver is neither a .class nor a .device" >&2; exit 1 ;;
    esac
    files+=("$driver")
done
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "error: $f missing" >&2; exit 1; }
done
grammars=("$prefix"/share/belr/grammars/*.belr)
[ -f "${grammars[0]}" ] || { echo "error: no belr grammars in $prefix/share/belr/grammars" >&2; exit 1; }

# A release tag has to name the version the programs carry.
release=$(sed -n 's/^set(AOS_VERSION \([0-9][0-9.]*\))$/\1/p' "$repo/cmake/version.cmake")
tag=$(git -C "$repo" describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null || true)
if [ -n "$tag" ] && [ "${tag#v}" != "$release" ]; then
    echo "error: the commit is tagged $tag, but cmake/version.cmake says version ${release:-?}" >&2
    exit 1
fi
# The patches that build-sdk.sh applies to the submodule do not make a tree
# dirty: they are part of the source.
if [ -z "${VERSION:-}" ]; then
    version=$(git -C "$repo" describe --tags --always --match 'v[0-9]*')
    version=${version#v}
    if ! git -C "$repo" diff --quiet HEAD -- . ':(exclude)third_party/linphone-sdk'; then
        version="$version-dirty"
        echo "warning: the working tree has uncommitted changes; SOURCE.txt names the last commit" >&2
    fi
else
    version=$VERSION
fi
commit=$(git -C "$repo" rev-parse HEAD)
sdk_commit=$(git -C "$repo" rev-parse HEAD:third_party/linphone-sdk)
url=${AOS_SOURCE_URL:-$(git -C "$repo" remote get-url origin 2>/dev/null || true)}
url=${url%.git}
case "$url" in
git@*:*) url=${url#git@}; url="https://${url/://}" ;;
esac

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$out"
out=$(cd "$out" && pwd)

# archive <name> <entry>...: packs the entries of the staging directory of
# the current variant into <name>.zip and <name>.lha in the output directory.
archive() {
    local name=$1
    shift
    rm -f "$out/$name.zip" "$out/$name.lha"
    (cd "$vstage" && zip -q -r -X "$out/$name.zip" "$@")
    echo "$out/$name.zip"
    if [ $make_lha -eq 1 ]; then
        (cd "$vstage" && "$lha" a "$out/$name.lha" "$@" >/dev/null)
        echo "$out/$name.lha"
    fi
}

# put_binary <file> <destination>: the program or driver as built for the
# debug variant, stripped for the release one.
put_binary() {
    if [ "$variant" = release ]; then
        "$strip" --strip-unneeded -R .comment -o "$2" "$1"
    else
        cp "$1" "$2"
    fi
}

for variant in release debug; do
    vstage="$stage/$variant"
    if [ "$variant" = release ]; then
        suffix=""
        variant_note="The programs are stripped; the archives named -debug hold the same
build with symbols and debug information."
    else
        suffix="-debug"
        variant_note="The programs keep their symbols and debug information; the archives
without -debug hold the same build stripped."
    fi

    d="$vstage/aos-linphone"
    mkdir -p "$d/share/belr/grammars" "$d/share/sounds/linphone/rings" "$d/share/images"
    put_binary "$build/aos-phone" "$d/aos-phone"
    put_binary "$build/aos-phone-cmd" "$d/aos-phone-cmd"
    [ -z "$register" ] || put_binary "$register" "$d/aos-register"
    cp "$build/aos-phone.info" "$d/"
    cp "$build/aos-linphone.info" "$vstage/"
    cp "${grammars[@]}" "$d/share/belr/grammars/"
    cp "$sounds/rings/oldphone-mono.wav" "$d/share/sounds/linphone/rings/"
    cp "$sounds/ringback.wav" "$d/share/sounds/linphone/"
    cp "$prefix/share/images/nowebcamCIF.jpg" "$d/share/images/"
    cp "$repo/README.md" "$repo/LICENCE.md" "$d/"
    cp "$repo/apps/aos-phone/README.md" "$d/aos-phone.md"
    [ -z "$register" ] || cp "$repo/apps/aos-register/README.md" "$d/aos-register.md"
    cat > "$d/SOURCE.txt" <<EOF
aos-linphone $version for $system_label

This is free software under the GNU Affero General Public License, version 3
or later; see LICENCE.md. The complete source of this build:

  aos-linphone   ${url:-(no source URL known)}
                 commit $commit
  linphone-sdk   https://gitlab.linphone.org/BC/public/linphone-sdk.git
                 commit $sdk_commit, with the patches in patches/linphone-sdk
                 of aos-linphone

sdk/README.md in the aos-linphone source tells how to build it.
$variant_note
EOF
    archive "aos-linphone-$version-$target$suffix" aos-linphone.info aos-linphone

    if [ ${#drivers[@]} -gt 0 ]; then
        d="$vstage/aos-linphone-drivers"
        mkdir -p "$d"
        for driver in "${drivers[@]}"; do
            case "$driver" in
            *.class) dir="$d/$class_dir" ;;
            *.device) dir="$d/Devs/USBHardware" ;;
            esac
            mkdir -p "$dir"
            put_binary "$driver" "$dir/$(basename "$driver")"
        done
        cp "$repo/drivers/README.md" "$d/README.md"
        cp "$repo/drivers/uvc/LICENSE" "$d/LICENSE"
        cat > "$d/SOURCE.txt" <<EOF
aos-linphone-drivers $version for $system_label

USB drivers ported from AROS, under the AROS Public License, version 1.1;
see LICENSE. The source of this build is in drivers/ of aos-linphone:

  aos-linphone   ${url:-(no source URL known)}
                 commit $commit

README.md tells how to build them and how to install them.
$variant_note
EOF
        archive "aos-linphone-drivers-$version-$target$suffix" aos-linphone-drivers
    fi
done
