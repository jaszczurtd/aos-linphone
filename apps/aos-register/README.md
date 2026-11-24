# aos-register

A console program that registers a SIP account from AROS with liblinphone,
then unregisters it. It checks that the SDK built for AROS can talk to a SIP
server over the AROS network stack.

```
aos-register <identity> <password> [<proxy>] [<seconds>]
```

- `identity`: the account's SIP address, for example `sip:alice@192.168.122.1`
- `password`: its password
- `proxy`: the registrar, by default the identity's domain over UDP
- `seconds`: time allowed for each of the two steps, default 20

The program prints each registration state and ends with `result: PASS`
and exit code 0 when the account registered and unregistered. It returns 10
when a step failed or timed out, and 20 on bad arguments or when
AROSTCP is not running.

## Building

Build and install the SDK first, see [sdk/README.md](../../sdk/README.md).
Then configure with the same prefix, toolchain file and initial cache:

```sh
cmake -S apps/aos-register -B build-register -G Ninja \
      -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
      -DAROS_TOOLCHAIN_DIR=<toolchain>
cmake --build build-register
```

## Running

The program needs the belr grammars at run time: copy
`<prefix>/share/belr/grammars/*.belr` to `share/belr/grammars/` in the
program's directory. The core keeps its configuration in `RAM:aos-register`.

`tools/sip-registrar` is a minimal registrar for the test, built and run
on another machine on the network:

```sh
cmake -S tools -B build-tools && cmake --build build-tools
build-tools/sip-registrar -a <address> -x
```

It serves the account `alice` with password `secret` in the realm
`aos-test`, challenges every REGISTER with an MD5 digest and prints one line
per event. With `-x` it exits with status 0 as soon as the account has
registered and unregistered. Then, on AROS:

```
aos-register sip:alice@<address> secret
```
