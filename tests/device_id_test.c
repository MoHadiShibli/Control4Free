/* Which kernel-log lines count as "our virtual pad was added". */
#include <assert.h>
#include <stdio.h>
#include "c4f_vda.h"

/* 0 when the line is not ours, otherwise the DeviceId it names. */
static uint64_t ours(const char *line)
{
    return c4fKlogIsVirtualAdd(line) ? c4fKlogDeviceId(line) : 0;
}

int main(void)
{
    /* What a virtual pad produces on firmware 10.01: type 1 when it is created
     * for user 1, type 4 when it is created for a local-user id. */
    assert(ours("<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED"
                " [DeviceId:0x7030d][type:1][subType:2]") == 0x7030d);
    assert(ours("<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED"
                " [DeviceId:0x50301][type:4][subType:2]") == 0x50301);

    /* Another MBus device turning up in the same window is not ours. A real pad
     * being plugged in is subType 0, and the generic event line has no subType at
     * all; taking either would send every input to a device we do not own. */
    assert(ours("DEVICE_ADDED [DeviceId:0x123401] [type:1] [subType:0]") == 0);
    assert(ours("ScePsP: sceMbusEvent has been received. (eventId=1 ADD, deviceId=0x123401)") == 0);
    assert(ours("<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_REMOVED"
                " [DeviceId:0x7030d][type:1][subType:2]") == 0);
    assert(ours("<118>#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_OWNER_CHANGED"
                " [DeviceId:0x7030d][UserId:0x1a2b3c4d]") == 0);

    /* The id is read in hex, whichever spelling the line uses. */
    assert(c4fKlogDeviceId("[DeviceID=0xABCD][subType:2]") == 0xabcd);
    assert(c4fKlogDeviceId("nothing here") == 0);

    puts("PASS only the virtual pad's device-added line is accepted");
}
