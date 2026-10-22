/* Browser-controlled VDA lifecycle. No automatic button presses or user binds.
 * WebRTC control carries requests and status; its unordered input channel
 * carries sequenced controller state and motion. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"
#include "qrcodegen.h"
#include "c4f_log.h"
#include "c4f_net.h"
#include "c4f_vda.h"
#include "c4f_version.h"
#include "c4f_web.h"

#define C4F_INPUT_MASK 0x0011ffffu
/* A real DualShock 4 reports every 4 ms (250 Hz). Ours matches that while the
 * player is doing something, and falls back to a keepalive rate when they are
 * not, so an idle controller costs the console almost nothing. */
#define C4F_REPORT_MS 16
#define C4F_REPORT_FAST_MS 4
#define C4F_ACTIVE_MS 500
#ifndef C4F_STALE_MS
#define C4F_STALE_MS 3000
#endif
#ifndef C4F_RELEASE_MS
#define C4F_RELEASE_MS 15000
#endif
#define C4F_QUEUE_SIZE 32
/* How often each controller's rumble and light bar are read back. Games pulse
 * rumble for a few tens of milliseconds, so this has to stay well under 100. */
#define C4F_FEEDBACK_MS 16
/* Rumble goes where a lost message isn't resent, so while it's on it is sent
 * again this often. */
#define C4F_RUMBLE_REPEAT_MS 200
#define C4F_USER_POLL_MS 250
#ifndef C4F_LATENCY_MS
#define C4F_LATENCY_MS 6000
#endif

typedef struct {
    C4fVirtualPad device;
    C4fConn *owner;
    ScePadData current, queue[C4F_QUEUE_SIZE];
    unsigned head, count;
    uint64_t lastInput, lastReport, nextReport, detachedAt, reports;
    int active, stale, error, assigned, loginSeen;
    uint32_t userId;
    /* What the game wants this controller to do, and what its owner was told. */
    C4fPadFeedback feedback;
    int feedbackKnown, rumbleSentValid, feedbackFailed;
    uint8_t rumbleSent[2];
    uint64_t feedbackAt, colorLogAt, rumbleSentAt;
    uint8_t colorLogged[3];
    int colorLogKnown;
    /* The signed-in user's name, once the PS4 will tell it. */
    char userName[72];
    int nameTries;
    uint64_t nameAt;
    /* Motion its owner sends ('m'), put into every sample. Until then, and after
     * any neutral reset, samples carry the still values c4fPadDataNeutral sets. */
    int motion;
    float quat[4], accel[3], gyro[3];
    /* The newest input ('i') and motion ('m') applied: over WebRTC they can
     * arrive late or out of order, and an older one must not undo a newer. */
    int64_t inputSeq, motionSeq;
    int inputSeqKnown, motionSeqKnown;
    uint64_t latencyAfter; /* no sample from before the last ownership/stale reset */
} C4fWebPad;

/* Informational, browser-measured input-channel RTT. The service accepts one
 * bounded report for a probe it just echoed on this connection; it never uses
 * the browser's clock or this report to renew a controller's input lease. */
typedef struct {
    C4fConn *conn;
    int64_t probeSeq;
    uint64_t probeAt, measuredProbeAt;
    int probeKnown, pending, known, rttMs;
} C4fWebConn;

/* Creating a controller means issuing AddDevice and then finding its DeviceId in
 * the kernel log, which takes a moment. None of that may stop the service from
 * answering the other players, so it runs as a state machine driven from the main
 * loop. One creation at a time: two AddDevice calls at once would produce two log
 * lines with no way to tell which device belongs to which request. */
typedef enum {
    C4F_ADD_IDLE = 0,
    C4F_ADD_DRAIN,    /* reading off the log's backlog, so no old line is reused */
    C4F_ADD_VERIFY,   /* a marker was written; waiting for it to come back */
    C4F_ADD_DEVICE,   /* AddDevice issued; waiting for the device-added line */
} C4fAddState;

typedef struct {
    C4fAddState   state;
    unsigned      wanted;    /* the whole claim this request asked for */
    unsigned      pending;   /* the slots still to create */
    unsigned      created;   /* the slots created so far, to undo on failure */
    C4fConn *client;    /* who asked, or NULL once they left */
    int64_t       id;
    int           hasId;
    int           slot;      /* the slot AddDevice was issued for, or -1 */
    uint64_t      deadline;
    uint64_t      quietAt;   /* when the log last had something to read */
    uint64_t      deviceId;  /* the DeviceId the reader picked up */
    char          marker[64];
    int           markerSeen;
} C4fAdd;

typedef struct {
    C4fNet net;
    C4fWebPad pads[C4F_MAX_PADS];
    int klogFd, changed, creationBlocked, feedbackMissing;
    unsigned rumbleChanges;   /* since the last heartbeat, to tell a silent game from a silent phone */
    /* What reading rumble and light bar costs, since the last heartbeat. Every
     * microsecond here is one an arriving input could have to wait. */
    uint64_t feedbackCalls, feedbackUs, feedbackMaxUs;
    char klogLine[1024];
    size_t klogUsed;
    C4fAdd add;
    uint64_t broadcastAt, stopAt, heartbeatAt, usersAt;
    int usersFailed;
    C4fWebConn conns[C4F_NET_CLIENTS + C4F_RTC_PEERS]; /* open peers and their optional RTT */
    int connCount;
    /* Loop iterations that took long, since the last heartbeat: while the loop
     * is held up, every page's input waits with it. */
    unsigned slowLoops;
    uint64_t slowestLoop, slowLogAt;
} C4fWeb;

#define C4F_MAX_ARGS 32
typedef struct {
    char method[24];
    int64_t id, args[C4F_MAX_ARGS];
    unsigned argc;
    int hasId;
} C4fRequest;

static int c4fTokenEquals(const char *json, const jsmntok_t *t, const char *s)
{ return t->type == JSMN_STRING && (size_t)(t->end-t->start) == strlen(s) && !memcmp(json+t->start, s, strlen(s)); }

static int c4fInteger(const char *json, const jsmntok_t *t, int64_t *out)
{
    char tmp[24], *end;
    int n = t->end-t->start;
    if (t->type != JSMN_PRIMITIVE || n < 1 || n >= (int)sizeof(tmp)) return -1;
    memcpy(tmp, json+t->start, (size_t)n); tmp[n] = 0;
    for (int i = tmp[0] == '-' ? 1 : 0; i < n; i++) if (tmp[i] < '0' || tmp[i] > '9') return -1;
    errno = 0; *out = strtoll(tmp, &end, 10);
    return errno || end == tmp || *end ? -1 : 0;
}

static int c4fParseRequest(const char *json, size_t len, C4fRequest *r)
{
    jsmn_parser parser;
    jsmntok_t tok[64];
    int count, method = 0, params = 0;
    if (strlen(json) != len) return -1;
    memset(r, 0, sizeof(*r)); jsmn_init(&parser);
    count = jsmn_parse(&parser, json, len, tok, 64);
    if (count < 1 || tok[0].type != JSMN_OBJECT) return -1;
    for (size_t i = (size_t)tok[0].end; i < len; i++) if (!strchr(" \r\n\t", json[i])) return -1;
    for (int i = 1; i < count;) {
        jsmntok_t *key = &tok[i++], *value;
        if (i >= count) return -1;
        value = &tok[i++];
        if (c4fTokenEquals(json, key, "id")) {
            if (r->hasId || c4fInteger(json, value, &r->id) || r->id < 0 || r->id > 9007199254740991LL) return -1;
            r->hasId = 1;
        } else if (c4fTokenEquals(json, key, "method")) {
            int n = value->end-value->start;
            if (method++ || value->type != JSMN_STRING || n < 1 || n >= (int)sizeof(r->method)) return -1;
            memcpy(r->method, json+value->start, (size_t)n); r->method[n] = 0;
        } else if (c4fTokenEquals(json, key, "params")) {
            if (params++ || value->type != JSMN_ARRAY || value->size > C4F_MAX_ARGS) return -1;
            r->argc = (unsigned)value->size;
            for (unsigned a = 0; a < r->argc; a++) {
                if (i >= count || c4fInteger(json, &tok[i++], &r->args[a])) return -1;
            }
        } else if (c4fTokenEquals(json, key, "jsonrpc")) {
            if (!c4fTokenEquals(json, value, "2.0")) return -1;
        } else return -1;
    }
    return method == 1 && params == 1 ? 0 : -1;
}

