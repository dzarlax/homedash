#include "voice_recovery_request.h"
#include "cJSON.h"
#include <cstring>
bool voice_recovery_parse(const char *body, size_t length, VoiceProvision &out, bool &disable) {
    voice_secure_zero(&out,sizeof(out)); disable=false;
    if (!body || !length || length>1024 || memchr(body,0,length) || body[length]!=0 || strstr(body,"\\u0000")) return false;
    const char *end=nullptr;
    cJSON *root=cJSON_ParseWithLengthOpts(body,length+1,&end,true);
    if (!root) return false;
    cJSON *url=cJSON_GetObjectItemCaseSensitive(root,"base_url"), *token=cJSON_GetObjectItemCaseSensitive(root,"token");
    cJSON *flag=cJSON_GetObjectItemCaseSensitive(root,"disable");
    unsigned urls=0,tokens=0,flags=0,fields=0;
    for (cJSON *item=root->child; item; item=item->next) {
        ++fields;
        if (item->string && !strcmp(item->string,"base_url")) ++urls;
        else if (item->string && !strcmp(item->string,"token")) ++tokens;
        else if (item->string && !strcmp(item->string,"disable")) ++flags;
    }
    bool valid=cJSON_IsObject(root) && fields==3 && urls==1 && tokens==1 && flags==1 &&
        cJSON_IsBool(flag) && cJSON_IsString(url) && cJSON_IsString(token) && url->valuestring && token->valuestring;
    if (valid) {
        disable=cJSON_IsTrue(flag);
        valid=disable ? (!url->valuestring[0] && !token->valuestring[0]) : voice_config_valid(url->valuestring,token->valuestring);
        if (valid && !disable) { out.version=1; strcpy(out.base_url,url->valuestring); strcpy(out.token,token->valuestring); }
    }
    for (cJSON *item=root->child; item; item=item->next)
        if (item->string && !strcmp(item->string,"token") && cJSON_IsString(item) && item->valuestring)
            voice_secure_zero(item->valuestring,strlen(item->valuestring));
    cJSON_Delete(root); if (!valid) { voice_secure_zero(&out,sizeof(out)); disable=false; }
    return valid;
}
