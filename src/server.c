/* Control4Free -- a small command server, so experiments stop depending on
 * GoldHEN's PayLoader.
 *
 * The PayLoader stops listening a few minutes after it is started, which meant
 * restarting it by hand before every single test. This payload is sent once and
 * then stays resident, listening on its own port, so the PC can create virtual
 * pads and inject input as often as it likes over one connection. Replies come
 * straight back down the socket, so there is no log file to fetch either.
 *
 * It is deliberately short-lived: an idle client or a long session makes it shut
 * down on its own, because it is running inside somebody else's process
 * (ScePartyDaemon) and must not sit there forever.
 *
 * Protocol: one ASCII command per line, one reply line per command.
 *   add [userhex]           create a virtual pad (default 1 = selection screen)
 *   padmbus                 call scePadMbusInit() (start the MBus client)
 *   mbusinit                call sceMbusInit()
 *   bind [userhex] [authid] give the pad to a user (default: signed-in, ShellCore authid)
 *   del                     delete it
 *   press <hexbits> [ms]    hold those button bits
 *   stick <lx> <ly> <rx> <ry> [ms]   hold stick positions (0-255, 128 centre)
 *   hold <ms>               neutral for that long (keeps the pad alive)
 *   sweep <first> <last> [ms]   press each single bit from first to last
 *   status                  what the server thinks is going on
 *   klog [lines]            recent system log (LOG lines, then OK klog finished)
 *   quit                    clean up and exit
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "c4f_log.h"
#include "c4f_sce.h"
#include "c4f_server.h"
#include "c4f_vda.h"

/* Shut down rather than linger inside the host process. */
#ifndef C4F_IDLE_TIMEOUT_S
#define C4F_IDLE_TIMEOUT_S    300
#endif
#ifndef C4F_SESSION_MAX_S
#define C4F_SESSION_MAX_S     1800
#endif
#define C4F_DEFAULT_HOLD_MS   150
#define C4F_MAX_HOLD_MS       30000
#define C4F_KLOG_LINES        128
#define C4F_KLOG_LINE_SIZE    512

static C4fVirtualPad g_pad;
static int           g_havePad;
static int           g_klogFd = -1;   /* how a new pad's handle is found */
static uint64_t      g_deadlineMs;
static uint64_t      g_nextFrameMs;
static uint64_t      g_frames;
static uint64_t      g_insertErrors;
static int32_t       g_lastInsert;
static int           g_replyFailed;

/* A bounded history: never echo these lines through c4fLog, which would feed
 * them straight back into the same klog stream. */
static char          g_klogLines[C4F_KLOG_LINES][C4F_KLOG_LINE_SIZE];
static char          g_klogPartial[C4F_KLOG_LINE_SIZE];
static size_t        g_klogUsed;
static unsigned      g_klogNext;
static unsigned      g_klogCount;
static uint64_t      g_klogTotal;

static uint64_t c4fNowMs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void c4fCaptureKlog(void)
{
    char buf[2048];
    int batch;

    if (g_klogFd < 0) return;
    /* Bound the work even when a noisy process keeps writing continuously. */
    for (batch = 0; batch < 8; batch++) {
        ssize_t n = read(g_klogFd, buf, sizeof(buf));
        ssize_t i;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) {
            g_klogFd = -1;  /* main still owns and closes the original fd */
            break;
        }
        for (i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') {
                g_klogPartial[g_klogUsed] = '\0';
                if (g_klogUsed && !strstr(g_klogPartial, "[c4f]")) {
                    (void)memcpy(g_klogLines[g_klogNext], g_klogPartial,
                                 g_klogUsed + 1);
                    g_klogNext = (g_klogNext + 1) % C4F_KLOG_LINES;
                    if (g_klogCount < C4F_KLOG_LINES) g_klogCount++;
                    g_klogTotal++;
                }
                g_klogUsed = 0;
            } else if ((ch >= 32 || ch == '\t') &&
                       g_klogUsed + 1 < sizeof(g_klogPartial)) {
                g_klogPartial[g_klogUsed++] = (char)ch;
            }
        }
    }
}

static int32_t c4fReport(const ScePadData *data)
{
    int32_t ret = c4fVirtualPadInsert(&g_pad, data);
    g_frames++;
    if (ret < 0) {
        g_insertErrors++;
        if (ret != g_lastInsert)
            c4fLog("InsertData failed = 0x%08x\n", (uint32_t)ret);
    }
    g_lastInsert = ret;
    g_nextFrameMs = c4fNowMs() + C4F_FRAME_MS;
    return ret;
}