static void c4fError(C4fConn *c, const C4fRequest *r, int code, const char *message)
{
    char reply[384];
    /* Messages are constant ASCII strings under our control. */
    if (r && r->hasId) snprintf(reply, sizeof(reply), "{\"id\":%lld,\"error\":{\"code\":%d,\"message\":\"%s\"}}", (long long)r->id, code, message);
    else snprintf(reply, sizeof(reply), "{\"method\":\"error\",\"params\":{\"message\":\"%s\"}}", message);
    c4fConnText(c, reply);
}

/* `in` as the inside of a JSON string. Quotes and backslashes are escaped, and
 * control characters and anything that isn't valid UTF-8 become '?'. A user
 * name is the one string here we don't write ourselves, and a browser drops the
 * whole WebSocket on a text frame that isn't valid UTF-8. */
static void c4fJsonText(char *out, size_t size, const char *in)
{
    const unsigned char *p = (const unsigned char *)in;
    size_t o = 0;

    if (!size) return;
    while (*p && o + 7 < size) {
        unsigned c = *p;
        unsigned n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        int valid = n > 0 && !(n == 2 && c < 0xc2) && c <= 0xf4;
        for (unsigned k = 1; valid && k < n; k++) valid = (p[k] & 0xc0) == 0x80;
        /* No overlong forms, no UTF-16 surrogates, nothing past U+10FFFF. */
        if (valid && n == 3 && ((c == 0xe0 && p[1] < 0xa0) || (c == 0xed && p[1] >= 0xa0))) valid = 0;
        if (valid && n == 4 && ((c == 0xf0 && p[1] < 0x90) || (c == 0xf4 && p[1] >= 0x90))) valid = 0;
        if (!valid || (n == 1 && (c < 0x20 || c == 0x7f))) { out[o++] = '?'; p++; continue; }
        if (c == '"' || c == '\\') out[o++] = '\\';
        memcpy(out + o, p, n); o += n; p += n;
    }
    out[o] = 0;
}

static void c4fStatus(C4fWeb *app, C4fConn *c, const C4fRequest *request)
{
    char json[4096];
    size_t pos;
    if (request && request->hasId) pos = (size_t)snprintf(json, sizeof(json), "{\"id\":%lld,\"result\":", (long long)request->id);
    else pos = (size_t)snprintf(json, sizeof(json), "{\"method\":\"s\",\"params\":");
    pos += (size_t)snprintf(json+pos, sizeof(json)-pos, "{\"version\":\"%s\",\"protocol\":3,\"pads\":[", C4F_VERSION);
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        const char *state = p->error ? "error" : app->add.state && (app->add.pending & (1u << i)) ? "connecting"
                          : !p->active ? "free" : !p->owner || p->stale ? "paused" : p->assigned ? "ready" : "select";
        static const unsigned colors[4][3] = {{32,96,255},{255,48,64},{48,200,96},{255,80,180}};
        unsigned rgb[3] = { colors[i][0], colors[i][1], colors[i][2] };
        if (p->active && p->feedbackKnown) {
            /* The PS4 sets player colours at a quarter strength (0x40), which on a
             * screen reads as near black: keep the hue, brighten it to full. An
             * unlit bar keeps the controller's own colour. */
            unsigned l[3] = { p->feedback.r, p->feedback.g, p->feedback.b };
            unsigned top = l[0] > l[1] ? l[0] : l[1];
            if (l[2] > top) top = l[2];
            if (top) for (int k = 0; k < 3; k++) rgb[k] = l[k] * 255 / top;
        }
        char user[160];
        c4fJsonText(user, sizeof(user), p->active && p->assigned ? p->userName : "");
        pos += (size_t)snprintf(json+pos, sizeof(json)-pos,
            "%s{\"pad\":%d,\"name\":\"Controller %d\",\"user\":\"%s\",\"enabled\":true,\"open\":%s,\"connected\":%s,\"clients\":%d,\"mine\":%s,\"state\":\"%s\",\"uid\":\"%s%08x\",\"color\":[%u,%u,%u],\"reports\":%llu,\"error\":%d}",
            i ? "," : "", i, i+1, user, p->active ? "true" : "false", p->owner && !p->stale ? "true" : "false", p->owner ? 1 : 0,
            p->owner == c ? "true" : "false", state, p->assigned ? "" : "unassigned-", p->userId,
            rgb[0], rgb[1], rgb[2], (unsigned long long)p->reports, p->error);
    }
    snprintf(json+pos, sizeof(json)-pos, "]}}"); c4fConnText(c, json);
}

/* How long ago `then` was, and never more than that. These are unsigned
 * milliseconds, and a stamp can be taken later in a loop iteration than the
 * iteration's own `now`: a claim that finishes inside the iteration stamps
 * lastInput from the clock, while the reaper below still holds the `now` it read
 * at the top. Subtracting those directly wrapped to about 49 days and deleted a
 * controller the moment it was created. */
static uint64_t c4fSince(uint64_t now, uint64_t then)
{
    return now > then ? now - then : 0;
}

static C4fWebConn *c4fWebConn(C4fWeb *app, const C4fConn *conn)
{
    for (int i = 0; i < app->connCount; i++) if (app->conns[i].conn == conn) return &app->conns[i];
    return NULL;
}

static void c4fNeutralize(C4fWebPad *p)
{
    p->head = p->count = 0;
    p->motion = 0;
    p->latencyAfter = c4fTimeMs();
    /* Preserve sequence floors across a stale/error reset: delayed packets
     * must not restore a state that a newer packet already superseded. A new
     * owner starts a new sequence space in c4fApplyClaim. */
    c4fPadDataNeutral(&p->current);
}

static void c4fApplyMotion(const C4fWebPad *p, ScePadData *data)
{
    if (!p->motion) return;
    memcpy(data->quat, p->quat, sizeof(data->quat));
    memcpy(data->accel, p->accel, sizeof(data->accel));
    memcpy(data->gyro, p->gyro, sizeof(data->gyro));
}

static void c4fRemove(C4fWeb *app, C4fWebPad *p)
{
    if (p->active) {
        c4fNeutralize(p);
        (void)c4fVirtualPadInsert(&p->device, &p->current);
        c4fVirtualPadRemove(&p->device);
    }
    memset(p, 0, sizeof(*p)); app->changed = 1;
}

static void c4fLogoutPad(C4fWeb *app, C4fWebPad *p, int index)
{
    C4fConn *owner = p->owner;
    if (owner) {
        char message[64];
        snprintf(message, sizeof(message), "{\"method\":\"v\",\"params\":[%d,0,0]}", index);
        c4fConnInput(owner, message);
    }
    c4fLog("web controller %d disconnected after PS4 user logout\n", index + 1);
    c4fRemove(app, p);
    if (owner) c4fError(owner, NULL, 410, "The PS4 user signed out. Controller disconnected. Select it again to play.");
}

/* Assignment and name caches are not login state. UserService keeps working
 * after we release /dev/klog. Logout events cover quick sign-in/sign-out;
 * snapshots also recover a missed event. Never treat a failed snapshot or a
 * user still entering the login list as logout. Foreground-user changes are
 * irrelevant: several local users may play at once. */
