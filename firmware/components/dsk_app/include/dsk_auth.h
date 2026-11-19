#pragma once
#include <stdbool.h>
#include <stddef.h>
/* HMAC-SHA256(token, msg) -> lowercase hex (65 bytes incl. NUL) */
bool dsk_auth_hmac_hex(const char *token, const char *msg, char out_hex[65]);
void dsk_auth_random_hex(char *out, size_t nbytes);          /* out must hold 2*nbytes+1 */
bool dsk_auth_hex_equal(const char *a, const char *b);        /* constant time */
