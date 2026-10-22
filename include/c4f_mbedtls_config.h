/* Mbed TLS 3.6 configuration for Control4Free (MBEDTLS_CONFIG_FILE).
 *
 * Just what a WebRTC data channel needs on the PS4 side: a DTLS 1.2 server
 * with ECDHE-ECDSA and AES-128-GCM, the self-signed P-256 certificate it shows
 * the browser, parsing the browser's certificate to check its fingerprint,
 * and HMAC-SHA1 for STUN. No files, sockets, threads or PSA: the service
 * feeds datagrams in and sends them out itself. */
#ifndef C4F_MBEDTLS_CONFIG_H
#define C4F_MBEDTLS_CONFIG_H

/* System */
#define MBEDTLS_HAVE_ASM

/* Features */
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECDSA_DETERMINISTIC
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_DTLS
#define MBEDTLS_SSL_DTLS_ANTI_REPLAY

/* Modules */
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_GCM_C
#define MBEDTLS_HMAC_DRBG_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_CRT_WRITE_C

/* Sizes: SCTP over DTLS stays under 1200 bytes a datagram, and the largest
 * handshake message is a certificate of a few hundred bytes. */
#define MBEDTLS_SSL_IN_CONTENT_LEN  4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_SSL_CIPHERSUITES MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
#define MBEDTLS_ENTROPY_MAX_SOURCES 2

#endif