static void c4fPollUsers(C4fWeb *app, uint64_t now)
{
    if (now < app->usersAt) return;
    app->usersAt = now + C4F_USER_POLL_MS;
    for (int n = 0; n < 16; n++) {
        int32_t type;
        uint32_t user;
        if (c4fUserEvent(&type, &user) != 0) break;
        if (type != C4F_USER_EVENT_LOGOUT || user == 0xffffffffu) continue;
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            C4fWebPad *p = &app->pads[i];
            if (p->active && p->assigned && p->userId == user) c4fLogoutPad(app, p, i);
        }
    }
    int assigned = 0;
    for (int i = 0; i < C4F_MAX_PADS; i++) assigned |= app->pads[i].active && app->pads[i].assigned;
    if (!assigned) return;
    int32_t users[4];
    int32_t ret = c4fLoginUsers(users);
    if (ret != 0) {
        if (!app->usersFailed) c4fLog("login session read failed (0x%08x); retaining controller assignments\n", (uint32_t)ret);
        app->usersFailed = 1;
        return;
    }
    if (app->usersFailed) c4fLog("login session reads recovered\n");
    app->usersFailed = 0;
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (!p->active || !p->assigned) continue;
        int present = 0;
        for (int j = 0; j < 4; j++) if ((uint32_t)users[j] == p->userId) present = 1;
        if (present) p->loginSeen = 1;
        else if (p->loginSeen) c4fLogoutPad(app, p, i);
    }
}

/* One report for one controller, taking the next queued sample if there is one. */
static void c4fReportPad(C4fWeb *app, C4fWebPad *p, int index, uint64_t now)
{
    int ret;
    if (p->count) {
        p->current = p->queue[p->head]; p->head = (p->head+1) % C4F_QUEUE_SIZE; p->count--;
    }
    ret = c4fVirtualPadInsert(&p->device, &p->current);
    p->reports++; p->lastReport = now;
    p->nextReport = now + (c4fSince(now, p->lastInput) < C4F_ACTIVE_MS ? C4F_REPORT_FAST_MS : C4F_REPORT_MS);
    if (ret < 0 && !p->error) {
        p->error = ret; app->changed = 1;
        c4fNeutralize(p);
        c4fLog("web controller %d InsertData = 0x%08x\n", index+1, (uint32_t)ret);
    }
    if (ret >= 0 && p->error) { p->error = 0; app->changed = 1; }
}

static void c4fReportPads(C4fWeb *app)
{
    uint64_t now = c4fTimeMs();
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (!p->active) continue;
        if (p->owner && c4fSince(now, p->lastInput) >= C4F_STALE_MS && !p->stale) {
            c4fNeutralize(p); p->stale = 1; app->changed = 1;
        }
        if (now < p->nextReport) continue;
        c4fReportPad(app, p, i, now);
    }
}

static uint64_t c4fTimeUs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}

/* The names of signed-in users. The PS4 may not have finished signing a user in
 * when it reports the controller assigned, so a failed look-up is retried for a
 * few seconds. Names are kept out of the log. */
static void c4fLookUpNames(C4fWeb *app, uint64_t now)
{
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (!p->active || !p->assigned || p->userName[0] || p->nameTries >= 10 || now < p->nameAt) continue;
        p->nameTries++;
        p->nameAt = now + 500;
        int32_t ret = c4fUserName(p->userId, p->userName, sizeof(p->userName));
        if (ret == 0 && p->userName[0]) {
            c4fLog("web controller %d user name found (%u bytes)\n", i + 1, (unsigned)strlen(p->userName));
            app->changed = 1;
        } else if (p->nameTries == 10) {
            p->userName[0] = 0;
            c4fLog("web controller %d user name unavailable (0x%08x)\n", i + 1, (uint32_t)ret);
        }
    }
}

/* Reads back what the game wants from each controller. Rumble goes to the
 * controller's owner, whose page drives the phone's vibration and any gamepad
 * playing as it; the light bar goes into the status everyone sees. */
static void c4fPollFeedback(C4fWeb *app, uint64_t now)
{
    if (app->feedbackMissing) return;
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        C4fPadFeedback f;
        int32_t ret;

        if (!p->active || now < p->feedbackAt) continue;
        p->feedbackAt = now + C4F_FEEDBACK_MS;
        uint64_t started = c4fTimeUs();
        ret = c4fVirtualPadFeedback(&p->device, &f);
        uint64_t took = c4fTimeUs() - started;
        app->feedbackCalls++; app->feedbackUs += took;
        if (took > app->feedbackMaxUs) app->feedbackMaxUs = took;
        if (ret == -1) {
            c4fLog("scePadVirtualDeviceGetRemoteSetting is not exported: no rumble or light bar\n");
            app->feedbackMissing = 1;
            return;
        }
        if (ret != 0) {
            if (!p->feedbackFailed) c4fLog("web controller %d GetRemoteSetting = 0x%08x\n", i + 1, (uint32_t)ret);
            p->feedbackFailed = 1;
            continue;
        }
        if (!p->feedbackKnown || f.r != p->feedback.r || f.g != p->feedback.g || f.b != p->feedback.b) {
            app->changed = 1;
        }
        if ((!p->colorLogKnown || f.r != p->colorLogged[0] || f.g != p->colorLogged[1] ||
             f.b != p->colorLogged[2]) && now >= p->colorLogAt) {
            c4fLog("web controller %d light bar %02x %02x %02x\n", i + 1, f.r, f.g, f.b);
            p->colorLogged[0] = f.r; p->colorLogged[1] = f.g; p->colorLogged[2] = f.b;
            p->colorLogKnown = 1; p->colorLogAt = now + 1000;
        }
        if (p->feedbackKnown && (f.large != p->feedback.large || f.small != p->feedback.small))
            app->rumbleChanges++;
        p->feedback = f;
        p->feedbackKnown = 1;
        int changed = !p->rumbleSentValid || f.large != p->rumbleSent[0] || f.small != p->rumbleSent[1];
        int again = p->owner && p->owner->input && (f.large || f.small) && now - p->rumbleSentAt >= C4F_RUMBLE_REPEAT_MS;
        if (p->owner && (changed || again)) {
            char message[64];
            snprintf(message, sizeof(message), "{\"method\":\"v\",\"params\":[%d,%u,%u]}", i, f.large, f.small);
            if (c4fConnInput(p->owner, message) == 0) {
                p->rumbleSent[0] = f.large; p->rumbleSent[1] = f.small; p->rumbleSentValid = 1; p->rumbleSentAt = now;
            }
        }
    }
}

/* Preserve button/touch edges for at least one report. Coalesce analogue-only
 * changes so mouse/gamepad updates cannot create seconds of input latency. */
static void c4fEnqueue(C4fWebPad *p, const ScePadData *data)
{
    ScePadData *last = p->count ? &p->queue[(p->head+p->count-1)%C4F_QUEUE_SIZE] : &p->current;
    if (p->count && last->buttons == data->buttons && last->touchData.fingers == data->touchData.fingers) { *last = *data; return; }
    if (p->count == C4F_QUEUE_SIZE) {
        /* Excessive input cannot leave an old press latched. Keep the latest
         * state and clear obsolete history instead of growing a backlog. */
        p->head = p->count = 0;
    }
    p->queue[(p->head+p->count)%C4F_QUEUE_SIZE] = *data; p->count++;
}

#ifdef C4F_PROBE_SETTING
/* Research only, built with `make C4F_PROBE=1`, never in a release.
 *
 * A real DualShock 4 is told to rumble and to change its light bar by the game.
 * For a virtual pad that has to arrive through the same API the Remote Play path
 * uses, and scePadVirtualDeviceGetRemoteSetting is the only call that looks like
 * it carries anything back. Nothing is known about its buffer, so this polls it
 * for every live controller and logs the return code once and then only what
 * changes. Play something that rumbles, set a light bar, and read the log.
 *
 * The page already knows how to act on both ('v' and 'l' messages), so if the
 * fields turn up here, driving them is a small step. */
