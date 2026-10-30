#include "action_feedback.h"
#include <assert.h>
#include <string.h>
int main() {
 ActionFeedback f;
 auto id=f.begin("light.a","turn_on",0,nullptr,0); assert(id);
 assert(!f.begin("light.a","turn_off",0,nullptr,0));
 assert(f.dispatch(id,1)); f.tick(20000); assert(!f.begin("light.a","turn_off",0,nullptr,20001));
 f.result(id,ACTION_UNKNOWN,20001);
 action_observation_t o={}; strcpy(o.entity,"light.a"); strcpy(o.kind,"light"); strcpy(o.state,"on"); o.available=true;
 assert(f.observe(id,o,20002)); assert(f.find("light.a")->phase==ACTION_UPDATED);
 auto newer=f.begin("light.a","turn_off",0,nullptr,20003); assert(newer && newer!=id);
 f.result(id,ACTION_REJECTED,20004); assert(f.find("light.a")->id==newer);
 assert(f.dispatch(newer,20004)); f.result(newer,ACTION_ACCEPTED,20005); assert(!f.observe(newer,o,20006)); strcpy(o.state,"off"); assert(f.observe(newer,o,20007)); assert(f.find("light.a")->phase==ACTION_CONFIRMED); f.tick(23007); assert(!f.find("light.a"));
 auto expired=f.begin("climate.a","set_temperature",22,nullptr,0); assert(expired); assert(!f.dispatch(expired,20000)); assert(f.find("climate.a")->phase==ACTION_NOT_SENT);
 auto temp=f.begin("climate.a","set_temperature",22,nullptr,20001); assert(temp && f.dispatch(temp,20002)); f.result(temp,ACTION_ACCEPTED,20003); strcpy(o.entity,"climate.a"); strcpy(o.kind,"climate"); strcpy(o.state,"cool"); o.has_target=true; o.target=22.09f; assert(f.observe(temp,o,20004));
}
