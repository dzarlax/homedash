#include "web_settings.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "cJSON.h"

namespace {
static bool copy_json_string(cJSON *obj, const char *key, char *dst, size_t cap,
                             bool required = false) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return !required;
    if (!cJSON_IsString(item) || !item->valuestring) return false;
    size_t n = strlen(item->valuestring);
    if (n >= cap) return false;
    memcpy(dst, item->valuestring, n + 1);
    return true;
}

static bool bool_field(cJSON *obj, const char *key, bool *dst) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return true;
    if (!cJSON_IsBool(item)) return false;
    *dst = cJSON_IsTrue(item);
    return true;
}

static bool terminated(const char *s, size_t cap) {
    return s && memchr(s, 0, cap) != nullptr;
}

static bool minute_value(const char *s, size_t cap) {
    if (!terminated(s, cap) || strlen(s) != 5 || s[2] != ':') return false;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[3]) || !isdigit((unsigned char)s[4])) return false;
    int hour = (s[0] - '0') * 10 + s[1] - '0';
    int minute = (s[3] - '0') * 10 + s[4] - '0';
    return hour < 24 && minute < 60;
}

static bool safe_text(const char *s, size_t cap) {
    if (!terminated(s, cap) || !s[0]) return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p < 0x20 || *p == 0x7f) return false;
    return true;
}

static bool bridge_id_valid(const char *s, size_t cap) {
    if (!terminated(s, cap)) return false;
    size_t n = strlen(s);
    if (!n) return true;
    if (n != 32) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool unsigned_field(cJSON *obj, const char *key, int min, int max,
                           uint16_t *dst) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint ||
        item->valueint < min || item->valueint > max) return false;
    *dst = (uint16_t)item->valueint;
    return true;
}
}

bool web_locale_valid(const char *locale) {
    return locale && (!strcmp(locale, "ru") || !strcmp(locale, "en") || !strcmp(locale, "sr-Latn"));
}