static void c4fProbeSetting(C4fWeb *app, uint64_t now)
{
    static uint64_t nextAt;
    static unsigned char previous[C4F_MAX_PADS][256];
    static int32_t lastRet[C4F_MAX_PADS];
    static int seen[C4F_MAX_PADS];
    unsigned char current[256];

    if (now < nextAt) return;
    nextAt = now + 100;
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        int32_t ret;
        int first, from = -1, to = -1;

        if (!p->active) { seen[i] = 0; continue; }
        if (!scePadVirtualDeviceGetRemoteSetting) {
            if (!seen[i]) c4fLog("probe: scePadVirtualDeviceGetRemoteSetting is not exported\n");
            seen[i] = 1;
            continue;
        }
        memset(current, 0, sizeof(current));
        ret = scePadVirtualDeviceGetRemoteSetting(p->device.handle, current);
        first = !seen[i];
        seen[i] = 1;
        if (first || ret != lastRet[i]) {
            c4fLog("probe: controller %d GetRemoteSetting = 0x%08x\n", i + 1, (uint32_t)ret);
            lastRet[i] = ret;
        }
        if (ret != 0) continue;
        for (int b = 0; b < (int)sizeof(current); b++)
            if (first || current[b] != previous[i][b]) { if (from < 0) from = b; to = b; }
        if (from >= 0) {
            char hex[3 * 256 + 1];
            int used = 0;
            for (int b = from; b <= to && used + 3 < (int)sizeof(hex); b++)
                used += snprintf(hex + used, sizeof(hex) - (size_t)used, "%02x ", current[b]);
            c4fLog("probe: controller %d bytes %d-%d: %s\n", i + 1, from, to, hex);
            memcpy(previous[i], current, sizeof(current));
        }
    }
}
#endif

/* One kernel-log line. Our own mirrored output is skipped: c4fLog writes to klog
 * too, so a line that reacted to a line would feed itself for ever. The marker
 * c4fKlogMark writes carries no [c4f], which is how it gets through. */
static void c4fKlogLine(C4fWeb *app, const char *line)
{
    unsigned long long device;
    unsigned user;
    const char *event;

    if (strstr(line, "[c4f]")) return;

    if (app->add.state == C4F_ADD_VERIFY && !app->add.markerSeen &&
        app->add.marker[0] && strstr(line, app->add.marker))
        app->add.markerSeen = 1;

    if (app->add.state == C4F_ADD_DEVICE && !app->add.deviceId && c4fKlogIsVirtualAdd(line))
        app->add.deviceId = c4fKlogDeviceId(line);

    event = strstr(line, "DEVICE_OWNER_CHANGED [DeviceId:");
    if (event && sscanf(event, "DEVICE_OWNER_CHANGED [DeviceId:0x%llx][UserId:0x%x]", &device, &user) == 2) {
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            C4fWebPad *p = &app->pads[i];
            if (p->active && p->device.deviceId == device) {
                if (user == 0xffffffffu && p->assigned) { c4fLogoutPad(app, p, i); continue; }
                if (user != p->userId) p->loginSeen = 0;
                p->assigned = user != 0xffffffffu; p->userId = user; app->changed = 1;
                p->userName[0] = 0; p->nameTries = 0; p->nameAt = 0;
                c4fLog("web controller %d assignment confirmed=%d\n", i+1, p->assigned);
            }
        }
    }
}

/* Reads whatever the log has right now and never waits. Returns the number of
 * bytes taken, so the caller can tell a quiet log from a busy one. */
static long c4fReadKlog(C4fWeb *app)
{
    char buf[2048];
    long total = 0;
    if (app->klogFd < 0) return 0;
    for (int batch = 0; batch < 8; batch++) {
        ssize_t n = read(app->klogFd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return total;
        if (n <= 0) {
            close(app->klogFd); app->klogFd = -1;
            c4fLog("klog source closed; reconnecting at the next controller request\n");
            return total;
        }
        total += (long)n;
        for (ssize_t j = 0; j < n; j++) {
            if (buf[j] == '\n') {
                app->klogLine[app->klogUsed] = 0;
                c4fKlogLine(app, app->klogLine);
                app->klogUsed = 0;
            } else if (buf[j] != '\r' && app->klogUsed+1 < sizeof(app->klogLine)) app->klogLine[app->klogUsed++] = buf[j];
        }
    }
    return total;
}

static void c4fReleaseKlog(C4fWeb *app)
{
    if (app->klogFd < 0) return;
    close(app->klogFd); app->klogFd = -1; app->klogUsed = 0;
    c4fLog("released klog reader\n");
}

/* Assignment events are needed through native sign-in, not throughout play. */
static void c4fReleaseIdleKlog(C4fWeb *app)
{
    for (int i = 0; i < C4F_MAX_PADS; i++)
        if (app->pads[i].active && !app->pads[i].assigned) return;
    c4fReleaseKlog(app);
}

/* Hands the claim its slots once every device it needed exists. */
static void c4fApplyClaim(C4fWeb *app, C4fConn *c, unsigned wanted, C4fRequest *r)
{
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (wanted & (1u << i)) {
            if (p->owner != c) {
                c4fNeutralize(p); p->rumbleSentValid = 0;
                p->inputSeqKnown = p->motionSeqKnown = 0;
            }
            p->owner = c; p->detachedAt = 0; p->lastInput = c4fTimeMs(); p->stale = 0;
        } else if (p->owner == c) c4fRemove(app, p);
    }
    app->changed = 1; c4fStatus(app, c, r);
}

static void c4fAddReset(C4fWeb *app)
{
    memset(&app->add, 0, sizeof(app->add));
    app->add.slot = -1;
}

/* Gives up on the creation in progress. `blocked` means AddDevice had already
 * run, so a device we can no longer address may exist. */
static void c4fAddFail(C4fWeb *app, int code, const char *message, int blocked)
{
    C4fConn *client = app->add.client;
    C4fRequest reply;

    if (blocked) app->creationBlocked = 1;
    for (int i = 0; i < C4F_MAX_PADS; i++)
        if (app->add.created & (1u << i)) c4fRemove(app, &app->pads[i]);

    memset(&reply, 0, sizeof(reply));
    reply.id = app->add.id; reply.hasId = app->add.hasId;
    c4fAddReset(app);
    app->changed = 1;
    if (client) c4fError(client, reply.hasId ? &reply : NULL, code, message);
}

/* Never abandon an issued AddDevice: capture its handle, then roll it back.
 * Unissued work can be canceled immediately without touching retained pads. */
static void c4fCancelAdd(C4fWeb *app)
{
    if (!app->add.state) return;
    app->add.client = NULL;
    if (app->add.state != C4F_ADD_DEVICE)
        c4fAddFail(app, 503, "Controller connection canceled", 0);
}

/* Issues AddDevice for the lowest slot still waiting. */
static void c4fAddNextDevice(C4fWeb *app, uint64_t now)
{
    for (int i = 0; i < C4F_MAX_PADS; i++) if (app->add.pending & (1u << i)) {
        app->add.slot = i;
        app->add.deviceId = 0;
        app->add.state = C4F_ADD_DEVICE;
        app->add.deadline = now + 2500;
        (void)c4fVirtualPadAdd(C4F_VDA_USER_SELECT);
        return;
    }
    app->add.slot = -1;
}

