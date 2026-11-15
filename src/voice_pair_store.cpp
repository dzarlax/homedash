#include "voice_pair_store.h"
#include "voice_pair_protocol.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include <string.h>
#include <stdlib.h>

namespace voice_pair_store {
// All on-flash fields are bytes; zero-filled tails and reserved bytes are
// checked. One NVS blob is one complete snapshot, never a set of mixed keys.
struct DiskCredential {
  uint8_t bridge[16], device[16], private_key[32], public_key[65], server_key[65], generation[16], epoch[8];
  uint8_t wss_len[2], token_len[2], wss[159], token[255], origin_len[2], origin[159];
};
struct DiskRecovery {
  uint8_t bridge[16], device[16], private_key[32], public_key[65], server_key[65], generation[16], epoch[8], origin_len[2], origin[159];
};
static_assert(sizeof(DiskRecovery)==379, "recovery format changed");
struct DiskIdentity { uint8_t bridge[16], device[16], private_key[32], public_key[65]; };
struct Snapshot {
  uint8_t magic[4], version, flags, reserved[2], revision[8], epoch[8];
  uint8_t revoked_generation[16], command_hash[32];
  DiskCredential active, candidate;
  DiskIdentity identity;
  uint8_t recovery_count;
  DiskRecovery recovery[kMaxRecoveryBindings];
  uint8_t checksum[32];
};
static_assert(sizeof(DiskCredential)==797, "disk format changed");
static_assert(sizeof(Snapshot)==3344, "snapshot format changed");
static const char *const ns="voice_pair_v1";
static const char *const keys[]={"slot0","slot1"};
static const uint8_t reset_marker[]={0x56,0x50,0x52,0x31,0xa5,0x5a,0xc3,0x3c};
static bool faulted=false;
// Serialized store calls distinguish entropy loss from actual record corruption.
static bool entropy_failed=false;
static const uint8_t ACTIVE=1, CANDIDATE=2, TOMBSTONE=4, IDENTITY=8, MANAGED=16, CLEAR_ACKED=32, RECOVERY=64;
void wipe(void *p,size_t n) { voice_pair::wipe(p,n); }
static bool nz(const uint8_t *p,size_t n) { uint8_t v=0; for(size_t i=0;i<n;++i) v|=p[i]; return v!=0; }
static bool eq(const void *a,const void *b,size_t n) { return memcmp(a,b,n)==0; }
static uint64_t get64(const uint8_t p[8]) { uint64_t x=0; for(int i=0;i<8;++i) x=(x<<8)|p[i]; return x; }
static void put64(uint8_t p[8],uint64_t x) { for(int i=7;i>=0;--i) {p[i]=static_cast<uint8_t>(x);x>>=8;} }
static size_t get16(const uint8_t p[2]) { return (static_cast<size_t>(p[0])<<8)|p[1]; }
static void put16(uint8_t p[2],size_t x) { p[0]=static_cast<uint8_t>(x>>8);p[1]=static_cast<uint8_t>(x); }
static size_t bounded(const char *s,size_t n) { for(size_t i=0;i<n;++i) if(!s[i]) return i; return n; }
struct EntropySeed { uint8_t bytes[64]; size_t used; };
static int seed_read(void *p, unsigned char *out,size_t n) {
  EntropySeed *seed=static_cast<EntropySeed *>(p);
  if(n>sizeof(seed->bytes)-seed->used)return -1;
  memcpy(out,seed->bytes+seed->used,n);seed->used+=n;return 0;
}
static bool keypair(const uint8_t priv[32],const uint8_t pub[65],const voice_pair::EntropyReady &ready) {
  if(!voice_pair::valid_public_key(pub,65)) return false;
  EntropySeed seed{}; if(!voice_pair::random_secret(ready,seed.bytes) || !voice_pair::random_secret(ready,seed.bytes+32)) {entropy_failed=true;wipe(&seed,sizeof(seed));return false;}
  mbedtls_ctr_drbg_context drbg;mbedtls_ctr_drbg_init(&drbg);mbedtls_ctr_drbg_set_entropy_len(&drbg,sizeof(seed.bytes));
  bool seeded=mbedtls_ctr_drbg_seed(&drbg,seed_read,&seed,nullptr,0)==0;
  if(!seeded)entropy_failed=true;
  wipe(seed.bytes,sizeof(seed.bytes));seed.used=sizeof(seed.bytes);
  mbedtls_ecp_group g; mbedtls_ecp_point q; mbedtls_mpi d;
  mbedtls_ecp_group_init(&g); mbedtls_ecp_point_init(&q); mbedtls_mpi_init(&d);
  uint8_t actual[65]; size_t len=0;
  bool ok=seeded && mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1)==0 && mbedtls_mpi_read_binary(&d,priv,32)==0 && mbedtls_ecp_check_privkey(&g,&d)==0 && mbedtls_ecp_mul(&g,&q,&d,&g.G,mbedtls_ctr_drbg_random,&drbg)==0 && mbedtls_ecp_point_write_binary(&g,&q,MBEDTLS_ECP_PF_UNCOMPRESSED,&len,actual,sizeof(actual))==0 && len==65 && eq(actual,pub,65);
  wipe(actual,sizeof(actual)); mbedtls_mpi_free(&d); mbedtls_ecp_point_free(&q); mbedtls_ecp_group_free(&g);mbedtls_ctr_drbg_free(&drbg);wipe(&seed,sizeof(seed));return ok;
}
static bool valid_credential(const DiskCredential &c,bool access,const voice_pair::EntropyReady &ready) {
  if(!nz(c.bridge,16)||!nz(c.device,16)||!nz(c.generation,16)||!keypair(c.private_key,c.public_key,ready)||!voice_pair::valid_public_key(c.server_key,65)) return false;
  size_t w=get16(c.wss_len),t=get16(c.token_len),o=get16(c.origin_len);
  if(w>sizeof(c.wss)||t>sizeof(c.token)||o>sizeof(c.origin)||nz(c.wss+w,sizeof(c.wss)-w)||nz(c.token+t,sizeof(c.token)-t)||nz(c.origin+o,sizeof(c.origin)-o)||!voice_pair::valid_origin(reinterpret_cast<const char *>(c.origin),o)) return false;
  if(!access) return w==0 && t==0;
  if(w<7 || t!=43 || memcmp(c.wss,"wss://",6)) return false;
  char origin[160]; size_t olen=w+2; if(olen>159) return false;
  memcpy(origin,"https://",8); memcpy(origin+8,c.wss+6,w-6);
  uint8_t decoded[32]; bool ok=olen==o && eq(origin,c.origin,o) && voice_pair::valid_origin(origin,olen) && voice_pair::decode_base64url(reinterpret_cast<const char *>(c.token),t,decoded,32) && nz(decoded,32);
  wipe(decoded,sizeof(decoded)); wipe(origin,sizeof(origin)); return ok;
}
static bool encode(const Credential &c,DiskCredential &d,const voice_pair::EntropyReady &ready) {
  memset(&d,0,sizeof(d)); size_t w=bounded(c.wss,sizeof(c.wss)),t=bounded(c.token,sizeof(c.token)),o=bounded(c.https_origin,sizeof(c.https_origin)); if(w>=sizeof(c.wss)||t>=sizeof(c.token)||o>=sizeof(c.https_origin)||w>159||t>255||o>159) return false;
  memcpy(d.bridge,c.bridge,16); memcpy(d.device,c.device,16); memcpy(d.private_key,c.private_key,32); memcpy(d.public_key,c.public_key,65); memcpy(d.server_key,c.server_key,65); memcpy(d.generation,c.generation,16); put64(d.epoch,c.epoch); put16(d.wss_len,w); put16(d.token_len,t); memcpy(d.wss,c.wss,w); memcpy(d.token,c.token,t); put16(d.origin_len,o); memcpy(d.origin,c.https_origin,o);
  return valid_credential(d,true,ready);
}
static void decode(const DiskCredential &d,Credential &c) {
  memset(&c,0,sizeof(c)); memcpy(c.bridge,d.bridge,16); memcpy(c.device,d.device,16); memcpy(c.private_key,d.private_key,32); memcpy(c.public_key,d.public_key,65); memcpy(c.server_key,d.server_key,65); memcpy(c.generation,d.generation,16); c.epoch=get64(d.epoch); memcpy(c.wss,d.wss,get16(d.wss_len)); memcpy(c.token,d.token,get16(d.token_len));memcpy(c.https_origin,d.origin,get16(d.origin_len));
}
static void from_recovery(const DiskRecovery &r,DiskCredential &c) {
  memset(&c,0,sizeof(c));memcpy(c.bridge,r.bridge,16);memcpy(c.device,r.device,16);memcpy(c.private_key,r.private_key,32);memcpy(c.public_key,r.public_key,65);memcpy(c.server_key,r.server_key,65);memcpy(c.generation,r.generation,16);memcpy(c.epoch,r.epoch,8);memcpy(c.origin_len,r.origin_len,2);memcpy(c.origin,r.origin,sizeof(r.origin));
}
static void to_recovery(const DiskCredential &c,DiskRecovery &r) {
  memset(&r,0,sizeof(r));memcpy(r.bridge,c.bridge,16);memcpy(r.device,c.device,16);memcpy(r.private_key,c.private_key,32);memcpy(r.public_key,c.public_key,65);memcpy(r.server_key,c.server_key,65);memcpy(r.generation,c.generation,16);memcpy(r.epoch,c.epoch,8);memcpy(r.origin_len,c.origin_len,2);memcpy(r.origin,c.origin,sizeof(r.origin));
}
static bool checksum(Snapshot &s) { return mbedtls_sha256(reinterpret_cast<uint8_t *>(&s),offsetof(Snapshot,checksum),s.checksum,0)==0; }
static bool valid(Snapshot &s,const voice_pair::EntropyReady &ready) {
  uint8_t h[32]; bool digest=mbedtls_sha256(reinterpret_cast<uint8_t *>(&s),offsetof(Snapshot,checksum),h,0)==0 && eq(h,s.checksum,32); wipe(h,sizeof(h));
  if(!digest||memcmp(s.magic,"VPS4",4)||s.version!=4||s.reserved[0]||s.reserved[1]||(s.flags&~127)||!get64(s.revision)) return false;
  bool a=s.flags&ACTIVE, c=s.flags&CANDIDATE, t=s.flags&TOMBSTONE, id=s.flags&IDENTITY; uint64_t epoch=get64(s.epoch);
  if(s.recovery_count>kMaxRecoveryBindings || (s.recovery_count && !(s.flags&RECOVERY))) return false;
  for(size_t i=0;i<kMaxRecoveryBindings;++i) {
    if(i<s.recovery_count){DiskCredential item{};from_recovery(s.recovery[i],item);bool good=valid_credential(item,false,ready)&&eq(item.bridge,s.identity.bridge,16)&&eq(item.device,s.identity.device,16);wipe(&item,sizeof(item));if(!good)return false;for(size_t j=0;j<i;++j)if(eq(s.recovery[i].generation,s.recovery[j].generation,16))return false;}
    else if(nz(reinterpret_cast<const uint8_t *>(&s.recovery[i]),sizeof(s.recovery[i])))return false;
  }
  if((s.flags&RECOVERY) && !(s.flags&MANAGED)) return false;
  if((s.flags&CLEAR_ACKED) && !t) return false;
  if((a||c||t) && !(s.flags&MANAGED)) return false;
  if((s.flags&MANAGED) && !id) return false;
  if(id) {
    if(!nz(s.identity.bridge,16)||!nz(s.identity.device,16)||!keypair(s.identity.private_key,s.identity.public_key,ready)) return false;
  } else if(nz(reinterpret_cast<const uint8_t *>(&s.identity),sizeof(s.identity))||a||c||t) return false;
  if(t ? (!epoch||!nz(s.revoked_generation,16)||!nz(s.command_hash,32)) : (epoch||nz(s.revoked_generation,16)||nz(s.command_hash,32))) return false;
  if(a) { if(!valid_credential(s.active,true,ready)||get64(s.active.epoch)!=epoch || (t&&eq(s.active.generation,s.revoked_generation,16))) return false; }
  else if(t) { if(!valid_credential(s.active,false,ready)||!eq(s.active.generation,s.revoked_generation,16)||get64(s.active.epoch)!=epoch) return false; }
  else if(nz(reinterpret_cast<uint8_t *>(&s.active),sizeof(s.active))) return false;
  if(a||t) { if(!eq(s.active.bridge,s.identity.bridge,16)||!eq(s.active.device,s.identity.device,16)) return false; }
  if(c) { if(!valid_credential(s.candidate,true,ready)||get64(s.candidate.epoch)!=epoch||(t&&eq(s.candidate.generation,s.revoked_generation,16))||!eq(&s.candidate,&s.identity,sizeof(s.identity))) return false; }
  else if(nz(reinterpret_cast<uint8_t *>(&s.candidate),sizeof(s.candidate))) return false;
  return true;
}
static Result read_slot(nvs_handle_t h,int i,Snapshot &s,bool &present,const voice_pair::EntropyReady &ready) {
  present=false; size_t n=0; esp_err_t e=nvs_get_blob(h,keys[i],nullptr,&n);
  if(e==ESP_ERR_NVS_NOT_FOUND) return Result::Ok;
  if(e!=ESP_OK) return Result::StorageError;
  present=true; if(n!=sizeof(s)) return Result::Corrupt;
  e=nvs_get_blob(h,keys[i],&s,&n); if(e!=ESP_OK) return Result::StorageError;
  if(n!=sizeof(s)||!valid(s,ready)) return Result::Corrupt;
  return Result::Ok;
}
static bool erase_and_verify(int slot) {
  nvs_handle_t h;
  if(nvs_open(ns,NVS_READWRITE,&h)!=ESP_OK) return false;
  esp_err_t e=nvs_erase_key(h,keys[slot]);
  if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;
  if(e==ESP_OK)e=nvs_commit(h);
  nvs_close(h);
  if(e!=ESP_OK || nvs_open(ns,NVS_READONLY,&h)!=ESP_OK) return false;
  size_t n=0; e=nvs_get_blob(h,keys[slot],nullptr,&n); nvs_close(h);
  return e==ESP_ERR_NVS_NOT_FOUND;
}
static Result read_latest(Snapshot &out,int &slot,const voice_pair::EntropyReady &ready) {
  if(faulted) return Result::StorageError;
  entropy_failed=false;
  nvs_handle_t h; esp_err_t e=nvs_open(ns,NVS_READONLY,&h);
  if(e==ESP_ERR_NVS_NOT_FOUND) { if(!ready.wifi_connected||!ready.time_valid)return Result::StorageError;memset(&out,0,sizeof(out));slot=-1;return Result::Empty; }
  if(e!=ESP_OK) {faulted=true;return Result::StorageError;}
  uint8_t marker[sizeof(reset_marker)]={};size_t marker_len=sizeof(marker);e=nvs_get_blob(h,"reset",marker,&marker_len);
  if(e==ESP_OK) {bool valid_marker=marker_len==sizeof(marker)&&eq(marker,reset_marker,sizeof(marker));wipe(marker,sizeof(marker));nvs_close(h);if(!valid_marker){faulted=true;return Result::Corrupt;}return Result::Reset;}
  wipe(marker,sizeof(marker));if(e!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);faulted=true;return Result::StorageError;}
  if(!ready.wifi_connected||!ready.time_valid){nvs_close(h);return Result::StorageError;}
  Snapshot *slots=static_cast<Snapshot *>(calloc(2,sizeof(Snapshot)));
  if(!slots) {nvs_close(h);faulted=true;return Result::StorageError;}
  Snapshot &a=slots[0], &b=slots[1];
  bool ap=false,bp=false; Result x=read_slot(h,0,a,ap,ready), y=read_slot(h,1,b,bp,ready); nvs_close(h);
  Result res=Result::Ok;
  if(x!=Result::Ok||y!=Result::Ok) res=(x==Result::Corrupt||y==Result::Corrupt)?Result::Corrupt:Result::StorageError;
  else if(!ap&&!bp) {memset(&out,0,sizeof(out));slot=-1;res=Result::Empty;}
  else if(ap&&bp&&get64(a.revision)==get64(b.revision)&&!eq(&a,&b,sizeof(a))) res=Result::Corrupt;
  else {slot=!bp||(ap&&get64(a.revision)>=get64(b.revision))?0:1; memcpy(&out,slot?&b:&a,sizeof(out));}
  if(entropy_failed)res=Result::StorageError;
  if(res==Result::Ok && (out.flags&(TOMBSTONE|RECOVERY)) && (slot==0?bp:ap) && !erase_and_verify(1-slot)) res=Result::StorageError;
  if(res!=Result::Ok && res!=Result::Empty) wipe(&out,sizeof(out));
  wipe(slots,2*sizeof(Snapshot));free(slots); if((res==Result::Corrupt||res==Result::StorageError)&&!entropy_failed) faulted=true; return res;
}
static Result write_snapshot(Snapshot &s,int previous,const voice_pair::EntropyReady &ready) {
  uint64_t revision=get64(s.revision); if(revision==UINT64_MAX) return Result::Rejected;
  memcpy(s.magic,"VPS4",4);s.version=4;put64(s.revision,revision+1); if(!checksum(s)||!valid(s,ready)) return entropy_failed?Result::StorageError:Result::Rejected;
  nvs_handle_t h; if(nvs_open(ns,NVS_READWRITE,&h)!=ESP_OK) {faulted=true;return Result::StorageError;}
  int target=previous==0?1:0; esp_err_t e=nvs_set_blob(h,keys[target],&s,sizeof(s)); if(e==ESP_OK)e=nvs_commit(h); nvs_close(h);
  if(e!=ESP_OK) {faulted=true;return Result::StorageError;}
  Snapshot *got=static_cast<Snapshot *>(calloc(1,sizeof(Snapshot)));
  if(!got) {faulted=true;return Result::StorageError;}
  int slot=-1; Result r=read_latest(*got,slot,ready);
  bool ok=r==Result::Ok&&slot==target&&eq(got,&s,sizeof(s)); wipe(got,sizeof(*got));free(got);
  if(!ok) {faulted=true;return r==Result::Corrupt?r:Result::StorageError;} return Result::Ok;
}
Result load(State &out,const voice_pair::EntropyReady &ready) {
  memset(&out,0,sizeof(out)); Snapshot s{}; int slot=-1; Result r=read_latest(s,slot,ready);
  if(r==Result::Ok) {out.has_active=s.flags&ACTIVE;out.has_candidate=s.flags&CANDIDATE;out.has_tombstone=s.flags&TOMBSTONE;out.has_identity=s.flags&IDENTITY;out.managed_started=s.flags&MANAGED;out.clear_acknowledged=s.flags&CLEAR_ACKED;out.recovery_required=s.flags&RECOVERY;if(out.has_identity) memcpy(&out.identity,&s.identity,sizeof(s.identity));out.epoch=get64(s.epoch);if(out.has_active||out.has_tombstone) decode(s.active,out.active);if(out.has_candidate) decode(s.candidate,out.candidate);memcpy(out.revoked_generation,s.revoked_generation,16);memcpy(out.command_hash,s.command_hash,32);out.recovery_count=s.recovery_count;for(size_t i=0;i<s.recovery_count;++i){DiskCredential item{};from_recovery(s.recovery[i],item);decode(item,out.recovery_bindings[i]);wipe(&item,sizeof(item));}}
  wipe(&s,sizeof(s));return r;
}
Result prepare(const Credential &candidate,const voice_pair::EntropyReady &ready) {
  Snapshot s{};DiskCredential c{};int slot=-1;Result r=read_latest(s,slot,ready);
  if(r!=Result::Ok&&r!=Result::Empty) goto done;
  if(((s.flags&TOMBSTONE)&&!(s.flags&CLEAR_ACKED))||!(s.flags&IDENTITY)||!encode(candidate,c,ready)||!eq(&c,&s.identity,sizeof(s.identity))||candidate.epoch!=get64(s.epoch)||((s.flags&TOMBSTONE)&&eq(c.generation,s.revoked_generation,16))) {r=entropy_failed?Result::StorageError:Result::Rejected;goto done;}
  if((s.flags&(ACTIVE|TOMBSTONE)) && (!eq(c.bridge,s.active.bridge,16)||!eq(c.device,s.active.device,16))) {r=Result::Rejected;goto done;}
  for(size_t i=0;i<s.recovery_count;++i)if(eq(c.generation,s.recovery[i].generation,16)){r=Result::Rejected;goto done;}
  if(s.flags&CANDIDATE) {r=eq(&s.candidate,&c,sizeof(c))?Result::Ok:Result::Rejected;goto done;}
  if((s.flags&ACTIVE)&&eq(s.active.generation,c.generation,16)) {r=eq(&s.active,&c,sizeof(c))?Result::Ok:Result::Rejected;goto done;}
  memcpy(&s.candidate,&c,sizeof(c));s.flags|=CANDIDATE|MANAGED;r=write_snapshot(s,slot,ready);
done: wipe(&c,sizeof(c));wipe(&s,sizeof(s));return r;
}
Result save_identity(const Identity &identity,bool replace_existing,const voice_pair::EntropyReady &ready) {
  Snapshot s{}; int slot=-1; Result r=read_latest(s,slot,ready);
  if(r!=Result::Ok&&r!=Result::Empty) goto done;
  if(!nz(identity.bridge,16)||!nz(identity.device,16)||!keypair(identity.private_key,identity.public_key,ready)) {r=entropy_failed?Result::StorageError:Result::Rejected;goto done;}
  if(s.flags&IDENTITY) {
    if(eq(&s.identity,&identity,sizeof(s.identity))) {r=Result::Ok;goto done;}
    if(!replace_existing || ((s.flags&TOMBSTONE)&&!(s.flags&CLEAR_ACKED)) || (s.flags&CANDIDATE) || !eq(s.identity.bridge,identity.bridge,16) || !eq(s.identity.device,identity.device,16)) {r=Result::Rejected;goto done;}
  }
  memcpy(&s.identity,&identity,sizeof(s.identity));s.flags|=IDENTITY;r=write_snapshot(s,slot,ready);
done:wipe(&s,sizeof(s));return r;
}
Result discard_candidate(const uint8_t generation[16],const voice_pair::EntropyReady &ready) {
  if(!generation||!nz(generation,16)) return Result::Rejected;
  Snapshot s{};int slot=-1;Result r=read_latest(s,slot,ready);if(r!=Result::Ok) goto done;
  if(!(s.flags&CANDIDATE)) {r=Result::Ok;goto done;}
  if(!eq(s.candidate.generation,generation,16)) {r=Result::Rejected;goto done;}
  if(s.recovery_count>=kMaxRecoveryBindings){r=Result::Rejected;goto done;}
  to_recovery(s.candidate,s.recovery[s.recovery_count++]);wipe(&s.candidate,sizeof(s.candidate));s.flags=(s.flags&~CANDIDATE)|RECOVERY;r=write_snapshot(s,slot,ready);
done:wipe(&s,sizeof(s));return r;
}
Result promote(const uint8_t generation[16],const voice_pair::EntropyReady &ready) {
  if(!generation||!nz(generation,16)) return Result::Rejected;
  Snapshot s{};int slot=-1;Result r=read_latest(s,slot,ready);if(r!=Result::Ok) goto done;
  if(!(s.flags&CANDIDATE)) {r=(s.flags&ACTIVE)&&eq(s.active.generation,generation,16)?Result::Ok:Result::Rejected;goto done;}
  if(!eq(s.candidate.generation,generation,16)) {r=Result::Rejected;goto done;}
  memcpy(&s.active,&s.candidate,sizeof(s.active));wipe(&s.candidate,sizeof(s.candidate));wipe(s.recovery,sizeof(s.recovery));s.recovery_count=0;s.flags=(s.flags|ACTIVE)&~(CANDIDATE|RECOVERY);r=write_snapshot(s,slot,ready);
done:wipe(&s,sizeof(s));return r;
}
Result revoke(const uint8_t generation[16],uint64_t epoch,const uint8_t hash[32],const voice_pair::EntropyReady &ready) {
  if(!generation||!hash||!nz(generation,16)||!nz(hash,32)||!epoch) return Result::Rejected;
  Snapshot s{};int slot=-1;Result r=read_latest(s,slot,ready);if(r!=Result::Ok)goto done;
  if(epoch==get64(s.epoch)&&(s.flags&TOMBSTONE)) {r=!(s.flags&ACTIVE)&&eq(generation,s.revoked_generation,16)&&eq(hash,s.command_hash,32)?Result::Ok:Result::Rejected;goto done;}
  if(epoch<=get64(s.epoch)) {r=Result::Rejected;goto done;}
  if((s.flags&CANDIDATE)&&eq(generation,s.candidate.generation,16)) memcpy(&s.active,&s.candidate,sizeof(s.active));
  else if(!(s.flags&ACTIVE)||!eq(generation,s.active.generation,16)) {
    bool found=false;for(size_t i=0;i<s.recovery_count;++i)if(eq(generation,s.recovery[i].generation,16)){from_recovery(s.recovery[i],s.active);found=true;break;}
    if(!found){r=Result::Rejected;goto done;}
  }
  wipe(s.recovery,sizeof(s.recovery));s.recovery_count=0;put64(s.epoch,epoch);put64(s.active.epoch,epoch);memcpy(s.revoked_generation,generation,16);memcpy(s.command_hash,hash,32);s.flags=(s.flags|TOMBSTONE)&~(ACTIVE|CANDIDATE|CLEAR_ACKED);wipe(&s.candidate,sizeof(s.candidate));wipe(s.active.wss,sizeof(s.active.wss));wipe(s.active.token,sizeof(s.active.token));put16(s.active.wss_len,0);put16(s.active.token_len,0);r=write_snapshot(s,slot,ready);
done:wipe(&s,sizeof(s));return r;
}
Result retire_for_reset() {
  nvs_handle_t h;if(nvs_open(ns,NVS_READWRITE,&h)!=ESP_OK){faulted=true;return Result::StorageError;}
  esp_err_t e=nvs_set_blob(h,"reset",reset_marker,sizeof(reset_marker));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
  if(e!=ESP_OK){faulted=true;return Result::StorageError;}
  if(nvs_open(ns,NVS_READONLY,&h)!=ESP_OK){faulted=true;return Result::StorageError;}
  uint8_t actual[sizeof(reset_marker)]={};size_t n=sizeof(actual);e=nvs_get_blob(h,"reset",actual,&n);nvs_close(h);
  bool ok=e==ESP_OK&&n==sizeof(actual)&&eq(actual,reset_marker,sizeof(actual));wipe(actual,sizeof(actual));
  if(!ok){faulted=true;return Result::StorageError;}
  if(nvs_open(ns,NVS_READWRITE,&h)!=ESP_OK){faulted=true;return Result::StorageError;}
  for(int i=0;i<2&&e==ESP_OK;++i){e=nvs_erase_key(h,keys[i]);if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;}
  if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);if(e!=ESP_OK){faulted=true;return Result::StorageError;}
  if(nvs_open(ns,NVS_READONLY,&h)!=ESP_OK){faulted=true;return Result::StorageError;}
  for(int i=0;i<2&&ok;++i){n=0;ok=nvs_get_blob(h,keys[i],nullptr,&n)==ESP_ERR_NVS_NOT_FOUND;}nvs_close(h);
  if(!ok){faulted=true;return Result::StorageError;}return Result::Ok;
}

