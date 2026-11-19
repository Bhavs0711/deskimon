#include <string.h>
#include "esp_random.h"
#include "mbedtls/md.h"
#include "dsk_auth.h"
bool dsk_auth_hmac_hex(const char *token, const char *msg, char out[65])
{
    unsigned char mac[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || !token || !msg) return false;
    if (mbedtls_md_hmac(info, (const unsigned char *)token, strlen(token), (const unsigned char *)msg, strlen(msg), mac) != 0) return false;
    for (int i = 0; i < 32; i++) { static const char *h = "0123456789abcdef"; out[i * 2] = h[mac[i] >> 4]; out[i * 2 + 1] = h[mac[i] & 15]; }
    out[64] = 0; return true;
}
void dsk_auth_random_hex(char *out, size_t n)
{
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { uint8_t b = esp_random() & 0xFF; out[i * 2] = h[b >> 4]; out[i * 2 + 1] = h[b & 15]; }
    out[n * 2] = 0;
}
bool dsk_auth_hex_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b); if (la != lb) return false;
    unsigned char d = 0; for (size_t i = 0; i < la; i++) d |= (unsigned char)(a[i] ^ b[i]); return d == 0;
}