static void c4fPump(void)
{
    c4fCaptureKlog();
    if (g_havePad && c4fNowMs() >= g_nextFrameMs) {
        ScePadData neutral;
        c4fPadDataNeutral(&neutral);
        (void)c4fReport(&neutral);
    }
}

/* Wait in frame-sized slices, so silence or a disconnected client never stops
 * the pad reporting. All pad calls stay on the same thread. */
static int c4fWaitReadable(int fd, uint64_t untilMs)
{
    for (;;) {
        struct timeval tv;
        fd_set set;
        uint64_t now, waitMs;
        int ready;

        c4fPump();
        now = c4fNowMs();
        if (now >= untilMs || now >= g_deadlineMs) return 0;
        waitMs = untilMs - now;
        if (waitMs > g_deadlineMs - now) waitMs = g_deadlineMs - now;
        if (waitMs > C4F_FRAME_MS) waitMs = C4F_FRAME_MS;
        if (g_havePad && g_nextFrameMs > now && waitMs > g_nextFrameMs - now)
            waitMs = g_nextFrameMs - now;
        tv.tv_sec = 0;
        tv.tv_usec = (int)waitMs * 1000;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        ready = select(fd + 1, &set, NULL, NULL, &tv);
        if (ready < 0 && errno == EINTR) continue;
        if (ready != 0) return ready;
    }
}

static void c4fReply(int fd, const char *text)
{
    size_t left = strlen(text);
    while (left && !g_replyFailed) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        ssize_t sent = send(fd, text, left, flags);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) { g_replyFailed = 1; break; }
        text += sent;
        left -= (size_t)sent;
    }
}

static void c4fReplyFmt(int fd, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    c4fReply(fd, line);
    c4fLog("> %s", line);
}

/* Holds `buttons` and the given stick positions for durationMs. */
static int32_t c4fHold(uint32_t buttons, uint8_t lx, uint8_t ly,
                    uint8_t rx, uint8_t ry, int durationMs)
{
    ScePadData data;
    uint64_t untilMs;
    int32_t result = 0;

    if (durationMs < C4F_FRAME_MS) durationMs = C4F_FRAME_MS;
    if (durationMs > C4F_MAX_HOLD_MS) durationMs = C4F_MAX_HOLD_MS;
    untilMs = c4fNowMs() + (unsigned)durationMs;
    c4fPadDataNeutral(&data);
    data.buttons = buttons;
    data.lx = lx;
    data.ly = ly;
    data.rx = rx;
    data.ry = ry;

    while (c4fNowMs() < untilMs && c4fNowMs() < g_deadlineMs) {
        int32_t ret = c4fReport(&data);
        if (ret < 0 && result == 0) result = ret;
        c4fCaptureKlog();
        usleep(C4F_FRAME_MS * 1000);
    }
    return result;
}

static int32_t c4fNeutral(int durationMs)
{
    return c4fHold(0, 128, 128, 128, 128, durationMs);
}

static int c4fDuration(int fd, const char *arg, int fallback, int *out)
{
    char *end;
    long value;
    if (!arg) { *out = fallback; return 1; }
    errno = 0;
    value = strtol(arg, &end, 10);
    if (errno || end == arg || *end || value < C4F_FRAME_MS ||
        value > C4F_MAX_HOLD_MS) {
        c4fReplyFmt(fd, "ERR duration must be %d..%d ms\n",
                    C4F_FRAME_MS, C4F_MAX_HOLD_MS);
        return 0;
    }
    *out = (int)value;
    return 1;
}

static int c4fInputResult(int fd, uint64_t errorsBefore)
{
    if (c4fNowMs() >= g_deadlineMs) {
        c4fReplyFmt(fd, "ERR session deadline reached; input stopped\n");
        return 0;
    }
    if (g_insertErrors == errorsBefore) return 1;
    c4fReplyFmt(fd, "ERR InsertData failed; errors=%llu last=0x%08x (see log)\n",
                (unsigned long long)g_insertErrors, (uint32_t)g_lastInsert);
    return 0;
}

static int c4fNeedPad(int fd)
{
    if (g_havePad) return 1;
    c4fReplyFmt(fd, "ERR no pad; run add first\n");
    return 0;
}

