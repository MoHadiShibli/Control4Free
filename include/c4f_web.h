#ifndef C4F_WEB_H
#define C4F_WEB_H
#define C4F_WEB_PORT 4264
#define C4F_MAX_PADS 4
/* Takes ownership of klogFd (may be -1): reconnects it as needed and closes it. */
int c4fWebRun(int klogFd);
#endif
