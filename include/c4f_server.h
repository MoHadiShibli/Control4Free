/* Control4Free -- the resident command server (see src/server.c). */

#ifndef C4F_SERVER_H
#define C4F_SERVER_H

#include <stdint.h>

/* Its own port, so experiments no longer go through GoldHEN's PayLoader. */
#define C4F_SERVER_PORT 4264

/* Listens, runs commands, and returns when told to quit or when idle too long.
 * `klogFd` is the kernel-log reader new pads are found through. Any pad still
 * alive is removed before it returns. */
int c4fServerRun(int32_t realUserId, int klogFd);

#endif /* C4F_SERVER_H */
