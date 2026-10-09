/* GoldHEN AutoRun for the bundled payload. */
#include "autorun.h"
#include "sandbox.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* GoldHEN reads this path from payloads.ini, whatever our test root is. */
#define C4F_AUTORUN_KEY "/user/data/payloads/control4free.elf"

static char c4fRoot[256] = "/user/data";
static unsigned char *c4fBundled;
static size_t c4fBundledSize;

void c4fAutorunSetRoot(const char *root) { snprintf(c4fRoot, sizeof(c4fRoot), "%s", root); }

const unsigned char *c4fAutorunBundled(size_t *size) { *size = c4fBundledSize; return c4fBundled; }

static int c4fPath(char *out, size_t size, const char *relative)
{
    return snprintf(out, size, "%s/%s", c4fRoot, relative) < (int)size ? 0 : -1;
}

/* The whole file, NUL-terminated; NULL (errno set) if missing or too large. */
static unsigned char *c4fReadFile(const char *path, size_t *size, size_t limit)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    unsigned char *data = NULL;
    size_t used = 0, capacity = 0;
    for (;;) {
        if (used + 1 >= capacity) {
            size_t grown = capacity ? capacity * 2 : 65536;
            unsigned char *next = grown <= limit + 1 ? realloc(data, grown) : NULL;
            if (!next) { free(data); close(fd); errno = EFBIG; return NULL; }
            data = next;
            capacity = grown;
        }
        ssize_t n = read(fd, data + used, capacity - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { int saved = errno; free(data); close(fd); errno = saved; return NULL; }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    data[used] = 0;
    *size = used;
    return data;
}

/* Written beside the target first, then renamed over it. */
static int c4fWriteFile(const char *path, const void *data, size_t size)
{
    char temporary[320];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) return -1;
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { int saved = errno; close(fd); unlink(temporary); errno = saved; return -1; }
        p += n;
        size -= (size_t)n;
    }
    if (close(fd) || rename(temporary, path)) { int saved = errno; unlink(temporary); errno = saved; return -1; }
    return 0;
}

int c4fAutorunLoadBundled(const char *path)
{
    size_t size = 0;
    unsigned char *data = c4fReadFile(path, &size, 16u << 20);
    if (!data) return -1;
    if (size < 64 || memcmp(data, "\177ELF", 4)) { free(data); errno = EINVAL; return -1; }
    free(c4fBundled);
    c4fBundled = data;
    c4fBundledSize = size;
    return 0;
}

/* ---- payloads.ini: "[AutoRun]", then "<path> = 1" lines ---- */

static void c4fTrim(const char **start, const char **end)
{
    while (*start < *end && (**start == ' ' || **start == '\t' || **start == '\r')) (*start)++;
    while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r')) (*end)--;
}

static int c4fIsSection(const char *start, const char *end, const char *name)
{
    c4fTrim(&start, &end);
    size_t n = strlen(name);
    return end - start >= 2 && *start == '[' && end[-1] == ']' && (size_t)(end - start - 2) == n && !memcmp(start + 1, name, n);
}

/* 1 for our entry; *enabled says whether its value is 1. */
static int c4fIsOurEntry(const char *start, const char *end, int *enabled)
{
    const char *equals = memchr(start, '=', (size_t)(end - start));
    if (!equals) return 0;
    const char *keyStart = start, *keyEnd = equals, *valueStart = equals + 1, *valueEnd = end;
    c4fTrim(&keyStart, &keyEnd);
    c4fTrim(&valueStart, &valueEnd);
    if ((size_t)(keyEnd - keyStart) != strlen(C4F_AUTORUN_KEY) || memcmp(keyStart, C4F_AUTORUN_KEY, strlen(C4F_AUTORUN_KEY))) return 0;
    *enabled = valueEnd - valueStart == 1 && *valueStart == '1';
    return 1;
}

static int c4fIniEnabled(const char *text)
{
    int inAutoRun = 0, enabled = 0;
    while (*text) {
        const char *end = strchr(text, '\n');
        if (!end) end = text + strlen(text);
        const char *start = text, *trimmedEnd = end;
        c4fTrim(&start, &trimmedEnd);
        if (start < trimmedEnd && *start == '[') inAutoRun = c4fIsSection(text, end, "AutoRun");
        else if (inAutoRun && c4fIsOurEntry(text, end, &enabled) && enabled) return 1;
        text = *end ? end + 1 : end;
    }
    return 0;
}

/* The same text with our entry present (enable) or gone; other lines stay. */
static char *c4fIniEdited(const char *text, int enable)
{
    static const char entry[] = C4F_AUTORUN_KEY " = 1\n";
    size_t length = strlen(text);
    char *out = malloc(length + sizeof(entry) + 16);
    if (!out) return NULL;
    size_t used = 0;
    int inAutoRun = 0, placed = 0, dummy;
#define C4F_APPEND(data, n) do { memcpy(out + used, (data), (n)); used += (n); } while (0)
    while (*text) {
        const char *end = strchr(text, '\n');
        if (!end) end = text + strlen(text);
        const char *start = text, *trimmedEnd = end;
        c4fTrim(&start, &trimmedEnd);
        if (start < trimmedEnd && *start == '[') {
            if (inAutoRun && enable && !placed) { C4F_APPEND(entry, sizeof(entry) - 1); placed = 1; }
            inAutoRun = c4fIsSection(text, end, "AutoRun");
            C4F_APPEND(text, (size_t)(end - text)); C4F_APPEND("\n", 1);
        } else if (inAutoRun && c4fIsOurEntry(text, end, &dummy)) {
            if (enable && !placed) { C4F_APPEND(entry, sizeof(entry) - 1); placed = 1; }
        } else if (end > text || *end) {
            C4F_APPEND(text, (size_t)(end - text)); C4F_APPEND("\n", 1);
        }
        text = *end ? end + 1 : end;
    }
    if (enable && !placed) {
        if (!inAutoRun) C4F_APPEND("[AutoRun]\n", 10);
        C4F_APPEND(entry, sizeof(entry) - 1);
    }
#undef C4F_APPEND
    out[used] = 0;
    return out;
}