/* One step of the creation in progress, from the main loop. */
static void c4fAdvanceAdd(C4fWeb *app, uint64_t now, long klogBytes)
{
    if (!app->add.state) return;

    if (app->klogFd < 0) {
        c4fAddFail(app, 503, "The PS4's kernel log went away while the controller was being connected. Try again.",
                   app->add.state == C4F_ADD_DEVICE);
        return;
    }
    if (klogBytes) app->add.quietAt = now;

    switch (app->add.state) {
    case C4F_ADD_DRAIN:
        /* A fresh reader can replay a backlog, and an old device-added line in it
         * would hand us a DeviceId that is not ours. Wait for the log to go quiet
         * first, but not for ever: a busy console is still a usable one. */
        if (c4fSince(now, app->add.quietAt) < 150 && now < app->add.deadline) return;
        snprintf(app->add.marker, sizeof(app->add.marker), "c4f-klog-mark-%llu",
                 (unsigned long long)now);
        app->add.markerSeen = 0;
        app->add.state = C4F_ADD_VERIFY;
        app->add.deadline = now + 2000;
        c4fKlogMark(app->add.marker);
        return;

    case C4F_ADD_VERIFY:
        if (app->add.markerSeen) { c4fAddNextDevice(app, now); return; }
        if (now < app->add.deadline) return;
        /* The reader was opened but delivers nothing. Nothing was created yet, so
         * drop it and let the next attempt open a fresh one. */
        c4fReleaseKlog(app);
        c4fAddFail(app, 503, "Signing a controller in needs the PS4's kernel log, and something else has it. Close any klog viewer connected to GoldHEN, then try again.", 0);
        return;

    case C4F_ADD_DEVICE:
        if (app->add.deviceId) {
            C4fWebPad *p = &app->pads[app->add.slot];
            c4fVirtualPadAdopt(&p->device, C4F_USER_ID_INVALID, C4F_VDA_USER_SELECT, app->add.deviceId);
            p->active = 1; p->userId = 0xffffffffu; p->lastInput = now;
            /* It has no owner until the whole claim is answered. Start its idle
             * clock now so the reaper does not take it in the meantime. */
            p->detachedAt = now;
            c4fNeutralize(p);
            app->add.created |= 1u << app->add.slot;
            app->add.pending &= ~(1u << app->add.slot);
            app->changed = 1;
            /* Done. If the player left while we worked, the devices have nobody
             * to drive them. */
            if (!app->add.client) {
                for (int i = 0; i < C4F_MAX_PADS; i++)
                    if (app->add.created & (1u << i)) c4fRemove(app, &app->pads[i]);
                c4fAddReset(app);
                return;
            }
            if (app->add.pending) { c4fAddNextDevice(app, now); return; }
            {
                C4fConn *client = app->add.client;
                unsigned wanted = app->add.wanted;
                C4fRequest reply;
                memset(&reply, 0, sizeof(reply));
                reply.id = app->add.id; reply.hasId = app->add.hasId;
                for (int i = 0; i < C4F_MAX_PADS; i++) if (wanted & (1u << i)) {
                    if (!app->pads[i].active || (app->pads[i].owner && app->pads[i].owner != client)) {
                        c4fAddFail(app, 409, "Controller ownership changed; select controllers again", 0);
                        return;
                    }
                }
                c4fAddReset(app);
                c4fApplyClaim(app, client, wanted, reply.hasId ? &reply : NULL);
            }
            return;
        }
        if (now < app->add.deadline) return;
        /* AddDevice ran and no handle came back: there may be a device out there
         * we cannot address. Stop creating until the payload is restarted rather
         * than collect more of them. */
        c4fLog("no virtual device line for controller %d within the wait\n", app->add.slot + 1);
        c4fAddFail(app, 503, "The PS4 did not report the new controller. Restart Control4Free from its app on the PS4 before trying again.", 1);
        return;

    default:
        return;
    }
}

static void c4fClaim(C4fWeb *app, C4fConn *c, C4fRequest *r)
{
    unsigned wanted = 0, needed = 0;
    int needsAssignment = 0;
    for (unsigned i = 0; i < r->argc; i++) {
        if (r->args[i] < 0 || r->args[i] >= C4F_MAX_PADS) { c4fError(c, r, 400, "There is no controller with that number"); return; }
        wanted |= 1u << r->args[i];
    }
    if (app->add.state && (app->add.client == c || (wanted & app->add.wanted))) {
        c4fError(c, r, 409, "A pending controller claim reserves these slots; try again in a moment"); return;
    }
    /* Validate all claims before changing any ownership. */
    for (int i = 0; i < C4F_MAX_PADS; i++) if (wanted & (1u << i)) {
        C4fWebPad *p = &app->pads[i];
        if (p->owner && p->owner != c) { c4fError(c, r, 409, "Controller is in use on another device"); return; }
        if (!p->active && app->creationBlocked) { c4fError(c, r, 503, "Control4Free cannot add controllers until it is restarted. Restart it from the Control4Free app on the PS4."); return; }
        if (!p->active) needed |= 1u << i;
        if (p->active && !p->assigned) needsAssignment = 1;
    }
    if (needed && app->add.state) {
        c4fError(c, r, 409, "Another controller is being connected; try again in a moment"); return;
    }
    if ((needed || needsAssignment) && app->klogFd < 0) {
        app->klogFd = c4fKlogOpenDevice();
        if (app->klogFd < 0 && needed) { c4fError(c, r, 503, "Signing a controller in needs the PS4's kernel log, and something else has it. Close any klog viewer connected to GoldHEN, then try again."); return; }
    }
    if (!needed) { c4fApplyClaim(app, c, wanted, r); return; }

    /* The devices are created from the main loop, which answers this request when
     * it is finished. Everyone else keeps being served in the meantime. */
    c4fAddReset(app);
    app->add.state = C4F_ADD_DRAIN;
    app->add.wanted = wanted;
    app->add.pending = needed;
    app->add.client = c;
    app->add.id = r->id;
    app->add.hasId = r->hasId;
    app->add.quietAt = c4fTimeMs();
    app->add.deadline = app->add.quietAt + 3000;
    app->changed = 1;
}

/* A QR code of the page's address, for the page's Invite panel. The page names
 * the address it reached us on, as [a, b, c, d, port], because that's the one
 * that works from this network; without it, the console's own address is used.
 * The answer is the module grid, one hex string per row, most significant bit
 * first: the page draws it, so it needs no QR library of its own. */
static void c4fInvite(C4fConn *c, const C4fRequest *r)
{
    char text[64], address[64], reply[1024];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(5)];
    size_t pos;

    if (r->argc == 5) {
        for (int i = 0; i < 4; i++)
            if (r->args[i] < 0 || r->args[i] > 255) { c4fError(c, r, 400, "That isn't an IPv4 address"); return; }
        if (r->args[4] < 1 || r->args[4] > 65535) { c4fError(c, r, 400, "That isn't a port"); return; }
        snprintf(text, sizeof(text), "http://%d.%d.%d.%d:%d/", (int)r->args[0], (int)r->args[1],
                 (int)r->args[2], (int)r->args[3], (int)r->args[4]);
    } else if (r->argc == 0) {
        c4fNetLocalAddress(address, sizeof(address));
        if (!address[0]) { c4fError(c, r, 503, "Couldn't work out the console's address"); return; }
        snprintf(text, sizeof(text), "http://%s:%d/", address, C4F_WEB_PORT);
    } else {
        c4fError(c, r, 400, "Invalid request"); return;
    }
    if (!qrcodegen_encodeText(text, scratch, qr, qrcodegen_Ecc_MEDIUM, 1, 5, qrcodegen_Mask_AUTO, true)) {
        c4fError(c, r, 500, "Couldn't make the QR code"); return;
    }
    int size = qrcodegen_getSize(qr);
    pos = (size_t)snprintf(reply, sizeof(reply), "{\"id\":%lld,\"result\":{\"text\":\"%s\",\"size\":%d,\"rows\":[",
                           (long long)r->id, text, size);
    for (int y = 0; y < size && pos < sizeof(reply) - 16; y++) {
        reply[pos++] = y ? ',' : '"';
        if (y) reply[pos++] = '"';
        for (int x = 0; x < size; x += 4) {
            unsigned nibble = 0;
            for (int b = 0; b < 4; b++)
                nibble = nibble << 1 | (x + b < size && qrcodegen_getModule(qr, x + b, y));
            reply[pos++] = "0123456789abcdef"[nibble];
        }
        reply[pos++] = '"';
    }
    snprintf(reply + pos, sizeof(reply) - pos, "]}}");
    c4fConnText(c, reply);
}

/* Buttons, sticks, triggers and touches from args[at..]: [buttons, lx, ly, rx,
 * ry, l2, r2, n, (id, x, y) x n]. The number of args it took, or 0 after
 * answering an error. */
