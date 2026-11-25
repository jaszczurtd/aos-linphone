# aos-phone

A SIP phone for AROS with a Zune interface, built on liblinphone. It
registers an account, makes and answers calls with sound and video, and
exchanges text messages. You hear the other side and the ring tone through
AHI and see its picture in the call window. The program does not take
sound from a microphone or pictures from a camera yet: the other side hears
silence, or a sound file you play into the call, and sees one of the test
pictures of mediastreamer2. Calls can also be recorded to files.

```
aos-phone [DATA=<directory>] [PORT=<number>] [RECORD=<directory>]
          [CAMERA=<name>] [VERBOSE]
```

- `DATA`: where the program keeps its settings, including the account and
  its password; default `PROGDIR:aos-phone-data`. Give a directory, not the
  root of a volume.
- `PORT`: the UDP port for SIP; by default a random free one. A fixed port
  helps when others call this computer by its IP address, without a SIP
  server.
- `RECORD`: records every call to a WAV file in this directory, both sides
  mixed, from the moment the call connects; by default nothing is recorded.
  The program creates the directory when it is missing and names the files
  `call-<date>-<time>-<n>.wav`.
- `CAMERA`: the camera video comes from, given by a part of its name:
  `Mire` sends a moving test picture, `StaticImage` a still picture, which
  is also the default. A name no camera has keeps the default and says so
  in the status line.
- `VERBOSE`: prints all messages of liblinphone, not only warnings and
  errors. They tell, among other things, which camera, codecs and picture
  sizes a call uses.