/* ---- public ---- */

int c4fAutorunCheck(char *problem, size_t size)
{
    char path[300];
    size_t length = 0;
    problem[0] = 0;
    if (!c4fBundled) { snprintf(problem, size, "the bundled payload could not be read"); return C4F_AUTORUN_UNKNOWN; }
    if (c4fSandboxLeave()) { snprintf(problem, size, "GoldHEN did not let the app check (errno %d)", errno); return C4F_AUTORUN_UNKNOWN; }
    int state = C4F_AUTORUN_OFF;
    char *ini = c4fPath(path, sizeof(path), "GoldHEN/payloads.ini") ? NULL : (char *)c4fReadFile(path, &length, 1u << 20);
    if (!ini && errno != ENOENT) {
        snprintf(problem, size, "Could not inspect AutoRun (errno %d)", errno); return C4F_AUTORUN_UNKNOWN;
    }
    if (ini && c4fIniEnabled(ini) && !c4fPath(path, sizeof(path), "payloads/control4free.elf")) {
        size_t installedSize = 0;
        unsigned char *installed = c4fReadFile(path, &installedSize, 16u << 20);
        if (installed)
            state = installedSize == c4fBundledSize && !memcmp(installed, c4fBundled, installedSize) ? C4F_AUTORUN_ON : C4F_AUTORUN_OUTDATED;
        else if (errno == ENOENT) {
            state = C4F_AUTORUN_OUTDATED;
            snprintf(problem, size, "Enabled, but the payload is missing; choose Update auto-start in Actions or press Triangle");
        } else {
            state = C4F_AUTORUN_UNKNOWN;
            snprintf(problem, size, "Enabled, but the payload cannot be inspected (errno %d)", errno);
        }
        free(installed);
    }
    free(ini);
    return state;
}

int c4fAutorunEnable(char *message, size_t size)
{
    char directory[300], path[300];
    size_t length = 0;
    if (!c4fBundled) { snprintf(message, size, "The bundled payload could not be read. Reinstall the Control4Free package."); return -1; }
    if (c4fSandboxLeave()) { snprintf(message, size, "GoldHEN did not let the app set up auto-start (errno %d).", errno); return -1; }
    int failed = c4fPath(directory, sizeof(directory), "payloads") || c4fPath(path, sizeof(path), "payloads/control4free.elf");
    if (!failed && mkdir(directory, 0777) && errno != EEXIST) failed = 1;
    if (!failed && c4fWriteFile(path, c4fBundled, c4fBundledSize)) failed = 1;
    if (failed) {
        snprintf(message, size, "Could not copy Control4Free to /data/payloads (errno %d).", errno);
        return -1;
    }
    failed = c4fPath(directory, sizeof(directory), "GoldHEN") || c4fPath(path, sizeof(path), "GoldHEN/payloads.ini");
    if (!failed && mkdir(directory, 0777) && errno != EEXIST) failed = 1;
    char *old = failed ? NULL : (char *)c4fReadFile(path, &length, 1u << 20);
    if (!failed && !old && errno != ENOENT) failed = 1;
    char *edited = failed ? NULL : c4fIniEdited(old ? old : "", 1);
    if (!failed && (!edited || c4fWriteFile(path, edited, strlen(edited)))) failed = 1;
    if (failed) snprintf(message, size, "Could not add Control4Free to GoldHEN's AutoRun list (errno %d).", errno);
    else snprintf(message, size, "Auto-start is on. GoldHEN starts Control4Free each time it loads.");
    free(old);
    free(edited);
    return failed ? -1 : 0;
}

int c4fAutorunDisable(char *message, size_t size)
{
    char path[300];
    size_t length = 0;
    if (c4fSandboxLeave()) { snprintf(message, size, "GoldHEN did not let the app change auto-start (errno %d).", errno); return -1; }
    int failed = c4fPath(path, sizeof(path), "GoldHEN/payloads.ini");
    char *old = failed ? NULL : (char *)c4fReadFile(path, &length, 1u << 20);
    char *edited = old ? c4fIniEdited(old, 0) : NULL;
    if (!failed && old && (!edited || c4fWriteFile(path, edited, strlen(edited)))) failed = 1;
    if (!failed && !old && errno != ENOENT) failed = 1;
    if (failed) snprintf(message, size, "Could not change GoldHEN's AutoRun list (errno %d).", errno);
    else snprintf(message, size, "Auto-start is off. Start Control4Free here with Cross, or from GoldHEN's Payloader LaunchPad.");
    free(old);
    free(edited);
    return failed ? -1 : 0;
}
