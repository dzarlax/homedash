#include "voice_pair_protocol.h"
#include "mbedtls/sha256.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace voice_pair;
void voice_pair_test_entropy_fail(bool);
static bool unhex(const char *s, uint8_t *b, size_t n) {
  if (strlen(s) != 2*n) return false;
  for(size_t i=0;i<n;++i) { unsigned x; if (sscanf(s+2*i, "%2x", &x)!=1) return false; b[i]=static_cast<uint8_t>(x); } return true;
}
static void print_hex(const uint8_t *p,size_t n) { for(size_t i=0;i<n;++i) printf("%02x",p[i]); putchar('\n'); }
int main(int argc, char **argv) {
  assert(argc==5);
  uint8_t pub[65], server[65], sig[64]; assert(unhex(argv[1],pub,65)); assert(unhex(argv[2],server,65)); assert(unhex(argv[3],sig,64));
  EntropyReady no{false,true}, yes{true,true}; uint8_t secret[32], generation[16], priv[32]={}; priv[31]=1;
  memset(secret,0xff,sizeof(secret)); assert(!random_secret(no,secret)); for(auto b:secret) assert(b==0);
  voice_pair_test_entropy_fail(true); memset(secret,0xff,sizeof(secret)); assert(!random_secret(yes,secret)); for(auto b:secret) assert(b==0); voice_pair_test_entropy_fail(false);
  assert(random_secret(yes,secret) && random_generation(yes,generation));
  uint8_t generated_priv[32], generated_pub[65]; assert(generate_key(yes,generated_priv,generated_pub)); assert(valid_public_key(generated_pub,65)); wipe(generated_priv,32);
  assert(!valid_public_key(pub,64)); uint8_t badpub[65]; memcpy(badpub,pub,65); badpub[0]=2; assert(!valid_public_key(badpub,65)); memset(badpub+1,0,64); badpub[0]=4; assert(!valid_public_key(badpub,65));
  QR q{}; q.bridge[0]=1; q.device[0]=2; q.nonce[0]=7; memcpy(q.public_key,pub,65);
  char encoded[kQRLength+1]; assert(encode_qr(q,"https://bridge.example:443",26,encoded,sizeof(encoded))); assert(strlen(encoded)==kQRLength);
  QR decoded{}; assert(decode_qr(encoded,kQRLength,&decoded)); assert(!memcmp(&q,&decoded,offsetof(QR,origin_hash))); assert(qr_matches_origin(decoded,"https://bridge.example:443",26)); assert(!qr_matches_origin(decoded,"https://attacker.example:443",28)); decoded.origin_hash[0]^=1; assert(!qr_matches_origin(decoded,"https://bridge.example:443",26));
  assert(!decode_qr(encoded,kQRLength-1,&decoded)); char altered[kQRLength+1]; memcpy(altered,encoded,sizeof(altered)); altered[kQRLength-1]='B'; assert(!decode_qr(altered,kQRLength,&decoded));
  memcpy(altered,encoded,sizeof(altered)); altered[10]='='; assert(!decode_qr(altered,kQRLength,&decoded));
  memcpy(altered,encoded,sizeof(altered)); altered[10]=0; assert(!decode_qr(altered,kQRLength,&decoded));
  q.nonce[0]=0; assert(!encode_qr(q,"https://bridge.example:443",26,altered,sizeof(altered))); q.nonce[0]=7;
  Proof p{}; p.operation=Operation::Deliver; p.bridge[0]=1; p.device[0]=2; p.session[0]=7; p.challenge[0]=8; p.origin="https://bridge.example:443"; p.origin_len=strlen(p.origin);
  uint8_t proof[64], bytes[kMaxProofLength]; size_t n=0; assert(proof_bytes(p,bytes,sizeof(bytes),&n)); assert(!sign_proof(p,priv,no,proof)); assert(sign_proof(p,priv,yes,proof)); uint8_t proof2[64]; assert(sign_proof(p,priv,yes,proof2)); assert(!memcmp(proof,proof2,64));
  Clear c{}; c.bridge[0]=1; c.device[0]=2; c.generation[0]=3; c.epoch=4; c.nonce[0]=5; memcpy(c.signature,sig,64); assert(mbedtls_sha256(pub,65,c.device_key_hash,0)==0);
  assert(verify_clear(c,server,c.bridge,c.device,pub,c.generation));
  c.signature[0]^=1; assert(!verify_clear(c,server,c.bridge,c.device,pub,c.generation)); c.signature[0]^=1;
  c.nonce[0]^=1; assert(!verify_clear(c,server,c.bridge,c.device,pub,c.generation)); c.nonce[0]^=1;
  uint8_t wrong_gen[16]={4}; assert(!verify_clear(c,server,c.bridge,c.device,pub,wrong_gen));
  c.device_key_hash[0]^=1; assert(!verify_clear(c,server,c.bridge,c.device,pub,c.generation)); c.device_key_hash[0]^=1;
  assert(unhex(argv[4],c.signature,64)); assert(!verify_clear(c,server,c.bridge,c.device,pub,c.generation));
  print_hex(bytes,n); print_hex(proof,64);
  p.operation=Operation::Ack; p.generation[0]=3; assert(proof_bytes(p,bytes,sizeof(bytes),&n)); assert(sign_proof(p,priv,yes,proof)); print_hex(bytes,n); print_hex(proof,64);
  p.operation=Operation::ClearAck; memset(p.session,0,32); p.epoch=4; assert(proof_bytes(p,bytes,sizeof(bytes),&n)); assert(sign_proof(p,priv,yes,proof)); print_hex(bytes,n); print_hex(proof,64);
  uint8_t pre_nonce[32],pre_sig[64]; assert(sign_clear_ack_preauth(p,priv,yes,pre_nonce,pre_sig)); print_hex(pre_nonce,32); print_hex(pre_sig,64);
  p.epoch=0; assert(!sign_clear_ack_preauth(p,priv,yes,pre_nonce,pre_sig));
  puts(encoded);
  wipe(secret,sizeof(secret)); wipe(&q,sizeof(q)); wipe(&decoded,sizeof(decoded)); wipe(priv,sizeof(priv));
}
