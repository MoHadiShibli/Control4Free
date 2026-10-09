/* Diagnostic build of the launcher: see diag.h.
 *
 * Everything is shown on pages a user can screenshot, and also saved to
 * /data/control4free/diag-report.txt (and a USB stick when one is plugged in).
 * The kernel log has a single reader, and Control4Free needs it to sign a
 * controller in. So the app reads it only in short windows, and never while
 * Control4Free runs: an app suspended in the background keeps its descriptor
 * open, and every controller would then fail to sign in (seen on 13.52). */
#include "diag.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include "autorun.h"
#include "draw.h"
#include "sandbox.h"
#include "service.h"

#ifndef C4F_DIAG_DIR
#define C4F_DIAG_DIR      "/user/data/control4free"
#endif
#define C4F_DIAG_LOG      C4F_DIAG_DIR "/control4free.log"
#define C4F_DIAG_REPORT   C4F_DIAG_DIR "/diag-report.txt"
#define C4F_DIAG_KLOG     C4F_DIAG_DIR "/diag-klog.txt"
#define C4F_DIAG_PAYLOAD  "/user/data/payloads/control4free.elf"
#define C4F_DIAG_INI      "/user/data/GoldHEN/payloads.ini"
#define C4F_LOG_TAIL      (64 * 1024)
#define C4F_KLOG_KEEP     1000
#define C4F_KLOG_WIDTH    192
#define C4F_KLOG_FILE_MAX (4u << 20)

#define C4F_D_BG     C4F_RGBA(6, 14, 34, 1)
#define C4F_D_TEXT   C4F_RGBA(235, 240, 255, 1)
#define C4F_D_FAINT  C4F_RGBA(160, 175, 205, 1)
#define C4F_D_ACCENT C4F_RGBA(90, 160, 255, 1)
#define C4F_D_WARN   C4F_RGBA(255, 208, 77, 1)
#define C4F_D_LINE   C4F_RGBA(255, 255, 255, .2f)
#define C4F_D_SIZE   20.0f
#define C4F_D_TITLE  28.0f
#define C4F_D_ROW    24
#define C4F_D_LEFT   40
#define C4F_D_TOP    186
#define C4F_D_BOTTOM 1052

static const char *const c4fPageNames[C4F_DIAG_PAGES] = {
    "Summary", "Control4Free log", "Previous Control4Free log", "Kernel log (filtered)", "Kernel log (everything)",
};

static pthread_mutex_t c4fLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t c4fKlogReader;
static int c4fReaderStarted, c4fQuit;
static uint64_t c4fBootMs, c4fCaptureUntil;
static int c4fServiceUp, c4fRefreshed;

/* Pages, rebuilt by the launcher's worker and drawn by its main loop. */
static char *c4fPages[C4F_DIAG_PAGES];
static unsigned c4fGeneration = 1;
static int c4fMaxScroll[C4F_DIAG_PAGES];

/* What start-up found. */
static char c4fFirmware[96], c4fGoldHen[96], c4fSandbox[96], c4fBundled[96];

/* The kernel log: the newest lines, and how reading it went. */
static char c4fKlog[C4F_KLOG_KEEP][C4F_KLOG_WIDTH];
static unsigned c4fKlogTotal, c4fKlogOpens;
static int c4fKlogError;
static unsigned long long c4fKlogBytes;

/* The last start. */
static int c4fStarted, c4fStartResult, c4fLogChanged;
static uint64_t c4fStartedAt, c4fStartTook;
static char c4fStartMessage[192];
static char *c4fLogBefore;

static uint64_t c4fLastReport;
static char c4fReportNote[160];

/* ---- helpers ---- */

typedef struct { char *data; size_t used, size; } C4fText;

