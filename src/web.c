/* Browser-controlled VDA lifecycle. No automatic button presses or user binds.
 * The page talks JSON over a WebSocket: info, status, claim, u (input), leave, stop. */
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
#include "c4f_log.h"
#include "c4f_net.h"
#include "c4f_vda.h"
#include "c4f_version.h"
#include "c4f_web.h"

#define C4F_INPUT_MASK 0x0011ffffu
#define C4F_REPORT_MS 16
#ifndef C4F_STALE_MS
#define C4F_STALE_MS 3000
#endif
#ifndef C4F_RELEASE_MS
#define C4F_RELEASE_MS 15000
#endif
#define C4F_QUEUE_SIZE 32

typedef struct {
    C4fVirtualPad device;
    C4fNetClient *owner;
    ScePadData current, queue[C4F_QUEUE_SIZE];
    unsigned head, count;
    uint64_t lastInput, nextReport, detachedAt, reports;
    int active, stale, error, assigned;
    uint32_t userId;
} C4fWebPad;

typedef struct {
    C4fNet net;
    C4fWebPad pads[C4F_MAX_PADS];
    int klogFd, stop, changed, creationBlocked;
    char klogLine[1024];
    size_t klogUsed;
    uint64_t broadcastAt, stopAt, heartbeatAt;
} C4fWeb;

typedef struct {
    char method[24];
    int64_t id, args[16];
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
            if (params++ || value->type != JSMN_ARRAY || value->size > 16) return -1;
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

static void c4fError(C4fNetClient *c, const C4fRequest *r, int code, const char *message)
{
    char reply[384];
    /* Messages are constant ASCII strings under our control. */
    if (r && r->hasId) snprintf(reply, sizeof(reply), "{\"id\":%lld,\"error\":{\"code\":%d,\"message\":\"%s\"}}", (long long)r->id, code, message);
    else snprintf(reply, sizeof(reply), "{\"method\":\"error\",\"params\":{\"message\":\"%s\"}}", message);
    c4fNetText(c, reply);
}

static void c4fStatus(C4fWeb *app, C4fNetClient *c, const C4fRequest *request)
{
    char json[3072];
    size_t pos;
    if (request && request->hasId) pos = (size_t)snprintf(json, sizeof(json), "{\"id\":%lld,\"result\":", (long long)request->id);
    else pos = (size_t)snprintf(json, sizeof(json), "{\"method\":\"s\",\"params\":");
    pos += (size_t)snprintf(json+pos, sizeof(json)-pos, "{\"version\":\"%s\",\"protocol\":2,\"pads\":[", C4F_VERSION);
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        const char *state = p->error ? "error" : !p->active ? "free" : !p->owner || p->stale ? "paused" : p->assigned ? "ready" : "select";
        unsigned colors[4][3] = {{32,96,255},{255,48,64},{48,200,96},{255,80,180}};
        pos += (size_t)snprintf(json+pos, sizeof(json)-pos,
            "%s{\"pad\":%d,\"name\":\"Controller %d\",\"enabled\":true,\"open\":%s,\"connected\":%s,\"clients\":%d,\"mine\":%s,\"state\":\"%s\",\"uid\":\"%s%08x\",\"color\":[%u,%u,%u],\"reports\":%llu,\"error\":%d}",
            i ? "," : "", i, i+1, p->active ? "true" : "false", p->owner && !p->stale ? "true" : "false", p->owner ? 1 : 0,
            p->owner == c ? "true" : "false", state, p->assigned ? "" : "unassigned-", p->userId,
            colors[i][0], colors[i][1], colors[i][2], (unsigned long long)p->reports, p->error);
    }
    snprintf(json+pos, sizeof(json)-pos, "]}}"); c4fNetText(c, json);
}

