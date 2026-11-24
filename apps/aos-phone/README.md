# aos-phone

A SIP phone for AROS with a Zune interface, built on liblinphone. It
registers an account, makes and answers calls and exchanges text messages.
Calls connect, but carry no sound yet: AROS has no sound device for
mediastreamer2 so far, so the program sends silence and drops what it
receives.

```
aos-phone [DATA=<directory>] [PORT=<number>]
```

- `DATA`: where the program keeps its settings, including the account and
  its password; default `PROGDIR:aos-phone-data`. Give a directory, not the
  root of a volume.
- `PORT`: the UDP port for SIP; by default a random free one. A fixed port
  helps when others call this computer by its IP address, without a SIP
  server.

The program needs AROSTCP running and the belr grammars in
`share/belr/grammars/` next to it, as `aos-register` does (see
[its README](../aos-register/README.md#running)). It prints what happens
to its standard output.

Started from its icon, `aos-phone` takes the same settings from the tool
types `DATA` and `PORT`. Its output then goes nowhere, so no window opens
for it. The tool type `DEBUG` shows the output in a console window, which
stays open after the program ends until you close it. The icon comes with
all three tool types switched off, in parentheses: remove the parentheses
in the icon information to switch one on. When the program cannot start
from its icon (no network, for one), it says why in a requester.

## Using it

The main window shows the account, an address field and the list of
calls:

- **Account...** opens the account window. Enter the identity, e.g.
  `sip:alice@example.org`, the password and, when the registrar is not the
  identity's domain, the server, e.g. `sip:example.org:5060`. **Register**
  saves the account and registers it; the program registers it again at
  every start until **Unregister**.
- **Call** calls the address in the field: a full SIP address, or a user
  name in the account's domain. The call window opens with the other party,
  the state, the duration and the sound levels. **Answer** and **Hang up**
  act on the call shown there; selecting a call in the list shows it.
- **Chat** opens a chat window for the address. Messages that arrive open
  the window of their sender by themselves. Windows are matched by the user
  name of the address, so `alice` and `sip:alice@example.org` share one.

Zune shows 8-bit text in the code page of the system. Received messages are
converted from UTF-8, and characters the code page lacks show as `?`; what
you type is sent as UTF-8.

## ARexx port

The port `AOSPHONE.1` takes these commands; they do what the buttons do:

| Command | Effect |
|---|---|
| `REGISTER <identity> <password> [<server>]` | sets the account and registers it |
| `UNREGISTER` | unregisters the account |
| `CALL <address>` | calls the address |
| `ANSWER` | answers the call in the call window, or the first incoming call |
| `HANGUP` | declines or ends the call in the call window, or the current call |
| `SEND <address> <text>` | sends a text message and shows it in its chat window |
| `STATUS` | returns the state, e.g. `registered\|port 5060\|call 1 connected sip:bob@example.org` |
| `QUIT` | ends the program |

A command returns 0, or 10 when it could not be passed on. Its `RESULT` is
the state for `STATUS` and `OK` or `ERROR` for the others.

`aos-phone-cmd` sends one command to the port and prints the result,
without an ARexx interpreter:

```
aos-phone-cmd [PORT=<name>] <command>
```

For example `aos-phone-cmd CALL sip:bob@example.org` or
`aos-phone-cmd STATUS`. Its exit code is the command's, or 20 when the port
does not exist.

## Building

Build and install the SDK first, see [sdk/README.md](../../sdk/README.md).
Then configure with the same prefix, toolchain file and initial cache:

```sh
cmake -S apps/aos-phone -B build-phone -G Ninja \
      -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
      -DAROS_TOOLCHAIN_DIR=<toolchain> \
      -DAOS_ILBMTOICON=<AROS build>/bin/<host>/tools/ilbmtoicon
cmake --build build-phone
```

The build gives `aos-phone`, `aos-phone.info`, `aos-phone-cmd` and
`test_queue`. `aos-phone.info` is the Workbench icon; copy it next to the
program. `ilbmtoicon` is a host tool built with AROS itself; without
`AOS_ILBMTOICON` (or `ilbmtoicon` in the `PATH`) the build skips the icon.
The icon is made from `icons/aos-phone.png` and `icons/aos-phone.info.src`,
which also holds its stack size and tool types. With
`-DAOS_TEST_RUNNER=...`, as for the tests of the compatibility layer (see
the [main README](../../README.md#running-the-tests)), CTest runs
`test_queue` on AROS.

## Structure

| File | Contents |
|---|---|
| `src/core.c`, `src/core.h` | the linphone core in its own thread; the only code that calls liblinphone |
| `src/queue.c`, `src/queue.h` | the fixed-size queues for commands and events between the two threads |
| `src/gui.c`, `src/gui.h` | the Zune windows, the ARexx port and the handling of core events |
| `src/text.c`, `src/text.h` | conversion between UTF-8 and the system code page |
| `src/main.c` | arguments, tool types and start-up |
| `src/cmd.c` | `aos-phone-cmd` |
| `icons/` | the image and description of the Workbench icon |
| `tests/test_queue.c` | tests of the queues, run on AROS |

The core thread creates the linphone core and makes every liblinphone call,
including the callbacks; each such function checks that it runs in that
thread and stops the program otherwise. The interface copies commands into
one queue and takes events from the other. After each event the core
thread sends an Exec signal, which the interface handles as a Zune input
handler, so events also arrive while a nested Zune loop runs.
