#include "photo_frame_client.h"
#include "esp_http_client.h"
#include "web_identity.h"
#include <openssl/sha.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct mock_http { esp_http_client_config_t config; };
struct scenario {
  std::string body, encoding, range, url, api_key, accept_encoding, device_id, content_type = "application/octet-stream";
  int open_error = 0, header_error = 0;
  int stall_after = 1, stalls = 0;
  int status = 200, chunked = 0, read_calls = 0, timeout = 0, cancel_after_reads = 0;
  int64_t length = -1, now = 0, read_elapsed = 0;
  size_t pos = 0, max_read = 8192;
  bool cancel = false, cancel_on_open = false, cancel_on_headers = false, cancel_on_final = false;
  bool deadline_on_open = false, deadline_on_headers = false, deadline_on_final = false;
  bool redirected = false, closed = false, cleaned = false;
} s;
static mock_http client;
static int failures = 0, cases = 0;
static bool cancelled(void *) { return s.cancel; }
int64_t esp_timer_get_time() { return s.now; }
esp_err_t esp_crt_bundle_attach(void *) { return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) { client.config = *c; s.url = c->url; s.redirected = !c->disable_auto_redirect; return &client; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *k, const char *v) { if (!strcmp(k,"X-API-Key")) s.api_key=v; if (!strcmp(k,"X-Device-ID")) s.device_id=v; if (!strcmp(k,"Accept-Encoding")) s.accept_encoding=v; return ESP_OK; }
esp_err_t web_identity_set_http_header(esp_http_client_handle_t client) { return esp_http_client_set_header(client,"X-Device-ID","0123456789abcdef0123456789abcdef"); }
esp_err_t esp_http_client_open(esp_http_client_handle_t, int) { if(s.cancel_on_open) s.cancel=true; if(s.deadline_on_open) s.now=60000000; return s.open_error; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
  auto send=[&](const char *k,const std::string &v) { if (!v.empty()) { esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,h->config.user_data,k,v.c_str()}; h->config.event_handler(&e); } };
  send("Content-Encoding",s.encoding); send("Content-Range",s.range); send("Content-Type",s.content_type);
  if(s.cancel_on_headers) s.cancel=true; if(s.deadline_on_headers) s.now=60000000;
  return s.header_error ? s.header_error : s.length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return s.status; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t) { return s.chunked; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t) { return s.length; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int ms) { s.timeout=ms; return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t,char *out,int n) {
  ++s.read_calls;
  s.now += s.read_elapsed;
  if (s.read_calls >= s.stall_after && s.stalls > 0) {
    --s.stalls; s.now += 2000000;
    if(s.cancel_after_reads && s.read_calls >= s.cancel_after_reads) s.cancel=true;
    return -ESP_ERR_HTTP_EAGAIN;
  }
  size_t count=std::min({(size_t)n,s.max_read,s.body.size()-s.pos});
  memcpy(out,s.body.data()+s.pos,count); s.pos+=count;
  if(s.cancel_after_reads && s.read_calls >= s.cancel_after_reads) s.cancel=true;
  if(s.pos >= (size_t)s.length) { if(s.cancel_on_final) s.cancel=true; if(s.deadline_on_final) s.now=60000000; }
  return (int)count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t) { return s.pos==s.body.size() && s.pos==(size_t)s.length; }