static void c4fNeutralize(C4fWebPad *p)
{
    p->head = p->count = 0;
    c4fPadDataNeutral(&p->current);
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

/* Also called during VDA creation waits; never re-enter the network or klog. */
static void c4fReportPads(void *context)
{
    C4fWeb *app = context;
    uint64_t now = c4fTimeMs();
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (!p->active) continue;
        if (p->owner && now-p->lastInput >= C4F_STALE_MS && !p->stale) {
            c4fNeutralize(p); p->stale = 1; app->changed = 1;
        }
        if (now < p->nextReport) continue;
        if (p->count) {
            p->current = p->queue[p->head]; p->head = (p->head+1) % C4F_QUEUE_SIZE; p->count--;
        }
        int ret = c4fVirtualPadInsert(&p->device, &p->current);
        p->reports++; p->nextReport = now + C4F_REPORT_MS;
        if (ret < 0 && !p->error) {
            p->error = ret; app->changed = 1;
            c4fNeutralize(p);
            c4fLog("web controller %d InsertData = 0x%08x\n", i+1, (uint32_t)ret);
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

static void c4fReadKlog(C4fWeb *app)
{
    char buf[2048];
    if (app->klogFd < 0) return;
    for (int batch = 0; batch < 8; batch++) {
        ssize_t n = read(app->klogFd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) {
            close(app->klogFd); app->klogFd = -1;
            c4fLog("klog source closed; reconnecting at the next controller request\n");
            return;
        }
        for (ssize_t j = 0; j < n; j++) {
            if (buf[j] == '\n') {
                unsigned long long device;
                unsigned user;
                char *event;
                app->klogLine[app->klogUsed] = 0;
                event = strstr(app->klogLine, "DEVICE_OWNER_CHANGED [DeviceId:");
                if (event && sscanf(event, "DEVICE_OWNER_CHANGED [DeviceId:0x%llx][UserId:0x%x]", &device, &user) == 2) {
                    for (int i = 0; i < C4F_MAX_PADS; i++) {
                        C4fWebPad *p = &app->pads[i];
                        if (p->active && p->device.deviceId == device) {
                            p->assigned = user != 0xffffffffu; p->userId = user; app->changed = 1;
                            c4fLog("web controller %d assignment confirmed=%d\n", i+1, p->assigned);
                        }
                    }
                }
                app->klogUsed = 0;
            } else if (buf[j] != '\r' && app->klogUsed+1 < sizeof(app->klogLine)) app->klogLine[app->klogUsed++] = buf[j];
        }
    }
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

static void c4fClaim(C4fWeb *app, C4fNetClient *c, C4fRequest *r)
{
    unsigned wanted = 0, created = 0;
    int needsDevice = 0, needsAssignment = 0;
    for (unsigned i = 0; i < r->argc; i++) {
        if (r->args[i] < 0 || r->args[i] >= C4F_MAX_PADS) { c4fError(c, r, 400, "Invalid controller"); return; }
        wanted |= 1u << r->args[i];
    }
    /* Validate all claims before changing any ownership. */
    for (int i = 0; i < C4F_MAX_PADS; i++) if (wanted & (1u << i)) {
        C4fWebPad *p = &app->pads[i];
        if (p->owner && p->owner != c) { c4fError(c, r, 409, "Controller is in use on another device"); return; }
        if (!p->active && app->creationBlocked) { c4fError(c, r, 503, "Controller creation unavailable; restart Control4Free"); return; }
        if (!p->active) needsDevice = 1;
        if (p->active && !p->assigned) needsAssignment = 1;
    }
    /* Recheck the stream before every creation. An accepted but unserved klog
     * socket must never lead to AddDevice and an orphan with no known handle. */
    if (needsDevice || (needsAssignment && app->klogFd < 0)) {
        if (app->klogFd >= 0) {
            c4fReadKlog(app);
            if (app->klogFd >= 0 && c4fKlogSelfTest(app->klogFd)) c4fReleaseKlog(app);
        }
        if (app->klogFd < 0) app->klogFd = c4fKlogOpenDevice();
        if (app->klogFd < 0 && needsDevice) { c4fError(c, r, 503, "Cannot read the PS4's kernel log, which sign-in needs. If a klog viewer is connected to GoldHEN, close it and try again."); return; }
    }
    for (int i = 0; i < C4F_MAX_PADS; i++) if ((wanted & (1u << i)) && !app->pads[i].active) {
        C4fWebPad *p = &app->pads[i];
        c4fKlogDrain(app->klogFd); app->klogUsed = 0;
        if (c4fVirtualPadAddAs(&p->device, C4F_USER_ID_INVALID, 1, app->klogFd)) {
            /* AddDevice may have created an orphan whose handle was lost. Do
             * not accumulate more devices through automatic retries. */
            app->creationBlocked = 1;
            for (int k = 0; k < C4F_MAX_PADS; k++) if (created & (1u << k)) c4fRemove(app, &app->pads[k]);
            c4fError(c, r, 503, "Could not create controller; restart payload before retrying"); return;
        }
        p->active = 1; p->userId = 0xffffffffu; p->lastInput = c4fTimeMs();
        c4fNeutralize(p); created |= 1u << i;
        c4fReportPads(app);
    }
    for (int i = 0; i < C4F_MAX_PADS; i++) {
        C4fWebPad *p = &app->pads[i];
        if (wanted & (1u << i)) {
            if (p->owner != c) c4fNeutralize(p);
            p->owner = c; p->detachedAt = 0; p->lastInput = c4fTimeMs(); p->stale = 0;
        } else if (p->owner == c) c4fRemove(app, p);
    }
    app->changed = 1; c4fStatus(app, c, r);
}

static void c4fUpdate(C4fWeb *app, C4fNetClient *c, C4fRequest *r)
{
    if (r->argc < 9 || r->args[0] < 0 || r->args[0] >= C4F_MAX_PADS) { c4fError(c, r, 400, "Invalid input"); return; }
    C4fWebPad *p = &app->pads[r->args[0]];
    if (p->owner != c || !p->active) return; /* input never implicitly claims a pad */
    if (r->args[1] < 0 || r->args[1] > C4F_INPUT_MASK || (r->args[1] & ~C4F_INPUT_MASK)) { c4fError(c, r, 400, "Invalid buttons"); return; }
    for (unsigned i = 2; i < 8; i++) if (r->args[i] < 0 || r->args[i] > 255) { c4fError(c, r, 400, "Invalid axis"); return; }
    if (r->args[8] < 0 || r->args[8] > 2 || r->argc != 9+3*r->args[8]) { c4fError(c, r, 400, "Invalid touch data"); return; }
    ScePadData data;
    c4fPadDataNeutral(&data); data.buttons = (uint32_t)r->args[1];
    data.lx = r->args[2]; data.ly = r->args[3]; data.rx = r->args[4]; data.ry = r->args[5]; data.l2 = r->args[6]; data.r2 = r->args[7];
    data.touchData.fingers = r->args[8];
    for (unsigned i = 0; i < data.touchData.fingers; i++) {
        int64_t *t = &r->args[9+3*i];
        if (t[0] < 0 || t[0] > 127 || t[1] < 0 || t[1] > 1919 || t[2] < 0 || t[2] > 941) { c4fError(c, r, 400, "Invalid touch position"); return; }
        data.touchData.touch[i].finger = t[0]; data.touchData.touch[i].x = t[1]; data.touchData.touch[i].y = t[2];
    }
    if (p->stale) { p->stale = 0; app->changed = 1; }
    p->lastInput = c4fTimeMs(); c4fEnqueue(p, &data);
}

static void c4fWebEvent(C4fNetClient *c, int event, const char *text, size_t len, void *context)
{
    C4fWeb *app = context;
    if (event == C4F_NET_HTTP_STATUS) {
        int active = 0;
        char reply[192];
        for (int i = 0; i < C4F_MAX_PADS; i++) active += app->pads[i].active != 0;
        snprintf(reply, sizeof(reply), "{\"application\":\"Control4Free\",\"api\":1,\"version\":\"%s\",\"controllers\":%d,\"stopping\":%s}", C4F_VERSION, active, app->stopAt ? "true" : "false");
        c4fNetHttpJson(c, reply); return;
    }
    if (event == C4F_NET_HTTP_STOP) {
        for (int i = 0; i < C4F_MAX_PADS; i++) c4fRemove(app, &app->pads[i]);
        c4fNetHttpJson(c, "{\"application\":\"Control4Free\",\"stopping\":true}");
        if (!app->stopAt) app->stopAt = c4fTimeMs() + 250;
        return;
    }
    if (event == C4F_NET_OPEN) { c4fStatus(app, c, NULL); return; }
    if (event == C4F_NET_CLOSE) {
        for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].owner == c) {
            C4fWebPad *p = &app->pads[i];
            c4fNeutralize(p); p->owner = NULL; p->detachedAt = c4fTimeMs(); app->changed = 1;
        }
        return;
    }
    C4fRequest r;
    if (c4fParseRequest(text, len, &r)) { c4fError(c, NULL, 400, "Invalid request"); return; }
    if (app->stopAt) { c4fError(c, &r, 503, "Control4Free is stopping"); return; }
    if (!strcmp(r.method, "info")) {
        char reply[160];
        if (r.hasId) { snprintf(reply, sizeof(reply), "{\"id\":%lld,\"result\":{\"version\":\"%s\",\"protocol\":2,\"pads\":%d}}", (long long)r.id, C4F_VERSION, C4F_MAX_PADS); c4fNetText(c, reply); }
    } else if (!strcmp(r.method, "status")) c4fStatus(app, c, &r);
    else if (!strcmp(r.method, "claim")) c4fClaim(app, c, &r);
    else if (!strcmp(r.method, "u")) c4fUpdate(app, c, &r);
    else if (!strcmp(r.method, "leave")) {
        if (r.argc != 1 || r.args[0] < 0 || r.args[0] >= C4F_MAX_PADS || app->pads[r.args[0]].owner != c) { c4fError(c, &r, 409, "You do not control this controller"); return; }
        c4fRemove(app, &app->pads[r.args[0]]); c4fStatus(app, c, &r);
    } else if (!strcmp(r.method, "stop")) {
        int occupied = 0;
        for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].owner && app->pads[i].owner != c) occupied = 1;
        if (occupied) { c4fError(c, &r, 409, "Another device is using a controller"); return; }
        app->stop = 1;
    } else c4fError(c, &r, 404, "Unknown method");
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
        free(app); return -1;
    }
    c4fVdaSetWaitCallback(c4fReportPads, app);
    c4fLog("Control4Free %s: browser controller on port %d\n", C4F_VERSION, C4F_WEB_PORT);
    c4fNotify("Control4Free: open PS4 IP:%d in your browser", C4F_WEB_PORT);
    uint64_t previous = c4fTimeMs(), retryAt = 0;
    time_t previousWall = time(NULL);
    while (!app->stop) {
        uint64_t now = c4fTimeMs();
        time_t wall = time(NULL);
        if (app->stopAt && now >= app->stopAt) break;
        /* Some clocks exclude suspension. Wall time is only a second signal
         * for a gap, never an input deadline or a trusted calendar timestamp. */
        if (now < previous || now - previous > 5000 || wall - previousWall > 5) {
            c4fLog("service resumed after a gap (monotonic=%lldms wall=%llds); resetting connections\n",
                   (long long)now - (long long)previous, (long long)wall - (long long)previousWall);
            c4fNetClose(&app->net); /* close events neutralize and discard queued input */
            c4fReleaseKlog(app);
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
        c4fReadKlog(app); c4fReportPads(app);
        for (int i = 0; i < C4F_MAX_PADS; i++) {
            C4fWebPad *p = &app->pads[i];
            if (p->active && ((!p->owner && now-p->detachedAt > C4F_RELEASE_MS) || (p->owner && now-p->lastInput > C4F_RELEASE_MS))) {
                C4fNetClient *owner = p->owner;
                c4fRemove(app, p);
                if (owner) c4fError(owner, NULL, 408, "Controller disconnected after inactivity; select it again");
            }
        }
        c4fReleaseIdleKlog(app);
        if (now >= app->heartbeatAt) {
            int active = 0;
            for (int i = 0; i < C4F_MAX_PADS; i++) active += app->pads[i].active;
            c4fLog("heartbeat: listener=%d controllers=%d klog=%d\n", app->net.fd >= 0, active, app->klogFd >= 0);
            app->heartbeatAt = now + 60000;
        }
        if (app->changed || now >= app->broadcastAt) {
            for (int i = 0; i < C4F_NET_CLIENTS; i++) {
                C4fNetClient *c = &app->net.clients[i];
                if (c->fd >= 0 && c->websocket && !c->closing) c4fStatus(app, c, NULL);
            }
            app->changed = 0; app->broadcastAt = now+1000;
        }
        if (app->net.fd < 0) usleep(8000);
        else {
            int result = c4fNetPoll(&app->net, 8);
            if (result) {
                if (result == -2) c4fLog("service resumed after a gap in select; resetting connections\n");
                else c4fLog("network failed errno=%d; resetting connections\n", errno);
                c4fNetClose(&app->net);
                c4fReleaseKlog(app);
                for (int i = 0; i < C4F_MAX_PADS; i++) if (app->pads[i].active)
                    app->pads[i].nextReport = c4fTimeMs();
                retryAt = c4fTimeMs() + (result == -2 ? 0 : 1000);
            }
        }
        /* NetPoll detects suspension inside select; exclude deliberate device
         * creation waits from the gap between iterations. */
        previous = c4fTimeMs(); previousWall = time(NULL);
    }
    c4fVdaSetWaitCallback(NULL, NULL);
    for (int i = 0; i < C4F_MAX_PADS; i++) c4fRemove(app, &app->pads[i]);
    c4fNetClose(&app->net);
    if (app->klogFd >= 0) close(app->klogFd);
    free(app);
    c4fLog("Control4Free web controller stopped\n");
    return 0;
}
