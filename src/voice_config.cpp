#include "voice_config.h"
#include <cstring>
#include <string>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#define voice_inet_pton lwip_inet_pton
#else
#include <arpa/inet.h>
#define voice_inet_pton inet_pton
#endif
static bool alpha_num(unsigned char c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9'); }
static bool valid_port(const char *port) {
    if (!*port) return false;
    unsigned value=0;
    for (; *port; ++port) { if (*port<'0'||*port>'9') return false; value=value*10+(*port-'0'); if (value>65535) return false; }
    return value>0;
}
static bool valid_dns_or_ipv4(const std::string &host) {
    if (host.empty() || host.size()>253 || host.front()=='.' || host.back()=='.') return false;
    bool numeric=true; size_t start=0;
    for (size_t i=0;i<=host.size();++i) {
        if (i<host.size() && host[i]!='.') { if (!(host[i]>='0' && host[i]<='9')) numeric=false; continue; }
        size_t n=i-start; if (!n || n>63 || host[start]=='-' || host[i-1]=='-') return false;
        for (size_t j=start;j<i;++j) if (!alpha_num(host[j]) && host[j]!='-') return false;
        start=i+1;
    }
    if (numeric) { unsigned char ipv4[4]; return voice_inet_pton(AF_INET,host.c_str(),ipv4)==1; }
    return true;
}
bool voice_config_valid(const char *base, const char *token) {
    if (!base || !token || strnlen(base,160)>=160 || strnlen(token,256)>=256 ||
        strncmp(base,"wss://",6) || !base[6] || !token[0]) return false;
    const char *authority=base+6;
    if (*authority=='[') {
        const char *end=strchr(authority,']');
        if (!end || end==authority+1 || (end[1] && (end[1]!=':' || !valid_port(end+2)))) return false;
        std::string host(authority+1,end); unsigned char ipv6[16];
        if (voice_inet_pton(AF_INET6,host.c_str(),ipv6)!=1) return false;
    } else {
        const char *colon=strchr(authority,':');
        if (colon && !valid_port(colon+1)) return false;
        std::string host=colon ? std::string(authority,colon) : std::string(authority);
        if (!valid_dns_or_ipv4(host)) return false;
    }
    for (const char *p=token; *p; ++p) if ((unsigned char)*p<33 || (unsigned char)*p>126) return false;
    return true;
}
