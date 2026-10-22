#ifndef C4F_STUN_H
#define C4F_STUN_H
/* STUN for an ICE-lite WebRTC endpoint (RFC 8489, RFC 8445): the PS4 only
 * answers the browser's connectivity checks. */
#include <stddef.h>
#include <stdint.h>

#define C4F_STUN_MAGIC 0x2112A442u
#define C4F_STUN_ANSWER_SIZE 64

/* RFC 7983: on the shared port, a first byte of 0-3 is STUN and 20-63 DTLS. */
static inline int c4fIsStun(const uint8_t *data, size_t len) { return len >= 20 && data[0] <= 3; }
static inline int c4fIsDtls(const uint8_t *data, size_t len) { return len >= 13 && data[0] >= 20 && data[0] <= 63; }

/* CRC-32 (IEEE 802.3), as STUN's FINGERPRINT uses it. */
uint32_t c4fCrc32(const uint8_t *data, size_t len);

/* The USERNAME of a well-formed Binding request ("ours:theirs"), or NULL. */
const uint8_t *c4fStunUsername(const uint8_t *msg, size_t len, size_t *size);

/* Whether the message's MESSAGE-INTEGRITY matches the key (ICE uses the
 * password as is), and its FINGERPRINT, when it has one, is right. */
int c4fStunVerify(const uint8_t *msg, size_t len, const char *key);

/* Answers a Binding request made with our password: writes the success
 * response, carrying the address and port it came from (both in network byte
 * order), into out (C4F_STUN_ANSWER_SIZE bytes). Returns its length, or 0 when
 * the request is not one to answer. *nominate is set when the browser picked
 * this path (USE-CANDIDATE). */
size_t c4fStunAnswer(const uint8_t *msg, size_t len, const char *password,
                     uint32_t address, uint16_t port, uint8_t out[C4F_STUN_ANSWER_SIZE], int *nominate);
#endif