static unsigned c4fParseState(C4fConn *c, C4fRequest *r, unsigned at, ScePadData *data)
{
    const int64_t *a = r->args + at;
    if (r->argc < at + 8) { c4fError(c, r, 400, "That input was not in a form Control4Free understands"); return 0; }
    if (a[0] < 0 || a[0] > C4F_INPUT_MASK || (a[0] & ~C4F_INPUT_MASK)) { c4fError(c, r, 400, "Those are not buttons a DualShock 4 has"); return 0; }
    for (unsigned i = 1; i < 7; i++) if (a[i] < 0 || a[i] > 255) { c4fError(c, r, 400, "A stick or trigger was outside its range"); return 0; }
    if (a[7] < 0 || a[7] > 2 || r->argc < at + 8 + 3 * (unsigned)a[7]) { c4fError(c, r, 400, "That touch was not in a form Control4Free understands"); return 0; }
    c4fPadDataNeutral(data); data->buttons = (uint32_t)a[0];
    data->lx = a[1]; data->ly = a[2]; data->rx = a[3]; data->ry = a[4]; data->l2 = a[5]; data->r2 = a[6];
    data->touchData.fingers = a[7];
    for (unsigned i = 0; i < data->touchData.fingers; i++) {
        const int64_t *t = &a[8 + 3 * i];
        if (t[0] < 0 || t[0] > 127 || t[1] < 0 || t[1] > 1919 || t[2] < 0 || t[2] > 941) { c4fError(c, r, 400, "A touch landed outside the touchpad"); return 0; }
        data->touchData.touch[i].finger = t[0]; data->touchData.touch[i].x = t[1]; data->touchData.touch[i].y = t[2];
    }
    return 8 + 3 * data->touchData.fingers;
}

/* A new state for the pad: queued, and out at once unless a report has only
 * just gone (holding it for the next tick would add lag for nothing). */
static void c4fTakeInput(C4fWeb *app, C4fWebPad *p, int index, ScePadData *data)
{
    c4fApplyMotion(p, data);
    if (p->stale) { p->stale = 0; app->changed = 1; }
    uint64_t now = c4fTimeMs();
    if (c4fSince(now, p->lastInput) >= C4F_STALE_MS) p->latencyAfter = now;
    p->lastInput = now; c4fEnqueue(p, data);
    if (c4fSince(now, p->lastReport) >= C4F_REPORT_FAST_MS) c4fReportPad(app, p, index, now);
}

/* u: [pad, buttons, lx, ly, rx, ry, l2, r2, n, (id, x, y) x n], in order on a
 * reliable connection (the host tests' WebSocket). */
#ifdef C4F_TEST_WEBSOCKET
static void c4fUpdate(C4fWeb *app, C4fConn *c, C4fRequest *r)
{
    if (r->argc < 9 || r->args[0] < 0 || r->args[0] >= C4F_MAX_PADS) { c4fError(c, r, 400, "That input was not in a form Control4Free understands"); return; }
    C4fWebPad *p = &app->pads[r->args[0]];
    if (p->owner != c || !p->active) return; /* input never implicitly claims a pad */
    ScePadData data;
    unsigned used = c4fParseState(c, r, 1, &data);
    if (!used) return;
    if (r->argc != 1 + used) { c4fError(c, r, 400, "That touch was not in a form Control4Free understands"); return; }
    c4fTakeInput(app, p, (int)r->args[0], &data);
}
#endif

/* i: [pad, seq, buttons, lx, ly, rx, ry, l2, r2, n, (id, x, y) x n, k,
 * (seq, buttons) x k], the page's input over WebRTC, where a message can be
 * lost or overtaken. Anything older than the newest applied is dropped. The k
 * pairs are the page's latest button changes: one newer than what arrived last
 * means messages were lost in between, so that button state is queued first,
 * and a quick tap still happens. */
static void c4fInput(C4fWeb *app, C4fConn *c, C4fRequest *r)
{
    if (r->argc < 11 || r->args[0] < 0 || r->args[0] >= C4F_MAX_PADS ||
        r->args[1] < 0 || r->args[1] > 9007199254740991LL) { c4fError(c, r, 400, "That input was not in a form Control4Free understands"); return; }
    C4fWebPad *p = &app->pads[r->args[0]];
    if (p->owner != c || !p->active) return;
    int64_t seq = r->args[1];
    ScePadData data;
    unsigned used = c4fParseState(c, r, 2, &data);
    if (!used) return;
    unsigned k = 2 + used;
    if (k >= r->argc || r->args[k] < 0 || r->args[k] > 4 || r->argc != k + 1 + 2 * (unsigned)r->args[k]) { c4fError(c, r, 400, "That input was not in a form Control4Free understands"); return; }
    unsigned edges = (unsigned)r->args[k];
    for (unsigned e = 0; e < edges; e++) {
        int64_t at = r->args[k + 1 + 2 * e];
        int64_t buttons = r->args[k + 2 + 2 * e];
        if (at < 0 || at > seq || (e && at <= r->args[k - 1 + 2 * e])) {
            c4fError(c, r, 400, "Input history is not in sequence"); return;
        }
        if (buttons < 0 || buttons > C4F_INPUT_MASK || (buttons & ~C4F_INPUT_MASK)) { c4fError(c, r, 400, "Those are not buttons a DualShock 4 has"); return; }
    }
    if (p->inputSeqKnown && seq <= p->inputSeq) return;   /* late: a newer one is in already */
    if (p->inputSeqKnown) {
        for (unsigned e = 0; e < edges; e++) {
            int64_t at = r->args[k + 1 + 2 * e];
            if (at <= p->inputSeq || at >= seq) continue;
            ScePadData missed = data;
            missed.buttons = (uint32_t)r->args[k + 2 + 2 * e];
            c4fApplyMotion(p, &missed);
            c4fEnqueue(p, &missed);
        }
    }
    p->inputSeq = seq; p->inputSeqKnown = 1;
    c4fTakeInput(app, p, (int)r->args[0], &data);
}

/* e: [seq, time]: answered at once on the same path, for the page's link
 * quality line. */
static void c4fEcho(C4fWeb *app, C4fConn *c, C4fRequest *r)
{
    if (r->argc != 2) { c4fError(c, r, 400, "Invalid request"); return; }
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"method\":\"e\",\"params\":[%lld,%lld]}", (long long)r->args[0], (long long)r->args[1]);
    C4fWebConn *peer = c4fWebConn(app, c);
    if (c4fConnInput(c, reply) == 0 && peer && r->args[0] >= 0 && r->args[0] <= 9007199254740991LL &&
        (!peer->probeKnown || r->args[0] > peer->probeSeq)) {
        peer->probeSeq = r->args[0]; peer->probeAt = c4fTimeMs();
        peer->probeKnown = peer->pending = 1;
    }
}

static void c4fLatency(C4fWeb *app, C4fConn *c, const C4fRequest *r)
{
    if (r->argc != 2 || r->args[0] < 0 || r->args[0] > 9007199254740991LL || r->args[1] < 0 || r->args[1] > 60000)
        return;
    C4fWebConn *peer = c4fWebConn(app, c);
    uint64_t now = c4fTimeMs();
    if (!peer || !peer->pending || r->args[0] != peer->probeSeq || c4fSince(now, peer->probeAt) > C4F_LATENCY_MS) return;
    peer->pending = 0; peer->known = 1;
    peer->rttMs = (int)r->args[1]; peer->measuredProbeAt = peer->probeAt;
}

/* m: [pad, seq, qx, qy, qz, qw, ax, ay, az, gx, gy, gz] in whole numbers: the
 * orientation x 10000, acceleration in thousandths of a G, angular velocity in
 * thousandths of a radian per second. Sequence numbers reject older motion.
 * The buttons, sticks and touches stay as
 * the last input left them. */
