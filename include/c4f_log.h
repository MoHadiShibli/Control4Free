/* Control4Free -- logging.
 *
 * Everything goes to klog (GoldHEN's klog server, port 3232) and is mirrored to a
 * file on the console, so a run can be read back over FTP even if the klog
 * connection dropped. Every line has [c4f] and elapsed HH:MM:SS.mmm, independent
 * of the console's calendar. The previous run is kept in spike.log.previous.
 *
 * c4fLogSetKlog(0) turns the klog half off. The /dev/klog scanner needs that:
 * while it is reading, its own writes would come back round and bury the kernel
 * lines it is looking for.
 */

#ifndef C4F_LOG_H
#define C4F_LOG_H

#define C4F_LOG_DIR  "/data/control4free"
#define C4F_LOG_PATH C4F_LOG_DIR "/spike.log"

void c4fLogOpen(void);
void c4fLogClose(void);
void c4fLogSetKlog(int enabled);
int  c4fLogKlogEnabled(void);

void c4fLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* On-screen toast. Keep these few: each one interrupts the user. */
void c4fNotify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* C4F_LOG_H */
