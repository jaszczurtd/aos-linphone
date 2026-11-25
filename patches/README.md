# Patches for linphone-sdk

`third_party/linphone-sdk` is pinned to an upstream revision and stays
unmodified in git. The few source changes the AROS build needs live here as
patch files and go on top of the checked-out submodule before the SDK is
configured. Everything else (POSIX gaps, sockets, build settings) is handled
outside the SDK, in `compat/`, `deps/`, `cmake/` and `sdk/`.

## Upstream revision

This set of patches is made for one exact revision of linphone-sdk, the
one the submodule `third_party/linphone-sdk` is pinned to:

| | |
|---|---|
| Repository | <https://gitlab.linphone.org/BC/public/linphone-sdk.git> |
| Branch | `release/5.5` |
| Commit | `d2a55124fb8ed09043a00fbf5b321dff037528cf` (2026-11-17) |
| `git describe` | `5.5.41-14-gd2a55124fb`: 14 commits after the tag `5.5.41` |

The patches are tested on that commit only. It is not a tagged release:
the tag `5.5.41` itself, a later commit of `release/5.5` or another branch
may need different patches, and some of these may not apply there at all.
Run `patches/apply.sh --check` before building any other revision. When
the submodule moves to a new commit, the patches and this table change
together.

A clone of aos-linphone gets exactly this commit with
`git submodule update --init third_party/linphone-sdk`. To fetch the same
revision on its own, for example to read the sources or to try the
patches outside the repository:

```sh
git init linphone-sdk
cd linphone-sdk
git remote add origin https://gitlab.linphone.org/BC/public/linphone-sdk.git
git fetch --depth 1 origin d2a55124fb8ed09043a00fbf5b321dff037528cf
git checkout FETCH_HEAD
git apply <aos-linphone>/patches/linphone-sdk/*.patch
```

The fetch brings this one commit without history (about 250 MB checked
out, without the nested submodules) and without tags. The SDK build takes
its version from `git describe`, so build such a checkout with
`LINPHONESDK_VERSION` set, see [sdk/README.md](../sdk/README.md).

## Applying

```sh
git submodule update --init third_party/linphone-sdk
patches/apply.sh
```

`apply.sh` applies the patches in file name order and skips the ones already
applied, so it is safe to run again. `patches/apply.sh --check` only reports
the state of each patch (`missing`, `applied` or `conflict`), and
`patches/apply.sh --reverse` takes them all out again, last one first.

A patch reported as `conflict` applies neither forwards nor backwards: the
submodule is at another revision, or local edits touch the same lines. Moving
the submodule to a new upstream revision means refreshing the patches: apply
them by hand with `git -C third_party/linphone-sdk apply --3way`, resolve,
and regenerate the patch file from `git diff`.

The patches touch files of the superproject only, not of its nested
submodules, so `git -C third_party/linphone-sdk diff` shows all of them.

## The patches

| Patch | Why |
|---|---|
| `0001-bctoolbox-thread-name-else.patch` | `bctbx_set_self_thread_name()` ends its platform chain with a bare `#elif`, which GCC rejects on systems that are neither Linux, Apple nor Windows. |
| `0002-liblinphone-paths-aros.patch` | liblinphone picks its data and config paths per platform and stops with `#error` on any other system; AROS uses the Linux variant (`$HOME`, falling back to the current directory). |
| `0004-bctoolbox-config-bcunit-only-with-tester.patch` | The installed `BCToolboxConfig.cmake` of a static build always looks for BCUnit, even when the tester library is not built, so programs linking the SDK fail to configure. |
| `0007-ortp-netsim-priority-only-when-enabled.patch` | `rtp_session_enable_network_simulation()` raises the priority of the calling thread to `sched_get_priority_max(SCHED_OTHER)` even when it disables the simulator, and liblinphone calls it for every stream. On Linux that maximum is 0, so nothing changes; on AROS it is 2, and the thread that runs the linphone core would stay at the priority of the MSTicker threads. The patch raises the priority only when the simulator is enabled. |
| `0008-liblinphone-ticker-of-prepared-stream.patch` | Preparing a video stream (and an audio stream when ICE is on) starts its MSTicker after liblinphone has copied the stream's sessions, so the copy that `finish()` destroys has no ticker. When the stream never starts, for example because the other side answers without video, the ticker thread outlives the call. On Linux that goes unnoticed; on AROS, `pthread.library` waits at program exit for every thread, so the program never ends. The patch copies the sessions again after preparing the stream. |