void web_preferences_defaults(web_preferences_t *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    strcpy(p->locale, "ru");
    strcpy(p->name, "HomeDash");
    strcpy(p->city, "Belgrade");
    strcpy(p->timezone, "Europe/Belgrade");
    strcpy(p->posix_timezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    strcpy(p->night_start, "23:00");
    strcpy(p->night_end, "07:00");
    p->photo_idle_seconds = 300;
    p->photo_interval_seconds = 60;
    p->idle_photos = true;
    p->photos_enabled = true;
    p->auto_update_check = true;
    p->visible_weather = p->visible_calendar = p->visible_transport = true;
    p->visible_health = p->visible_tasks = p->visible_news = p->visible_home = true;
}

bool web_preferences_valid(const web_preferences_t *p) {
    if (!p || !terminated(p->locale, sizeof(p->locale)) || !web_locale_valid(p->locale) ||
        !terminated(p->name, sizeof(p->name)) ||
        !bridge_id_valid(p->bridge_id, sizeof(p->bridge_id)) ||
        !terminated(p->city, sizeof(p->city)) ||
        !terminated(p->timezone, sizeof(p->timezone)) ||
        !terminated(p->posix_timezone, sizeof(p->posix_timezone)) ||
        !terminated(p->settings_url, sizeof(p->settings_url)) ||
        !terminated(p->night_start, sizeof(p->night_start)) ||
        !terminated(p->night_end, sizeof(p->night_end))) return false;
    const char *expected_tz = p && !strcmp(p->timezone, "UTC") ? "UTC0" :
        (p && !strcmp(p->timezone, "Europe/Belgrade")
             ? "CET-1CEST,M3.5.0,M10.5.0/3" : "");
    if (!p->revision || !safe_text(p->name, sizeof(p->name)) ||
        !safe_text(p->city, sizeof(p->city)) ||
        (strcmp(p->timezone, "UTC") && strcmp(p->timezone, "Europe/Belgrade")) ||
        !expected_tz[0] || strcmp(p->posix_timezone, expected_tz) ||
        !(strncmp(p->settings_url, "https://", 8) == 0 ||
          strncmp(p->settings_url, "http://", 7) == 0) ||
        !safe_text(p->settings_url, sizeof(p->settings_url)) ||
        !minute_value(p->night_start, sizeof(p->night_start)) ||
        !minute_value(p->night_end, sizeof(p->night_end)) ||
        p->photo_idle_seconds < 30 || p->photo_idle_seconds > 3600 ||
        p->photo_interval_seconds < 15 || p->photo_interval_seconds > 3600) return false;
    return true;
}

bool web_preferences_parse(const char *json, size_t length, uint32_t revision,
                           const char *bridge_id, const char *posix_timezone,
                           const char *settings_url,
                           web_preferences_t *out) {
    if (!json || !length || length > 8192 || !revision || !bridge_id ||
        strlen(bridge_id) != 32 || !posix_timezone ||
        !settings_url || !out || strlen(posix_timezone) >= sizeof(out->posix_timezone) ||
        strlen(settings_url) >= sizeof(out->settings_url)) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!root) return false;
    web_preferences_t p;
    web_preferences_defaults(&p);
    p.revision = revision;
    strcpy(p.bridge_id, bridge_id);
    strcpy(p.posix_timezone, posix_timezone);
    strcpy(p.settings_url, settings_url);
    cJSON *screen = cJSON_GetObjectItemCaseSensitive(root, "screen");
    cJSON *photos = cJSON_GetObjectItemCaseSensitive(root, "photos");
    cJSON *visible = cJSON_GetObjectItemCaseSensitive(root, "visible");
    bool ok = cJSON_IsObject(screen) && cJSON_IsObject(photos) && cJSON_IsObject(visible);
    if (ok) {
        ok = copy_json_string(screen, "locale", p.locale, sizeof(p.locale)) &&
             copy_json_string(screen, "name", p.name, sizeof(p.name), true) &&
             copy_json_string(screen, "city", p.city, sizeof(p.city), true) &&
             copy_json_string(screen, "timezone", p.timezone, sizeof(p.timezone), true) &&
             copy_json_string(screen, "night_start", p.night_start, sizeof(p.night_start)) &&
             copy_json_string(screen, "night_end", p.night_end, sizeof(p.night_end)) &&
             bool_field(screen, "night_enabled", &p.night_enabled) &&
             bool_field(screen, "auto_update_check", &p.auto_update_check) &&
             bool_field(screen, "auto_install", &p.auto_install) &&
             bool_field(photos, "enabled", &p.photos_enabled) &&
             unsigned_field(photos, "idle_seconds", 30, 3600, &p.photo_idle_seconds) &&
             unsigned_field(photos, "interval_seconds", 15, 3600, &p.photo_interval_seconds) &&
             bool_field(visible, "weather", &p.visible_weather) &&
             bool_field(visible, "calendar", &p.visible_calendar) &&
             bool_field(visible, "transport", &p.visible_transport) &&
             bool_field(visible, "health", &p.visible_health) &&
             bool_field(visible, "tasks", &p.visible_tasks) &&
             bool_field(visible, "news", &p.visible_news) &&
             bool_field(visible, "home", &p.visible_home);
        cJSON *clock = cJSON_GetObjectItemCaseSensitive(screen, "clock_format");
        if (clock) {
            if (!cJSON_IsString(clock)) ok = false;
            else if (!strcmp(clock->valuestring, "12h")) p.clock_12h = true;
            else if (!strcmp(clock->valuestring, "24h")) p.clock_12h = false;
            else ok = false;
        }
        cJSON *idle = cJSON_GetObjectItemCaseSensitive(screen, "idle_action");
        if (idle) {
            if (!cJSON_IsString(idle)) ok = false;
            else if (!strcmp(idle->valuestring, "photos")) p.idle_photos = true;
            else if (!strcmp(idle->valuestring, "off")) p.idle_photos = false;
            else ok = false;
        }
    }
    cJSON_Delete(root);
    if (!ok || !web_preferences_valid(&p)) return false;
    *out = p;
    return true;
}

bool web_preferences_revision_allowed(const web_preferences_t *current,
                                      bool has_current, uint32_t incoming_revision,
                                      const char *incoming_bridge_id) {
    if (!incoming_revision || !incoming_bridge_id || strlen(incoming_bridge_id) != 32 ||
        !bridge_id_valid(incoming_bridge_id, 33)) return false;
    if (!has_current) return true;
    if (!current || !web_preferences_valid(current)) return false;
    return incoming_revision >= current->revision ||
           strcmp(incoming_bridge_id, current->bridge_id) != 0;
}

bool web_command_id_valid(const char *id) {
    if (!id) return false;
    size_t n = strlen(id);
    if (!n || n > 64) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)id[i];
        if (!(isalnum(c) || c == '-' || c == '_')) return false;
    }
    return true;
}

bool web_command_is_new(const char *last_id, const char *candidate_id) {
    return web_command_id_valid(candidate_id) &&
           (!last_id || strcmp(last_id, candidate_id) != 0);
}
