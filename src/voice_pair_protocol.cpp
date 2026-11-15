#include "voice_pair_protocol.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

namespace voice_pair {
void wipe(void *p, size_t n) { volatile uint8_t *q = static_cast<volatile uint8_t *>(p); while (n--) *q++ = 0; }
static bool nonzero(const uint8_t *p, size_t n) { uint8_t v = 0; while (n--) v |= *p++; return v != 0; }
bool decode_id(const char *s, size_t n, uint8_t out[16]) {
  if (!s || !out || n != 32) return false;
  uint8_t tmp[16];
  for (size_t i = 0; i < 32; ++i) {
    const char c = s[i];
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) { wipe(tmp, sizeof(tmp)); return false; }
    if (!(i & 1)) tmp[i / 2] = static_cast<uint8_t>(v << 4); else tmp[i / 2] |= static_cast<uint8_t>(v);
  }
  if (!nonzero(tmp, sizeof(tmp))) { wipe(tmp, sizeof(tmp)); return false; }
  memcpy(out, tmp, sizeof(tmp)); wipe(tmp, sizeof(tmp)); return true;
}
bool encode_id(const uint8_t value[16], char *out, size_t capacity) {
  if (!value || !out || capacity < 33 || !nonzero(value,16)) return false;
  static const char hex[] = "0123456789abcdef";
  for (size_t i=0; i<16; ++i) { out[2*i]=hex[value[i]>>4]; out[2*i+1]=hex[value[i]&15]; }
  out[32]=0; return true;
}
bool valid_origin(const char *s, size_t n) {
  if (!s || n < 11 || n > 159 || memcmp(s, "https://", 8)) return false;
  size_t end = 8;
  if (s[end] == '[') {
    const size_t start = ++end;
    while (end < n && s[end] != ']') ++end;
    if (end == n || end == start || end - start >= INET6_ADDRSTRLEN) return false;
    char input[INET6_ADDRSTRLEN], canonical[INET6_ADDRSTRLEN];
    memcpy(input, s + start, end - start); input[end - start] = 0;
    struct in6_addr addr;
    if (inet_pton(AF_INET6, input, &addr) != 1) return false;
    const uint8_t *ip = reinterpret_cast<const uint8_t *>(&addr);
    bool mapped = true;
    for (size_t j = 0; j < 10; ++j) mapped = mapped && ip[j] == 0;
    mapped = mapped && ip[10] == 0xff && ip[11] == 0xff;
    if (mapped) return false;
    // lwIP compresses the first zero run, whereas Go canonicalizes the
    // longest run (the first on ties). Format explicitly for wire parity.
    uint16_t words[8];
    for (size_t j = 0; j < 8; ++j) words[j] = static_cast<uint16_t>((ip[j * 2] << 8) | ip[j * 2 + 1]);
    size_t best = 8, best_len = 0;
    for (size_t j = 0; j < 8;) {
      if (words[j]) { ++j; continue; }
      size_t k = j; while (k < 8 && !words[k]) ++k;
      if (k - j > best_len && k - j >= 2) { best = j; best_len = k - j; }
      j = k;
    }
    size_t used = 0;
    for (size_t j = 0; j < 8;) {
      if (j == best) {
        canonical[used++] = ':'; canonical[used++] = ':'; j += best_len;
        continue;
      }
      if (used && canonical[used - 1] != ':') canonical[used++] = ':';
      int count = snprintf(canonical + used, sizeof(canonical) - used, "%x", static_cast<unsigned>(words[j++]));
      if (count < 0 || static_cast<size_t>(count) >= sizeof(canonical) - used) return false;
      used += static_cast<size_t>(count);
    }
    canonical[used] = 0;
    if (strcmp(input, canonical)) return false;
    ++end;
  } else {
    const size_t start = end;
    size_t label = 0;
    while (end < n && s[end] != ':') {
      const char c = s[end];
      if (c == '.') {
        if (!label || label > 63 || s[end - 1] == '-') return false;
        label = 0;
      } else {
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') return false;
        if (!label && c == '-') return false;
        ++label;
      }
      ++end;
    }
    if (!label || label > 63 || s[end - 1] == '-' || end - start > 253) return false;
  }
  if (end >= n || s[end++] != ':' || end == n || s[end] == '0') return false;
  unsigned port = 0;
  for (; end < n; ++end) {
    if (s[end] < '0' || s[end] > '9') return false;
    port = port * 10 + static_cast<unsigned>(s[end] - '0');
    if (port > 65535) return false;
  }
  return port != 0;
}
bool proof_bytes(const Proof &p, uint8_t *out, size_t cap, size_t *written) {
  if (written) *written = 0;
  if (!out || !written || !valid_origin(p.origin, p.origin_len) || !nonzero(p.bridge, 16) || !nonzero(p.device, 16) || !nonzero(p.challenge, 32)) return false;
  bool gen = nonzero(p.generation, 16), session = nonzero(p.session, 32);
  switch (p.operation) {
    case Operation::Deliver: if (!session || gen || p.epoch) return false; break;
    case Operation::Ack: if (!session || !gen || p.epoch) return false; break;
    case Operation::ClearAck: if (session || !gen || !p.epoch) return false; break;
    default: return false;
  }
  static const char domain[] = "HomeDash voice device proof v1";
  const size_t required = sizeof(domain) + 2 + 16 + 16 + 32 + 32 + 16 + 2 + p.origin_len + 8;
  if (cap < required) return false;
  size_t i = 0;
  auto append = [&](const void *b, size_t n) { memcpy(out + i, b, n); i += n; };
  append(domain, sizeof(domain)); out[i++] = 1; out[i++] = static_cast<uint8_t>(p.operation);
  append(p.bridge, 16); append(p.device, 16); append(p.session, 32); append(p.challenge, 32); append(p.generation, 16);
  out[i++] = static_cast<uint8_t>(p.origin_len >> 8); out[i++] = static_cast<uint8_t>(p.origin_len);
  append(p.origin, p.origin_len);
  for (int shift = 56; shift >= 0; shift -= 8) out[i++] = static_cast<uint8_t>(p.epoch >> shift);
  *written = i; return true;
}
}

