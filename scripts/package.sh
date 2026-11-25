#!/bin/bash
# Packs the AROS build of the phone into an LHA and a ZIP archive, ready to
# unpack on AROS. Both hold the drawer aos-linphone and its icon:
#
#   aos-linphone.info                   the drawer icon
#   aos-linphone/aos-phone(.info)       the phone and its icon
#   aos-linphone/aos-phone-cmd          the ARexx command tool
#   aos-linphone/aos-register           with -r: the registration test tool
#   aos-linphone/share/...              belr grammars, ring tones, still picture
#   aos-linphone/*.md, SOURCE.txt       documentation, licence, where the
#                                       source of this build is
#
# usage: scripts/package.sh [-o <out dir>] [-r <aos-register>] [--no-lha]
#                           <phone build dir> <prefix>
#
#   phone build dir  the build directory of apps/aos-phone, holding
#                    aos-phone, aos-phone-cmd, aos-phone.info and
#                    aos-linphone.info (the icons need ilbmtoicon)
#   prefix           the SDK prefix of scripts/build-sdk.sh
#   -o <out dir>     where the archives go, default <repository>/builds
#   -r <file>        also pack this aos-register executable
#   --no-lha         make the ZIP archive only
#
# The archives are named aos-linphone-<version>-aros-x86_64.lha and .zip.
# <version> is VERSION when set, else `git describe` of the release tags
# without the v: 0.1 on the commit tagged v0.1, 0.1-3-g1234abc three commits
# later, the short commit before the first tag; -dirty is added when files
# other than the submodule differ from the last commit. A commit tagged v*
# must carry that version in cmake/version.cmake, else the script stops.
# The LHA archive needs an lha archiver: LHA names it, else lha (LHa for
# UNIX) or jlha (jlha-utils) from the PATH. The source URL in SOURCE.txt comes from the remote origin, or from
# AOS_SOURCE_URL when set.

set -euo pipefail

usage() {
    echo "usage: $0 [-o <out dir>] [-r <aos-register>] [--no-lha] <phone build dir> <prefix>" >&2
    exit 2
}

repo=$(cd "$(dirname "$0")/.." && pwd)
out="$repo/builds"
register=""
make_lha=1
while [ $# -gt 0 ]; do
    case "$1" in
    -o) [ $# -ge 2 ] || usage; out=$2; shift 2 ;;
    -r) [ $# -ge 2 ] || usage; register=$2; shift 2 ;;
    --no-lha) make_lha=0; shift ;;
    -*) usage ;;
    *) break ;;
    esac
done
[ $# -eq 2 ] || usage
build=$(cd "$1" && pwd)
prefix=$(cd "$2" && pwd)
sounds="$repo/third_party/linphone-sdk/liblinphone/share"

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
name="aos-linphone-$version-aros-x86_64"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
d="$stage/aos-linphone"
mkdir -p "$d/share/belr/grammars" "$d/share/sounds/linphone/rings" "$d/share/images"
cp "$build/aos-phone" "$build/aos-phone-cmd" "$build/aos-phone.info" "$d/"
cp "$build/aos-linphone.info" "$stage/"
[ -z "$register" ] || cp "$register" "$d/aos-register"
cp "${grammars[@]}" "$d/share/belr/grammars/"
cp "$sounds/rings/oldphone-mono.wav" "$d/share/sounds/linphone/rings/"
cp "$sounds/ringback.wav" "$d/share/sounds/linphone/"
cp "$prefix/share/images/nowebcamCIF.jpg" "$d/share/images/"
cp "$repo/README.md" "$repo/LICENCE.md" "$d/"
cp "$repo/apps/aos-phone/README.md" "$d/aos-phone.md"
[ -z "$register" ] || cp "$repo/apps/aos-register/README.md" "$d/aos-register.md"
cat > "$d/SOURCE.txt" <<EOF
aos-linphone $version for AROS x86_64

This is free software under the GNU Affero General Public License, version 3
or later; see LICENCE.md. The complete source of this build:

  aos-linphone   ${url:-(no source URL known)}
                 commit $commit
  linphone-sdk   https://gitlab.linphone.org/BC/public/linphone-sdk.git
                 commit $sdk_commit, with the patches in patches/linphone-sdk
                 of aos-linphone

sdk/README.md in the aos-linphone source tells how to build it.
EOF

mkdir -p "$out"
out=$(cd "$out" && pwd)
rm -f "$out/$name.zip" "$out/$name.lha"
(cd "$stage" && zip -q -r -X "$out/$name.zip" aos-linphone.info aos-linphone)
echo "$out/$name.zip"
if [ $make_lha -eq 1 ]; then
    (cd "$stage" && "$lha" a "$out/$name.lha" aos-linphone.info aos-linphone >/dev/null)
    echo "$out/$name.lha"
fi
