#include "voice_pair_runtime.h"
#include <cstring>
#include <memory>
#include <new>
namespace voice_pair_runtime {
namespace {
struct Field { char key[32]; char value[256]; bool number; };
struct Object { Field fields[8]; size_t count; };
static void spaces(const char *s,size_t n,size_t &i){while(i<n&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;}
static bool string(const char *s,size_t n,size_t &i,char *out,size_t cap){
  if(i>=n||s[i++]!='"')return false;size_t used=0;
  while(i<n&&s[i]!='"'){unsigned char c=s[i++];if(c<0x20||c>=0x7f||c=='\\'||used+1>=cap)return false;out[used++]=static_cast<char>(c);}
  if(i>=n||s[i++]!='"')return false;out[used]=0;return true;
}
static bool object(const char *s,size_t n,Object &o){
  if(!s||n==0||n>2048)return false;size_t i=0;spaces(s,n,i);if(i>=n||s[i++]!='{')return false;spaces(s,n,i);
  if(i<n&&s[i]=='}')return false;
  while(i<n){if(o.count>=8)return false;Field &f=o.fields[o.count];if(!string(s,n,i,f.key,sizeof(f.key)))return false;for(size_t j=0;j<o.count;++j)if(!strcmp(o.fields[j].key,f.key))return false;
    spaces(s,n,i);if(i>=n||s[i++]!=':')return false;spaces(s,n,i);
    if(i<n&&s[i]=='"'){if(!string(s,n,i,f.value,sizeof(f.value)))return false;}
    else {size_t start=i;while(i<n&&s[i]>='0'&&s[i]<='9')++i;size_t k=i-start;if(!k||k>=sizeof(f.value))return false;memcpy(f.value,s+start,k);f.value[k]=0;f.number=true;}
    ++o.count;spaces(s,n,i);if(i>=n)return false;if(s[i]=='}'){++i;spaces(s,n,i);return i==n;}if(s[i++]!=',')return false;spaces(s,n,i);
  }return false;
}
static const Field *find(const Object &o,const char *key){for(size_t i=0;i<o.count;++i)if(!strcmp(o.fields[i].key,key))return &o.fields[i];return nullptr;}
static bool nz(const uint8_t *p,size_t n){uint8_t x=0;for(size_t i=0;i<n;++i)x|=p[i];return x!=0;}
}
bool parse_challenge(const char *json,size_t len,uint8_t out[32]){
  if(!out)return false;voice_pair::wipe(out,32);Object o{};bool ok=object(json,len,o)&&o.count==2;
  const Field *c=find(o,"challenge"),*e=find(o,"expires_in");
  ok=ok&&c&&e&&!c->number&&e->number&&!strcmp(e->value,"30")&&voice_pair::decode_base64url(c->value,strlen(c->value),out,32)&&nz(out,32);
  voice_pair::wipe(&o,sizeof(o));if(!ok)voice_pair::wipe(out,32);return ok;
}
bool parse_delivery(const char *json,size_t len,const char *origin,size_t origin_len,Delivery &out){
  voice_pair::wipe(&out,sizeof(out));Object o{};bool ok=voice_pair::valid_origin(origin,origin_len)&&object(json,len,o)&&o.count==4;
  const Field *g=find(o,"generation"),*w=find(o,"wss_base"),*t=find(o,"token"),*k=find(o,"server_signing_key");
  uint8_t token[32]={};char expected[160]={};
  if(ok&&g&&w&&t&&k&&!g->number&&!w->number&&!t->number&&!k->number&&origin_len>=8&&origin_len-2<sizeof(expected)){
    memcpy(expected,"wss://",6);memcpy(expected+6,origin+8,origin_len-8);expected[origin_len-2]=0;
    ok=!strcmp(w->value,expected)&&strlen(w->value)<sizeof(out.wss)&&strlen(t->value)<sizeof(out.token)&&voice_pair::decode_id(g->value,strlen(g->value),out.generation)&&voice_pair::decode_base64url(t->value,strlen(t->value),token,32)&&nz(token,32)&&voice_pair::decode_base64url(k->value,strlen(k->value),out.server_key,65)&&voice_pair::valid_public_key(out.server_key,65);
    if(ok){strcpy(out.wss,w->value);strcpy(out.token,t->value);}
  }else ok=false;
  voice_pair::wipe(token,sizeof(token));voice_pair::wipe(expected,sizeof(expected));voice_pair::wipe(&o,sizeof(o));if(!ok)voice_pair::wipe(&out,sizeof(out));return ok;
}
bool parse_ack(const char *json,size_t len){Object o{};bool ok=object(json,len,o)&&o.count==1;const Field *s=find(o,"status");ok=ok&&s&&!s->number&&!strcmp(s->value,"ok");voice_pair::wipe(&o,sizeof(o));return ok;}
bool parse_clear(const char *json,size_t len,voice_pair::Clear &out){
  voice_pair::wipe(&out,sizeof(out));Object o{};bool ok=object(json,len,o)&&o.count==8;
  const Field *v=find(o,"version"),*b=find(o,"bridge_id"),*d=find(o,"device_id"),*h=find(o,"device_key_hash"),*g=find(o,"generation"),*e=find(o,"revocation_epoch"),*n=find(o,"nonce"),*sig=find(o,"signature");
  uint64_t epoch=0;
  if(ok&&v&&b&&d&&h&&g&&e&&n&&sig&&v->number&&!strcmp(v->value,"1")&&!b->number&&!d->number&&!h->number&&!g->number&&!e->number&&!n->number&&!sig->number&&e->value[0]>='1'&&e->value[0]<='9'){
    for(size_t i=0;e->value[i];++i){char c=e->value[i];if(c<'0'||c>'9'||epoch>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10){ok=false;break;}epoch=epoch*10+static_cast<unsigned>(c-'0');}
    ok=ok&&epoch&&voice_pair::decode_id(b->value,strlen(b->value),out.bridge)&&voice_pair::decode_id(d->value,strlen(d->value),out.device)&&voice_pair::decode_id(g->value,strlen(g->value),out.generation)&&voice_pair::decode_base64url(h->value,strlen(h->value),out.device_key_hash,32)&&voice_pair::decode_base64url(n->value,strlen(n->value),out.nonce,32)&&nz(out.nonce,32)&&voice_pair::decode_base64url(sig->value,strlen(sig->value),out.signature,64);out.epoch=epoch;
  }else ok=false;
  voice_pair::wipe(&o,sizeof(o));if(!ok)voice_pair::wipe(&out,sizeof(out));return ok;
}
bool verify_clear_command(const voice_pair::Clear &c,const voice_pair::EntropyReady &ready){
  auto state=std::unique_ptr<voice_pair_store::State>(new(std::nothrow) voice_pair_store::State{});if(!state)return false;
  bool ok=voice_pair_store::load(*state,ready)==voice_pair_store::Result::Ok;
  const voice_pair_store::Credential *target=nullptr;
  if(ok && state->has_candidate && !memcmp(c.generation,state->candidate.generation,16)) target=&state->candidate;
  else if(ok && (state->has_active||state->has_tombstone) && !memcmp(c.generation,state->active.generation,16)) target=&state->active;
  if(ok&&!target)for(size_t i=0;i<state->recovery_count;++i)if(!memcmp(c.generation,state->recovery_bindings[i].generation,16)){target=&state->recovery_bindings[i];break;}
  if(!target)ok=false;
  if(ok)ok=voice_pair::verify_clear(c,target->server_key,target->bridge,target->device,target->public_key,target->generation);
  if(ok){uint8_t hash[32]={};ok=voice_pair::clear_command_hash(c,hash);if(ok){
    if(!state->has_active&&state->has_tombstone&&target==&state->active) ok=c.epoch==state->epoch&&!memcmp(c.generation,state->revoked_generation,16)&&!memcmp(hash,state->command_hash,32);
    else ok=c.epoch>state->epoch;
  }voice_pair::wipe(hash,sizeof(hash));}
  voice_pair_store::wipe(state.get(),sizeof(*state));return ok;
}
bool persist_clear(const voice_pair::Clear &c,bool stopped,const voice_pair::EntropyReady &ready){
  if(!stopped||!verify_clear_command(c,ready))return false;
  uint8_t hash[32]={};bool ok=voice_pair::clear_command_hash(c,hash)&&voice_pair_store::revoke(c.generation,c.epoch,hash,ready)==voice_pair_store::Result::Ok;voice_pair::wipe(hash,sizeof(hash));return ok;
}
Session::Session():status_(Status::Idle),started_(0),origin_{},qr_{},origin_len_(0),nonce_{},bridge_{},device_{} {}
Session::~Session(){clear_secrets();}
void Session::clear_secrets(){voice_pair::wipe(nonce_,sizeof(nonce_));voice_pair::wipe(bridge_,sizeof(bridge_));voice_pair::wipe(device_,sizeof(device_));voice_pair::wipe(qr_,sizeof(qr_));voice_pair::wipe(origin_,sizeof(origin_));origin_len_=0;started_=0;}
bool Session::begin(const uint8_t bridge[16],const uint8_t device[16],const char *origin,size_t len,uint64_t now,const voice_pair::EntropyReady &ready,bool recovery_websocket_stopped){
  clear_secrets();status_=Status::Failed;
  if(!bridge||!device||!ready.wifi_connected||!ready.time_valid||!voice_pair::valid_origin(origin,len)||now>UINT64_MAX-120000) return false;
  std::unique_ptr<voice_pair_store::State> state(new(std::nothrow) voice_pair_store::State{});
  if(!state)return false;
  auto result=voice_pair_store::load(*state,ready);
  if(result!=voice_pair_store::Result::Ok&&result!=voice_pair_store::Result::Empty){voice_pair_store::wipe(state.get(),sizeof(*state));return false;}
  voice_pair_store::Identity id{};
  if(state->has_tombstone&&!state->clear_acknowledged){voice_pair_store::wipe(state.get(),sizeof(*state));return false;}
  if(state->has_candidate){
    if(!recovery_websocket_stopped||voice_pair_store::discard_candidate(state->candidate.generation,ready)!=voice_pair_store::Result::Ok){voice_pair_store::wipe(state.get(),sizeof(*state));return false;}
  }
  if(state->has_identity){
    if(memcmp(state->identity.bridge,bridge,16)||memcmp(state->identity.device,device,16)){voice_pair_store::wipe(state.get(),sizeof(*state));return false;}
    memcpy(&id,&state->identity,sizeof(id));
  }else{
    memcpy(id.bridge,bridge,16);memcpy(id.device,device,16);
    if(!voice_pair::generate_key(ready,id.private_key,id.public_key)||voice_pair_store::save_identity(id,false,ready)!=voice_pair_store::Result::Ok){voice_pair_store::wipe(&id,sizeof(id));voice_pair_store::wipe(state.get(),sizeof(*state));return false;}
  }
  voice_pair_store::wipe(state.get(),sizeof(*state));
  voice_pair::QR q{};memcpy(q.bridge,bridge,16);memcpy(q.device,device,16);memcpy(q.public_key,id.public_key,65);
  bool ok=voice_pair::random_secret(ready,nonce_);if(ok){memcpy(q.nonce,nonce_,32);ok=voice_pair::encode_qr(q,origin,len,qr_,sizeof(qr_));}
  voice_pair_store::wipe(&id,sizeof(id));voice_pair::wipe(&q,sizeof(q));
  if(!ok){clear_secrets();return false;}
  memcpy(origin_,origin,len);origin_[len]=0;memcpy(bridge_,bridge,16);memcpy(device_,device,16);origin_len_=len;started_=now;status_=Status::AwaitingApproval;return true;
}
Status Session::tick(const char *origin,size_t len,uint64_t now,const voice_pair::EntropyReady &ready){
  if(status_!=Status::AwaitingApproval&&status_!=Status::AwaitingAck)return status_;
  if(now<started_||now-started_>=120000){clear_secrets();status_=Status::Expired;}
  else if(!ready.wifi_connected||!ready.time_valid||!origin||len!=origin_len_||memcmp(origin_,origin,len)){clear_secrets();status_=Status::Cancelled;}
  return status_;
}
bool Session::copy_qr(char *out,size_t cap,const char *origin,size_t len,uint64_t now,const voice_pair::EntropyReady &ready){
  if(!out||cap<sizeof(qr_))return false;
  Status current=tick(origin,len,now,ready);if(current!=Status::AwaitingApproval&&current!=Status::AwaitingAck){out[0]=0;return false;}
  memcpy(out,qr_,sizeof(qr_));return true;
}
void Session::cancel(){clear_secrets();status_=Status::Cancelled;}
}
#ifdef ESP_PLATFORM
#include "wifi_manager.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <cstdio>
#include <inttypes.h>
namespace voice_pair_runtime {
namespace {
struct Response { char bytes[2049];size_t used;bool overflow; };
esp_err_t response_event(esp_http_client_event_t *ev) {
  Response *r=static_cast<Response *>(ev->user_data);
  if(ev->event_id==HTTP_EVENT_ON_DATA && ev->data_len>0){size_t n=static_cast<size_t>(ev->data_len);if(n>2048-r->used){r->overflow=true;return ESP_FAIL;}memcpy(r->bytes+r->used,ev->data,n);r->used+=n;r->bytes[r->used]=0;}
  return ESP_OK;
}
bool post(const char *origin,const char *path,const char *body,Response &response){
  char endpoint[256];int n=snprintf(endpoint,sizeof(endpoint),"%s%s",origin,path);if(n<=0||static_cast<size_t>(n)>=sizeof(endpoint)||strlen(body)>2048)return false;
  esp_http_client_config_t cfg{};cfg.url=endpoint;cfg.method=HTTP_METHOD_POST;cfg.timeout_ms=2000;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.skip_cert_common_name_check=false;cfg.disable_auto_redirect=true;cfg.event_handler=response_event;cfg.user_data=&response;
  auto client=esp_http_client_init(&cfg);if(!client)return false;
  bool ok=esp_http_client_set_header(client,"Content-Type","application/json")==ESP_OK&&esp_http_client_set_header(client,"Cache-Control","no-store")==ESP_OK&&esp_http_client_set_header(client,"Accept-Encoding","identity")==ESP_OK&&esp_http_client_set_post_field(client,body,strlen(body))==ESP_OK;
  if(ok)ok=esp_http_client_perform(client)==ESP_OK && esp_http_client_get_status_code(client)==200 && !response.overflow;
  esp_http_client_cleanup(client);voice_pair::wipe(endpoint,sizeof(endpoint));return ok;
}
uint64_t now_ms(){int64_t t=esp_timer_get_time();return t<0?UINT64_MAX:static_cast<uint64_t>(t)/1000;}
}
Status Session::poll(const char *current,size_t len,const std::atomic<bool> &cancel_requested){
  auto permitted=[&](){if(cancel_requested.load(std::memory_order_acquire)){cancel();return false;}voice_pair::EntropyReady r{wifi_is_connected(),wifi_pairing_time_is_valid()};Status s=tick(current,len,now_ms(),r);return s==Status::AwaitingApproval||s==Status::AwaitingAck;};
  if(!permitted())return status_;
  auto state=std::unique_ptr<voice_pair_store::State>(new(std::nothrow) voice_pair_store::State{});if(!state){status_=Status::Failed;clear_secrets();return status_;}
  voice_pair::EntropyReady ready{wifi_is_connected(),wifi_pairing_time_is_valid()};
  auto stored=voice_pair_store::load(*state,ready);
  if(stored!=voice_pair_store::Result::Ok||!state->has_identity||memcmp(state->identity.bridge,bridge_,16)||memcmp(state->identity.device,device_,16)||(status_==Status::AwaitingAck)!=state->has_candidate){voice_pair_store::wipe(state.get(),sizeof(*state));status_=Status::Failed;clear_secrets();return status_;}
  char bridge_hex[33]={},device_hex[33]={},session64[44]={},gen_hex[33]={},challenge64[44]={},signature64[87]={},body[768]={};
  uint8_t challenge[32]={},signature[64]={};Response response{};voice_pair::Proof proof{};voice_pair_runtime::Delivery delivery{};
  bool ack=status_==Status::AwaitingAck,ok=voice_pair::encode_id(bridge_,bridge_hex,sizeof(bridge_hex))&&voice_pair::encode_id(device_,device_hex,sizeof(device_hex))&&voice_pair::encode_base64url(nonce_,32,session64,sizeof(session64));
  if(ack)ok=ok&&voice_pair::encode_id(state->candidate.generation,gen_hex,sizeof(gen_hex));
  if(ok){int n=ack?snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"session\":\"%s\",\"op\":\"ack\",\"generation\":\"%s\"}",bridge_hex,device_hex,session64,gen_hex):snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"session\":\"%s\",\"op\":\"deliver\"}",bridge_hex,device_hex,session64);ok=n>0&&static_cast<size_t>(n)<sizeof(body);}
  if(ok&&permitted())ok=post(origin_,"/api/voice/pair/v1/challenge",body,response)&&parse_challenge(response.bytes,response.used,challenge);else ok=false;
  voice_pair::wipe(body,sizeof(body));voice_pair::wipe(&response,sizeof(response));
  if(ok&&permitted()){
    proof.operation=ack?voice_pair::Operation::Ack:voice_pair::Operation::Deliver;memcpy(proof.bridge,bridge_,16);memcpy(proof.device,device_,16);memcpy(proof.session,nonce_,32);memcpy(proof.challenge,challenge,32);if(ack)memcpy(proof.generation,state->candidate.generation,16);proof.origin=origin_;proof.origin_len=origin_len_;
    ready={wifi_is_connected(),wifi_pairing_time_is_valid()};ok=voice_pair::sign_proof(proof,state->identity.private_key,ready,signature)&&voice_pair::encode_base64url(challenge,32,challenge64,sizeof(challenge64))&&voice_pair::encode_base64url(signature,64,signature64,sizeof(signature64));
  }else ok=false;
  if(ok&&permitted()){
    int n=ack?snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"session\":\"%s\",\"op\":\"ack\",\"generation\":\"%s\",\"challenge\":\"%s\",\"signature\":\"%s\"}",bridge_hex,device_hex,session64,gen_hex,challenge64,signature64):snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"session\":\"%s\",\"op\":\"deliver\",\"challenge\":\"%s\",\"signature\":\"%s\"}",bridge_hex,device_hex,session64,challenge64,signature64);
    ok=n>0&&static_cast<size_t>(n)<sizeof(body)&&post(origin_,"/api/voice/pair/v1/proof",body,response);
  }else ok=false;
  if(ok&&permitted()){
    if(ack){
      ok=parse_ack(response.bytes,response.used)&&voice_pair_store::promote(state->candidate.generation,ready)==voice_pair_store::Result::Ok;
      if(ok){clear_secrets();status_=Status::Complete;}
      else {clear_secrets();status_=Status::Failed;}
    }else if(parse_delivery(response.bytes,response.used,origin_,origin_len_,delivery)){
      voice_pair_store::Credential c{};memcpy(c.bridge,bridge_,16);memcpy(c.device,device_,16);memcpy(c.private_key,state->identity.private_key,32);memcpy(c.public_key,state->identity.public_key,65);memcpy(c.server_key,delivery.server_key,65);memcpy(c.generation,delivery.generation,16);c.epoch=state->epoch;strcpy(c.wss,delivery.wss);strcpy(c.token,delivery.token);strcpy(c.https_origin,origin_);
      auto result=voice_pair_store::prepare(c,ready);voice_pair_store::wipe(&c,sizeof(c));if(result==voice_pair_store::Result::Ok)status_=Status::AwaitingAck;else {clear_secrets();status_=Status::Failed;}
    }
  }
  voice_pair_store::wipe(state.get(),sizeof(*state));voice_pair::wipe(&proof,sizeof(proof));voice_pair::wipe(&delivery,sizeof(delivery));voice_pair::wipe(&response,sizeof(response));voice_pair::wipe(challenge,sizeof(challenge));voice_pair::wipe(signature,sizeof(signature));voice_pair::wipe(session64,sizeof(session64));voice_pair::wipe(challenge64,sizeof(challenge64));voice_pair::wipe(signature64,sizeof(signature64));voice_pair::wipe(body,sizeof(body));return status_;
}
ClearAckResult acknowledge_clear(bool stopped,const voice_pair::EntropyReady &ready){
  if(!stopped||!ready.wifi_connected||!ready.time_valid)return ClearAckResult::Pending;
  auto state=std::unique_ptr<voice_pair_store::State>(new(std::nothrow) voice_pair_store::State{});if(!state)return ClearAckResult::Error;
  auto loaded=voice_pair_store::load(*state,ready);if(loaded!=voice_pair_store::Result::Ok||!state->has_tombstone){voice_pair_store::wipe(state.get(),sizeof(*state));return ClearAckResult::Error;}
  if(state->clear_acknowledged){voice_pair_store::wipe(state.get(),sizeof(*state));return ClearAckResult::Done;}
  char bridge[33]={},device[33]={},generation[33]={},nonce64[44]={},pre64[87]={},challenge64[44]={},signature64[87]={},body[768]={};
  uint8_t nonce[32]={},presig[64]={},challenge[32]={},signature[64]={};Response response{};voice_pair::Proof proof{};
  proof.operation=voice_pair::Operation::ClearAck;memcpy(proof.bridge,state->active.bridge,16);memcpy(proof.device,state->active.device,16);memcpy(proof.generation,state->revoked_generation,16);proof.epoch=state->epoch;proof.origin=state->active.https_origin;proof.origin_len=strlen(proof.origin);
  bool ok=voice_pair::encode_id(proof.bridge,bridge,sizeof(bridge))&&voice_pair::encode_id(proof.device,device,sizeof(device))&&voice_pair::encode_id(proof.generation,generation,sizeof(generation))&&voice_pair::sign_clear_ack_preauth(proof,state->active.private_key,ready,nonce,presig)&&voice_pair::encode_base64url(nonce,32,nonce64,sizeof(nonce64))&&voice_pair::encode_base64url(presig,64,pre64,sizeof(pre64));
  if(ok){int n=snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"op\":\"clear_ack\",\"generation\":\"%s\",\"revocation_epoch\":%" PRIu64 ",\"client_nonce\":\"%s\",\"pre_signature\":\"%s\"}",bridge,device,generation,proof.epoch,nonce64,pre64);ok=n>0&&static_cast<size_t>(n)<sizeof(body)&&post(proof.origin,"/api/voice/pair/v1/challenge",body,response)&&parse_challenge(response.bytes,response.used,challenge);}
  voice_pair::wipe(body,sizeof(body));voice_pair::wipe(&response,sizeof(response));
  if(ok&&wifi_is_connected()&&wifi_pairing_time_is_valid()){memcpy(proof.challenge,challenge,32);ok=voice_pair::sign_proof(proof,state->active.private_key,ready,signature)&&voice_pair::encode_base64url(challenge,32,challenge64,sizeof(challenge64))&&voice_pair::encode_base64url(signature,64,signature64,sizeof(signature64));}else ok=false;
  if(ok){int n=snprintf(body,sizeof(body),"{\"bridge_id\":\"%s\",\"device_id\":\"%s\",\"op\":\"clear_ack\",\"generation\":\"%s\",\"revocation_epoch\":%" PRIu64 ",\"challenge\":\"%s\",\"signature\":\"%s\"}",bridge,device,generation,proof.epoch,challenge64,signature64);ok=n>0&&static_cast<size_t>(n)<sizeof(body)&&post(proof.origin,"/api/voice/pair/v1/proof",body,response)&&parse_ack(response.bytes,response.used);}
  ClearAckResult result=ClearAckResult::Pending;if(ok)result=voice_pair_store::mark_clear_acknowledged(state->revoked_generation,state->epoch,state->command_hash,ready)==voice_pair_store::Result::Ok?ClearAckResult::Done:ClearAckResult::Error;
  voice_pair::wipe(nonce,sizeof(nonce));voice_pair::wipe(presig,sizeof(presig));voice_pair::wipe(challenge,sizeof(challenge));voice_pair::wipe(signature,sizeof(signature));voice_pair::wipe(nonce64,sizeof(nonce64));voice_pair::wipe(pre64,sizeof(pre64));voice_pair::wipe(challenge64,sizeof(challenge64));voice_pair::wipe(signature64,sizeof(signature64));voice_pair::wipe(body,sizeof(body));voice_pair::wipe(&response,sizeof(response));voice_pair::wipe(&proof,sizeof(proof));voice_pair_store::wipe(state.get(),sizeof(*state));return result;
}

}
#endif