/* Returns 0 to keep going, 1 to shut down. */
static int c4fCommand(int fd, char *line, int32_t realUserId)
{
    char *cmd = strtok(line, " \t");
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    char *a3 = strtok(NULL, " \t");
    char *a4 = strtok(NULL, " \t");
    char *a5 = strtok(NULL, " \t");
    uint64_t errorsBefore = g_insertErrors;

    if (!cmd || !*cmd) return 0;
    c4fLog("< %s %s %s\n", cmd, a1 ? a1 : "", a2 ? a2 : "");

    if (strcmp(cmd, "quit") == 0) {
        c4fReplyFmt(fd, "OK bye\n");
        return 1;
    }

    if (strcmp(cmd, "status") == 0) {
        c4fReplyFmt(fd, "OK pad=%d handle=0x%08x deviceId=0x%llx vdaUser=0x%08x realUser=0x%08x frames=%llu errors=%llu last=0x%08x klog=%d\n",
                    g_havePad, (uint32_t)g_pad.handle,
                    (unsigned long long)g_pad.deviceId,
                    (uint32_t)g_pad.vdaUserId, (uint32_t)realUserId,
                    (unsigned long long)g_frames, (unsigned long long)g_insertErrors,
                    (uint32_t)g_lastInsert, g_klogFd >= 0);
        return 0;
    }

    if (strcmp(cmd, "klog") == 0) {
        unsigned count = g_klogCount;
        unsigned i;
        if (a1) {
            char *end;
            long requested;
            errno = 0;
            requested = strtol(a1, &end, 10);
            if (errno || end == a1 || *end || requested < 1 || requested > C4F_KLOG_LINES) {
                c4fReplyFmt(fd, "ERR klog [1..%d]\n", C4F_KLOG_LINES);
                return 0;
            }
            if ((unsigned)requested < count) count = (unsigned)requested;
        }
        for (i = 0; i < count; i++) {
            unsigned index = (g_klogNext + C4F_KLOG_LINES - count + i) % C4F_KLOG_LINES;
            c4fReply(fd, "LOG ");
            c4fReply(fd, g_klogLines[index]);
            c4fReply(fd, "\n");
        }
        c4fReplyFmt(fd, "OK klog finished lines=%u total=%llu available=%d\n",
                    count, (unsigned long long)g_klogTotal, g_klogFd >= 0);
        return 0;
    }

    if (strcmp(cmd, "add") == 0) {
        int32_t user = a1 ? (int32_t)strtoul(a1, NULL, 0) : 1;
        if (g_havePad) {
            c4fReplyFmt(fd, "ERR pad already exists; del first\n");
            return 0;
        }
        /* Only lines written after this point may name the new device. */
        c4fKlogDrain(g_klogFd);
        g_klogUsed = 0;  /* drain/scanner consume whole sections of the stream */
        /* Exactly the value asked for: the whole point of the argument is to try
         * values the default mapping would rewrite (1 became 0x10000000). */
        if (c4fVirtualPadAddAs(&g_pad, user, user, g_klogFd) != 0) {
            c4fReplyFmt(fd, "ERR AddDevice gave no handle\n");
            return 0;
        }
        g_havePad = 1;
        g_frames = g_insertErrors = 0;
        g_lastInsert = 0;
        errorsBefore = 0;
        /* One neutral sample so the device starts reporting like a real pad. */
        c4fNeutral(C4F_FRAME_MS * 3);
        if (!c4fInputResult(fd, errorsBefore)) return 0;
        c4fReplyFmt(fd, "OK handle=0x%08x deviceId=0x%llx vdaUser=0x%08x\n",
                    (uint32_t)g_pad.handle, (unsigned long long)g_pad.deviceId,
                    (uint32_t)g_pad.vdaUserId);
        return 0;
    }

    /* padmbus / mbusinit: start the MBus client before a bind. */
    if (strcmp(cmd, "padmbus") == 0 || strcmp(cmd, "mbusinit") == 0) {
        int which = strcmp(cmd, "padmbus") == 0 ? 0 : 1;
        int32_t ret = 0;
        if (c4fMbusClientInit(which, &ret) != 0) {
            c4fReplyFmt(fd, "ERR %s not found\n", which == 0 ? "scePadMbusInit" : "sceMbusInit");
            return 0;
        }
        c4fReplyFmt(fd, "OK %s() = 0x%08x\n",
                    which == 0 ? "scePadMbusInit" : "sceMbusInit", (uint32_t)ret);
        return 0;
    }

    /* bind [userhex] [authidhex]: give the pad to a user. Default user is the
     * signed-in account; default authid is SceShellCore's, as Ghostcontrol uses
     * on PS4. "bind <user> 0" makes the call with our own credentials -- that
     * is the form that crashed the host on 2026-10-04. */
    if (strcmp(cmd, "bind") == 0) {
        int32_t user = a1 ? (int32_t)strtoul(a1, NULL, 0) : realUserId;
        uint64_t authid = a2 ? strtoull(a2, NULL, 0) : C4F_AUTHID_SHELLCORE;
        if (!c4fNeedPad(fd)) return 0;
        if (c4fVirtualPadBindAs(&g_pad, user, authid) != 0) {
            c4fReplyFmt(fd, "ERR bind to 0x%08x failed (see log)\n", (uint32_t)user);
            return 0;
        }
        c4fReplyFmt(fd, "OK bound to 0x%08x (authid 0x%llx)\n",
                    (uint32_t)user, (unsigned long long)authid);
        return 0;
    }

    if (strcmp(cmd, "del") == 0) {
        if (!c4fNeedPad(fd)) return 0;
        c4fVirtualPadRemove(&g_pad);
        g_havePad = 0;
        c4fReplyFmt(fd, "OK deleted\n");
        return 0;
    }

    if (strcmp(cmd, "press") == 0) {
        uint32_t bits;
        int ms;
        if (!a1) { c4fReplyFmt(fd, "ERR press <hexbits> [ms]\n"); return 0; }
        if (!c4fNeedPad(fd)) return 0;
        bits = (uint32_t)strtoul(a1, NULL, 0);
        if (!c4fDuration(fd, a2, C4F_DEFAULT_HOLD_MS, &ms)) return 0;
        c4fNeutral(C4F_FRAME_MS * 2);
        c4fHold(bits, 128, 128, 128, 128, ms);
        c4fNeutral(C4F_FRAME_MS * 3);
        if (!c4fInputResult(fd, errorsBefore)) return 0;
        c4fReplyFmt(fd, "OK pressed 0x%08x for %dms\n", bits, ms);
        return 0;
    }

    if (strcmp(cmd, "stick") == 0) {
        int ms;
        if (!a4) { c4fReplyFmt(fd, "ERR stick <lx> <ly> <rx> <ry> [ms]\n"); return 0; }
        if (!c4fNeedPad(fd)) return 0;
        if (!c4fDuration(fd, a5, 1000, &ms)) return 0;
        c4fHold(0, (uint8_t)atoi(a1), (uint8_t)atoi(a2),
                (uint8_t)atoi(a3), (uint8_t)atoi(a4), ms);
        c4fNeutral(C4F_FRAME_MS * 3);
        if (!c4fInputResult(fd, errorsBefore)) return 0;
        c4fReplyFmt(fd, "OK sticks %s,%s %s,%s for %dms\n", a1, a2, a3, a4, ms);
        return 0;
    }

    if (strcmp(cmd, "hold") == 0) {
        int ms;
        if (!c4fNeedPad(fd)) return 0;
        if (!c4fDuration(fd, a1, 1000, &ms)) return 0;
        c4fNeutral(ms);
        if (!c4fInputResult(fd, errorsBefore)) return 0;
        c4fReplyFmt(fd, "OK held neutral %dms\n", ms);
        return 0;
    }

    /* Presses each single bit in a range, so one command can walk a whole byte
     * while somebody watches the screen. Each one is announced on screen. */
    if (strcmp(cmd, "sweep") == 0) {
        int first = a1 ? atoi(a1) : 0;
        int last = a2 ? atoi(a2) : 15;
        int ms;
        int bit;

        if (!c4fNeedPad(fd)) return 0;
        if (!c4fDuration(fd, a3, 400, &ms)) return 0;
        if (first < 0) first = 0;
        if (last > 31) last = 31;

        for (bit = first; bit <= last; bit++) {
            uint32_t mask = 1u << bit;
            if (g_replyFailed || c4fNowMs() >= g_deadlineMs) break;
            c4fNotify("C4F: bit %d (0x%x)", bit, mask);
            c4fNeutral(400);
            c4fHold(mask, 128, 128, 128, 128, ms);
            c4fNeutral(600);
            if (!c4fInputResult(fd, errorsBefore)) return 0;
            c4fReplyFmt(fd, "OK bit %d = 0x%08x\n", bit, mask);
        }
        c4fReplyFmt(fd, "OK sweep finished %d..%d\n", first, last);
        return 0;
    }

    c4fReplyFmt(fd, "ERR unknown command %s\n", cmd);
    return 0;
}

