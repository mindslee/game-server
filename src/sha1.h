#pragma once
// Compact SHA-1 (RFC 3174) + Base64 — needed for WebSocket handshake
#include <cstdint>
#include <cstring>
#include <string>

namespace sha1 {

namespace detail {
    inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    inline void processBlock(uint32_t h[5], const uint8_t blk[64]) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)
                 | ((uint32_t)blk[i*4+2]<<8 )| blk[i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if      (i < 20) { f=(b&c)|(~b&d);       k=0x5A827999u; }
            else if (i < 40) { f=b^c^d;               k=0x6ED9EBA1u; }
            else if (i < 60) { f=(b&c)|(b&d)|(c&d);  k=0x8F1BBCDCu; }
            else             { f=b^c^d;               k=0xCA62C1D6u; }
            uint32_t t = rol(a,5)+f+e+k+w[i];
            e=d; d=c; c=rol(b,30); b=a; a=t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
} // namespace detail

// Returns 20-byte raw SHA-1 hash
inline std::string hash(const std::string& input) {
    uint32_t h[5] = {0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u,0xC3D2E1F0u};
    const uint8_t* data = reinterpret_cast<const uint8_t*>(input.data());
    size_t len = input.size();

    uint8_t blk[64] = {};
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        detail::processBlock(h, data + i);

    size_t rem = len - i;
    std::memcpy(blk, data + i, rem);
    blk[rem] = 0x80;
    if (rem >= 56) { detail::processBlock(h, blk); std::memset(blk, 0, 64); }

    uint64_t bits = (uint64_t)len * 8;
    for (int j = 0; j < 8; j++) blk[56+j] = (uint8_t)(bits >> (56 - j*8));
    detail::processBlock(h, blk);

    std::string out(20, '\0');
    for (int j = 0; j < 5; j++) {
        out[j*4  ] = (char)((h[j]>>24)&0xFF);
        out[j*4+1] = (char)((h[j]>>16)&0xFF);
        out[j*4+2] = (char)((h[j]>> 8)&0xFF);
        out[j*4+3] = (char)( h[j]     &0xFF);
    }
    return out;
}

inline std::string base64Encode(const std::string& in) {
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t len = in.size();
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint8_t)in[i] << 16;
        if (i+1 < len) b |= (uint8_t)in[i+1] << 8;
        if (i+2 < len) b |= (uint8_t)in[i+2];
        out += T[(b>>18)&63];
        out += T[(b>>12)&63];
        out += (i+1 < len) ? T[(b>>6)&63] : '=';
        out += (i+2 < len) ? T[ b    &63] : '=';
    }
    return out;
}

// Compute Sec-WebSocket-Accept from Sec-WebSocket-Key
inline std::string wsAcceptKey(const std::string& clientKey) {
    return base64Encode(hash(clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

} // namespace sha1