static void c4fAdd(C4fText *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void c4fAdd(C4fText *t, const char *fmt, ...)
{
    for (;;) {
        size_t room = t->data ? t->size - t->used : 0;
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(t->data ? t->data + t->used : NULL, room, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (t->data && (size_t)n < room) { t->used += (size_t)n; return; }
        size_t grown = t->size ? t->size * 2 : 4096;
        while (grown < t->used + (size_t)n + 1) grown *= 2;
        char *next = realloc(t->data, grown);
        if (!next) return;
        t->data = next; t->size = grown;
    }
}

static char *c4fTake(C4fText *t) { return t->data ? t->data : strdup(""); }

/* The last `limit` bytes of a file, NUL-terminated, NULs shown as spaces;
 * NULL with errno when it cannot be read. */
static char *c4fReadTail(const char *path, size_t limit, long long *total)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    off_t size = lseek(fd, 0, SEEK_END);
    off_t start = size > (off_t)limit ? size - (off_t)limit : 0;
    if (size < 0 || lseek(fd, start, SEEK_SET) < 0) { int saved = errno; close(fd); errno = saved; return NULL; }
    size_t want = (size_t)(size - start), used = 0;
    char *data = malloc(want + 1);
    if (!data) { close(fd); errno = ENOMEM; return NULL; }
    while (used < want) {
        ssize_t n = read(fd, data + used, want - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(fd);
    for (size_t i = 0; i < used; i++) if (!data[i]) data[i] = ' ';
    data[used] = 0;
    if (total) *total = (long long)size;
    return data;
}

static uint32_t c4fCrc32Update(uint32_t crc, const unsigned char *data, size_t size)
{
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint32_t c4fCrc32(const unsigned char *data, size_t size) { return c4fCrc32Update(0, data, size); }

/* A file's size and CRC-32; -1 with errno when it cannot be read. */
static int c4fCrcFile(const char *path, size_t *size, uint32_t *crc)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    unsigned char buf[16384];
    *size = 0; *crc = 0;
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { int saved = errno; close(fd); errno = saved; return -1; }
        if (n == 0) break;
        *crc = c4fCrc32Update(*crc, buf, (size_t)n);
        *size += (size_t)n;
    }
    close(fd);
    return 0;
}

static const char *c4fLastLine(const char *text, char *out, size_t size)
{
    const char *end = text + strlen(text);
    while (end > text && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) end--;
    const char *start = end;
    while (start > text && start[-1] != '\n') start--;
    snprintf(out, size, "%.*s", (int)(end - start), start);
    return out;
}

static int c4fContains(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (i < n && haystack[i] && (haystack[i] | 0x20) == (needle[i] | 0x20)) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int c4fWriteAll(const char *path, const char *text, int append)
{
    int fd = open(path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0666);
    if (fd < 0) return -1;
    size_t size = strlen(text);
    while (size) {
        ssize_t n = write(fd, text, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { int saved = n < 0 ? errno : EIO; close(fd); errno = saved; return -1; }
        text += n; size -= (size_t)n;
    }
    if (fsync(fd)) { int saved = errno; close(fd); errno = saved; return -1; }
    return close(fd);
}

/* ---- the kernel log ---- */

/* One line, without terminal colour codes (GoldHEN's lines carry them). */
static void c4fKlogAdd(const char *line)
{
    char clean[C4F_KLOG_WIDTH];
    size_t used = 0;
    for (const char *p = line; *p && used + 1 < sizeof(clean); p++) {
        if (*p == '\033') {
            if (p[1] == '[') { p += 2; while (*p && !(*p >= '@' && *p <= '~')) p++; if (!*p) break; }
            continue;
        }
        if (*p != '\r') clean[used++] = *p;
    }
    clean[used] = 0;
    pthread_mutex_lock(&c4fLock);
    snprintf(c4fKlog[c4fKlogTotal % C4F_KLOG_KEEP], C4F_KLOG_WIDTH, "%s", clean);
    c4fKlogTotal++;
    pthread_mutex_unlock(&c4fLock);
}

static void *c4fKlogThread(void *unused)
{
    (void)unused;
    int fd = -1;
    size_t used = 0, written = 0;
    char line[C4F_KLOG_WIDTH * 2];
    for (;;) {
        pthread_mutex_lock(&c4fLock);
        int quit = c4fQuit, want = !c4fServiceUp && c4fLauncherTimeMs() < c4fCaptureUntil;
        pthread_mutex_unlock(&c4fLock);
        if (quit) break;
        if (want && fd < 0) {
            fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
            int error = fd < 0 ? errno : 0;
            if (fd >= (int)FD_SETSIZE) { close(fd); fd = -1; error = EMFILE; }
            pthread_mutex_lock(&c4fLock);
            c4fKlogError = error;
            if (fd < 0) c4fCaptureUntil = 0; else c4fKlogOpens++;
            pthread_mutex_unlock(&c4fLock);
            continue;
        }
        if (!want && fd >= 0) {
            close(fd); fd = -1;
            if (used) { line[used] = 0; c4fKlogAdd(line); used = 0; }
        }
        if (fd < 0) { usleep(100000); continue; }
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        struct timeval tv = { 0, 100000 };
        if (select(fd + 1, &set, NULL, NULL, &tv) <= 0) continue;
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) {
            pthread_mutex_lock(&c4fLock);
            c4fKlogError = n < 0 ? errno : EPIPE; c4fCaptureUntil = 0;
            pthread_mutex_unlock(&c4fLock);
            close(fd); fd = -1;
            continue;
        }
        buf[n] = 0;
        if (written < C4F_KLOG_FILE_MAX) { c4fWriteAll(C4F_DIAG_KLOG, buf, 1); written += (size_t)n; }
        pthread_mutex_lock(&c4fLock);
        c4fKlogBytes += (unsigned long long)n;
        pthread_mutex_unlock(&c4fLock);
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n' || used + 1 >= sizeof(line)) {
                line[used] = 0; c4fKlogAdd(line); used = 0;
                if (buf[i] == '\n') continue;
            }
            line[used++] = buf[i];
        }
    }
    if (fd >= 0) close(fd);
    return NULL;
}

void c4fDiagCapture(unsigned milliseconds)
{
    pthread_mutex_lock(&c4fLock);
    uint64_t until = c4fLauncherTimeMs() + milliseconds;
    if (until > c4fCaptureUntil) c4fCaptureUntil = until;
    pthread_mutex_unlock(&c4fLock);
}

/* ---- start-up and starts ---- */

void c4fDiagStart(int sandboxResult, int sandboxError)
{
    /* A pre-main payload failure has never created this directory. */
    (void)mkdir(C4F_DIAG_DIR, 0777);
    c4fBootMs = c4fLauncherTimeMs();
    OrbisKernelSwVersion version;
    memset(&version, 0, sizeof(version));
    version.Size = sizeof(version);
    int ret = sceKernelGetSystemSwVersion(&version);
    version.VersionString[sizeof(version.VersionString) - 1] = 0;
    snprintf(c4fFirmware, sizeof(c4fFirmware), "\"%s\" = 0x%08x (call returned 0x%08x)",
             version.VersionString, version.Version, (uint32_t)ret);
    /* GoldHEN returns the version as the call's error value (seen on 13.52:
     * 256 = SDK 1.00), so only ENOSYS (78) means there is no GoldHEN call. */
    long goldHen = c4fGoldHenCommand(0, NULL);
    if (goldHen == -78) snprintf(c4fGoldHen, sizeof(c4fGoldHen), "no answer (errno 78: no GoldHEN SDK call)");
    else snprintf(c4fGoldHen, sizeof(c4fGoldHen), "answers, SDK version 0x%lx", goldHen < 0 ? -goldHen : goldHen);
    if (sandboxResult == 0) snprintf(c4fSandbox, sizeof(c4fSandbox), "left through GoldHEN");
    else snprintf(c4fSandbox, sizeof(c4fSandbox), "still inside, errno %d", sandboxError);
    size_t size = 0;
    const unsigned char *payload = c4fAutorunBundled(&size);
    if (payload) snprintf(c4fBundled, sizeof(c4fBundled), "%zu bytes, crc32 %08x", size, c4fCrc32(payload, size));
    else snprintf(c4fBundled, sizeof(c4fBundled), "could not be read");
    c4fWriteAll(C4F_DIAG_KLOG, "", 0);
    c4fReaderStarted = pthread_create(&c4fKlogReader, NULL, c4fKlogThread, NULL) == 0;
    /* The backlog (GoldHEN, AutoRun, earlier runs) is read at the first
     * refresh, once it is known whether Control4Free runs. */
}

void c4fDiagStop(void)
{
    pthread_mutex_lock(&c4fLock);
    c4fQuit = 1;
    pthread_mutex_unlock(&c4fLock);
    if (c4fReaderStarted) pthread_join(c4fKlogReader, NULL);
}

void c4fDiagBeforeStart(void)
{
    free(c4fLogBefore);
    c4fLogBefore = c4fReadTail(C4F_DIAG_LOG, C4F_LOG_TAIL, NULL);
    c4fStartedAt = c4fLauncherTimeMs();
    c4fDiagCapture(30000);
}

void c4fDiagAfterStart(int result, const char *message)
{
    char *after = c4fReadTail(C4F_DIAG_LOG, C4F_LOG_TAIL, NULL);
    c4fLogChanged = after && (!c4fLogBefore || strcmp(after, c4fLogBefore));
    free(after);
    c4fStarted = 1;
    c4fStartResult = result;
    c4fStartTook = c4fLauncherTimeMs() - c4fStartedAt;
    snprintf(c4fStartMessage, sizeof(c4fStartMessage), "%s", message);
    c4fLastReport = 0; /* save the report straight away */
    if (result) c4fDiagCapture(3000);
    else { pthread_mutex_lock(&c4fLock); c4fCaptureUntil = 0; pthread_mutex_unlock(&c4fLock); }
}

/* ---- pages ---- */

static void c4fFileSummary(C4fText *t, const char *label, const char *path)
{
    long long size = 0;
    char *text = c4fReadTail(path, C4F_LOG_TAIL, &size);
    if (!text) { c4fAdd(t, "  %s: not there (errno %d)\n", label, errno); return; }
    char last[200];
    const char *firstEnd = strchr(text, '\n');
    c4fAdd(t, "  %s: %lld bytes\n    first: %.*s\n    last:  %s\n", label, size,
           (int)(firstEnd ? firstEnd - text : (long)strlen(text)), text, c4fLastLine(text, last, sizeof(last)));
    free(text);
}

static char *c4fSummary(const C4fDiagState *s)
{
    C4fText t = {0};
    char last[200] = "";
    char *log = c4fReadTail(C4F_DIAG_LOG, C4F_LOG_TAIL, NULL);
    if (log) c4fLastLine(log, last, sizeof(last));

    if (s->running == 1)
        c4fAdd(&t, ">> Control4Free is RUNNING (%s). Now open %s on your phone or PC. Whatever happens, come back\n"
                   ">> here and take screenshots of pages 1 and 2: page 2 lists every connection it saw.\n",
               s->version, s->address[0] ? s->address : "the PS4's address with :4264");
    else if (c4fStarted && c4fContains(c4fStartMessage, "PayLoader did not answer"))
        c4fAdd(&t, ">> GoldHEN's PayLoader (port 9090) did not take the payload. Turn PayLoader on in GoldHEN's\n"
                   ">> settings, then press Cross again.\n");
    else if (c4fStarted && c4fStartResult == -2 && !c4fLogChanged)
        c4fAdd(&t, ">> The payload was handed to PayLoader, but Control4Free's own code never ran (it wrote no log).\n"
                   ">> PayLoader did not run it, or it stopped while starting up on this firmware. The kernel log\n"
                   ">> pages (4 and 5) show what the PS4 said meanwhile.\n");
    else if (c4fStarted && c4fLogChanged)
        c4fAdd(&t, ">> Control4Free started but does not answer. Its log (page 2) shows how far it got. Last line:\n"
                   ">> %s\n", last);
    else if (c4fStarted)
        c4fAdd(&t, ">> The start did not work: %s\n", c4fStartMessage);
    else
        c4fAdd(&t, ">> Not running. Press Cross to start it, wait for the result (up to 30 seconds), then take a\n"
                   ">> screenshot of every page.\n");
    free(log);

    c4fAdd(&t, "\nConsole\n");
    c4fAdd(&t, "  system software:  %s\n", c4fFirmware);
    c4fAdd(&t, "  GoldHEN SDK call: %s\n", c4fGoldHen);
    c4fAdd(&t, "  app sandbox:      %s\n", c4fSandbox);
    c4fAdd(&t, "  network address:  %s\n", s->address[0] ? s->address : "(none)");

    c4fAdd(&t, "\nThis app\n");
    c4fAdd(&t, "  version %s, bundled payload %s, open for %llu s\n", C4F_LAUNCHER_VERSION, c4fBundled,
           (unsigned long long)((c4fLauncherTimeMs() - c4fBootMs) / 1000));
    c4fAdd(&t, "  input: %s\n", s->inputNote && s->inputNote[0] ? s->inputNote : "waiting");

    c4fAdd(&t, "\nService (port 4264)\n");
    if (s->running == 1) c4fAdd(&t, "  running, version %s, %d controller(s)\n", s->version, s->controllers);
    else if (s->running == 0) c4fAdd(&t, "  not running (nothing listens)\n");
    else c4fAdd(&t, "  unclear: %s\n", s->problem[0] ? s->problem : "(no detail)");
    c4fAdd(&t, "  screen message: %s%s\n", s->message, s->locked ? "  [app error: reopen the app]" : "");

    c4fAdd(&t, "\nLast start from this app\n");
    if (!c4fStarted) c4fAdd(&t, "  none yet\n");
    else c4fAdd(&t, "  result %d after %llu ms; service log %s\n  message: %s\n", c4fStartResult,
                (unsigned long long)c4fStartTook, c4fLogChanged ? "CHANGED (it ran)" : "unchanged (it did not run)",
                c4fStartMessage);

    c4fAdd(&t, "\nAuto-start\n");
    static const char *const states[] = { "unknown", "off", "on", "on, but an older copy" };
    int state = s->autorun + 1;
    c4fAdd(&t, "  state: %s%s%s\n", state >= 0 && state < 4 ? states[state] : "?", s->autorunNote[0] ? "; " : "",
           s->autorunNote);
    size_t installedSize = 0;
    uint32_t installedCrc = 0;
    if (!c4fCrcFile(C4F_DIAG_PAYLOAD, &installedSize, &installedCrc))
        c4fAdd(&t, "  /data/payloads/control4free.elf: %zu bytes, crc32 %08x\n", installedSize, installedCrc);
    else c4fAdd(&t, "  /data/payloads/control4free.elf: not there (errno %d)\n", errno);
    char *ini = c4fReadTail(C4F_DIAG_INI, 2048, NULL);
    if (ini) {
        c4fAdd(&t, "  /data/GoldHEN/payloads.ini:\n");
        for (char *line = strtok(ini, "\n"); line; line = strtok(NULL, "\n")) c4fAdd(&t, "    %s\n", line);
        free(ini);
    } else c4fAdd(&t, "  /data/GoldHEN/payloads.ini: not there (errno %d)\n", errno);

    c4fAdd(&t, "\nLogs\n");
    c4fFileSummary(&t, "control4free.log", C4F_DIAG_LOG);
    c4fFileSummary(&t, "control4free.log.previous", C4F_DIAG_LOG ".previous");

    pthread_mutex_lock(&c4fLock);
    unsigned total = c4fKlogTotal, opens = c4fKlogOpens;
    int error = c4fKlogError;
    unsigned long long bytes = c4fKlogBytes;
    pthread_mutex_unlock(&c4fLock);
    c4fAdd(&t, "  kernel log: read %u time(s), %llu bytes, %u lines", opens, bytes, total);
    if (s->running == 1) c4fAdd(&t, "; not read while Control4Free runs, which needs it to sign controllers in\n");
    else if (error == 16) c4fAdd(&t, "; BUSY: a klog viewer is connected to GoldHEN (port 3232). Close it, press Options.\n");
    else if (error) c4fAdd(&t, "; last error errno %d\n", error);
    else c4fAdd(&t, "\n");
    c4fAdd(&t, "  report: %s\n", c4fReportNote[0] ? c4fReportNote : "not saved yet");
    return c4fTake(&t);
}

static char *c4fLogPage(const char *path)
{
    long long size = 0;
    char *text = c4fReadTail(path, C4F_LOG_TAIL, &size);
    if (!text) {
        C4fText t = {0};
        c4fAdd(&t, "%s is not there (errno %d).\n", path, errno);
        return c4fTake(&t);
    }
    C4fText t = {0};
    c4fAdd(&t, "===== %s (%lld bytes%s) =====\n%s", path, size,
           size > C4F_LOG_TAIL ? "; truncated to last 64 KiB" : "", text);
    free(text);
    return c4fTake(&t);
}

static char *c4fCurrentLogPage(void)
{
    char *rollover = c4fLogPage(C4F_DIAG_LOG ".1"), *active = c4fLogPage(C4F_DIAG_LOG);
    C4fText t = {0};
    c4fAdd(&t, "%s\n%s", rollover, active);
    free(rollover); free(active);
    return c4fTake(&t);
}

/* Lines that tend to say why a payload did not run. */
static int c4fInteresting(const char *line)
{
    static const char *const words[] = {
        "c4f", "control4free", "unsupported", "firmware", "kexec", "payload", "elf", "goldhen", "signal",
        "crash", "fault", "panic", "trap", "segv", "party", "rtld", "dlsym", "unable", "fail", "error",
        "denied", "9090", "4264",
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) if (c4fContains(line, words[i])) return 1;
    return 0;
}

static char *c4fKlogPage(int filtered)
{
    C4fText t = {0};
    pthread_mutex_lock(&c4fLock);
    unsigned total = c4fKlogTotal, first = total > C4F_KLOG_KEEP ? total - C4F_KLOG_KEEP : 0;
    if (!total) c4fAdd(&t, "Nothing read from the kernel log yet (see the summary page). Options reads it again.\n");
    for (unsigned i = first; i < total; i++) {
        const char *line = c4fKlog[i % C4F_KLOG_KEEP];
        if (!filtered || c4fInteresting(line)) c4fAdd(&t, "%s\n", line);
    }
    pthread_mutex_unlock(&c4fLock);
    return c4fTake(&t);
}

static void c4fSaveReport(void)
{
    C4fText t = {0};
    pthread_mutex_lock(&c4fLock);
    for (int i = 0; i < C4F_DIAG_PAGES; i++)
        c4fAdd(&t, "===== %s =====\n%s\n", c4fPageNames[i], c4fPages[i] ? c4fPages[i] : "");
    pthread_mutex_unlock(&c4fLock);
    char *text = c4fTake(&t);
    if (!c4fWriteAll(C4F_DIAG_REPORT, text, 0))
        snprintf(c4fReportNote, sizeof(c4fReportNote), "Saved /data/control4free/diag-report.txt");
    else snprintf(c4fReportNote, sizeof(c4fReportNote), "Internal save failed (errno %d)", errno);
    static const char *const usb[] = { "/mnt/usb0", "/mnt/usb1" };
    for (int i = 0; i < 2; i++) {
        char path[64];
        snprintf(path, sizeof(path), "%s/control4free-diag.txt", usb[i]);
        struct stat info;
        if (stat(usb[i], &info) || !S_ISDIR(info.st_mode)) continue;
        int result = c4fWriteAll(path, text, 0), saved = errno;
        size_t used = strlen(c4fReportNote);
        if (!result) snprintf(c4fReportNote + used, sizeof(c4fReportNote) - used, "; saved %s", path);
        else snprintf(c4fReportNote + used, sizeof(c4fReportNote) - used, "; USB%d save failed (%d)", i, saved);
    }
    free(text);
}

unsigned c4fDiagRefresh(const C4fDiagState *state)
{
    pthread_mutex_lock(&c4fLock);
    c4fServiceUp = state->running == 1;
    if (c4fServiceUp) c4fCaptureUntil = 0;
    pthread_mutex_unlock(&c4fLock);
    if (!c4fRefreshed++ && state->running != 1) c4fDiagCapture(4000);
    char *pages[C4F_DIAG_PAGES] = {
        c4fSummary(state), c4fCurrentLogPage(), c4fLogPage(C4F_DIAG_LOG ".previous"),
        c4fKlogPage(1), c4fKlogPage(0),
    };
    int changed = 0;
    pthread_mutex_lock(&c4fLock);
    for (int i = 0; i < C4F_DIAG_PAGES; i++) {
        if (c4fPages[i] && !strcmp(c4fPages[i], pages[i])) { free(pages[i]); continue; }
        free(c4fPages[i]);
        c4fPages[i] = pages[i];
        changed = 1;
    }
    if (changed) c4fGeneration++;
    unsigned generation = c4fGeneration;
    pthread_mutex_unlock(&c4fLock);
    uint64_t now = c4fLauncherTimeMs();
    if (changed && (!c4fLastReport || now - c4fLastReport >= 10000)) { c4fSaveReport(); c4fLastReport = now; }
    return generation;
}

/* ---- drawing ---- */

typedef struct { const char *text; int length, highlight; } C4fRow;

/* How many characters of a line fit in the width. */
static int c4fFit(const char *text, int length, float width)
{
    char line[400];
    int n = length < (int)sizeof(line) - 1 ? length : (int)sizeof(line) - 1;
    memcpy(line, text, (size_t)n); line[n] = 0;
    float measured = c4fTextWidth(C4F_FONT_REGULAR, C4F_D_SIZE, line);
    if (measured <= width) return n;
    n = (int)(n * width / measured);
    if (n < 1) n = 1;
    line[n] = 0;
    while (n > 1 && c4fTextWidth(C4F_FONT_REGULAR, C4F_D_SIZE, line) > width) line[--n] = 0;
    return n;
}

void c4fDrawDiag(uint32_t *pixels, const C4fLauncherScreen *s)
{
    C4fCanvas c = { pixels, C4F_SCREEN_WIDTH, C4F_SCREEN_HEIGHT };
    const float width = C4F_SCREEN_WIDTH - 2 * C4F_D_LEFT;
    int page = s->diagPage >= 0 && s->diagPage < C4F_DIAG_PAGES ? s->diagPage : 0;
    char line[320];
    c4fFillRect(&c, 0, 0, c.w, c.h, C4F_D_BG);

    /* The build time tells one diagnostic build from the next in a screenshot. */
    snprintf(line, sizeof(line), "Control4Free DIAGNOSTIC BUILD %s (%s %s)    page %d of %d: %s", C4F_LAUNCHER_VERSION,
             __DATE__, __TIME__, page + 1, C4F_DIAG_PAGES, c4fPageNames[page]);
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_TITLE, C4F_D_LEFT, 48, C4F_D_ACCENT, line);
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_D_LEFT, 84, C4F_D_FAINT,
            "Left / Right: page    Up / Down: scroll    TV remote OK: actions    "
            "Cross: start    Triangle: auto-start    Square: stop    Circle: close");
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_D_LEFT, 114, C4F_D_WARN,
            "Please take a screenshot of EVERY page (hold SHARE, or press SHARE then Triangle) and send them all.");
    if (s->confirmStop) snprintf(line, sizeof(line), "%s", s->confirmMenu ?
        "Select Cancel or Stop with the arrows, then press OK." : "Press Cross to stop Control4Free, or Circle to cancel.");
    else snprintf(line, sizeof(line), "%s%s", s->busy ? "WORKING: " : "Now: ", s->message);
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_D_LEFT, 146, C4F_D_TEXT, line);
    c4fFillRect(&c, C4F_D_LEFT, 160, (int)width, 1, C4F_D_LINE);

    pthread_mutex_lock(&c4fLock);
    char *text = strdup(c4fPages[page] ? c4fPages[page] : "(not read yet)");
    pthread_mutex_unlock(&c4fLock);
    if (!text) { c4fDrawActionOverlay(pixels, s); return; }

    /* Wrap every line, then show the window the scroll position picks. */
    int count = 0, capacity = 256;
    C4fRow *rows = malloc(sizeof(*rows) * (size_t)capacity);
    for (char *p = text; rows && *p; ) {
        char *end = strchr(p, '\n');
        int length = end ? (int)(end - p) : (int)strlen(p);
        int highlight = !strncmp(p, ">> ", 3);
        if (highlight) { p += 3; length -= 3; }
        do {
            int fit = length ? c4fFit(p, length, width) : 0;
            if (count == capacity) {
                C4fRow *grown = realloc(rows, sizeof(*rows) * (size_t)(capacity *= 2));
                if (!grown) break;
                rows = grown;
            }
            rows[count++] = (C4fRow){ p, fit, highlight };
            p += fit; length -= fit;
        } while (length > 0);
        p = end ? end + 1 : p + strlen(p);
    }
    int visible = (C4F_D_BOTTOM - C4F_D_TOP) / C4F_D_ROW;
    int maxScroll = count > visible ? count - visible : 0;
    c4fMaxScroll[page] = maxScroll;
    int first = s->diagScroll[page] >= C4F_DIAG_END ? maxScroll : s->diagScroll[page];
    if (first > maxScroll) first = maxScroll;
    if (first < 0) first = 0;
    for (int i = 0; rows && i < visible && first + i < count; i++) {
        const C4fRow *row = &rows[first + i];
        c4fTextN(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_D_LEFT, C4F_D_TOP + i * C4F_D_ROW,
                 row->highlight ? C4F_D_WARN : C4F_D_TEXT, row->text, row->length);
    }
    snprintf(line, sizeof(line), "lines %d-%d of %d", count ? first + 1 : 0, first + visible < count ? first + visible : count, count);
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_D_LEFT, 1072, C4F_D_FAINT, line);
    const char *remoteHint = "TV remote: OK actions / Back returns    L1 / R1: page    Options: kernel log";
    c4fText(&c, C4F_FONT_REGULAR, C4F_D_SIZE, C4F_SCREEN_WIDTH - C4F_D_LEFT -
            c4fTextWidth(C4F_FONT_REGULAR, C4F_D_SIZE, remoteHint), 1072, C4F_D_FAINT, remoteHint);
    free(rows);
    free(text);
    c4fDrawActionOverlay(pixels, s);
}

int c4fDiagMaxScroll(int page)
{
    return page >= 0 && page < C4F_DIAG_PAGES ? c4fMaxScroll[page] : 0;
}