Result finish_reset() {
  nvs_handle_t h;if(nvs_open(ns,NVS_READONLY,&h)!=ESP_OK)return Result::StorageError;
  uint8_t marker[sizeof(reset_marker)]={};size_t n=sizeof(marker);esp_err_t e=nvs_get_blob(h,"reset",marker,&n);
  bool ok=e==ESP_OK&&n==sizeof(marker)&&eq(marker,reset_marker,sizeof(marker));wipe(marker,sizeof(marker));
  for(int i=0;i<2&&ok;++i){n=0;ok=nvs_get_blob(h,keys[i],nullptr,&n)==ESP_ERR_NVS_NOT_FOUND;}nvs_close(h);
  if(!ok)return Result::Rejected;
  e=nvs_open("voice_push",NVS_READONLY,&h);
  if(e==ESP_OK){n=0;e=nvs_get_blob(h,"config",nullptr,&n);nvs_close(h);if(e!=ESP_ERR_NVS_NOT_FOUND)return Result::Rejected;}
  else if(e!=ESP_ERR_NVS_NOT_FOUND)return Result::StorageError;
  if(nvs_open(ns,NVS_READWRITE,&h)!=ESP_OK)return Result::StorageError;
  e=nvs_erase_key(h,"reset");if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);if(e!=ESP_OK){faulted=true;return Result::StorageError;}
  e=nvs_open(ns,NVS_READONLY,&h);if(e==ESP_ERR_NVS_NOT_FOUND)return Result::Ok;
  if(e!=ESP_OK){faulted=true;return Result::StorageError;}
  n=0;e=nvs_get_blob(h,"reset",nullptr,&n);nvs_close(h);if(e!=ESP_ERR_NVS_NOT_FOUND){faulted=true;return Result::StorageError;}return Result::Ok;
}

Result mark_clear_acknowledged(const uint8_t generation[16],uint64_t epoch,const uint8_t hash[32],const voice_pair::EntropyReady &ready) {
  if(!generation||!hash||!epoch)return Result::Rejected;
  Snapshot s{};int slot=-1;Result r=read_latest(s,slot,ready);if(r!=Result::Ok)goto done;
  if(!(s.flags&TOMBSTONE)||!eq(generation,s.revoked_generation,16)||epoch!=get64(s.epoch)||!eq(hash,s.command_hash,32)){r=Result::Rejected;goto done;}
  if(s.flags&CLEAR_ACKED){r=Result::Ok;goto done;}
  s.flags|=CLEAR_ACKED;r=write_snapshot(s,slot,ready);
done:wipe(&s,sizeof(s));return r;
}

}
#ifdef VOICE_PAIR_STORE_TEST
namespace voice_pair_store { void test_reboot() { faulted=false; } }
#endif