static void c4fMotion(C4fWeb *app, C4fConn *c, C4fRequest *r)
{
    static const int64_t limits[3] = { 10000, 16000, 35000 };
    unsigned offset = 2;
#ifdef C4F_TEST_WEBSOCKET
    if (r->argc == 11) offset = 1;   /* legacy application test adapter only */
#endif
    if (r->argc != offset + 10 || r->args[0] < 0 || r->args[0] >= C4F_MAX_PADS ||
        (offset == 2 && (r->args[1] < 0 || r->args[1] > 9007199254740991LL))) { c4fError(c, r, 400, "That motion was not in a form Control4Free understands"); return; }
    C4fWebPad *p = &app->pads[r->args[0]];
    if (p->owner != c || !p->active) return;
    for (unsigned i = 0; i < 10; i++) {
        int64_t limit = limits[i < 4 ? 0 : i < 7 ? 1 : 2];
        if (r->args[offset+i] < -limit || r->args[offset+i] > limit) { c4fError(c, r, 400, "Motion was outside its range"); return; }
    }
    if (offset == 2) {
        if (p->motionSeqKnown && r->args[1] <= p->motionSeq) return;
        p->motionSeq = r->args[1]; p->motionSeqKnown = 1;
    }
    for (int i = 0; i < 4; i++) p->quat[i] = (float)r->args[offset+i] / 10000.0f;
    for (int i = 0; i < 3; i++) {
        p->accel[i] = (float)r->args[offset+4+i] / 1000.0f;
        p->gyro[i] = (float)r->args[offset+7+i] / 1000.0f;
    }
    p->motion = 1;
    uint64_t now = c4fTimeMs();
    /* Motion alone cannot renew an old held button's input lease. */
    /* The newest state, moved: coalesces with a queued sample of the same buttons. */
    ScePadData data = p->count ? p->queue[(p->head+p->count-1)%C4F_QUEUE_SIZE] : p->current;
    c4fApplyMotion(p, &data);
    c4fEnqueue(p, &data);
    if (c4fSince(now, p->lastReport) >= C4F_REPORT_FAST_MS) c4fReportPad(app, p, (int)r->args[0], now);
}

static void c4fWebEvent(C4fConn *c, int event, const char *text, size_t len, void *context)
{
    C4fWeb *app = context;
    if (event == C4F_NET_HTTP_STATUS) {
        int active = 0;
        char reply[1024];
        for (int i = 0; i < C4F_MAX_PADS; i++) active += app->pads[i].active != 0;
        size_t pos = (size_t)snprintf(reply, sizeof(reply), "{\"application\":\"Control4Free\",\"api\":1,\"version\":\"%s\",\"controllers\":%d,\"stopping\":%s,\"latency\":true", C4F_VERSION, active, app->stopAt ? "true" : "false");
        uint64_t now = c4fTimeMs();
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            const C4fWebPad *p = &app->pads[i];
            int eligible = p->active && p->assigned && p->owner && !p->stale && c4fSince(now, p->lastInput) < C4F_STALE_MS;
            C4fWebConn *peer = eligible ? c4fWebConn(app, p->owner) : NULL;
            int known = peer && peer->known && peer->measuredProbeAt > p->latencyAfter &&
                        c4fSince(now, peer->measuredProbeAt) <= C4F_LATENCY_MS;
            pos += (size_t)snprintf(reply + pos, sizeof(reply) - pos,
                ",\"pad%dUser\":%lld,\"pad%dRtt\":%d,\"pad%dAge\":%lld", i,
                eligible ? (long long)p->userId : -1LL, i, known ? peer->rttMs : -1, i,
                known ? (long long)c4fSince(now, peer->measuredProbeAt) : -1LL);
        }
        snprintf(reply + pos, sizeof(reply) - pos, "}");
        c4fNetHttpJson(c, reply); return;
    }
    if (event == C4F_NET_HTTP_STOP) {
        c4fCancelAdd(app);
        for (int i = 0; i < C4F_MAX_PADS; i++) c4fRemove(app, &app->pads[i]);
        c4fNetHttpJson(c, "{\"application\":\"Control4Free\",\"stopping\":true}");
        if (!app->stopAt) app->stopAt = c4fTimeMs() + 250;
        return;
    }
    if (event == C4F_NET_OPEN) {
        if (app->connCount < (int)(sizeof(app->conns) / sizeof(app->conns[0]))) {
            C4fWebConn *peer = &app->conns[app->connCount++];
            memset(peer, 0, sizeof(*peer)); peer->conn = c;
        }
        c4fStatus(app, c, NULL);
        return;
    }
    if (event == C4F_NET_CLOSE) {
        for (int i = 0; i < app->connCount; i++) if (app->conns[i].conn == c) { app->conns[i] = app->conns[--app->connCount]; break; }
        /* Before the slot can be reused by someone else. */
        if (app->add.client == c) c4fCancelAdd(app);
        for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].owner == c) {
            C4fWebPad *p = &app->pads[i];
            c4fNeutralize(p); p->owner = NULL; p->detachedAt = c4fTimeMs(); app->changed = 1;
        }
        return;
    }
    C4fRequest r;
    if (c4fParseRequest(text, len, &r)) { c4fError(c, NULL, 400, "Invalid request"); return; }
    int input = !strcmp(r.method, "i") || !strcmp(r.method, "m") || !strcmp(r.method, "e") || !strcmp(r.method, "latency");
    if (c->input && input != (event == C4F_NET_INPUT_EVENT)) {
        c4fError(c, &r, 400, "Message sent on the wrong data channel"); return;
    }
    if (app->stopAt) { c4fError(c, &r, 503, "Control4Free is shutting down"); return; }
    if (!strcmp(r.method, "info")) {
        char reply[192];
        if (r.hasId) { snprintf(reply, sizeof(reply), "{\"id\":%lld,\"result\":{\"version\":\"%s\",\"protocol\":3,\"pads\":%d,\"motion\":true,\"input\":true,\"latency\":true}}", (long long)r.id, C4F_VERSION, C4F_MAX_PADS); c4fConnText(c, reply); }
    } else if (!strcmp(r.method, "ping")) {
        char reply[64];
        if (r.hasId) { snprintf(reply, sizeof(reply), "{\"id\":%lld,\"result\":{}}", (long long)r.id); c4fConnText(c, reply); }
    } else if (!strcmp(r.method, "invite")) {
        if (r.hasId) c4fInvite(c, &r);
    } else if (!strcmp(r.method, "status")) c4fStatus(app, c, &r);
    else if (!strcmp(r.method, "claim")) c4fClaim(app, c, &r);
#ifdef C4F_TEST_WEBSOCKET
    else if (!strcmp(r.method, "u")) c4fUpdate(app, c, &r);
#endif
    else if (!strcmp(r.method, "i")) c4fInput(app, c, &r);
    else if (!strcmp(r.method, "e")) c4fEcho(app, c, &r);
    else if (!strcmp(r.method, "latency")) c4fLatency(app, c, &r);
    else if (!strcmp(r.method, "m")) c4fMotion(app, c, &r);
    else if (!strcmp(r.method, "leave")) {
        if (app->add.client == c) { c4fError(c, &r, 409, "Wait for the pending controller claim"); return; }
        if (r.argc != 1 || r.args[0] < 0 || r.args[0] >= C4F_MAX_PADS || app->pads[r.args[0]].owner != c) { c4fError(c, &r, 409, "That controller is not yours to disconnect"); return; }
        c4fRemove(app, &app->pads[r.args[0]]); c4fStatus(app, c, &r);
    } else if (!strcmp(r.method, "stop")) {
        int occupied = 0;
        for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].owner && app->pads[i].owner != c) occupied = 1;
        if (occupied) { c4fError(c, &r, 409, "Someone else is still using a controller. Ask them to disconnect, or stop Control4Free from its app on the PS4."); return; }
        c4fCancelAdd(app);
        app->stopAt = c4fTimeMs() + 250;
    } else c4fError(c, &r, 404, "Control4Free on the PS4 is older than this page. Update the PS4 side.");
}

