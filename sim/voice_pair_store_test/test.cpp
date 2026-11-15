#include "voice_pair_store.h"
#include "voice_pair_protocol.h"
#include "voice_pair_runtime.h"
#include "voice_pair_signals.h"
#include "voice_pair_policy.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "esp_http_client.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace voice_pair_store;
void voice_pair_test_entropy_fail(bool);
using Blob=std::vector<unsigned char>;
static std::map<std::string,Blob> disk,pending,legacy_disk;
static int step=0,fail_step=0; static std::vector<std::string> erased; static bool fail_after_commit=false;
static bool fail_next_commit=false;
static bool fail() { ++step; return fail_step && step==fail_step; }
extern "C" esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h) { if(fail())return 9; if(!strcmp(ns,"voice_push")){if(mode==NVS_READONLY&&legacy_disk.empty())return ESP_ERR_NVS_NOT_FOUND;*h=2;return ESP_OK;} assert(!strcmp(ns,"voice_pair_v1")); if(mode==NVS_READONLY&&disk.empty()) return ESP_ERR_NVS_NOT_FOUND; *h=1;return ESP_OK; }
extern "C" esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *buf,size_t *n) { if(fail())return 9; auto &source=h==2?legacy_disk:disk; auto it=source.find(key); if(it==source.end())return ESP_ERR_NVS_NOT_FOUND; if(!buf){*n=it->second.size();return ESP_OK;} if(*n<it->second.size())return 9; *n=it->second.size();memcpy(buf,it->second.data(),*n);return ESP_OK; }
extern "C" esp_err_t nvs_set_blob(nvs_handle_t,const char *key,const void *buf,size_t n) { if(fail())return 9; pending[key]=Blob((const unsigned char *)buf,(const unsigned char *)buf+n);return ESP_OK; }
extern "C" esp_err_t nvs_erase_key(nvs_handle_t,const char *key) { if(fail())return 9; if(!disk.count(key))return ESP_ERR_NVS_NOT_FOUND; erased.push_back(key);return ESP_OK; }
extern "C" esp_err_t nvs_commit(nvs_handle_t) { bool f=fail();if(fail_next_commit){fail_next_commit=false;f=true;} if(!f||fail_after_commit) {for(auto &p:pending)disk[p.first]=p.second;for(const auto &key:erased)disk.erase(key);} pending.clear();erased.clear(); return f?9:ESP_OK; }
extern "C" void nvs_close(nvs_handle_t) { pending.clear();erased.clear(); }
struct mock_http_client {esp_http_client_config_t cfg;std::string url,body;};
static std::string challenge_response,deliver_response;static bool drop_ack=false,drop_clear_ack=false,expire_in_challenge=false,server_promoted=false;static std::atomic<bool> *cancel_in_challenge=nullptr;static VoicePairSignals *signals_in_challenge=nullptr;static int requests=0;static int64_t mock_time_us=1000*1000;
int64_t esp_timer_get_time(){return mock_time_us;}
bool wifi_is_connected(){return true;} bool wifi_pairing_time_is_valid(){return true;}
int esp_crt_bundle_attach(void *){return 0;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->method==HTTP_METHOD_POST&&c->crt_bundle_attach&&!c->skip_cert_common_name_check&&c->disable_auto_redirect&&c->timeout_ms<=2000);assert(!strncmp(c->url,"https://bridge.example:443/",27));auto *v=new mock_http_client{};v->cfg=*c;v->url=c->url;return v;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *name,const char *){assert(strcmp(name,"Authorization"));return ESP_OK;}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c,const char *body,int len){assert(len>0&&len<=2048);c->body.assign(body,len);return ESP_OK;}
esp_err_t esp_http_client_perform(esp_http_client_handle_t c){++requests;assert(c->body.find("\"bridge_id\"")!=std::string::npos);std::string response;
 if(c->url.find("/challenge")!=std::string::npos){if(c->body.find("\"op\":\"clear_ack\"")!=std::string::npos)assert(c->body.find("\"pre_signature\"")!=std::string::npos&&c->body.find("\"client_nonce\"")!=std::string::npos);response=challenge_response;if(cancel_in_challenge)cancel_in_challenge->store(true);if(signals_in_challenge){signals_in_challenge->cancel();signals_in_challenge->start();}if(expire_in_challenge)mock_time_us=121000LL*1000;}
 else {assert(c->body.find("\"signature\"")!=std::string::npos&&c->body.find("\"challenge\"")!=std::string::npos);if(c->body.find("\"op\":\"clear_ack\"")!=std::string::npos){if(drop_clear_ack)return 9;response="{\"status\":\"ok\"}";}else if(c->body.find("\"op\":\"ack\"")!=std::string::npos){server_promoted=true;if(drop_ack)return 9;response="{\"status\":\"ok\"}";}else response=deliver_response;}
 esp_http_client_event_t ev{HTTP_EVENT_ON_DATA,c->cfg.user_data,const_cast<char *>(response.data()),static_cast<int>(response.size())};return c->cfg.event_handler(&ev);
}
int esp_http_client_get_status_code(esp_http_client_handle_t){return 200;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){delete c;return ESP_OK;}
static void reboot(){pending.clear();erased.clear();step=0;fail_step=0;fail_after_commit=false;test_reboot();}
static Credential credential(unsigned char generation,uint64_t epoch) {
 Credential c{};c.bridge[0]=1;c.device[0]=2;c.private_key[31]=1;c.generation[0]=generation;c.epoch=epoch;
 const char *g="046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
 for(int i=0;i<65;++i){unsigned x; assert(sscanf(g+2*i,"%2x",&x)==1);c.public_key[i]=c.server_key[i]=static_cast<unsigned char>(x);}
 strcpy(c.wss,"wss://bridge.example:443");strcpy(c.https_origin,"https://bridge.example:443");uint8_t token[32]={};token[0]=1;assert(voice_pair::encode_base64url(token,32,c.token,sizeof(c.token)));return c;
}
static int test_rng(void *,unsigned char *out,size_t n){arc4random_buf(out,n);return 0;}
static voice_pair::Clear make_clear(const Credential &c,uint64_t epoch){
 voice_pair::Clear clear{};memcpy(clear.bridge,c.bridge,16);memcpy(clear.device,c.device,16);memcpy(clear.generation,c.generation,16);clear.epoch=epoch;clear.nonce[0]=9;assert(mbedtls_sha256(c.public_key,65,clear.device_key_hash,0)==0);
 uint8_t msg[200]={},hash[32];size_t pos=0;const char label[]="HomeDash voice server clear v1";auto add=[&](const void *p,size_t n){memcpy(msg+pos,p,n);pos+=n;};add(label,sizeof(label));msg[pos++]=1;msg[pos++]=1;add(clear.bridge,16);add(clear.device,16);add(clear.device_key_hash,32);add(clear.generation,16);for(int shift=56;shift>=0;shift-=8)msg[pos++]=static_cast<uint8_t>(epoch>>shift);add(clear.nonce,32);assert(mbedtls_sha256(msg,pos,hash,0)==0);
 mbedtls_ecp_group g;mbedtls_mpi d,r,s,half;mbedtls_ecp_group_init(&g);mbedtls_mpi_init(&d);mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);mbedtls_mpi_init(&half);assert(!mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1));assert(!mbedtls_mpi_lset(&d,1));assert(!mbedtls_ecdsa_sign_det_ext(&g,&r,&s,&d,hash,32,MBEDTLS_MD_SHA256,test_rng,nullptr));assert(!mbedtls_mpi_copy(&half,&g.N));assert(!mbedtls_mpi_shift_r(&half,1));if(mbedtls_mpi_cmp_mpi(&s,&half)>0)assert(!mbedtls_mpi_sub_mpi(&s,&g.N,&s));assert(!mbedtls_mpi_write_binary(&r,clear.signature,32));assert(!mbedtls_mpi_write_binary(&s,clear.signature+32,32));mbedtls_mpi_free(&half);mbedtls_mpi_free(&s);mbedtls_mpi_free(&r);mbedtls_mpi_free(&d);mbedtls_ecp_group_free(&g);return clear;
}
static const voice_pair::EntropyReady ready{true,true};
static State state(){State s{};assert(load(s,ready)==Result::Ok);return s;}
static Identity identity(const Credential &c) { Identity id{}; memcpy(id.bridge,c.bridge,16);memcpy(id.device,c.device,16);memcpy(id.private_key,c.private_key,32);memcpy(id.public_key,c.public_key,65);return id; }
static void baseline(const Credential &a) {disk.clear();reboot();assert(save_identity(identity(a),false,ready)==Result::Ok);{auto r=prepare(a,ready); if(r!=Result::Ok) fprintf(stderr,"prepare result %d\n",static_cast<int>(r)); assert(r==Result::Ok);}assert(promote(a.generation,ready)==Result::Ok);}
int main(){
 Credential a=credential(3,0),b=credential(4,0); uint8_t hash[32]={5};
 voice_pair::EntropyReady offline{false,true}; State unavailable{}; assert(load(unavailable,offline)==Result::StorageError && !unavailable.has_active);
 assert(voice_pair::valid_public_key(a.public_key,65)); assert(voice_pair::valid_public_key(a.server_key,65)); assert(voice_pair::valid_origin("https://bridge.example:443",26));
 disk.clear();reboot(); assert(prepare(a,ready)==Result::Rejected); assert(save_identity(identity(a),false,ready)==Result::Ok); State initial=state();assert(initial.has_identity&&!initial.has_active&&!initial.has_candidate&&!initial.managed_started); reboot();initial=state();assert(initial.has_identity&&!memcmp(initial.identity.public_key,a.public_key,65));
 disk.clear();reboot();assert(save_identity(identity(a),false,ready)==Result::Ok);assert(prepare(a,ready)==Result::Ok);assert(discard_candidate(a.generation,ready)==Result::Ok);reboot();initial=state();assert(initial.managed_started&&initial.recovery_required&&initial.has_identity&&!initial.has_candidate&&!initial.has_active);
 baseline(a);State s=state();assert(s.has_active&&!s.has_candidate);assert(prepare(b,ready)==Result::Ok);assert(prepare(b,ready)==Result::Ok);
 Identity waiting=identity(b); assert(voice_pair::generate_key(ready,waiting.private_key,waiting.public_key)); assert(save_identity(waiting,true,ready)==Result::Rejected);
 uint8_t wrong[16]={9};assert(discard_candidate(wrong,ready)==Result::Rejected);assert(discard_candidate(b.generation,ready)==Result::Ok);assert(discard_candidate(b.generation,ready)==Result::Ok);s=state();assert(s.has_active&&!s.has_candidate&&s.managed_started&&s.recovery_required);assert(s.recovery_count==1&&!s.recovery_bindings[0].token[0]);assert(prepare(b,ready)==Result::Rejected);Credential fresh=credential(6,0);assert(prepare(fresh,ready)==Result::Ok);assert(promote(fresh.generation,ready)==Result::Ok);s=state();assert(!s.recovery_count&&!s.recovery_required);baseline(a);assert(prepare(b,ready)==Result::Ok);s=state();assert(s.has_active&&s.has_candidate&&s.active.generation[0]==3);assert(promote(b.generation,ready)==Result::Ok);assert(promote(b.generation,ready)==Result::Ok);s=state();assert(s.active.generation[0]==4&&!s.has_candidate&&!s.recovery_required);
 Identity replacement=identity(b); assert(voice_pair::generate_key(ready,replacement.private_key,replacement.public_key)); assert(save_identity(replacement,false,ready)==Result::Rejected); assert(save_identity(replacement,true,ready)==Result::Ok); s=state();assert(s.has_active && !memcmp(s.active.public_key,b.public_key,65) && !memcmp(s.identity.public_key,replacement.public_key,65)); assert(save_identity(identity(b),true,ready)==Result::Ok);
 assert(revoke(b.generation,1,hash,ready)==Result::Ok);assert(revoke(b.generation,1,hash,ready)==Result::Ok);s=state();assert(!s.has_active&&s.has_tombstone&&s.epoch==1&&!s.active.wss[0]&&!s.active.token[0]);assert(!memcmp(s.active.public_key,b.public_key,65));assert(!strcmp(s.active.https_origin,"https://bridge.example:443"));
 uint8_t badhash[32]={6};assert(revoke(b.generation,1,badhash,ready)==Result::Rejected);assert(prepare(b,ready)==Result::Rejected);Credential c=credential(5,1);assert(prepare(c,ready)==Result::Rejected);assert(mark_clear_acknowledged(b.generation,1,badhash,ready)==Result::Rejected);assert(mark_clear_acknowledged(b.generation,1,hash,ready)==Result::Ok);assert(mark_clear_acknowledged(b.generation,1,hash,ready)==Result::Ok);reboot();s=state();assert(s.clear_acknowledged);assert(prepare(c,ready)==Result::Ok);assert(promote(c.generation,ready)==Result::Ok);assert(revoke(b.generation,1,hash,ready)==Result::Rejected);assert(revoke(c.generation,1,hash,ready)==Result::Rejected);
 Credential invalid=a;invalid.public_key[2]^=1;assert(prepare(invalid,ready)==Result::Rejected); invalid=credential(6,1);invalid.device[0]=9;assert(prepare(invalid,ready)==Result::Rejected);
 // Every read/write/commit fault; after reboot each atomic NVS write has
 // either left the old snapshot intact or installed the new one.
 int counts[6];
 for(int phase=0;phase<6;++phase) {
   if(phase==3) {disk.clear();reboot();} else {baseline(a); if(phase==1||phase==5) assert(prepare(b,ready)==Result::Ok); reboot();}
   Result r=phase==0?prepare(b,ready):phase==1?promote(b.generation,ready):phase==2?revoke(a.generation,1,hash,ready):phase==3?save_identity(identity(a),false,ready):phase==4?save_identity(replacement,true,ready):discard_candidate(b.generation,ready);
   assert(r==Result::Ok); counts[phase]=step;
 }
 for(int phase=0;phase<6;++phase) for(int after=0;after<2;++after) for(int fault=1;fault<=counts[phase];++fault) {
   if(phase==3){disk.clear();reboot();}else{baseline(a);if(phase==1||phase==5)assert(prepare(b,ready)==Result::Ok);reboot();}auto initial=disk;fail_step=fault;fail_after_commit=after;
   Result r=phase==0?prepare(b,ready):phase==1?promote(b.generation,ready):phase==2?revoke(a.generation,1,hash,ready):phase==3?save_identity(identity(a),false,ready):phase==4?save_identity(replacement,true,ready):discard_candidate(b.generation,ready);
   if(r==Result::StorageError || r==Result::Corrupt) { State blocked{}; assert(load(blocked,ready)==Result::StorageError); assert(!blocked.has_active); }
   reboot(); if(phase==3 && disk.empty()) { State empty{}; assert(load(empty,ready)==Result::Empty); continue; } s=state();
   if(phase==0) {assert(s.has_active&&s.active.generation[0]==3);if(s.has_candidate)assert(s.candidate.generation[0]==4);}
   if(phase==1) {assert(s.has_active);assert((s.active.generation[0]==3&&s.has_candidate)||(s.active.generation[0]==4&&!s.has_candidate));}
   if(phase==3) { assert(s.has_identity && !s.has_active && !s.has_candidate); }
   if(phase==5) {assert(s.has_active&&s.active.generation[0]==3);assert((s.has_candidate&&!s.recovery_required)||(!s.has_candidate&&s.recovery_required));}
   if(phase==4) {assert(s.has_identity&&s.has_active&&!memcmp(s.active.public_key,a.public_key,65)); assert(!memcmp(s.identity.public_key,a.public_key,65)||!memcmp(s.identity.public_key,replacement.public_key,65));}
   if(phase==2) {assert((s.has_active&&s.active.generation[0]==3&&!s.has_tombstone)||(!s.has_active&&s.has_tombstone&&s.epoch==1&&!s.active.token[0])); if(s.has_tombstone) assert(disk.size()==1);}
   if(phase!=3) assert(!initial.empty());
 }
 baseline(a);assert(prepare(b,ready)==Result::Ok);auto saved=disk;disk.begin()->second[0]^=1;reboot();State corrupted{};assert(load(corrupted,ready)==Result::Corrupt);assert(!corrupted.has_active);reboot();disk=saved;disk.begin()->second.pop_back();assert(load(corrupted,ready)==Result::Corrupt);
 // Recompute the unkeyed checksum to exercise structural checks separately.
 reboot();disk=saved; auto &malformed=disk.begin()->second; malformed[290]=0xff; malformed[291]=0xff; assert(mbedtls_sha256(malformed.data(),malformed.size()-32,malformed.data()+malformed.size()-32,0)==0); assert(load(corrupted,ready)==Result::Corrupt);
 // Strict bounded JSON: duplicate/unknown fields, escaped strings,
 // noncanonical numbers/base64, trailing bytes and mismatched origin fail.
 uint8_t challenge[32]={},raw[32]={1};char b64[44];assert(voice_pair::encode_base64url(raw,32,b64,sizeof(b64)));char json[1024];snprintf(json,sizeof(json),"{\"challenge\":\"%s\",\"expires_in\":30}",b64);assert(voice_pair_runtime::parse_challenge(json,strlen(json),challenge));
 snprintf(json,sizeof(json),"{\"challenge\":\"%s\",\"challenge\":\"%s\",\"expires_in\":30}",b64,b64);assert(!voice_pair_runtime::parse_challenge(json,strlen(json),challenge));assert(!voice_pair_runtime::parse_ack("{\"status\":\"ok\"}x",16));assert(!voice_pair_runtime::parse_ack("{\"status\":\"o\\u006b\"}",21));assert(voice_pair_runtime::parse_ack("{\"status\":\"ok\"}",15));
 char key64[88];assert(voice_pair::encode_base64url(a.server_key,65,key64,sizeof(key64)));snprintf(json,sizeof(json),"{\"generation\":\"03000000000000000000000000000000\",\"wss_base\":\"wss://bridge.example:443\",\"token\":\"%s\",\"server_signing_key\":\"%s\"}",b64,key64);voice_pair_runtime::Delivery delivery{};assert(voice_pair_runtime::parse_delivery(json,strlen(json),"https://bridge.example:443",26,delivery));assert(!voice_pair_runtime::parse_delivery(json,strlen(json),"https://other.example:443",25,delivery));
 uint8_t signature[64]={};signature[0]=1;signature[32]=1;char sig64[87];assert(voice_pair::encode_base64url(signature,64,sig64,sizeof(sig64)));snprintf(json,sizeof(json),"{\"version\":1,\"bridge_id\":\"01000000000000000000000000000000\",\"device_id\":\"02000000000000000000000000000000\",\"device_key_hash\":\"%s\",\"generation\":\"03000000000000000000000000000000\",\"revocation_epoch\":\"18446744073709551615\",\"nonce\":\"%s\",\"signature\":\"%s\"}",b64,b64,sig64);voice_pair::Clear clear{};assert(voice_pair_runtime::parse_clear(json,strlen(json),clear)&&clear.epoch==UINT64_MAX);char *epoch=strstr(json,"18446744073709551615");assert(epoch);memcpy(epoch,"18446744073709551616",20);assert(!voice_pair_runtime::parse_clear(json,strlen(json),clear));
 // Entropy can disappear after a ready snapshot: do not misclassify a
 // healthy journal as corrupt or permanently poison subsequent reads.
 baseline(a);voice_pair_test_entropy_fail(true);State entropy_state{};assert(load(entropy_state,ready)==Result::StorageError);voice_pair_test_entropy_fail(false);assert(load(entropy_state,ready)==Result::Ok&&entropy_state.has_active);
 // Coordinator cannot emit a code until identity has been committed/read back;
 // cancel, exact expiry, backwards monotonic time and origin changes wipe it.
 disk.clear(); reboot(); voice_pair_runtime::Session session; const char *origin="https://bridge.example:443"; char code[200];
 assert(session.begin(a.bridge,a.device,origin,strlen(origin),1000,ready)); State paired=state(); assert(paired.has_identity&&!paired.has_active); voice_pair::QR qr{}; assert(session.copy_qr(code,sizeof(code),origin,strlen(origin),1000,ready)); assert(voice_pair::decode_qr(code,199,&qr) && voice_pair::qr_matches_origin(qr,origin,strlen(origin)));
 assert(session.tick(origin,strlen(origin),120999,ready)==voice_pair_runtime::Status::AwaitingApproval); assert(session.tick(origin,strlen(origin),121000,ready)==voice_pair_runtime::Status::Expired); assert(!session.copy_qr(code,sizeof(code),origin,strlen(origin),121000,ready)&&!code[0]);
 assert(session.begin(a.bridge,a.device,origin,strlen(origin),5000,ready)); assert(session.tick("https://evil.example:443",24,5001,ready)==voice_pair_runtime::Status::Cancelled); assert(!session.copy_qr(code,sizeof(code),origin,strlen(origin),5001,ready));
 assert(session.begin(a.bridge,a.device,origin,strlen(origin),5000,ready)); session.cancel();assert(!session.copy_qr(code,sizeof(code),origin,strlen(origin),5001,ready));
 assert(session.begin(a.bridge,a.device,origin,strlen(origin),5000,ready)); assert(session.tick(origin,strlen(origin),4999,ready)==voice_pair_runtime::Status::Expired);
 assert(!session.begin(a.bridge,a.device,origin,strlen(origin),1,offline));
 // Reboot with an orphan candidate: no QR while WSS might be active;
 // explicit physical retry after stopping WSS preserves old record while
 // durably prohibiting its use until a fresh promotion succeeds.
 baseline(a);assert(prepare(b,ready)==Result::Ok);reboot();voice_pair_runtime::Session recovery;assert(!recovery.begin(a.bridge,a.device,origin,strlen(origin),1,ready));assert(recovery.begin(a.bridge,a.device,origin,strlen(origin),1,ready,true));s=state();assert(s.recovery_required&&s.has_active&&!s.has_candidate);assert(s.active.generation[0]==3);reboot();s=state();assert(s.recovery_required&&s.has_active);recovery.cancel();
 // Mocked verified HTTPS performs delivery, stages candidate, handles a lost
 // ACK response without promoting, then retries the idempotent ACK.
 disk.clear();reboot();voice_pair_runtime::Session transport;mock_time_us=1000*1000;assert(transport.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));snprintf(json,sizeof(json),"{\"challenge\":\"%s\",\"expires_in\":30}",b64);challenge_response=json;snprintf(json,sizeof(json),"{\"generation\":\"03000000000000000000000000000000\",\"wss_base\":\"wss://bridge.example:443\",\"token\":\"%s\",\"server_signing_key\":\"%s\"}",b64,key64);deliver_response=json;std::atomic<bool> cancelled{false};requests=0;assert(transport.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::AwaitingAck);assert(requests==2);paired=state();assert(paired.has_candidate&&!paired.has_active&&paired.managed_started);
 drop_ack=true;assert(transport.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::AwaitingAck);paired=state();assert(paired.has_candidate&&!paired.has_active);drop_ack=false;assert(transport.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::Complete);paired=state();assert(paired.has_active&&!paired.has_candidate);
 disk.clear();reboot();voice_pair_runtime::Session cancelled_session;mock_time_us=1000*1000;assert(cancelled_session.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));cancelled=false;cancel_in_challenge=&cancelled;requests=0;assert(cancelled_session.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::Cancelled);assert(requests==1);cancel_in_challenge=nullptr;
 disk.clear();reboot();voice_pair_runtime::Session expired_session;mock_time_us=1000*1000;assert(expired_session.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));cancelled=false;expire_in_challenge=true;requests=0;assert(expired_session.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::Expired);assert(requests==1);expire_in_challenge=false;
 // ACK-lost candidate is retained without its token across physical retry.
 // A newly staged candidate must not erase the old generation's clear key.
 baseline(a);assert(prepare(b,ready)==Result::Ok);assert(discard_candidate(b.generation,ready)==Result::Ok);reboot();s=state();assert(s.recovery_count==1&&!s.recovery_bindings[0].token[0]&&!s.recovery_bindings[0].wss[0]);Credential newer=credential(8,0);assert(prepare(newer,ready)==Result::Ok);reboot();s=state();assert(s.has_candidate&&s.recovery_count==1);auto old_clear=make_clear(b,1);assert(voice_pair_runtime::verify_clear_command(old_clear,ready));assert(voice_pair_runtime::persist_clear(old_clear,true,ready));reboot();s=state();assert(s.has_tombstone&&!s.has_active&&!s.has_candidate&&!s.recovery_count&&s.recovery_required&&s.active.generation[0]==4);
 // Identical signed clear remains idempotent even with recovery_required set.
 assert(voice_pair_runtime::verify_clear_command(old_clear,ready));assert(voice_pair_runtime::persist_clear(old_clear,true,ready));reboot();s=state();assert(s.has_tombstone&&s.recovery_required&&s.active.generation[0]==4);
 auto changed_clear=old_clear;changed_clear.nonce[0]^=1;assert(!voice_pair_runtime::verify_clear_command(changed_clear,ready));
 // Multiple ambiguous retries remain bounded: never replace an older binding
 // when capacity is full; its later signed clear remains verifiable.
 baseline(a);for(unsigned char g=10;g<14;++g){Credential retry_binding=credential(g,0);assert(prepare(retry_binding,ready)==Result::Ok);assert(discard_candidate(retry_binding.generation,ready)==Result::Ok);}s=state();assert(s.recovery_count==kMaxRecoveryBindings&&s.recovery_required);Credential overflow=credential(14,0);assert(prepare(overflow,ready)==Result::Ok);assert(discard_candidate(overflow.generation,ready)==Result::Rejected);reboot();s=state();assert(s.has_candidate&&s.recovery_count==kMaxRecoveryBindings);Credential oldest=credential(10,0);auto oldest_clear=make_clear(oldest,1);assert(voice_pair_runtime::verify_clear_command(oldest_clear,ready));assert(voice_pair_runtime::persist_clear(oldest_clear,true,ready));s=state();assert(s.has_tombstone&&!s.has_candidate&&!s.recovery_count);
 // A lost server ACK can leave the newly active server generation in the
 // local candidate slot. Its signed clear must revoke both local credentials.
 baseline(a);assert(prepare(b,ready)==Result::Ok);auto candidate_clear=make_clear(b,1);assert(voice_pair_runtime::verify_clear_command(candidate_clear,ready));assert(!voice_pair_runtime::persist_clear(candidate_clear,false,ready));assert(voice_pair_runtime::persist_clear(candidate_clear,true,ready));s=state();assert(!s.has_active&&!s.has_candidate&&s.has_tombstone&&s.active.generation[0]==4&&!s.active.token[0]);reboot();s=state();assert(s.has_tombstone&&s.active.generation[0]==4);
 baseline(a);auto signed_clear=make_clear(a,1);assert(voice_pair_runtime::verify_clear_command(signed_clear,ready));assert(!voice_pair_runtime::persist_clear(signed_clear,false,ready));assert(state().has_active);assert(voice_pair_runtime::persist_clear(signed_clear,true,ready));assert(!state().has_active);assert(voice_pair_runtime::acknowledge_clear(false,ready)==voice_pair_runtime::ClearAckResult::Pending);drop_clear_ack=true;assert(voice_pair_runtime::acknowledge_clear(true,ready)==voice_pair_runtime::ClearAckResult::Pending);reboot();assert(!state().clear_acknowledged);drop_clear_ack=false;assert(voice_pair_runtime::acknowledge_clear(true,ready)==voice_pair_runtime::ClearAckResult::Done);reboot();assert(state().clear_acknowledged);assert(voice_pair_runtime::acknowledge_clear(true,ready)==voice_pair_runtime::ClearAckResult::Done);
 // Simulate server promoting the candidate but its ACK response being lost.
 // Reboot leaves the candidate and blocks access; physical recovery stops WSS,
 // records recovery_required, uses a fresh QR, and only a fresh ACK promotes.
 baseline(a);reboot();voice_pair_runtime::Session first;mock_time_us=1000*1000;assert(first.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));snprintf(json,sizeof(json),"{\"generation\":\"04000000000000000000000000000000\",\"wss_base\":\"wss://bridge.example:443\",\"token\":\"%s\",\"server_signing_key\":\"%s\"}",b64,key64);deliver_response=json;cancelled=false;assert(first.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::AwaitingAck);drop_ack=true;server_promoted=false;assert(first.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::AwaitingAck);assert(server_promoted);drop_ack=false;reboot();s=state();assert(s.has_candidate&&s.has_active);voice_pair_runtime::Session retry;assert(!retry.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));assert(retry.begin(a.bridge,a.device,origin,strlen(origin),1000,ready,true));s=state();assert(s.recovery_required&&s.has_active&&!s.has_candidate);snprintf(json,sizeof(json),"{\"generation\":\"05000000000000000000000000000000\",\"wss_base\":\"wss://bridge.example:443\",\"token\":\"%s\",\"server_signing_key\":\"%s\"}",b64,key64);deliver_response=json;assert(retry.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::AwaitingAck);assert(retry.poll(origin,strlen(origin),cancelled)==voice_pair_runtime::Status::Complete);reboot();s=state();assert(s.has_active&&s.active.generation[0]==5&&!s.recovery_required&&!s.has_candidate);
 baseline(a);reboot();assert(retire_for_reset()==Result::Ok);int reset_steps=step;reboot();State reset_state{};assert(load(reset_state,offline)==Result::Reset);assert(disk.size()==1&&disk.count("reset"));
 for(int after=0;after<2;++after)for(int fault=1;fault<=reset_steps;++fault){baseline(a);reboot();fail_step=fault;fail_after_commit=after;Result rr=retire_for_reset();(void)rr;reboot();if(disk.count("reset")){assert(load(reset_state,offline)==Result::Reset);assert(retire_for_reset()==Result::Ok);reboot();assert(load(reset_state,offline)==Result::Reset);assert(disk.size()==1&&disk.count("reset"));}else {s=state();assert(s.has_active&&s.active.generation[0]==3);}}
 // The fence stays until both v2 slots AND legacy credential are absent.
 baseline(a);reboot();legacy_disk["config"]={1,2,3};assert(retire_for_reset()==Result::Ok);assert(finish_reset()==Result::Rejected);assert(disk.count("reset")&&legacy_disk.count("config"));legacy_disk.clear();reboot();assert(finish_reset()==Result::Ok);reboot();State erased_state{};assert(load(erased_state,ready)==Result::Empty);
 baseline(a);reboot();assert(retire_for_reset()==Result::Ok);legacy_disk.clear();reboot();assert(finish_reset()==Result::Ok);int finish_steps=step;
 for(int after=0;after<2;++after)for(int fault=1;fault<=finish_steps;++fault){baseline(a);reboot();assert(retire_for_reset()==Result::Ok);legacy_disk.clear();reboot();fail_step=fault;fail_after_commit=after;Result fr=finish_reset();(void)fr;reboot();if(disk.count("reset")){assert(load(erased_state,offline)==Result::Reset);assert(finish_reset()==Result::Ok);}else{assert(!disk.count("slot0")&&!disk.count("slot1")&&!legacy_disk.count("config"));}reboot();assert(load(erased_state,ready)==Result::Empty);}
 // Cancel then Start during the old HTTP response cannot clear cancellation.
 disk.clear();reboot();voice_pair_runtime::Session race;mock_time_us=1000*1000;assert(race.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));VoicePairSignals signals;signals_in_challenge=&signals;requests=0;assert(race.poll(origin,strlen(origin),signals.cancel_flag())==voice_pair_runtime::Status::Cancelled);assert(requests==1&&!signals.should_hide_cancelled());assert(signals.take_cancel()&&!signals.should_hide_cancelled());assert(signals.take_start()&&signals.should_hide_cancelled());signals_in_challenge=nullptr;
 // A committed candidate with an error result must stop cached access even
 // when re-pairing started with a healthy active credential.
 baseline(a);reboot();voice_pair_runtime::Session faulted_pair;mock_time_us=1000*1000;
 assert(faulted_pair.begin(a.bridge,a.device,origin,strlen(origin),1000,ready));
 snprintf(json,sizeof(json),"{\"generation\":\"04000000000000000000000000000000\",\"wss_base\":\"wss://bridge.example:443\",\"token\":\"%s\",\"server_signing_key\":\"%s\"}",b64,key64);deliver_response=json;
 fail_next_commit=true;fail_after_commit=true;cancelled=false;
 auto before_failure=faulted_pair.status();auto after_failure=faulted_pair.poll(origin,strlen(origin),cancelled);
 assert(before_failure==voice_pair_runtime::Status::AwaitingApproval&&after_failure==voice_pair_runtime::Status::Failed);
 assert(voice_pair_policy::stop_after_poll(before_failure,after_failure));
 State unresolved{};assert(load(unresolved,ready)==Result::StorageError);reboot();s=state();assert(s.has_active&&s.has_candidate);
 // Legacy is only inspected by finish_reset; it is never changed here.
 return 0;
}