static int c4fAcceptOne(int listenFd, int timeoutS)
{
    int ready;
    ready = c4fWaitReadable(listenFd, c4fNowMs() + (unsigned)timeoutS * 1000u);
    if (ready <= 0) return -1;
    return accept(listenFd, NULL, NULL);
}

int c4fServerRun(int32_t realUserId, int klogFd)
{
    struct sockaddr_in addr;
    int listenFd;
    int one = 1;
    int sessions = 0;

    g_klogFd = klogFd;
    g_havePad = 0;
    g_klogUsed = g_klogNext = g_klogCount = 0;
    g_klogTotal = 0;
    g_frames = g_insertErrors = 0;
    g_deadlineMs = c4fNowMs() + (uint64_t)C4F_SESSION_MAX_S * 1000u;
    /* Never let an unexpected blocking log source freeze controller reports. */
    if (g_klogFd >= 0) {
        int flags = fcntl(g_klogFd, F_GETFL, 0);
        if (flags < 0 || fcntl(g_klogFd, F_SETFL, flags | O_NONBLOCK) < 0) {
            c4fLog("klog nonblocking setup failed errno=%d\n", errno);
            g_klogFd = -1;
        }
    }

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        c4fLog("socket() failed errno=%d\n", errno);
        return -1;
    }
    (void)setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    (void)memset(&addr, 0, sizeof(addr));