#include "wifi_manager.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ctr_drbg.h"

namespace voice_pair {
static bool ready(const EntropyReady &r) { return r.wifi_connected && r.time_valid; }
struct Seed { uint8_t bytes[48]; size_t offset; };
static int seed_read(void *ctx, unsigned char *out, size_t n) {
  Seed *seed = static_cast<Seed *>(ctx);
  if (!seed || n > sizeof(seed->bytes) - seed->offset) return -1;
  memcpy(out, seed->bytes + seed->offset, n); seed->offset += n; return 0;
}
class Random {
 public:
  explicit Random(const EntropyReady &r) : ok_(false) {
    mbedtls_ctr_drbg_init(&ctx_);
    if (!ready(r)) return;
    if (!wifi_pairing_fill_entropy(seed_.bytes, sizeof(seed_.bytes))) return;
    mbedtls_ctr_drbg_set_entropy_len(&ctx_, sizeof(seed_.bytes));
    ok_ = mbedtls_ctr_drbg_seed(&ctx_, seed_read, &seed_, nullptr, 0) == 0;
    wipe(&seed_, sizeof(seed_));
    seed_.offset = sizeof(seed_.bytes); // Unexpected reseeding fails closed.
  }
  ~Random() { mbedtls_ctr_drbg_free(&ctx_); }
  bool ok() const { return ok_; }
  int fill(uint8_t *out, size_t n) { return ok_ ? mbedtls_ctr_drbg_random(&ctx_, out, n) : -1; }
  static int callback(void *ctx, unsigned char *out, size_t n) { return static_cast<Random *>(ctx)->fill(out,n); }
 private:
  mbedtls_ctr_drbg_context ctx_;
  Seed seed_{};
  bool ok_;
  Random(const Random &) = delete;
  Random &operator=(const Random &) = delete;
};
bool random_secret(const EntropyReady &r, uint8_t out[32]) {
  if (!out) return false;
  wipe(out, 32); Random random(r);
  if (random.fill(out, 32) != 0 || !nonzero(out, 32)) { wipe(out, 32); return false; } return true;
}
bool random_generation(const EntropyReady &r, uint8_t out[16]) {
  if (!out) return false;
  wipe(out, 16); Random random(r);
  if (random.fill(out, 16) != 0 || !nonzero(out, 16)) { wipe(out, 16); return false; } return true;
}
bool valid_public_key(const uint8_t *b, size_t n) {
  if (!b || n != 65 || b[0] != 4) return false;
  mbedtls_ecp_group g; mbedtls_ecp_point p;
  mbedtls_ecp_group_init(&g); mbedtls_ecp_point_init(&p);
  const bool ok = mbedtls_ecp_group_load(&g, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
    mbedtls_ecp_point_read_binary(&g, &p, b, n) == 0 && mbedtls_ecp_check_pubkey(&g, &p) == 0;
  mbedtls_ecp_point_free(&p); mbedtls_ecp_group_free(&g); return ok;
}
bool generate_key(const EntropyReady &r, uint8_t priv[32], uint8_t pub[65]) {
  if (!priv || !pub) return false;
  wipe(priv, 32); wipe(pub, 65); if (!ready(r)) return false;
  mbedtls_ecp_keypair k; mbedtls_ecp_keypair_init(&k); size_t len = 0;
  Random random(r);
  bool ok = random.ok() && mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &k, Random::callback, &random) == 0 &&
    mbedtls_mpi_write_binary(&k.MBEDTLS_PRIVATE(d), priv, 32) == 0 &&
    mbedtls_ecp_point_write_binary(&k.MBEDTLS_PRIVATE(grp), &k.MBEDTLS_PRIVATE(Q), MBEDTLS_ECP_PF_UNCOMPRESSED, &len, pub, 65) == 0 && len == 65;
  mbedtls_ecp_keypair_free(&k); if (!ok) { wipe(priv, 32); wipe(pub, 65); } return ok;
}
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static bool b64_encode(const uint8_t *in, size_t n, char *out, size_t cap) {
  size_t need = (n * 8 + 5) / 6;
  if (cap < need + 1) return false;
  uint32_t bits = 0; unsigned count = 0; size_t j = 0;
  for (size_t i = 0; i < n; ++i) { bits = (bits << 8) | in[i]; count += 8; while (count >= 6) { count -= 6; out[j++] = alphabet[(bits >> count) & 63]; } }
  if (count) out[j++] = alphabet[(bits << (6 - count)) & 63];
  out[j] = 0; return true;
}
static bool b64_decode(const char *in, size_t n, uint8_t *out, size_t len) {
  if (n != (len * 8 + 5) / 6) return false;
  uint32_t bits = 0; unsigned count = 0; size_t j = 0;
  for (size_t i = 0; i < n; ++i) {
    const char *p = in[i] ? strchr(alphabet, in[i]) : nullptr;
    if (!p) return false;
    bits = (bits << 6) | static_cast<uint32_t>(p - alphabet); count += 6;
    if (count >= 8) { count -= 8; if (j >= len) return false; out[j++] = static_cast<uint8_t>(bits >> count); }
  }
  return j == len && (!count || !(bits & ((1u << count) - 1)));
}
bool encode_base64url(const uint8_t *data, size_t len, char *out, size_t capacity) {
  return data && out && len <= 145 && b64_encode(data,len,out,capacity);
}
bool decode_base64url(const char *encoded, size_t len, uint8_t *out, size_t output_len) {
  if (!encoded || !out || output_len > 145 || len != (output_len * 8 + 5) / 6) return false;
  uint8_t tmp[145] = {};
  bool ok = b64_decode(encoded,len,tmp,output_len);
  if (ok) memcpy(out,tmp,output_len); else wipe(out,output_len);
  wipe(tmp,sizeof(tmp)); return ok;
}
bool encode_qr(const QR &q, const char *origin, size_t origin_len, char *out, size_t capacity) {
  if (!out || capacity < kQRLength + 1 || !valid_origin(origin,origin_len) || !nonzero(q.bridge, 16) || !nonzero(q.device, 16) || !nonzero(q.nonce, 32) || !valid_public_key(q.public_key, 65)) return false;
  uint8_t digest[32]; if(mbedtls_sha256(reinterpret_cast<const uint8_t *>(origin),origin_len,digest,0)!=0) return false;
  uint8_t b[145] = {}; b[0] = 2; memcpy(b+1, q.bridge, 16); memcpy(b+17, q.device, 16); memcpy(b+33, q.public_key, 65); memcpy(b+98, q.nonce, 32); memcpy(b+130,digest,15); wipe(digest,sizeof(digest));
  memcpy(out, "HDV2:", 5); bool ok = b64_encode(b, sizeof(b), out+5, capacity-5); wipe(b, sizeof(b));
  if (!ok) wipe(out, capacity); return ok;
}
bool decode_qr(const char *encoded, size_t len, QR *out) {
  if (!out) return false; wipe(out, sizeof(*out));
  if (!encoded || len != kQRLength || memcmp(encoded, "HDV2:", 5)) return false;
  uint8_t b[145] = {}; QR q{};
  bool ok = b64_decode(encoded+5, len-5, b, sizeof(b)) && b[0] == 2 && nonzero(b+1, 16) && nonzero(b+17, 16) && nonzero(b+98, 32) && valid_public_key(b+33, 65);
  if (ok) { memcpy(q.bridge,b+1,16); memcpy(q.device,b+17,16); memcpy(q.public_key,b+33,65); memcpy(q.nonce,b+98,32); memcpy(q.origin_hash,b+130,15); memcpy(out,&q,sizeof(q)); }
  wipe(&q,sizeof(q)); wipe(b,sizeof(b)); return ok;
}
bool qr_matches_origin(const QR &q,const char *origin,size_t len) {
  if(!valid_origin(origin,len)) return false;
  uint8_t hash[32] = {}; bool ok=mbedtls_sha256(reinterpret_cast<const uint8_t *>(origin),len,hash,0)==0;
  uint8_t diff=0; for(size_t i=0;i<15;++i) diff|=q.origin_hash[i]^hash[i];
  wipe(hash,sizeof(hash));return ok && diff==0;
}
static bool load_private(mbedtls_ecp_group *g, mbedtls_mpi *d, const uint8_t priv[32]) {
  return mbedtls_ecp_group_load(g, MBEDTLS_ECP_DP_SECP256R1) == 0 && mbedtls_mpi_read_binary(d, priv, 32) == 0 && mbedtls_ecp_check_privkey(g, d) == 0;
}
static bool sign_data(const uint8_t *msg,size_t len,const uint8_t priv[32],const EntropyReady &r,uint8_t sig[64]) {
  uint8_t hash[32]; bool ok = false;
  mbedtls_ecp_group g; mbedtls_mpi d, x, y, half;
  mbedtls_ecp_group_init(&g); mbedtls_mpi_init(&d); mbedtls_mpi_init(&x); mbedtls_mpi_init(&y); mbedtls_mpi_init(&half);
  Random random(r);
  if (random.ok() && mbedtls_sha256(msg,len,hash,0) == 0 && load_private(&g,&d,priv) &&
      mbedtls_ecdsa_sign_det_ext(&g,&x,&y,&d,hash,32,MBEDTLS_MD_SHA256,Random::callback,&random) == 0 && mbedtls_mpi_copy(&half,&g.N) == 0 && mbedtls_mpi_shift_r(&half,1) == 0) {
    if (mbedtls_mpi_cmp_mpi(&y,&half) > 0 && mbedtls_mpi_sub_mpi(&y,&g.N,&y) != 0) goto done;
    ok = mbedtls_mpi_write_binary(&x,sig,32) == 0 && mbedtls_mpi_write_binary(&y,sig+32,32) == 0;
  }
done:
  mbedtls_mpi_free(&half); mbedtls_mpi_free(&y); mbedtls_mpi_free(&x); mbedtls_mpi_free(&d); mbedtls_ecp_group_free(&g);
  wipe(hash,sizeof(hash)); if (!ok) wipe(sig,64); return ok;
}
bool sign_proof(const Proof &p, const uint8_t priv[32], const EntropyReady &r, uint8_t sig[64]) {
  if (!sig) return false; wipe(sig,64); if (!priv || !ready(r)) return false;
  uint8_t msg[kMaxProofLength];size_t len=0;
  bool ok=proof_bytes(p,msg,sizeof(msg),&len) && sign_data(msg,len,priv,r,sig);
  wipe(msg,sizeof(msg));if(!ok)wipe(sig,64);return ok;
}
bool sign_clear_ack_preauth(const Proof &p,const uint8_t priv[32],const EntropyReady &r,uint8_t nonce[32],uint8_t sig[64]) {
  if(!nonce||!sig)return false; wipe(nonce,32);wipe(sig,64);
  if(!priv||!ready(r)||p.operation!=Operation::ClearAck||!valid_origin(p.origin,p.origin_len)||!nonzero(p.bridge,16)||!nonzero(p.device,16)||!nonzero(p.generation,16)||nonzero(p.session,32)||!p.epoch) return false;
  if(!random_secret(r,nonce))return false;
  uint8_t msg[kMaxProofLength];size_t i=0;static const char domain[]="HomeDash voice clear ack preauth v1";
  auto append=[&](const void *b,size_t n){memcpy(msg+i,b,n);i+=n;};
  append(domain,sizeof(domain));msg[i++]=1;append(p.bridge,16);append(p.device,16);append(p.generation,16);
  for(int shift=56;shift>=0;shift-=8)msg[i++]=static_cast<uint8_t>(p.epoch>>shift);
  msg[i++]=static_cast<uint8_t>(p.origin_len>>8);msg[i++]=static_cast<uint8_t>(p.origin_len);append(p.origin,p.origin_len);append(nonce,32);
  bool ok=sign_data(msg,i,priv,r,sig);wipe(msg,sizeof(msg));if(!ok){wipe(nonce,32);wipe(sig,64);}return ok;
}
static bool equal(const uint8_t *a, const uint8_t *b, size_t n) { uint8_t x=0; for(size_t i=0;i<n;++i) x |= a[i]^b[i]; return x==0; }
bool clear_command_hash(const Clear &c,uint8_t out[32]) {
  if(!out)return false;wipe(out,32);uint8_t msg[220];size_t i=0;static const char domain[]="HomeDash voice server clear v1";
  auto append=[&](const void *b,size_t n){memcpy(msg+i,b,n);i+=n;};append(domain,sizeof(domain));msg[i++]=1;msg[i++]=1;append(c.bridge,16);append(c.device,16);append(c.device_key_hash,32);append(c.generation,16);for(int shift=56;shift>=0;shift-=8)msg[i++]=static_cast<uint8_t>(c.epoch>>shift);append(c.nonce,32);append(c.signature,64);
  bool ok=mbedtls_sha256(msg,i,out,0)==0;wipe(msg,sizeof(msg));if(!ok)wipe(out,32);return ok;
}
bool verify_clear(const Clear &c, const uint8_t server[65], const uint8_t bridge[16], const uint8_t device[16], const uint8_t key[65], const uint8_t gen[16]) {
  if (!server || !bridge || !device || !key || !gen || !nonzero(bridge,16) || !nonzero(device,16) || !nonzero(gen,16) || !nonzero(c.nonce,32) || !c.epoch || !equal(c.bridge,bridge,16) || !equal(c.device,device,16) || !equal(c.generation,gen,16) || !valid_public_key(key,65) || !valid_public_key(server,65)) return false;
  uint8_t hash[32], msg[155]; size_t i=0; static const char domain[]="HomeDash voice server clear v1";
  if (mbedtls_sha256(key,65,hash,0) != 0 || !equal(hash,c.device_key_hash,32)) { wipe(hash,32); return false; }
  auto append = [&](const void *b,size_t n){ memcpy(msg+i,b,n); i+=n; };
  append(domain,sizeof(domain)); msg[i++]=1; msg[i++]=1; append(c.bridge,16); append(c.device,16); append(c.device_key_hash,32); append(c.generation,16);
  for(int shift=56;shift>=0;shift-=8) msg[i++]=static_cast<uint8_t>(c.epoch>>shift);
  append(c.nonce,32);
  mbedtls_ecp_group g; mbedtls_ecp_point q; mbedtls_mpi r,s,half;
  mbedtls_ecp_group_init(&g); mbedtls_ecp_point_init(&q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s); mbedtls_mpi_init(&half);
  bool ok = mbedtls_sha256(msg,i,hash,0)==0 && mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1)==0 && mbedtls_ecp_point_read_binary(&g,&q,server,65)==0 && mbedtls_ecp_check_pubkey(&g,&q)==0 && mbedtls_mpi_read_binary(&r,c.signature,32)==0 && mbedtls_mpi_read_binary(&s,c.signature+32,32)==0 && mbedtls_mpi_copy(&half,&g.N)==0 && mbedtls_mpi_shift_r(&half,1)==0 && mbedtls_mpi_cmp_int(&r,0)>0 && mbedtls_mpi_cmp_mpi(&r,&g.N)<0 && mbedtls_mpi_cmp_int(&s,0)>0 && mbedtls_mpi_cmp_mpi(&s,&half)<=0 && mbedtls_ecdsa_verify(&g,hash,32,&q,&r,&s)==0;
  mbedtls_mpi_free(&half); mbedtls_mpi_free(&s); mbedtls_mpi_free(&r); mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&g); wipe(hash,sizeof(hash)); wipe(msg,sizeof(msg)); return ok;
}
}