esp_err_t esp_http_client_close(esp_http_client_handle_t) { s.closed=true; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t) { s.cleaned=true; return ESP_OK; }
static void expect(const char *name, photo_result got, photo_result wanted) { ++cases; if(got != wanted) { ++failures; std::fprintf(stderr,"FAIL %s: got %d expected %d\n",name,got,wanted); } }
static void assert_true(const char *name, bool ok) { ++cases; if(!ok) { ++failures; std::fprintf(stderr,"FAIL %s\n",name); } }
static std::string hex(const unsigned char *data,size_t n) { unsigned char digest[32]; SHA256(data,n,digest); char out[65]; for(int i=0;i<32;i++) std::snprintf(out+i*2,3,"%02x",digest[i]); return out; }
static const std::string id(64,'a');
static std::string frame_json() { return "{\"id\":\""+id+"\",\"sha256\":\""+std::string(64,'b')+"\"}"; }
static std::string manifest(const std::string &state="ready", const std::string &frames=frame_json()) { return "{\"version\":1,\"width\":1024,\"height\":600,\"frame_bytes\":1228800,\"idle_after_s\":30,\"slide_interval_s\":15,\"max_stale_s\":60,\"pixel_format\":\"rgb565le\",\"state\":\""+state+"\",\"generation\":\"g1\",\"frames\":["+frames+"]}"; }
static void reset(const std::string &body) { s=scenario{}; s.body=body; s.length=body.size(); }
static photo_result get_manifest(photo_manifest *out) { return photo_fetch_manifest("https://bridge", "secret",out,cancelled,nullptr); }
static void replace(std::string &str,const std::string &a,const std::string &b) { size_t pos=str.find(a); if(pos==std::string::npos) { std::fprintf(stderr,"bad fixture\n"); std::abort(); } str.replace(pos,a.size(),b); }
int main() {
  photo_manifest m{};
  reset(manifest()); expect("valid manifest",get_manifest(&m),PHOTO_OK);
  assert_true("manifest values",m.count==1 && !strcmp(m.frames[0].id,id.c_str()) && m.idle==30);
  assert_true("request configuration",s.url=="https://bridge/api/photo-frame/v1/manifest" && s.api_key=="secret" && s.accept_encoding=="identity" && !s.redirected && s.closed && s.cleaned);
  for(const char *state : {"disabled","empty","warming","unavailable"}) { reset(manifest(state,"")); expect(state,get_manifest(&m),PHOTO_OK); reset(manifest(state)); expect("nonready with frame",get_manifest(&m),PHOTO_FAILED); }
  struct mutation { const char *name,*from,*to; } mutations[]={
    {"version","\"version\":1","\"version\":2"}, {"format","rgb565le","rgb888"}, {"size","1228800","1228799"}, {"width","\"width\":1024","\"width\":1023"}, {"fractional number","\"version\":1","\"version\":1.5"}, {"bad id","aaaaaaaa","AAAAAAAA"}, {"bad state","ready","unknown"}, {"missing field","\"generation\"","\"other\""}
  };
  for(auto x:mutations) { auto b=manifest(); replace(b,x.from,x.to); reset(b); expect(x.name,get_manifest(&m),PHOTO_FAILED); }
  std::string many; for(int i=0;i<9;i++) { if(i) many+=","; many+=frame_json(); } reset(manifest("ready",many)); expect("too many frames",get_manifest(&m),PHOTO_FAILED);
  reset(manifest("ready","")); expect("ready without frames",get_manifest(&m),PHOTO_FAILED);
  auto wrong_schema=manifest(); replace(wrong_schema,"["+frame_json()+"]","{}"); reset(wrong_schema); expect("frames object schema",get_manifest(&m),PHOTO_FAILED);
  reset("not json"); expect("invalid JSON",get_manifest(&m),PHOTO_FAILED);
  for(int status : {401,302,404}) { reset(manifest()); s.status=status; expect("manifest status",get_manifest(&m),status==404?PHOTO_NOT_FOUND:PHOTO_FAILED); }
  reset(manifest()); s.chunked=1; expect("manifest chunked",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.encoding="gzip"; expect("manifest gzip",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.range="bytes 0-10/100"; expect("manifest range",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); ++s.length; expect("manifest short",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()+"extra"); s.length-=5; expect("manifest extra bytes",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.cancel=true; expect("cancel before open manifest",get_manifest(&m),PHOTO_CANCELLED);
  reset(manifest()); s.cancel_on_open=true; expect("cancel after open manifest",get_manifest(&m),PHOTO_CANCELLED);
  reset(manifest()); s.cancel_on_headers=true; expect("cancel after headers manifest",get_manifest(&m),PHOTO_CANCELLED);
  reset(manifest()); s.max_read=10; s.cancel_after_reads=1; expect("cancel before final manifest read",get_manifest(&m),PHOTO_CANCELLED);
  reset(manifest()); s.cancel_on_final=true; expect("cancel after final manifest read",get_manifest(&m),PHOTO_CANCELLED);
  reset(manifest()); s.deadline_on_open=true; expect("deadline on open manifest",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.deadline_on_headers=true; expect("deadline on headers manifest",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.deadline_on_final=true; expect("deadline on final manifest read",get_manifest(&m),PHOTO_FAILED);
  reset(manifest()); s.stalls=1;
  expect("manifest recovers from EAGAIN", get_manifest(&m), PHOTO_OK);
  reset(manifest()); s.stalls=100;
  expect("manifest stalls respect deadline", get_manifest(&m), PHOTO_FAILED);
  reset(manifest()); s.stalls=1; s.cancel_after_reads=1;
  expect("manifest stall cancellation", get_manifest(&m), PHOTO_CANCELLED);
  photo_diagnostic diagnostic;
  auto diagnose = [&]() { return photo_fetch_manifest("https://bridge", "secret", &m, cancelled, nullptr, &diagnostic); };
  reset(manifest()); s.open_error = -123;
  expect("diagnose connect", diagnose(), PHOTO_FAILED);
  assert_true("connect diagnostic", !strcmp(diagnostic.stage, "connect") && diagnostic.transport_error == -123 && diagnostic.http_status == 0);
  reset(manifest()); s.header_error = -456;
  expect("diagnose headers", diagnose(), PHOTO_FAILED);
  assert_true("header diagnostic", !strcmp(diagnostic.stage, "headers") && diagnostic.transport_error == -456);
  reset(manifest()); s.status = 401;
  expect("diagnose HTTP", diagnose(), PHOTO_FAILED);
  assert_true("HTTP diagnostic", !strcmp(diagnostic.stage, "HTTP") && diagnostic.http_status == 401 && diagnostic.transport_error == 0);
  reset("not json"); expect("diagnose JSON", diagnose(), PHOTO_FAILED);
  assert_true("JSON diagnostic", !strcmp(diagnostic.stage, "JSON"));
  reset(manifest()); expect("diagnose success", diagnose(), PHOTO_OK);
  assert_true("success diagnostic reset", !strcmp(diagnostic.stage, "ok") && diagnostic.transport_error == 0);
  assert_true("manifest has device header", s.device_id=="0123456789abcdef0123456789abcdef");
  std::string pixels(PHOTO_FRAME_BYTES,'\0'); for(size_t i=0;i<pixels.size();i++) pixels[i]=(char)(i%251);
  photo_frame_entry entry{}; std::strcpy(entry.id,id.c_str()); std::strcpy(entry.sha256,hex((const unsigned char*)pixels.data(),pixels.size()).c_str());
  std::vector<uint8_t> buf(PHOTO_FRAME_BYTES+2,0x5a);
  auto get_pixels=[&]() { return photo_fetch_pixels("https://bridge","secret",&entry,buf.data()+1,cancelled,nullptr); };
  reset(pixels); s.read_elapsed=200000;
  expect("slow frame completes after original 15s deadline",get_pixels(),PHOTO_OK);
  reset(pixels); s.stall_after=2; s.stalls=1;
  expect("pixels recover from EAGAIN after progress",get_pixels(),PHOTO_OK);
  reset(pixels); s.stalls=100;
  expect("pixel stalls respect deadline",get_pixels(),PHOTO_FAILED);
  reset(pixels); s.stalls=1; s.cancel_after_reads=1;
  expect("pixel stall cancellation",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); expect("valid pixels",get_pixels(),PHOTO_OK);
  assert_true("binary frame has device header", s.device_id=="0123456789abcdef0123456789abcdef");
  assert_true("bytes and guards",buf.front()==0x5a && buf.back()==0x5a && !memcmp(buf.data()+1,pixels.data(),pixels.size()));
  assert_true("frame URL",s.url=="https://bridge/api/photo-frame/v1/frames/"+id);
  for(int status : {401,302,404}) { reset(pixels); s.status=status; expect("pixel status",get_pixels(),status==404?PHOTO_NOT_FOUND:PHOTO_FAILED); }
  reset(pixels); s.chunked=1; expect("pixel chunked",get_pixels(),PHOTO_FAILED);
  reset(pixels); s.encoding="gzip"; expect("pixel gzip",get_pixels(),PHOTO_FAILED);
  reset(pixels); s.range="bytes 0-10/100"; expect("pixel range",get_pixels(),PHOTO_FAILED);
  reset(pixels); --s.length; expect("incorrect pixel length",get_pixels(),PHOTO_FAILED);
  reset(pixels.substr(0,pixels.size()-1)); s.length=PHOTO_FRAME_BYTES; expect("short pixels",get_pixels(),PHOTO_FAILED);
  reset(pixels+"extra"); s.length=PHOTO_FRAME_BYTES; expect("extra pixels",get_pixels(),PHOTO_FAILED);
  reset(pixels); entry.sha256[0]=entry.sha256[0]=='a'?'b':'a'; expect("incorrect hash",get_pixels(),PHOTO_FAILED); std::strcpy(entry.sha256,hex((const unsigned char*)pixels.data(),pixels.size()).c_str());
  reset(pixels); s.cancel=true; expect("cancel before open pixels",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); s.cancel_on_open=true; expect("cancel after open pixels",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); s.cancel_on_headers=true; expect("cancel after headers pixels",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); s.cancel_after_reads=1; expect("cancel before final pixel read",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); s.cancel_on_final=true; expect("cancel after final pixel read",get_pixels(),PHOTO_CANCELLED);
  reset(pixels); s.deadline_on_open=true; expect("deadline on open pixels",get_pixels(),PHOTO_FAILED);
  reset(pixels); s.deadline_on_headers=true; expect("deadline on headers pixels",get_pixels(),PHOTO_FAILED);
  reset(pixels); s.deadline_on_final=true; expect("deadline on final pixel read",get_pixels(),PHOTO_FAILED);
  auto diagnose_pixels=[&]() { return photo_fetch_pixels("https://bridge","secret",&entry,buf.data()+1,cancelled,nullptr,&diagnostic); };
  reset(pixels); s.stall_after=2; s.stalls=100;
  expect("diagnose pixel deadline",diagnose_pixels(),PHOTO_FAILED);
  assert_true("pixel deadline progress", !strcmp(diagnostic.stage,"deadline") && diagnostic.received==8192);
  reset(pixels.substr(0,100)); s.length=PHOTO_FRAME_BYTES;
  expect("diagnose pixel EOF",diagnose_pixels(),PHOTO_FAILED);
  assert_true("pixel EOF progress", !strcmp(diagnostic.stage,"EOF") && diagnostic.received==100);
  reset(pixels); entry.sha256[0]=entry.sha256[0]=='a'?'b':'a';
  expect("diagnose pixel hash",diagnose_pixels(),PHOTO_FAILED);
  assert_true("pixel hash diagnostic", !strcmp(diagnostic.stage,"sha256") && diagnostic.received==PHOTO_FRAME_BYTES);
  std::strcpy(entry.sha256,hex((const unsigned char*)pixels.data(),pixels.size()).c_str());
  reset(pixels); s.stalls=1;
  expect("diagnose pixel recovery",diagnose_pixels(),PHOTO_OK);
  assert_true("pixel recovery diagnostic", !strcmp(diagnostic.stage,"ok") && diagnostic.received==PHOTO_FRAME_BYTES && diagnostic.transport_error==0);
  assert_true("pixel guards after failures",buf.front()==0x5a && buf.back()==0x5a);
  const std::string block = "{\"kind\":\"weather\",\"render_id\":\""+id+"\",\"sha256\":\""+std::string(64,'b')+"\",\"x\":752,\"y\":36,\"width\":236,\"height\":200,\"expires_at\":1793363400}";
  const std::string context = "{\"version\":1,\"revision\":\""+id+"\",\"server_time\":1793361600,\"clock\":{\"enabled\":true,\"locale\":\"ru\",\"timezone\":\"Europe/Belgrade\",\"clock_format\":\"24h\"},\"blocks\":["+block+"]}";
  photo_context c{}; auto get_context = [&]() { return photo_fetch_context("https://bridge","secret",&c,cancelled,nullptr); };
  reset(context); expect("valid context",get_context(),PHOTO_OK);
  assert_true("context request and metadata",s.url=="https://bridge/api/photo-frame/v1/context" && c.count==1 && c.blocks[0].width==236 && c.server_time==1793361600);
  struct mutation context_mutations[] = {{"context version","\"version\":1","\"version\":2"},{"bad kind","weather","unknown"},{"bad width","\"width\":236","\"width\":237"},{"bad position","\"x\":752","\"x\":900"},{"bad expiry","1793363400","1.5"},{"bad locale","\"ru\"","\"fr\""},{"bad hash","aaaaaaaa","AAAAAAAA"}};
  for (auto x:context_mutations) { auto b=context; replace(b,x.from,x.to); reset(b); expect(x.name,get_context(),PHOTO_FAILED); }
  auto duplicate_key=context; replace(duplicate_key,"\"version\":1", "\"version\":1,\"version\":1"); reset(duplicate_key); expect("duplicate JSON key",get_context(),PHOTO_FAILED);
  auto duplicate=context; replace(duplicate,block,block+","+block); reset(duplicate); expect("duplicate kind",get_context(),PHOTO_FAILED);
  reset(context); s.status=404; expect("old bridge",get_context(),PHOTO_NOT_FOUND);
  reset(context); s.cancel_on_final=true; expect("context cancellation",get_context(),PHOTO_CANCELLED);
  reset(context); s.length=4097; expect("context size cap",get_context(),PHOTO_FAILED);
  const std::string v2clock = "{\"kind\":\"clock\",\"render_id\":\""+std::string(64,'c')+"\",\"sha256\":\""+std::string(64,'d')+"\",\"x\":24,\"y\":460,\"width\":480,\"height\":128,\"expires_at\":1793363400}";
  const std::string v2event = "{\"kind\":\"event\",\"render_id\":\""+std::string(64,'e')+"\",\"sha256\":\""+std::string(64,'f')+"\",\"x\":530,\"y\":450,\"width\":456,\"height\":112,\"expires_at\":1793361900}";
  const std::string v2 = "{\"version\":2,\"frame_id\":\""+id+"\",\"revision\":\""+id+"\",\"server_time\":1793361600,\"clock\":{\"enabled\":true,\"locale\":\"en\",\"timezone\":\"Europe/Belgrade\",\"clock_format\":\"12h\",\"tone\":\"dark\",\"backdrop\":true},\"blocks\":["+block+","+v2event+","+v2clock+"]}";
  auto get_v2 = [&]() { return photo_fetch_context("https://bridge","secret",&c,cancelled,nullptr,nullptr,2,id.c_str()); };
  reset(v2); expect("v2 three blocks",get_v2(),PHOTO_OK);
  assert_true("v2 frame query and metadata",s.url=="https://bridge/api/photo-frame/v2/context?frame_id="+id && c.count==3 && c.backdrop && !strcmp(c.tone,"dark"));
  auto invalid=v2; replace(invalid,"\"frame_id\":\""+id+"\"","\"frame_id\":\""+std::string(64,'b')+"\""); reset(invalid); expect("wrong frame",get_v2(),PHOTO_FAILED);
  invalid=v2; replace(invalid,"\"tone\":\"dark\"","\"tone\":\"blue\""); reset(invalid); expect("invalid tone",get_v2(),PHOTO_FAILED);
  invalid=v2; replace(invalid,"\"backdrop\":true","\"backdrop\":false"); reset(invalid); expect("backdrop mismatch",get_v2(),PHOTO_FAILED);
  invalid=v2; replace(invalid,v2clock,v2event); reset(invalid); expect("duplicate third kind",get_v2(),PHOTO_FAILED);
  invalid=v2; replace(invalid,"\"height\":128","\"height\":129"); reset(invalid); expect("clock bounds",get_v2(),PHOTO_FAILED);
  reset(v2); s.cancel_on_final=true; expect("v2 cancelled",get_v2(),PHOTO_CANCELLED);
  invalid=v2; replace(invalid,"\"tone\":\"dark\"", "\"tone\":\"dark\",\"tone\":\"light\""); reset(invalid); expect("duplicate clock tone",get_v2(),PHOTO_FAILED);
  invalid=v2; replace(invalid,"\"backdrop\":true", "\"backdrop\":true,\"backdrop\":false"); reset(invalid); expect("duplicate clock backdrop",get_v2(),PHOTO_FAILED);
  invalid=v2; std::string duplicated_id=v2clock; replace(duplicated_id,"\"render_id\":\""+std::string(64,'c')+"\"", "\"render_id\":\""+std::string(64,'c')+"\",\"render_id\":\""+std::string(64,'c')+"\""); replace(invalid,v2clock,duplicated_id); reset(invalid); expect("duplicate third block render id",get_v2(),PHOTO_FAILED);
  invalid=v2; std::string duplicated_width=v2clock; replace(duplicated_width,"\"width\":480", "\"width\":480,\"width\":1"); replace(invalid,v2clock,duplicated_width); reset(invalid); expect("duplicate third block geometry",get_v2(),PHOTO_FAILED);
  std::string v3block=block; replace(v3block, "\"expires_at\"", "\"pixel_format\":\"argb8888-le-straight\",\"byte_count\":188800,\"expires_at\"");
  std::string v3 = "{\"version\":3,\"revision\":\""+id+"\",\"server_time\":1793361600,\"clock\":{\"enabled\":true,\"locale\":\"en\",\"timezone\":\"Europe/Belgrade\",\"clock_format\":\"12h\",\"tone\":\"light\",\"backdrop\":true},\"blocks\":["+v3block+"]}";
  auto get_v3 = [&]() { return photo_fetch_context("https://bridge","secret",&c,cancelled,nullptr,nullptr,3); };
  reset(v3); expect("v3 valid", get_v3(), PHOTO_OK);
  assert_true("v3 independent route", s.url=="https://bridge/api/photo-frame/v3/context");
  invalid=v3; replace(invalid, "argb8888-le-straight", "rgb565"); reset(invalid); expect("v3 format",get_v3(),PHOTO_FAILED);
  invalid=v3; replace(invalid, "188800", "188799"); reset(invalid); expect("v3 byte count",get_v3(),PHOTO_FAILED);
  invalid=v3; replace(invalid, "\"tone\":\"light\"", "\"tone\":\"dark\""); reset(invalid); expect("v3 tone",get_v3(),PHOTO_FAILED);
  photo_overlay_block b{}; strcpy(b.id,id.c_str()); b.width=2; b.height=2;
  std::string card(8,'x'); strcpy(b.sha256,hex((const unsigned char*)card.data(),card.size()).c_str());
  uint8_t cardbuf[8]; auto get_card = [&]() {return photo_fetch_overlay("https://bridge","secret",&b,cardbuf,cancelled,nullptr);};
  reset(card); expect("card data",get_card(),PHOTO_OK); assert_true("card route",s.url=="https://bridge/api/photo-frame/v1/overlays/"+id);
  reset(card); expect("v2 card data",photo_fetch_overlay("https://bridge","secret",&b,cardbuf,cancelled,nullptr,nullptr,2),PHOTO_OK); assert_true("v2 card route",s.url=="https://bridge/api/photo-frame/v2/overlays/"+id);
  std::string alpha(16, '\0'); alpha[4] = 24; alpha[5] = 30; alpha[6] = 40; alpha[7] = char(128);
  strcpy(b.sha256,hex((const unsigned char*)alpha.data(),alpha.size()).c_str());
  uint8_t alphabuf[16]; reset(alpha); expect("v3 alpha data",photo_fetch_overlay("https://bridge","secret",&b,alphabuf,cancelled,nullptr,nullptr,3),PHOTO_OK);
  assert_true("v3 alpha route and exact bytes",s.url=="https://bridge/api/photo-frame/v3/overlays/"+id && !memcmp(alphabuf,alpha.data(),alpha.size()));
  alpha[0] = 1; strcpy(b.sha256,hex((const unsigned char*)alpha.data(),alpha.size()).c_str()); reset(alpha);
  expect("v3 transparent pixel halo",photo_fetch_overlay("https://bridge","secret",&b,alphabuf,cancelled,nullptr,nullptr,3),PHOTO_FAILED);
  strcpy(b.sha256,hex((const unsigned char*)card.data(),card.size()).c_str());
  reset(card); s.length=7; expect("card length",get_card(),PHOTO_FAILED);
  reset(card); s.content_type="text/plain"; expect("card type",get_card(),PHOTO_FAILED);
  reset(card); b.sha256[0] = b.sha256[0]=='a'?'b':'a'; expect("card hash",get_card(),PHOTO_FAILED);
  std::printf("%d cases, %d failures\n",cases,failures); return failures?1:0;
}
