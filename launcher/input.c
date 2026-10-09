#include "input.h"
#include <string.h>

static void c4fInputRelease(C4fInput *input, C4fInputSource *source, uint64_t retryAt)
{
    if (source->handle >= 0 && source->owned && input->ops.close)
        input->ops.close(input->ops.context, source->handle);
    source->handle = -1;
    source->owned = 0;
    source->ready = 0;
    source->user = -1;
    source->previous = 0;
    source->retryAt = retryAt;
}

void c4fInputInit(C4fInput *input, const C4fInputOps *ops)
{
    memset(input, 0, sizeof(*input));
    if (ops) input->ops = *ops;
    for (int i = 0; i < C4F_INPUT_SOURCE_COUNT; i++) {
        input->sources[i].handle = -1;
        input->sources[i].user = -1;
    }
}

static void c4fInputRead(C4fInput *input, int index, uint64_t now, C4fInputSample *sample)
{
    C4fInputSource *source = &input->sources[index];
    if (!input->ops.open || !input->ops.read) return;
    if (source->handle < 0) {
        if (now < source->retryAt) return;
        int32_t user = C4F_INPUT_SYSTEM_USER;
        if (index == C4F_INPUT_STANDARD &&
            (!input->ops.initialUser || input->ops.initialUser(input->ops.context, &user) != 0 || user < 0)) {
            source->retryAt = now + C4F_INPUT_RETRY_MS;
            return;
        }
        int owned = 0;
        int port = index == C4F_INPUT_STANDARD ? C4F_INPUT_PORT_STANDARD : C4F_INPUT_PORT_REMOTE;
        int handle = input->ops.open(input->ops.context, user, port, &owned);
        if (handle < 0) {
            source->retryAt = now + C4F_INPUT_RETRY_MS;
            return;
        }
        source->handle = handle;
        source->owned = !!owned;
        source->user = user;
        source->ready = 0;
        source->previous = 0;
    }
    uint32_t buttons = 0;
    int connected = 0;
    if (input->ops.read(input->ops.context, source->handle, &buttons, &connected) != 0 || !connected) {
        c4fInputRelease(input, source, now + C4F_INPUT_RETRY_MS);
        return;
    }
    sample->available = 1;
    if (!source->ready) {
        if (!buttons) source->ready = 1;
        return;
    }
    sample->buttons = buttons;
    sample->pressed = buttons & ~source->previous;
    source->previous = buttons;
}

void c4fInputPoll(C4fInput *input, uint64_t now, C4fInputFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    c4fInputRead(input, C4F_INPUT_STANDARD, now, &frame->standard);
    c4fInputRead(input, C4F_INPUT_REMOTE, now, &frame->remote);
}

void c4fInputReset(C4fInput *input, uint64_t now)
{
    for (int i = 0; i < C4F_INPUT_SOURCE_COUNT; i++)
        c4fInputRelease(input, &input->sources[i], now);
}

void c4fInputClose(C4fInput *input)
{
    c4fInputReset(input, 0);
}
