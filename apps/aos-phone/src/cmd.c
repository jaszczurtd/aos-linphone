/*
 * aos-phone-cmd - sends one command to the ARexx port of aos-phone and
 * prints its result, without an ARexx interpreter.
 *
 * usage: aos-phone-cmd [PORT=<name>] <command>
 *
 *   PORT     the ARexx port, default AOSPHONE.1
 *   command  e.g. STATUS, CALL sip:alice@example.org, SEND alice Hello
 *
 * Exit code: the command's return code (0 OK, 10 ERROR), or 20 when the
 * port does not exist or the message cannot be built.
 */
#include <dos/dos.h>
#include <exec/ports.h>
#include <rexx/storage.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>

#include <stdio.h>
#include <string.h>

/* For the Version command; AOS_VERSION comes from cmake/version.cmake. */
static const char version[] __attribute__((used)) = "$VER: aos-phone-cmd " AOS_VERSION " (" AOS_VERSION_DATE ")";

int main(void) {
    SIPTR args[2] = {0, 0};
    struct RDArgs *rdargs = ReadArgs((CONST_STRPTR) "PORT/K,COMMAND/A/F", args, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR) "aos-phone-cmd");
        return 20;
    }
    const char *port_name = args[0] != 0 ? (const char *)args[0] : "AOSPHONE.1";
    const char *command = (const char *)args[1];

    int rc = 20;
    struct MsgPort *reply = CreateMsgPort();
    struct RexxMsg *msg = reply != NULL ? CreateRexxMsg(reply, NULL, NULL) : NULL;
    UBYTE *text = msg != NULL ? CreateArgstring((CONST_STRPTR)command, strlen(command)) : NULL;
    if (text != NULL) {
        msg->rm_Args[0] = (IPTR)text;
        msg->rm_Action = RXCOMM | RXFF_RESULT;

        Forbid();
        struct MsgPort *port = FindPort((CONST_STRPTR)port_name);
        if (port != NULL) {
            PutMsg(port, &msg->rm_Node);
        }
        Permit();

        if (port == NULL) {
            fprintf(stderr, "aos-phone-cmd: no port %s; is aos-phone running?\n", port_name);
        } else {
            WaitPort(reply);
            GetMsg(reply);
            rc = (int)msg->rm_Result1;
            if (rc == 0 && msg->rm_Result2 != 0) {
                printf("%s\n", (const char *)msg->rm_Result2);
                DeleteArgstring((UBYTE *)msg->rm_Result2);
            }
        }
        DeleteArgstring(text);
    } else {
        fprintf(stderr, "aos-phone-cmd: cannot build the message\n");
    }
    if (msg != NULL) {
        DeleteRexxMsg(msg);
    }
    if (reply != NULL) {
        DeleteMsgPort(reply);
    }
    FreeArgs(rdargs);
    return rc;
}
