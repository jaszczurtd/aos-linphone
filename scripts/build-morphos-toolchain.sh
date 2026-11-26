#!/usr/bin/env bash
# Builds the cross toolchain of the MorphOS build from pkgsrc, without root:
# cross/ppc-morphos-gcc (GCC 15.2 with the MorphOS patches of the package)
# together with ppc-morphos-binutils (2.45.1) and ppc-morphos-sdk (MorphOS
# SDK 3.20, sdk-20260529.lha).
#
# usage: scripts/build-morphos-toolchain.sh <work dir>
#
# The compiler ends up in <work dir>/pkg/gg/bin (ppc-morphos-gcc, -g++,
# ...), with <work dir>/pkg/gg as its sysroot: that directory is the
# toolchain directory of scripts/build-sdk.sh and scripts/build-release.sh.
# The tree records its own path, so it cannot be moved after the build.
#
# pkgsrc comes from the GitHub mirror of NetBSD pkgsrc at a pinned commit and
# is bootstrapped unprivileged into <work dir>/pkg. It builds its own tools
# and libraries (perl, gmp, mpfr, mpc, lha...), so the host needs only a
# C/C++ compiler, make, curl and tar. pkgsrc downloads the SDK archive from
# morphos-team.net and checks it against its distinfo; MORPHOS_SDK_DISTFILE
# names a local copy of sdk-20260529.lha to use instead. JOBS sets the number
# of parallel jobs (default: all processors).
#
# GCC gets the patches in patches/ppc-morphos-gcc/ as pkgsrc local patches,
# applied with patch -p0 after the MorphOS patches of the package. The
# installed toolchain records the pkgsrc commit and the checksums of these
# patches: a run with the same ones does nothing, a run with other patches
# rebuilds only the GCC package. Another pkgsrc commit needs a new work
# directory. A full build takes about 13 minutes with 12 jobs; pkg/ then
# holds about 620 MB, the pkgsrc tree next to it about 940 MB.
#
# The MorphOS SDK may not be distributed separately, and the licence of LHa,
# which unpacks it, restricts redistribution too. The script accepts both
# licences for this build: keep the toolchain, which holds the SDK, to
# yourself.
set -euo pipefail

PKGSRC_COMMIT=640adb73b8acffe470c3d5774e4fc0fbb60e2954
SDK_DISTFILE=sdk-20260529.lha

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <work dir>" >&2
    exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$1"
work=$(cd "$1" && pwd)
prefix=$work/pkg
jobs=${JOBS:-$(nproc)}
stamp=$prefix/gg/.toolchain-stamp

patches=("$repo"/patches/ppc-morphos-gcc/*.patch)
[[ -f "${patches[0]}" ]] || { echo "error: no patches in $repo/patches/ppc-morphos-gcc" >&2; exit 1; }
want="pkgsrc $PKGSRC_COMMIT"
for p in "${patches[@]}"; do
    want+=$'\n'"$(sha256sum "$p" | cut -d' ' -f1) $(basename "$p")"
done

if [[ -x "$prefix/gg/bin/ppc-morphos-gcc" && "$(cat "$stamp" 2>/dev/null)" == "$want" ]]; then
    echo "$prefix/gg: $("$prefix/gg/bin/ppc-morphos-gcc" --version | head -1)"
    exit 0
fi

if [[ ! -d "$work/pkgsrc" ]]; then
    echo "== pkgsrc $PKGSRC_COMMIT"
    curl -fsSL -o "$work/pkgsrc.tar.gz" \
        "https://github.com/NetBSD/pkgsrc/archive/$PKGSRC_COMMIT.tar.gz"
    tar -xzf "$work/pkgsrc.tar.gz" -C "$work"
    mv "$work/pkgsrc-$PKGSRC_COMMIT" "$work/pkgsrc"
    rm "$work/pkgsrc.tar.gz"
    echo "$PKGSRC_COMMIT" > "$work/pkgsrc/.commit"
fi
if [[ "$(cat "$work/pkgsrc/.commit" 2>/dev/null)" != "$PKGSRC_COMMIT" ]]; then
    echo "error: $work/pkgsrc is not at $PKGSRC_COMMIT; use a new work directory" >&2
    exit 1
fi

if [[ ! -x "$prefix/bin/bmake" ]]; then
    echo "== bootstrap into $prefix"
    (cd "$work/pkgsrc/bootstrap" &&
        ./bootstrap --unprivileged --prefix="$prefix" \
            --workdir="$work/bootstrap-work" --make-jobs "$jobs")
    rm -rf "$work/bootstrap-work"
fi

# The settings of this script live in their own file, written on every run;
# mk.conf includes it.
settings=$work/toolchain.mk.conf
cat > "$settings" <<EOF
# Written by aos-linphone/scripts/build-morphos-toolchain.sh
DISTDIR=		$work/distfiles
WRKOBJDIR=		$work/pkgwork
MAKE_JOBS=		$jobs
ACCEPTABLE_LICENSES+=	morphos-sdk-license lha-license
LOCALPATCHES=		$work/localpatches
EOF
mkconf=$prefix/etc/mk.conf
if ! grep -qF ".include \"$settings\"" "$mkconf"; then
    echo ".include \"$settings\"" >> "$mkconf"
fi

rm -rf "$work/localpatches"
mkdir -p "$work/localpatches/cross/ppc-morphos-gcc"
cp "${patches[@]}" "$work/localpatches/cross/ppc-morphos-gcc/"

mkdir -p "$work/distfiles"
if [[ -n "${MORPHOS_SDK_DISTFILE:-}" && ! -f "$work/distfiles/$SDK_DISTFILE" ]]; then
    cp "$MORPHOS_SDK_DISTFILE" "$work/distfiles/$SDK_DISTFILE"
fi

echo "== cross/ppc-morphos-gcc"
export PATH=$prefix/bin:$prefix/sbin:$PATH
if pkg_info -q -e ppc-morphos-gcc; then
    echo "-- rebuilding GCC with the current local patches"
    pkg_delete ppc-morphos-gcc
fi
(cd "$work/pkgsrc/cross/ppc-morphos-gcc" && bmake install && bmake clean clean-depends)
# The binary packages bmake leaves next to the tree are not needed later.
rm -rf "$work/pkgsrc/packages"
printf '%s\n' "$want" > "$stamp"

tmp=$(mktemp -d "$work/.toolchain-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
printf '#include <stdio.h>\nint main(void) { puts("ok"); return 0; }\n' > "$tmp/t.c"
"$prefix/gg/bin/ppc-morphos-gcc" -mclib=libnix -o "$tmp/t" "$tmp/t.c"
echo "$prefix/gg: $("$prefix/gg/bin/ppc-morphos-gcc" --version | head -1)"
