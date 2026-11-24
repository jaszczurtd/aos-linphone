#!/bin/sh
# Applies the patches in patches/linphone-sdk/ to the third_party/linphone-sdk
# submodule, in file name order.
#
#   patches/apply.sh            apply the patches that are not applied yet
#   patches/apply.sh --check    report the state of each patch, change nothing
#   patches/apply.sh --reverse  remove the applied patches, last one first
#
# Exit code: 0 on success, 1 when a patch applies neither way (the submodule
# is at a different revision or has local changes in the same lines).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
sdk="$here/../third_party/linphone-sdk"
mode=apply

case "${1:-}" in
    "") ;;
    --check) mode=check ;;
    --reverse) mode=reverse ;;
    *) echo "usage: $0 [--check | --reverse]" >&2; exit 2 ;;
esac

if [ ! -e "$sdk/.git" ]; then
    echo "$sdk is not checked out; run: git submodule update --init third_party/linphone-sdk" >&2
    exit 1
fi

# One patch path per line, so that paths with spaces survive the loop.
nl='
'
IFS=$nl
if [ "$mode" = reverse ]; then
    patches=$(ls -r "$here"/linphone-sdk/*.patch)
else
    patches=$(ls "$here"/linphone-sdk/*.patch)
fi

status=0
for patch in $patches; do
    name=$(basename "$patch")
    if git -C "$sdk" apply --check "$patch" 2>/dev/null; then
        state=missing
    elif git -C "$sdk" apply --check --reverse "$patch" 2>/dev/null; then
        state=applied
    else
        state=conflict
    fi

    case "$mode:$state" in
        apply:missing)
            git -C "$sdk" apply "$patch"
            echo "applied  $name" ;;
        reverse:applied)
            git -C "$sdk" apply --reverse "$patch"
            echo "removed  $name" ;;
        apply:applied | reverse:missing)
            echo "$state  $name (unchanged)" ;;
        check:*)
            echo "$state  $name" ;;
    esac
    if [ "$state" = conflict ]; then
        echo "  $name applies neither way; check the submodule revision" >&2
        status=1
    fi
done
exit $status
