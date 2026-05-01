#include "aes128.h"
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>

static const unsigned char sbox[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static const unsigned char inv_sbox[256] = {
0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};

static const unsigned char rcon[11] = {0,1,2,4,8,16,32,64,128,27,54};

static unsigned char xtime(unsigned char x) {
    return (unsigned char)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static unsigned char gf_mul(unsigned char a, unsigned char b) {
    unsigned char r = 0;
    while (a && b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

static void aes128_key_expansion(const unsigned char *key, unsigned char *rk) {
    memcpy(rk, key, 16);
    for (int i = 16; i < 176; i += 4) {
        unsigned char t[4];
        memcpy(t, rk + i - 4, 4);
        if (i % 16 == 0) {
            unsigned char u = t[0];
            t[0] = sbox[t[1]] ^ rcon[i / 16];
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[u];
        }
        rk[i+0] = rk[i-16] ^ t[0];
        rk[i+1] = rk[i-15] ^ t[1];
        rk[i+2] = rk[i-14] ^ t[2];
        rk[i+3] = rk[i-13] ^ t[3];
    }
}

static void aes128_decrypt_block(unsigned char *block, const unsigned char *rk) {
    int i, j;
    for (i = 0; i < 16; i++) block[i] ^= rk[160 + i];
    for (int round = 9; round >= 1; round--) {
        unsigned char tmp[16];
        /* InvShiftRows: shift rows RIGHT (undo encryption left shift) */
        /* Row 0 unchanged: state[0,c] = block[0+4c] */
        tmp[0] =block[0];  tmp[4] =block[4];  tmp[8] =block[8];  tmp[12]=block[12];
        /* Row 1 right 1: [13,1,5,9] */
        tmp[1] =block[13]; tmp[5] =block[1];  tmp[9] =block[5];  tmp[13]=block[9];
        /* Row 2 right 2: [10,14,2,6] */
        tmp[2] =block[10]; tmp[6] =block[14]; tmp[10]=block[2];  tmp[14]=block[6];
        /* Row 3 right 3: [7,11,15,3] */
        tmp[3] =block[7];  tmp[7] =block[11]; tmp[11]=block[15]; tmp[15]=block[3];
        for (i = 0; i < 16; i++) tmp[i] = inv_sbox[tmp[i]] ^ rk[round * 16 + i];
        for (j = 0; j < 4; j++) {
            int c = j * 4;
            unsigned char a0=tmp[c],a1=tmp[c+1],a2=tmp[c+2],a3=tmp[c+3];
            tmp[c]  =gf_mul(0x0e,a0)^gf_mul(0x0b,a1)^gf_mul(0x0d,a2)^gf_mul(0x09,a3);
            tmp[c+1]=gf_mul(0x09,a0)^gf_mul(0x0e,a1)^gf_mul(0x0b,a2)^gf_mul(0x0d,a3);
            tmp[c+2]=gf_mul(0x0d,a0)^gf_mul(0x09,a1)^gf_mul(0x0e,a2)^gf_mul(0x0b,a3);
            tmp[c+3]=gf_mul(0x0b,a0)^gf_mul(0x0d,a1)^gf_mul(0x09,a2)^gf_mul(0x0e,a3);
        }
        memcpy(block, tmp, 16);
    }
    {   /* final round: no InvMixColumns */
        unsigned char tmp[16];
        tmp[0] =block[0];  tmp[4] =block[4];  tmp[8] =block[8];  tmp[12]=block[12];
        tmp[1] =block[13]; tmp[5] =block[1];  tmp[9] =block[5];  tmp[13]=block[9];
        tmp[2] =block[10]; tmp[6] =block[14]; tmp[10]=block[2];  tmp[14]=block[6];
        tmp[3] =block[7];  tmp[7] =block[11]; tmp[11]=block[15]; tmp[15]=block[3];
        for (i = 0; i < 16; i++) block[i] = inv_sbox[tmp[i]] ^ rk[i];
    }
}

static void aes128_encrypt_block(unsigned char *block, const unsigned char *rk) {
    int i;
    for (i = 0; i < 16; i++) block[i] ^= rk[i];
    for (int round = 1; round <= 9; round++) {
        unsigned char tmp[16];
        for (i = 0; i < 16; i++) tmp[i] = sbox[block[i]];
        block[0]=tmp[0];  block[1]=tmp[5];  block[2]=tmp[10]; block[3]=tmp[15];
        block[4]=tmp[4];  block[5]=tmp[9];  block[6]=tmp[14]; block[7]=tmp[3];
        block[8]=tmp[8];  block[9]=tmp[13]; block[10]=tmp[2]; block[11]=tmp[7];
        block[12]=tmp[12]; block[13]=tmp[1]; block[14]=tmp[6]; block[15]=tmp[11];
        for (int c = 0; c < 4; c++) {
            int off = c * 4;
            unsigned char a0=block[off],a1=block[off+1],a2=block[off+2],a3=block[off+3];
            block[off]   = gf_mul(2,a0)^gf_mul(3,a1) ^ a2 ^ a3;
            block[off+1] = a0 ^ gf_mul(2,a1)^gf_mul(3,a2) ^ a3;
            block[off+2] = a0 ^ a1 ^ gf_mul(2,a2)^gf_mul(3,a3);
            block[off+3] = gf_mul(3,a0) ^ a1 ^ a2 ^ gf_mul(2,a3);
        }
        for (i = 0; i < 16; i++) block[i] ^= rk[round * 16 + i];
    }
    {
        unsigned char tmp[16];
        for (i = 0; i < 16; i++) tmp[i] = sbox[block[i]];
        block[0]=tmp[0];  block[1]=tmp[5];  block[2]=tmp[10]; block[3]=tmp[15];
        block[4]=tmp[4];  block[5]=tmp[9];  block[6]=tmp[14]; block[7]=tmp[3];
        block[8]=tmp[8];  block[9]=tmp[13]; block[10]=tmp[2]; block[11]=tmp[7];
        block[12]=tmp[12]; block[13]=tmp[1]; block[14]=tmp[6]; block[15]=tmp[11];
        for (i = 0; i < 16; i++) block[i] ^= rk[160 + i];
    }
}

static int l_aes128_cbc_encrypt(lua_State *L) {
    size_t dlen, klen, ilen;
    const unsigned char *data = (const unsigned char *)luaL_checklstring(L, 1, &dlen);
    const unsigned char *key  = (const unsigned char *)luaL_checklstring(L, 2, &klen);
    const unsigned char *iv   = (const unsigned char *)luaL_checklstring(L, 3, &ilen);
    if (klen != 16) luaL_error(L, "aes128_encrypt: key must be 16 bytes");
    if (ilen != 16) luaL_error(L, "aes128_encrypt: iv must be 16 bytes");

    /* PKCS7 padding */
    size_t pad = 16 - (dlen % 16);
    size_t in_len = dlen + pad;
    unsigned char *buf = (unsigned char *)malloc(in_len);
    if (!buf) luaL_error(L, "aes128_encrypt: OOM");
    memcpy(buf, data, dlen);
    memset(buf + dlen, (unsigned char)pad, pad);

    unsigned char rk[176];
    aes128_key_expansion(key, rk);

    unsigned char prev[16];
    memcpy(prev, iv, 16);
    for (size_t b = 0; b < in_len / 16; b++) {
        for (int i = 0; i < 16; i++) buf[b*16+i] ^= prev[i];
        aes128_encrypt_block(buf + b * 16, rk);
        memcpy(prev, buf + b * 16, 16);
    }

    lua_pushlstring(L, (const char *)buf, in_len);
    free(buf);
    return 1;
}

static int l_aes128_cbc_decrypt(lua_State *L) {
    size_t dlen, klen, ilen;
    const unsigned char *data = (const unsigned char *)luaL_checklstring(L, 1, &dlen);
    const unsigned char *key  = (const unsigned char *)luaL_checklstring(L, 2, &klen);
    const unsigned char *iv   = (const unsigned char *)luaL_checklstring(L, 3, &ilen);
    if (klen != 16) luaL_error(L, "aes128_decrypt: key must be 16 bytes");
    if (ilen != 16) luaL_error(L, "aes128_decrypt: iv must be 16 bytes");
    if (dlen == 0 || dlen % 16 != 0) luaL_error(L, "aes128_decrypt: data length must be multiple of 16");

    unsigned char rk[176];
    aes128_key_expansion(key, rk);
    unsigned char *buf = (unsigned char *)malloc(dlen);
    if (!buf) luaL_error(L, "aes128_decrypt: OOM");
    memcpy(buf, data, dlen);

    unsigned char prev[16];
    memcpy(prev, iv, 16);
    for (size_t b = 0; b < dlen / 16; b++) {
        aes128_decrypt_block(buf + b * 16, rk);
        for (int i = 0; i < 16; i++) buf[b*16+i] ^= prev[i];
        memcpy(prev, data + b * 16, 16);
    }

    unsigned char pad = buf[dlen - 1];
    if (pad == 0 || pad > 16) { free(buf); luaL_error(L, "aes128_decrypt: invalid PKCS7 padding"); }
    size_t out_len = dlen - pad;
    lua_pushlstring(L, (const char *)buf, out_len);
    free(buf);
    return 1;
}

/* http.aes128_encrypt_block(data, key) → encrypted_block (16 bytes)
 * data: 16-byte input block
 * key:  16-byte AES-128 key
 * No padding — caller must ensure data is exactly 16 bytes.
 * Useful for implementing custom CBC modes (e.g. whitespace padding).
 */
static int l_aes128_encrypt_block(lua_State *L) {
    size_t dlen, klen;
    const unsigned char *data = (const unsigned char *)luaL_checklstring(L, 1, &dlen);
    const unsigned char *key  = (const unsigned char *)luaL_checklstring(L, 2, &klen);
    if (klen != 16) return luaL_error(L, "aes128_encrypt_block: key must be 16 bytes");
    if (dlen != 16) return luaL_error(L, "aes128_encrypt_block: data must be exactly 16 bytes");

    unsigned char rk[176];
    aes128_key_expansion(key, rk);
    unsigned char buf[16];
    memcpy(buf, data, 16);
    aes128_encrypt_block(buf, rk);
    lua_pushlstring(L, (const char *)buf, 16);
    return 1;
}

static const luaL_Reg aes_funcs[] = {
    { "aes128_cbc_encrypt", l_aes128_cbc_encrypt },
    { "aes128_cbc_decrypt", l_aes128_cbc_decrypt },
    { "aes128_encrypt_block", l_aes128_encrypt_block },
    { NULL, NULL }
};

void aes128_module_register(lua_State *L) {
    lua_getglobal(L, "http");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); luaL_newlib(L, aes_funcs); lua_setglobal(L, "http"); return; }
    luaL_setfuncs(L, aes_funcs, 0);
    lua_pop(L, 1);
}