#ifdef __FreeBSD__
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C4F_SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        c4fLog("bind(%d) failed errno=%d\n", C4F_SERVER_PORT, errno);
        (void)close(listenFd);
        return -1;
    }
    if (listen(listenFd, 1) < 0) {
        c4fLog("listen() failed errno=%d\n", errno);
        (void)close(listenFd);
        return -1;
    }

    c4fLog("command server listening on port %d\n", C4F_SERVER_PORT);
    c4fNotify("Control4Free: server on port %d", C4F_SERVER_PORT);

    while (c4fNowMs() < g_deadlineMs) {
        char buf[512];
        size_t used = 0;
        int discarding = 0;
        int clientFd = c4fAcceptOne(listenFd, 30);
        int done = 0;
        uint64_t idleDeadline;

        if (clientFd < 0) continue;
#ifdef SO_NOSIGPIPE
        (void)setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        {
            struct timeval sendTimeout = { .tv_sec = 1, .tv_usec = 0 };
            (void)setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO,
                             &sendTimeout, sizeof(sendTimeout));
        }
        g_replyFailed = 0;
        idleDeadline = c4fNowMs() + (uint64_t)C4F_IDLE_TIMEOUT_S * 1000u;
        sessions++;
        c4fLog("client %d connected\n", sessions);
        c4fReplyFmt(clientFd, "OK Control4Free ready (port %d)\n", C4F_SERVER_PORT);

        while (!done && !g_replyFailed) {
            char incoming[512];
            ssize_t n;
            ssize_t i;

            if (c4fWaitReadable(clientFd, idleDeadline) <= 0) {
                c4fLog("client wait ended (timeout, session limit, or socket error)\n");
                break;
            }

            n = recv(clientFd, incoming, sizeof(incoming), 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            idleDeadline = c4fNowMs() + (uint64_t)C4F_IDLE_TIMEOUT_S * 1000u;

            for (i = 0; i < n && !done && !g_replyFailed; i++) {
                char ch = incoming[i];
                if (c4fNowMs() >= g_deadlineMs) break;
                if (ch == '\n') {
                    if (!discarding) {
                        if (used && buf[used - 1] == '\r') used--;
                        buf[used] = '\0';
                        done = c4fCommand(clientFd, buf, realUserId);
                    }
                    used = 0;
                    discarding = 0;
                    c4fPump();
                } else if (!discarding) {
                    if (used == sizeof(buf) - 1 || ch == '\0') {
                        c4fReplyFmt(clientFd, "ERR invalid or too long line\n");
                        discarding = 1;  /* discard the entire command, including its tail */
                        used = 0;
                    } else {
                        buf[used++] = ch;
                    }
                }
            }
        }

        (void)close(clientFd);
        c4fLog("client disconnected\n");
        if (done) break;
    }

    if (g_havePad) {
        c4fLog("cleaning up the pad before exit\n");
        c4fVirtualPadRemove(&g_pad);
        g_havePad = 0;
    }
    (void)close(listenFd);
    c4fLog("command server stopped\n");
    c4fNotify("Control4Free: server stopped");
    return 0;
}