The program needs AROSTCP running and the belr grammars in
`share/belr/grammars/` next to it, as `aos-register` does (see
[its README](../aos-register/README.md#running)). It prints what happens
to its standard output.

Sound goes to the default unit of `ahi.device`, which you choose in the AHI
preferences. The ring tone and the tone you hear while the other side
rings are files that the SDK build does not install; copy them from the
submodule next to the program:

| From `third_party/linphone-sdk/liblinphone/share/` | To |
|---|---|
| `rings/oldphone-mono.wav` | `share/sounds/linphone/rings/oldphone-mono.wav` |
| `ringback.wav` | `share/sounds/linphone/ringback.wav` |

Without them the calls work, but nothing rings. The still picture comes
from `share/images/nowebcamCIF.jpg`, which the SDK installs into
`<prefix>/share/images/`; copy it next to the program too, or the camera
sends a black picture.

Started from its icon, `aos-phone` takes the same settings from the tool
types `DATA`, `PORT`, `RECORD`, `CAMERA` and `VERBOSE`. Its output then
goes nowhere, so no window opens for it. The tool type `DEBUG` shows the
output in a console window, which stays open after the program ends until
you close it. The icon comes with these tool types switched off, in
parentheses: remove the parentheses in the icon information to switch one
on. When the program cannot start
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
you type is sent as UTF-8. Letters beyond Latin-1, Polish ones for example,
also need a system code page that has them (set by the Locale preferences)
and an outline font with their glyphs in the Zune preferences: the bitmap
fonts AROS installs cover Latin-1 only.

Every call offers video and accepts it when the other side offers it. The
call window then shows the picture of the other side and, next to it, the
picture this side sends; both keep their proportions when you resize the
window. Video uses VP8, so the other client has to support it; a client or
SIP server without video leaves the call with sound only.

Calls use G.711, G.722, Speex or Opus for sound, whichever both sides
support. To encrypt the media with SRTP, add `media_encryption=srtp` to the `[sip]`
section of `linphonerc` in the `DATA` directory while the program is not
running; the other client then has to support SRTP as well.

## ARexx port

The port `AOSPHONE.1` takes these commands; they do what the buttons do:

| Command | Effect |
|---|---|
| `REGISTER <identity> <password> [<server>]` | sets the account and registers it |
| `UNREGISTER` | unregisters the account |
| `CALL <address>` | calls the address |
| `ANSWER` | answers the call in the call window, or the first incoming call |
| `HANGUP` | declines or ends the call in the call window, or the current call |
| `PLAY <file>` | plays a WAV file to the other side of the connected call in the call window, or of the current call |
| `SNAPSHOT <file>` | saves the next picture received in that call as a JPEG file |
| `SEND <address> <text>` | sends a text message and shows it in its chat window |
| `STATUS` | returns the state, e.g. `registered\|port 5060\|call 1 connected sip:bob@example.org video` |
| `QUIT` | ends the program |

A command returns 0, or 10 when it could not be passed on. Its `RESULT` is
the state for `STATUS` and `OK` or `ERROR` for the others.

`PLAY` mixes the file into what the call sends; a new `PLAY` replaces the
file still playing. The file may have any sample rate. After the call has
been paused, resumed or renegotiated, `PLAY` refuses with a message in the
status line: liblinphone keeps the player of the first sound stream of the
call, which such a change replaces.

`SNAPSHOT` works in a call with video. The status line names the file when
the request is passed to the call; the picture is written a moment later,
when the next one arrives.

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

The build gives `aos-phone`, `aos-phone.info`, `aos-phone-cmd`,
`test_queue` and `test_video_display`. `aos-phone.info` is the Workbench icon; copy it next to the
program. `ilbmtoicon` is a host tool built with AROS itself; without
`AOS_ILBMTOICON` (or `ilbmtoicon` in the `PATH`) the build skips the icon.
The icon is made from `icons/aos-phone.png` and `icons/aos-phone.info.src`,
which also holds its stack size and tool types. With
`-DAOS_TEST_RUNNER=...`, as for the tests of the compatibility layer (see
the [main README](../../README.md#running-the-tests)), CTest runs both
tests on AROS.

## Structure

| File | Contents |
|---|---|
| `src/core.c`, `src/core.h` | the linphone core in its own thread; the only code that calls liblinphone |
| `src/queue.c`, `src/queue.h` | the fixed-size queues for commands and events between the two threads |
| `src/gui.c`, `src/gui.h` | the Zune windows, the ARexx port and the handling of core events |
| `src/text.c`, `src/text.h` | conversion between UTF-8 and the system code page |
| `src/video.c`, `src/video.h` | the Zune area that draws one picture of a call |
| `src/main.c` | arguments, tool types and start-up |
| `src/cmd.c` | `aos-phone-cmd` |
| `icons/` | the image and description of the Workbench icon |
| `tests/test_queue.c` | tests of the queues, run on AROS |
| `../../devices/ahi/` | the AHI sound card for mediastreamer2, shared with other programs |
| `../../devices/video/` | the video display and the Mire test camera for mediastreamer2 |
| `../../devices/tests/test_video_display.c` | tests of the video display, run on AROS |

The core thread creates the linphone core and makes every liblinphone call,
including the callbacks; each such function checks that it runs in that
thread and stops the program otherwise. The interface copies commands into
one queue and takes events from the other. After each event the core
thread sends an Exec signal, which the interface handles as a Zune input
handler, so events also arrive while a nested Zune loop runs.

The core thread registers the AHI card with mediastreamer2 right after it
creates the core. Each sound that plays, the call or the ring tone, gets
its own filter with a writer thread, which opens `ahi.device` and keeps
two linked 40 ms writes going. The filter only queues the samples, so the
mediastreamer2 ticker does not wait for the device.

Pictures take another way. The core thread registers the video display
filter of the device layer and gives liblinphone, as the video window, a
surface that the interface created. The filter runs in the mediastreamer2
ticker: it scales each picture to the size of the area that shows it,
converts it to ARGB and leaves it on the surface, then wakes the interface
with a second Exec signal. The interface draws the newest picture with
`WritePixelArray()`. A hidden area, in a closed call window for one, has
size zero, and the filter drops its pictures before converting them.
