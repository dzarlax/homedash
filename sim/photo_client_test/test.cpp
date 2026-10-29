#include "photo_frame_client.h"
#include "esp_http_client.h"
#include <openssl/sha.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct mock_http { esp_http_client_config_t config; };
struct scenario {
  std::string body, encoding, range, url, api_key, accept_encoding;
  int status = 200, chunked = 0, read_calls = 0, timeout = 0, cancel_after_reads = 0;
  int64_t length = -1, now = 0;
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
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *k, const char *v) { if (!strcmp(k,"X-API-Key")) s.api_key=v; if (!strcmp(k,"Accept-Encoding")) s.accept_encoding=v; return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t, int) { if(s.cancel_on_open) s.cancel=true; if(s.deadline_on_open) s.now=15000000; return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
  auto send=[&](const char *k,const std::string &v) { if (!v.empty()) { esp_http_client_event_t e={HTTP_EVENT_ON_HEADER,h->config.user_data,k,v.c_str()}; h->config.event_handler(&e); } };
  send("Content-Encoding",s.encoding); send("Content-Range",s.range);
  if(s.cancel_on_headers) s.cancel=true; if(s.deadline_on_headers) s.now=15000000;
  return s.length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return s.status; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t) { return s.chunked; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t) { return s.length; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int ms) { s.timeout=ms; return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t,char *out,int n) {
  ++s.read_calls;
  size_t count=std::min({(size_t)n,s.max_read,s.body.size()-s.pos});
  memcpy(out,s.body.data()+s.pos,count); s.pos+=count;
  if(s.cancel_after_reads && s.read_calls >= s.cancel_after_reads) s.cancel=true;
  if(s.pos >= (size_t)s.length) { if(s.cancel_on_final) s.cancel=true; if(s.deadline_on_final) s.now=15000000; }
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
  std::string pixels(PHOTO_FRAME_BYTES,'\0'); for(size_t i=0;i<pixels.size();i++) pixels[i]=(char)(i%251);
  photo_frame_entry entry{}; std::strcpy(entry.id,id.c_str()); std::strcpy(entry.sha256,hex((const unsigned char*)pixels.data(),pixels.size()).c_str());
  std::vector<uint8_t> buf(PHOTO_FRAME_BYTES+2,0x5a);
  auto get_pixels=[&]() { return photo_fetch_pixels("https://bridge","secret",&entry,buf.data()+1,cancelled,nullptr); };
  reset(pixels); expect("valid pixels",get_pixels(),PHOTO_OK);
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
  assert_true("pixel guards after failures",buf.front()==0x5a && buf.back()==0x5a);
  std::printf("%d cases, %d failures\n",cases,failures); return failures?1:0;
}
