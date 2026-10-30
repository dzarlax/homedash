#include "action_executor.h"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Test { ActionFeedback tracker; uint64_t now=0; int posts=0, reads=0, merges=0; bool replace_on_read=false; int timeout=0; };
static uint64_t now(void *p) { return ((Test*)p)->now; }
static bool dispatch(void *p,uint32_t id) { auto &t=*(Test*)p; return t.tracker.dispatch(id,t.now); }
static bridge_action_result_t post(void *p,const action_request_t &) { auto &t=*(Test*)p; ++t.posts; bridge_action_result_t r={}; r.phase=ACTION_ACCEPTED; return r; }
static bridge_action_result_t read(void *p,const char *entity,int timeout) { auto &t=*(Test*)p; ++t.reads; t.timeout=timeout; bridge_action_result_t r={}; r.has_observation=true; strcpy(r.observation.entity,entity); strcpy(r.observation.kind,"light"); strcpy(r.observation.state,"off"); r.observation.available=true; if(t.replace_on_read) { t.now=20001; t.tracker.tick(t.now); assert(t.tracker.begin(entity,"turn_off",0,nullptr,t.now)); } return r; }
static void publish_post(void *p,uint32_t id,const bridge_action_result_t &r) { auto &t=*(Test*)p; t.tracker.result(id,r.phase,t.now,r.error); }
static void publish_read(void *p,uint32_t id,const bridge_action_result_t &r) { auto &t=*(Test*)p; if(t.tracker.by_id(id) && r.has_observation) { ++t.merges; t.tracker.observe(id,r.observation,t.now); } }
static bool waiting(void *p,uint32_t id,uint64_t *deadline) { auto &t=*(Test*)p; auto *r=t.tracker.by_id(id); if(!r || r->timed_out || (r->phase!=ACTION_ACCEPTED && r->phase!=ACTION_UNKNOWN)) return false; *deadline=r->deadline_ms; return true; }
static action_request_t request(uint32_t id) { action_request_t r={}; r.id=id; strcpy(r.entity,"light.a"); strcpy(r.action,"turn_on"); return r; }
int main() {
 Test t; ActionExecutor executor({&t,now,dispatch,post,read,publish_post,publish_read,waiting});
 auto id=t.tracker.begin("light.a","turn_on",0,nullptr,0); executor.run(request(id)); assert(t.posts==1 && t.reads==1 && t.timeout==3000); assert(executor.wait_ms(10000)==2000);
 t.now=1999; executor.poll(); assert(t.reads==1); t.now=2000; executor.poll(); assert(t.posts==1 && t.reads==2); t.now=5000; executor.poll(); assert(t.reads==2);
 Test expired; ActionExecutor x({&expired,now,dispatch,post,read,publish_post,publish_read,waiting}); id=expired.tracker.begin("light.a","turn_on",0,nullptr,0); expired.now=20000; x.run(request(id)); assert(expired.posts==0 && expired.reads==0 && expired.tracker.by_id(id)->phase==ACTION_NOT_SENT);
 Test race; ActionExecutor y({&race,now,dispatch,post,read,publish_post,publish_read,waiting}); id=race.tracker.begin("light.a","turn_on",0,nullptr,0); race.replace_on_read=true; y.run(request(id)); assert(race.posts==1 && race.reads==1 && race.merges==0); race.now+=2000; y.poll(); assert(race.reads==1);
 puts("PASS: one POST, immediate and scheduled reads, deadline and stale-ID gating");
}
