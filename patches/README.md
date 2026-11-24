# Patches for linphone-sdk

`third_party/linphone-sdk` is pinned to an upstream revision and stays
unmodified in git. The few source changes the AROS build needs live here as
patch files and go on top of the checked-out submodule before the SDK is
configured. Everything else (POSIX gaps, sockets, build settings) is handled
outside the SDK, in `compat/`, `deps/`, `cmake/` and `sdk/`.

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
