#include "voice_pair_protocol.h"
#include <cassert>
#include <cstring>
using namespace voice_pair;
int main() {
  assert(valid_origin("https://bridge.example:443", 26));
  assert(valid_origin("https://[2001:db8::1]:443", 25));
  assert(valid_origin("https://[1:0:0:2::3]:443", 24));
  const char *bad[] = {"https://Bridge:443", "https://x:0443", "https://x:0", "https://x:65536", "https://x:443/", "https://x.:443", "https://[2001:0db8::1]:443", "https://x:443\0x"};
  for (size_t i = 0; i < sizeof(bad)/sizeof(*bad)-1; ++i) assert(!valid_origin(bad[i], strlen(bad[i])));
  assert(!valid_origin(bad[7], 15));
  uint8_t id[16]; assert(decode_id("00112233445566778899aabbccddeeff", 32, id));
  assert(!decode_id("00112233445566778899AABBCCDDEEFF", 32, id));
  assert(!decode_id("00000000000000000000000000000000", 32, id));
  char hex[33]; assert(encode_id(id, hex, sizeof(hex))); assert(!strcmp(hex, "00112233445566778899aabbccddeeff"));
  uint8_t one[1]={255}, decoded[1]={}; char b64[4]; assert(encode_base64url(one,1,b64,sizeof(b64))); assert(!strcmp(b64,"_w")); assert(decode_base64url(b64,2,decoded,1) && decoded[0]==255); assert(!decode_base64url("_x",2,decoded,1) && decoded[0]==0); assert(!decode_base64url("_w=",3,decoded,1));
  Proof p{}; p.bridge[0] = 1; p.device[0] = 2; p.operation = Operation::Deliver; p.session[0] = 1; p.challenge[31] = 2; p.origin = "https://x:443"; p.origin_len = strlen(p.origin);
  uint8_t bytes[kMaxProofLength]; size_t written = 0;
  assert(proof_bytes(p, bytes, sizeof(bytes), &written));
  assert(written == 31 + 2 + 16 + 16 + 32 + 32 + 16 + 2 + p.origin_len + 8);
  assert(memcmp(bytes, "HomeDash voice device proof v1\0\1\1", 33) == 0);
  assert(!proof_bytes(p, bytes, written - 1, &written));
  p.session[0] = 0; assert(!proof_bytes(p, bytes, sizeof(bytes), &written));
  p.operation = Operation::ClearAck; p.generation[0] = 1; p.epoch = 1;
  assert(proof_bytes(p, bytes, sizeof(bytes), &written));
  p.epoch = 0; assert(!proof_bytes(p, bytes, sizeof(bytes), &written));
  p.operation = Operation::Ack; p.session[0]=1; assert(proof_bytes(p, bytes, sizeof(bytes), &written));
  p.challenge[31]=0; assert(!proof_bytes(p, bytes, sizeof(bytes), &written));
}
