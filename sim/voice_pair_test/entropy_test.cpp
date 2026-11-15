#include "voice_pair_entropy.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
struct Mock { std::mutex lease,signal;std::condition_variable cv;bool inside=false,release=false;std::atomic<bool> ready{true},stopped{false}; };
static bool lock(void *p) {static_cast<Mock *>(p)->lease.lock();return true;}
static void unlock(void *p) {static_cast<Mock *>(p)->lease.unlock();}
static bool ready(void *p) {return static_cast<Mock *>(p)->ready.load();}
static void fill(void *p,void *out,size_t n) {auto &m=*static_cast<Mock *>(p);std::unique_lock<std::mutex> lk(m.signal);m.inside=true;m.cv.notify_all();m.cv.wait(lk,[&]{return m.release;});memset(out,42,n);}
int main(){
 Mock m;voice_pair_entropy::Ops ops{&m,lock,unlock,ready,fill};uint8_t bytes[48]{};bool result=false;
 std::thread seeded([&]{result=voice_pair_entropy::fill(ops,bytes,sizeof(bytes));});
 {std::unique_lock<std::mutex> lk(m.signal);m.cv.wait(lk,[&]{return m.inside;});}
 std::thread stop([&]{std::lock_guard<std::mutex> lk(m.lease);m.ready=false;m.stopped=true;});
 assert(!m.stopped.load());{std::lock_guard<std::mutex> lk(m.signal);m.release=true;}m.cv.notify_all();seeded.join();stop.join();assert(result && m.stopped && bytes[0]==42);
 memset(bytes,1,sizeof(bytes));assert(!voice_pair_entropy::fill(ops,bytes,sizeof(bytes)));for(auto b:bytes)assert(b==0);
 // A connection lost during the entropy read is detected after the fill.
 Mock m2;voice_pair_entropy::Ops ops2{&m2,lock,unlock,ready,fill};std::thread lost([&]{result=voice_pair_entropy::fill(ops2,bytes,sizeof(bytes));});
 {std::unique_lock<std::mutex> lk(m2.signal);m2.cv.wait(lk,[&]{return m2.inside;});m2.ready=false;m2.release=true;}m2.cv.notify_all();lost.join();assert(!result);for(auto b:bytes)assert(b==0);
}