int c4fWebRun(int klogFd)
{
    /* Keep network and pad queues off the payload thread's small stack. */
    C4fWeb *app = calloc(1, sizeof(*app));
    if (!app) { if (klogFd >= 0) close(klogFd); return -1; }
    app->klogFd = klogFd;
    if (c4fNetOpen(&app->net, C4F_WEB_PORT, c4fWebEvent, app)) {
        c4fLog("web listen failed errno=%d\n", errno);
        if (klogFd >= 0) close(klogFd);
        c4fRtcDestroy(&app->net.rtc);
        free(app); return -1;
    }
    c4fLog("Control4Free %s: browser controller on port %d\n", C4F_VERSION, C4F_WEB_PORT);
    {
        /* The address to type in, so nobody has to go and look it up. */
        char address[64];
        c4fNetLocalAddress(address, sizeof(address));
        if (address[0]) {
            c4fLog("reachable at http://%s:%d\n", address, C4F_WEB_PORT);
            c4fNotify("Control4Free: open http://%s:%d on your phone or PC", address, C4F_WEB_PORT);
        } else {
            c4fNotify("Control4Free: open your PS4's IP address with :%d in a browser", C4F_WEB_PORT);
        }
    }
    uint64_t previous = c4fTimeMs(), retryAt = 0;
    time_t previousWall = time(NULL);
    for (;;) {
        uint64_t now = c4fTimeMs();
        time_t wall = time(NULL);
        if (app->stopAt && now >= app->stopAt && !app->add.state) break;
        /* Some clocks exclude suspension. Wall time is only a second signal
         * for a gap, never an input deadline or a trusted calendar timestamp. */
        if (now < previous || now - previous > 5000 || wall - previousWall > 5) {
            c4fLog("service resumed after a gap (monotonic=%lldms wall=%llds); resetting connections\n",
                   (long long)now - (long long)previous, (long long)wall - (long long)previousWall);
            c4fNetClose(&app->net); /* close events neutralize and discard queued input */
            c4fCancelAdd(app);
            if (!app->add.state) c4fReleaseKlog(app);
            for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].active)
                app->pads[i].nextReport = now;
            app->broadcastAt = app->heartbeatAt = now;
            retryAt = now;
        }
        if (app->net.fd < 0 && now >= retryAt) {
            if (c4fNetOpen(&app->net, C4F_WEB_PORT, c4fWebEvent, app)) {
                c4fLog("listener reopen failed errno=%d; retrying in 1s\n", errno);
                retryAt = now + 1000;
            } else c4fLog("listener recovered on port %d\n", C4F_WEB_PORT);
        }
        uint64_t loopStart = now, mark = c4fTimeMs();
        long klogBytes = c4fReadKlog(app);
        uint64_t klogMs = c4fTimeMs() - mark; mark += klogMs;
        c4fPollUsers(app, now);
        uint64_t usersMs = c4fTimeMs() - mark; mark += usersMs;
        c4fReportPads(app);
        uint64_t reportMs = c4fTimeMs() - mark; mark += reportMs;
        c4fPollFeedback(app, now);
        uint64_t feedbackMs = c4fTimeMs() - mark; mark += feedbackMs;
        c4fLookUpNames(app, now);
        uint64_t namesMs = c4fTimeMs() - mark;
#ifdef C4F_PROBE_SETTING
        c4fProbeSetting(app, now);
#endif
        c4fAdvanceAdd(app, now, klogBytes);
        now = c4fTimeMs();
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            C4fWebPad *p = &app->pads[i];
            if (app->add.state && (app->add.wanted & (1u << i))) continue;
            if (p->active && ((!p->owner && c4fSince(now, p->detachedAt) > C4F_RELEASE_MS) ||
                              (p->owner && c4fSince(now, p->lastInput) > C4F_RELEASE_MS))) {
                C4fConn *owner = p->owner;
                c4fLog("web controller %d removed after %llu ms %s\n", i + 1,
                       (unsigned long long)c4fSince(now, owner ? p->lastInput : p->detachedAt),
                       owner ? "without input" : "with nobody connected");
                c4fRemove(app, p);
                if (owner) c4fError(owner, NULL, 408, "That controller was disconnected after sitting unused. Select it again to play.");
            }
        }
        if (!app->add.state) c4fReleaseIdleKlog(app);
        if (now >= app->heartbeatAt) {
            int active = 0;
            for (int i = 0; i < C4F_MAX_PADS; i++) active += app->pads[i].active;
            c4fLog("heartbeat: listener=%d controllers=%d klog=%d connecting=%d rumble-changes=%u "
                   "feedback-reads=%llu avg=%lluus max=%lluus slow-loops=%u slowest=%llums\n",
                   app->net.fd >= 0, active, app->klogFd >= 0, app->add.state, app->rumbleChanges,
                   (unsigned long long)app->feedbackCalls,
                   (unsigned long long)(app->feedbackCalls ? app->feedbackUs / app->feedbackCalls : 0),
                   (unsigned long long)app->feedbackMaxUs, app->slowLoops, (unsigned long long)app->slowestLoop);
            app->slowLoops = 0; app->slowestLoop = 0;
            app->rumbleChanges = 0;
            app->feedbackCalls = app->feedbackUs = app->feedbackMaxUs = 0;
            app->heartbeatAt = now + 60000;
        }
        if (app->changed || now >= app->broadcastAt) {
            for (int i = 0; i < app->connCount; i++) c4fStatus(app, app->conns[i].conn, NULL);
            app->changed = 0; app->broadcastAt = now+1000;
        }
        /* Wake often enough to keep a moving controller at its report rate, and
         * rarely when there is nothing to report. */
        int moving = 0, anyActive = 0;
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            C4fWebPad *p = &app->pads[i];
            if (!p->active) continue;
            anyActive = 1;
            if (c4fSince(now, p->lastInput) < C4F_ACTIVE_MS) moving = 1;
        }
        int wait = moving ? 2 : anyActive || app->add.state ? 8 : 50;
        uint64_t netStart = c4fTimeMs();
        if (app->net.fd < 0) usleep(8000);
        else {
            int result = c4fNetPoll(&app->net, wait);
            uint64_t netMs = c4fTimeMs() - netStart, total = c4fTimeMs() - loopStart;
            if (total > 150) {
                app->slowLoops++;
                if (total > app->slowestLoop) app->slowestLoop = total;
                if (netStart >= app->slowLogAt) {
                    c4fLog("slow loop: %llu ms (klog %llu, users %llu, reports %llu, feedback %llu, names %llu, "
                           "network %llu for a %d ms wait)\n", (unsigned long long)total, (unsigned long long)klogMs,
                           (unsigned long long)usersMs, (unsigned long long)reportMs, (unsigned long long)feedbackMs,
                           (unsigned long long)namesMs, (unsigned long long)netMs, wait);
                    app->slowLogAt = netStart + 1000;
                }
            }
            if (result) {
                if (result == -2) c4fLog("service resumed after a gap in select; resetting connections\n");
                else c4fLog("network failed errno=%d; resetting connections\n", errno);
                c4fNetClose(&app->net);
                c4fCancelAdd(app);
                if (!app->add.state) c4fReleaseKlog(app);
                for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].active)
                    app->pads[i].nextReport = c4fTimeMs();
                retryAt = c4fTimeMs() + (result == -2 ? 0 : 1000);
            }
        }
        /* NetPoll detects suspension inside select; exclude deliberate device
         * creation waits from the gap between iterations. */
        previous = c4fTimeMs(); previousWall = time(NULL);
    }
    for (int i = 0; i < C4F_MAX_PADS; i++) c4fRemove(app, &app->pads[i]);
    c4fNetClose(&app->net);
    c4fRtcDestroy(&app->net.rtc);
    if (app->klogFd >= 0) close(app->klogFd);
    free(app);
    c4fLog("Control4Free web controller stopped\n");
    return 0;
}
