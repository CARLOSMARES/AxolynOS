/*
 * AxolynOS - Criptografia propia (SHA-256, HMAC, PBKDF2, ChaCha20).
 *
 * Implementacion directa y sin dependencias.  Esta pensada para el gestor
 * cifrado de usuarios del kernel, pero cualquier modulo puede usarla.
 */
#include "ax_crypto.h"

namespace {

inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

inline uint32_t rotl32(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

inline uint32_t load_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline void store_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

inline uint32_t load_le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline void store_le32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

inline void copy_bytes(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

inline void zero_bytes(void* dst, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* SHA-256                                                            */
/* ------------------------------------------------------------------ */
struct Sha256 {
    uint32_t state[8];
    uint64_t total;
    uint8_t  block[64];
    uint32_t used;
};

const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

void sha_init(Sha256* s)
{
    s->state[0] = 0x6a09e667u;
    s->state[1] = 0xbb67ae85u;
    s->state[2] = 0x3c6ef372u;
    s->state[3] = 0xa54ff53au;
    s->state[4] = 0x510e527fu;
    s->state[5] = 0x9b05688cu;
    s->state[6] = 0x1f83d9abu;
    s->state[7] = 0x5be0cd19u;
    s->total = 0;
    s->used = 0;
    zero_bytes(s->block, sizeof(s->block));
}

void sha_transform(Sha256* s, const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load_be32(block + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = s->state[0];
    uint32_t b = s->state[1];
    uint32_t c = s->state[2];
    uint32_t d = s->state[3];
    uint32_t e = s->state[4];
    uint32_t f = s->state[5];
    uint32_t g = s->state[6];
    uint32_t h = s->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    s->state[0] += a;
    s->state[1] += b;
    s->state[2] += c;
    s->state[3] += d;
    s->state[4] += e;
    s->state[5] += f;
    s->state[6] += g;
    s->state[7] += h;
}

void sha_update(Sha256* s, const void* data, uint64_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    s->total += len;
    while (len > 0) {
        uint32_t room = 64 - s->used;
        uint32_t take = (len < room) ? (uint32_t)len : room;
        copy_bytes(s->block + s->used, p, take);
        s->used += take;
        p += take;
        len -= take;
        if (s->used == 64) {
            sha_transform(s, s->block);
            s->used = 0;
        }
    }
}

void sha_final(Sha256* s, uint8_t out[32])
{
    uint64_t bits = s->total * 8ull;
    uint8_t pad[72];
    zero_bytes(pad, sizeof(pad));
    pad[0] = 0x80;
    uint32_t pad_len = (s->used < 56) ? (56 - s->used) : (120 - s->used);
    sha_update(s, pad, pad_len);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) {
        lenbuf[7 - i] = (uint8_t)(bits >> (i * 8));
    }
    sha_update(s, lenbuf, sizeof(lenbuf));
    for (int i = 0; i < 8; ++i) {
        store_be32(out + i * 4, s->state[i]);
    }
}

/* ------------------------------------------------------------------ */
/* ChaCha20                                                           */
/* ------------------------------------------------------------------ */
inline uint32_t chacha_rotl(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

void chacha_quarter(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
{
    a += b; d ^= a; d = chacha_rotl(d, 16);
    c += d; b ^= c; b = chacha_rotl(b, 12);
    a += b; d ^= a; d = chacha_rotl(d, 8);
    c += d; b ^= c; b = chacha_rotl(b, 7);
}

void chacha_block(const uint8_t key[32], const uint8_t nonce[12],
                  uint32_t counter, uint8_t out[64])
{
    uint32_t state[16];
    const uint8_t sigma[16] = {
        'e','x','p','a','n','d',' ','3','2','-','b','y','t','e',' ','k'
    };
    for (int i = 0; i < 4; ++i) {
        state[i] = load_le32(sigma + i * 4);
        state[4 + i] = load_le32(key + i * 4);
        state[8 + i] = load_le32(key + 16 + i * 4);
        state[12 + i] = (i == 0) ? counter : load_le32(nonce + (i - 1) * 4);
    }

    uint32_t x[16];
    for (int i = 0; i < 16; ++i) {
        x[i] = state[i];
    }
    for (int round = 0; round < 10; ++round) {
        chacha_quarter(x[0], x[4], x[8],  x[12]);
        chacha_quarter(x[1], x[5], x[9],  x[13]);
        chacha_quarter(x[2], x[6], x[10], x[14]);
        chacha_quarter(x[3], x[7], x[11], x[15]);
        chacha_quarter(x[0], x[5], x[10], x[15]);
        chacha_quarter(x[1], x[6], x[11], x[12]);
        chacha_quarter(x[2], x[7], x[8],  x[13]);
        chacha_quarter(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; ++i) {
        store_le32(out + i * 4, x[i] + state[i]);
    }
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* API publica                                                        */
/* ------------------------------------------------------------------ */
extern "C" void ax_sha256(const void* data, uint64_t len,
                          uint8_t out[AX_SHA256_SIZE])
{
    Sha256 s;
    sha_init(&s);
    if (len && data) {
        sha_update(&s, data, len);
    }
    sha_final(&s, out);
    zero_bytes(&s, sizeof(s));
}

extern "C" void ax_hmac_sha256(const void* key, uint64_t key_len,
                               const void* data, uint64_t data_len,
                               uint8_t out[AX_SHA256_SIZE])
{
    uint8_t k[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    zero_bytes(k, sizeof(k));
    if (key_len > 64) {
        ax_sha256(key, key_len, k);
    } else if (key_len && key) {
        copy_bytes(k, key, key_len);
    }
    for (int i = 0; i < 64; ++i) {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    Sha256 inner;
    sha_init(&inner);
    sha_update(&inner, ipad, sizeof(ipad));
    if (data_len && data) {
        sha_update(&inner, data, data_len);
    }
    uint8_t digest[32];
    sha_final(&inner, digest);

    Sha256 outer;
    sha_init(&outer);
    sha_update(&outer, opad, sizeof(opad));
    sha_update(&outer, digest, sizeof(digest));
    sha_final(&outer, out);

    zero_bytes(k, sizeof(k));
    zero_bytes(ipad, sizeof(ipad));
    zero_bytes(opad, sizeof(opad));
    zero_bytes(digest, sizeof(digest));
}

extern "C" void ax_pbkdf2_sha256(const char* password, uint64_t password_len,
                                 const uint8_t* salt, uint64_t salt_len,
                                 uint32_t iterations,
                                 uint8_t* out, uint64_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    if (out_len > 32) {
        out_len = 32;
    }
    if (iterations == 0) {
        iterations = 1;
    }

    /* U1 = HMAC(password, salt || INT_BE(1)) */
    uint8_t block[128];
    uint64_t slen = salt_len;
    if (slen > sizeof(block) - 4) {
        slen = sizeof(block) - 4;
    }
    if (slen && salt) {
        copy_bytes(block, salt, slen);
    }
    block[slen + 0] = 0;
    block[slen + 1] = 0;
    block[slen + 2] = 0;
    block[slen + 3] = 1;

    uint8_t u[32];
    uint8_t t[32];
    ax_hmac_sha256(password, password_len, block, slen + 4, u);
    copy_bytes(t, u, sizeof(t));

    for (uint32_t i = 1; i < iterations; ++i) {
        ax_hmac_sha256(password, password_len, u, sizeof(u), u);
        for (int k = 0; k < 32; ++k) {
            t[k] ^= u[k];
        }
    }
    copy_bytes(out, t, out_len);
    zero_bytes(block, sizeof(block));
    zero_bytes(u, sizeof(u));
    zero_bytes(t, sizeof(t));
}

extern "C" void ax_chacha20_xor(const uint8_t key[AX_CHACHA20_KEY],
                                const uint8_t nonce[AX_CHACHA20_NONCE],
                                uint32_t counter,
                                const void* in, void* out, uint64_t len)
{
    const uint8_t* src = (const uint8_t*)in;
    uint8_t* dst = (uint8_t*)out;
    uint8_t stream[64];
    uint64_t offset = 0;

    while (offset < len) {
        chacha_block(key, nonce, counter++, stream);
        uint64_t chunk = len - offset;
        if (chunk > 64) {
            chunk = 64;
        }
        for (uint64_t i = 0; i < chunk; ++i) {
            uint8_t b = src ? src[offset + i] : 0;
            dst[offset + i] = (uint8_t)(b ^ stream[i]);
        }
        offset += chunk;
    }
    zero_bytes(stream, sizeof(stream));
}

extern "C" bool ax_crypto_equal(const void* a, const void* b, uint64_t len)
{
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    uint8_t diff = 0;
    for (uint64_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return diff == 0;
}

/* ------------------------------------------------------------------ */
/* Generador pseudoaleatorio                                          */
/* ------------------------------------------------------------------ */
namespace {
uint8_t g_random_key[32];
uint8_t g_random_nonce[12];
uint32_t g_random_counter = 0;
bool g_random_ready = false;

inline uint64_t read_tsc()
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
} /* namespace */

extern "C" void ax_crypto_init(void)
{
    uint64_t seed = read_tsc() ^ 0xA5A5C0DE12345678ull;
    uint64_t mix = seed;
    for (int i = 0; i < 32; ++i) {
        mix ^= mix << 13;
        mix ^= mix >> 7;
        mix ^= mix << 17;
        g_random_key[i] = (uint8_t)(mix >> ((i % 8) * 8));
    }
    for (int i = 0; i < 12; ++i) {
        mix ^= mix << 11;
        mix ^= mix >> 5;
        g_random_nonce[i] = (uint8_t)(mix >> ((i % 8) * 8));
    }
    g_random_counter = 0;
    g_random_ready = true;
}

extern "C" void ax_random_bytes(void* out, uint64_t len)
{
    if (!out || len == 0) {
        return;
    }
    if (!g_random_ready) {
        ax_crypto_init();
    }
    uint8_t* dst = (uint8_t*)out;
    uint8_t stream[64];
    uint64_t offset = 0;
    while (offset < len) {
        chacha_block(g_random_key, g_random_nonce, g_random_counter++, stream);
        uint64_t chunk = len - offset;
        if (chunk > 64) {
            chunk = 64;
        }
        for (uint64_t i = 0; i < chunk; ++i) {
            dst[offset + i] = stream[i];
        }
        offset += chunk;
    }
    zero_bytes(stream, sizeof(stream));
}

extern "C" void ax_crypto_hex(const uint8_t* data, uint64_t len,
                              char* out, uint64_t max)
{
    static const char hex[] = "0123456789abcdef";
    if (!out || max == 0) {
        return;
    }
    uint64_t pos = 0;
    for (uint64_t i = 0; i < len && pos + 2 < max; ++i) {
        out[pos++] = hex[(data[i] >> 4) & 0x0f];
        out[pos++] = hex[data[i] & 0x0f];
    }
    out[pos] = '\0';
}
