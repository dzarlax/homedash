#include "voice_protocol.h"
#include "voice_fragments.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace voice;
static bool parse(const std::string &s, Message &m) { return parse_message(s.data(),s.size(),m); }
static std::string replace(std::string s,const std::string &a,const std::string &b) { auto p=s.find(a); assert(p!=std::string::npos); s.replace(p,a.size(),b); return s; }
int main(int argc,char **argv) {
    assert(argc>=2 && argc<=4); std::ifstream f(argv[1]); std::ostringstream stream; stream<<f.rdbuf(); auto s=stream.str(); Message m;
    assert(parse(s,m) && !m.clear);
    // Mutations use a stable compact fixture independently of shared JSON formatting.
    s="{\"v\":1,\"type\":\"voice.snapshot\",\"lease_id\":\"fixture-lease\",\"revision\":1,\"session_id\":\"fixture-session\",\"turn_id\":\"turn-1\",\"phase\":\"answer\",\"backend_active\":false,\"playback\":\"unknown\",\"text\":\"Привет!\",\"text_truncated\":false,\"interrupted\":false,\"active\":true,\"ttl_ms\":60000,\"replay\":false}";
    assert(parse(s,m) && !m.replay);
    auto replay=replace(s,"\"replay\":false","\"replay\":true"); assert(parse(replay,m) && m.replay);
    assert(!parse(replace(s,",\"replay\":false",""),m));
    assert(!parse(replace(s,"\"replay\":false","\"replay\":1"),m));
    assert(!parse(replace(s,"\"replay\":false","\"replay\":false,\"replay\":true"),m));
    auto n=replace(s,"\"revision\":1","\"revision\":18446744073709551615");
    assert(parse(n,m) && m.snapshot.revision==UINT64_MAX);
    n=replace(s,"\"revision\":1","\"revision\":18446744073709551616"); assert(!parse(n,m));
    n=replace(s,"\"revision\":1","\"revision\":1.0"); assert(!parse(n,m));
    n=replace(s,"\"revision\":1","\"revision\":1, \"revision\":2"); assert(!parse(n,m));
    n=replace(s,"\"voice.snapshot\"","\"voice.other\""); assert(!parse(n,m));
    n=replace(s,"\"answer\"","\"bogus\""); assert(!parse(n,m));
    n=replace(s,"\"text\":\"Привет!\"",std::string("\"text\": \"")+char(0xc0)+char(0x80)+"\""); assert(!parse(n,m));
    n=replace(s,"\"text\":\"Привет!\"","\"text\": \"\\uD800\""); assert(!parse(n,m));
    n=replace(s,"\"text\":\"Привет!\"","\"text\": \"\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442\""); assert(parse(n,m)&&m.snapshot.text=="Привет");
    std::string exact(8190,'a'); exact += "я";
    n=replace(s,"\"text\":\"Привет!\"","\"text\":\""+exact+"\"");
    assert(parse(n,m) && m.snapshot.text.size()==8192);
    n=replace(s,"\"text\":\"Привет!\"","\"text\":\""+exact+"a\""); assert(!parse(n,m));
    assert(!parse(std::string(65537,'x'),m));
    const std::string clear="{\"v\":1,\"type\":\"voice.clear\",\"lease_id\":\"x\",\"revision\":2,\"reason\":\"expired\"}";
    if (argc>2) { std::ifstream clear_file(argv[2]); std::ostringstream shared; shared<<clear_file.rdbuf(); assert(parse(shared.str(),m)&&m.clear); }
    if (argc>3) { std::ifstream replay_file(argv[3]); std::ostringstream shared; shared<<replay_file.rdbuf(); assert(parse(shared.str(),m)&&!m.clear&&m.replay); }
    assert(!parse(replace(clear,"\"reason\"","\"replay\":true,\"reason\""),m));
    assert(parse(clear,m)&&m.clear); assert(!parse(replace(clear,"\"reason\"","\"text\":\"secret\",\"reason\""),m));
    Fragments fragments; const size_t half=s.size()/2;
    assert(fragments.add(s.data(),half,0,s.size(),1,true)==Fragments::Result::Pending);
    assert(fragments.add(s.data()+half,s.size()-half,half,s.size(),1,true)==Fragments::Result::Complete);
    assert(parse(fragments.message(),m));
    fragments.reset();
    assert(fragments.add(s.data(),half,0,half,1,false)==Fragments::Result::Pending);
    assert(fragments.add(s.data()+half,s.size()-half,0,s.size()-half,0,true)==Fragments::Result::Complete);
    assert(parse(fragments.message(),m));
    assert(fragments.add(s.data(),1,2,s.size(),1,true)==Fragments::Result::Invalid);
    assert(fragments.add(s.data(),1,0,65537,1,true)==Fragments::Result::Invalid);
    assert(fragments.add(s.data(),1,0,s.size(),2,true)==Fragments::Result::Invalid);
    std::cout<<"voice protocol tests passed\n";
}
