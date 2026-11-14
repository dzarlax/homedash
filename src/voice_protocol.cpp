#include "voice_protocol.h"
#include <cctype>
#include <algorithm>
#include <utility>
#include <limits>
#include <map>

namespace voice { namespace {
enum class Kind { String, Number, Bool, Other };
struct Value { Kind kind = Kind::Other; std::string s; bool b = false; ~Value() { std::fill(s.begin(),s.end(), '\0'); } };
static bool utf8(const std::string &s) {
    for (size_t i=0; i<s.size();) {
        unsigned char c=s[i++]; if (c<0x80) { if (!c) return false; continue; }
        unsigned n; uint32_t v, min;
        if (c>=0xc2 && c<=0xdf) { n=1; v=c&31; min=0x80; }
        else if (c>=0xe0 && c<=0xef) { n=2; v=c&15; min=0x800; }
        else if (c>=0xf0 && c<=0xf4) { n=3; v=c&7; min=0x10000; }
        else return false;
        if (i+n>s.size()) return false;
        while (n--) { unsigned char t=s[i++]; if ((t&0xc0)!=0x80) return false; v=(v<<6)|(t&63); }
        if (v<min || v>0x10ffff || (v>=0xd800 && v<=0xdfff)) return false;
    } return true;
}
static void append_utf8(std::string &s, uint32_t c) {
    if(c<0x80) s.push_back((char)c);
    else if(c<0x800) { s.push_back((char)(0xc0|(c>>6))); s.push_back((char)(0x80|(c&63))); }
    else if(c<0x10000) { s.push_back((char)(0xe0|(c>>12))); s.push_back((char)(0x80|((c>>6)&63))); s.push_back((char)(0x80|(c&63))); }
    else { s.push_back((char)(0xf0|(c>>18))); s.push_back((char)(0x80|((c>>12)&63))); s.push_back((char)(0x80|((c>>6)&63))); s.push_back((char)(0x80|(c&63))); }
}
class Parser {
    const char *p, *end;
    void ws() { while(p<end && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) ++p; }
    bool hex4(uint32_t &v) { v=0; for(int i=0;i<4;i++) { if(p==end) return false; char c=*p++; int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(n<0) return false; v=(v<<4)|n; } return true; }
    bool str(std::string &s) {
        if(p==end || *p++!='"') return false;
        while(p<end) { unsigned char c=*p++; if(c=='"') return utf8(s); if(c<0x20) return false;
            if(c!='\\') { s.push_back((char)c); continue; }
            if(p==end) return false; char e=*p++;
            switch(e) { case '"': case '\\': case '/': s.push_back(e); break; case 'b': s.push_back('\b'); break; case 'f': s.push_back('\f'); break; case 'n': s.push_back('\n'); break; case 'r': s.push_back('\r'); break; case 't': s.push_back('\t'); break;
            case 'u': { uint32_t a; if(!hex4(a)) return false; if(a>=0xd800 && a<=0xdbff) { if(end-p<2 || p[0]!='\\' || p[1]!='u') return false; p+=2; uint32_t b; if(!hex4(b) || b<0xdc00 || b>0xdfff) return false; a=0x10000+((a-0xd800)<<10)+(b-0xdc00); } else if(a>=0xdc00 && a<=0xdfff) return false; append_utf8(s,a); break; }
            default:return false; }
        } return false;
    }
    bool value(Value &v, int depth) {
        if(depth>16) return false; ws(); if(p==end) return false;
        if(*p=='"') { v.kind=Kind::String; return str(v.s); }
        if(*p=='{' || *p=='[') { char close=*p++=='{'?'}':']'; bool object=close=='}'; ws(); if(p<end&&*p==close){++p;return true;}
            for(;;) { if(object) { std::string key; if(!str(key)) return false; ws(); if(p==end||*p++!=':') return false; } Value ignored; if(!value(ignored,depth+1)) return false; ws(); if(p==end) return false; if(*p==close){++p;return true;} if(*p++!=',') return false; ws(); } }
        const char *start=p;
        if(*p=='-' || (*p>='0'&&*p<='9')) { if(*p=='-') ++p; if(p==end) return false; if(*p=='0') ++p; else { if(*p<'1'||*p>'9') return false; while(p<end&&*p>='0'&&*p<='9') ++p; } if(p<end&&*p=='.') { ++p; if(p==end||*p<'0'||*p>'9') return false; while(p<end&&*p>='0'&&*p<='9') ++p; } if(p<end&&(*p=='e'||*p=='E')) { ++p; if(p<end&&(*p=='+'||*p=='-')) ++p; if(p==end||*p<'0'||*p>'9') return false; while(p<end&&*p>='0'&&*p<='9') ++p; } v.kind=Kind::Number; v.s.assign(start,p); return true; }
        for (auto word : {"true","false","null"}) { size_t n=std::char_traits<char>::length(word); if((size_t)(end-p)>=n && std::char_traits<char>::compare(p,word,n)==0) { p+=n; if(word[0]!='n') {v.kind=Kind::Bool; v.b=word[0]=='t';} return true; } } return false;
    }
public:
    Parser(const char *d,size_t n):p(d),end(d+n) {}
    bool root(std::map<std::string,Value> &m) { ws(); if(p==end||*p++!='{') return false; ws(); if(p<end&&*p=='}') {++p;ws();return p==end;} for(;;) { std::string key; if(!str(key)) return false; ws(); if(p==end||*p++!=':') return false; Value v; if(!value(v,0)) return false;
        const bool known=key=="v" || key=="type" || key=="lease_id" || key=="revision" ||
            key=="reason" || key=="session_id" || key=="turn_id" || key=="phase" ||
            key=="playback" || key=="text" || key=="backend_active" || key=="text_truncated" ||
            key=="interrupted" || key=="active" || key=="ttl_ms" || key=="replay";
        if(known && !m.emplace(key,std::move(v)).second) return false; ws(); if(p==end) return false; if(*p=='}') {++p;ws();return p==end;} if(*p++!=',') return false; ws(); } }
};
static bool uintval(const Value &v,uint64_t &out) { if(v.kind!=Kind::Number||v.s.empty()) return false; uint64_t n=0; for(char c:v.s) { if(c<'0'||c>'9'||n>(std::numeric_limits<uint64_t>::max()-(c-'0'))/10) return false; n=n*10+(c-'0'); } out=n; return true; }
static bool getstr(const std::map<std::string,Value>&m,const char *key,std::string &out,bool nonempty=true) { auto i=m.find(key); if(i==m.end()||i->second.kind!=Kind::String||(nonempty&&i->second.s.empty())) return false; out=i->second.s; return true; }
static bool getbool(const std::map<std::string,Value>&m,const char *key,bool &out) { auto i=m.find(key); if(i==m.end()||i->second.kind!=Kind::Bool) return false; out=i->second.b; return true; }
static bool getuint(const std::map<std::string,Value>&m,const char *key,uint64_t &out) { auto i=m.find(key); return i!=m.end()&&uintval(i->second,out); }
} // namespace
bool parse_message(const char *data,size_t size,Message &out) {
    if(!data||!size||size>65536) return false;
    std::map<std::string,Value> m; Parser p(data,size); if(!p.root(m)) return false;
    Message result; uint64_t v,rev,ttl; std::string type;
    if(!getuint(m,"v",v)||v!=1||!getstr(m,"type",type)||!getstr(m,"lease_id",result.snapshot.lease)||!getuint(m,"revision",rev)||!rev) return false;
    result.snapshot.revision=rev;
    if(type=="voice.clear") { result.clear=true; if(m.count("text")||m.count("replay")||!getstr(m,"reason",result.reason) || (result.reason!="expired"&&result.reason!="closed"&&result.reason!="unpaired"&&result.reason!="revoked"&&result.reason!="reset")) return false; out=std::move(result); return true; }
    if(type!="voice.snapshot" || !getbool(m,"replay",result.replay)) return false;
    auto &s=result.snapshot; std::string phase,playback;
    if(!getstr(m,"session_id",s.session)||!getstr(m,"turn_id",s.turn)||!getstr(m,"phase",phase)||!getstr(m,"playback",playback)||!getstr(m,"text",s.text,false)||s.text.size()>8192||!getbool(m,"backend_active",s.backend_active)||!getbool(m,"text_truncated",s.text_truncated)||!getbool(m,"interrupted",s.interrupted)||!getbool(m,"active",s.active)||!getuint(m,"ttl_ms",ttl)||!ttl||ttl>150000) return false;
    s.ttl_ms=(uint32_t)ttl;
    if(phase=="listening")s.phase=Phase::Listening; else if(phase=="processing")s.phase=Phase::Processing; else if(phase=="lookup")s.phase=Phase::Lookup; else if(phase=="answer")s.phase=Phase::Answer; else if(phase=="approval_required")s.phase=Phase::ApprovalRequired; else if(phase=="error")s.phase=Phase::Error; else if(phase=="closed")s.phase=Phase::Closed; else return false;
    if(playback=="pending")s.playback=Playback::Pending; else if(playback=="drained")s.playback=Playback::Drained; else if(playback=="unknown")s.playback=Playback::Unknown; else return false;
    out=std::move(result); return true;
}
}
