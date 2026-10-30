#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Compact, versioned subset of Bridge settings consumed by the display.
// Frozen v2 binary layout used by preferences and the v1 network journal.
struct web_preferences_v2_t {
    uint32_t revision;
    char bridge_id[33];
    char name[33];
    char city[49];
    char timezone[40];
    char posix_timezone[48];
    char settings_url[192];
    char night_start[6];
    char night_end[6];
    uint16_t photo_idle_seconds;
    uint16_t photo_interval_seconds;
    bool clock_12h;
    bool idle_photos;
    bool night_enabled;
    bool photos_enabled;
    bool auto_update_check;
    bool auto_install;
    bool visible_weather;
    bool visible_calendar;
    bool visible_transport;
    bool visible_health;
    bool visible_tasks;
    bool visible_news;
    bool visible_home;
};

static_assert(sizeof(web_preferences_v2_t) == 432, "Preserve the v2 NVS layout");

struct web_favorite_t {
    char entity_id[48];
    char kind[8];
    char label[33];
};

// Runtime-only extension; never persist this structure as a raw blob.
struct web_preferences_t : web_preferences_v2_t {
    char locale[8];
    uint8_t backlight_percent; // zero = absent
    uint8_t night_backlight_percent;
    bool night_dim;
    uint8_t favorite_count;
    web_favorite_t favorites[4];
};

bool web_locale_valid(const char *locale);
void web_preferences_defaults(web_preferences_t *out);
bool web_preferences_valid(const web_preferences_t *prefs);
bool web_preferences_parse(const char *json, size_t length,
                           uint32_t revision, const char *bridge_id,
                           const char *posix_timezone,
                           const char *settings_url, web_preferences_t *out);
bool web_preferences_revision_allowed(const web_preferences_t *current,
                                      bool has_current, uint32_t incoming_revision,
                                      const char *incoming_bridge_id);

// A command is new only when it differs from the durable last-seen ID.
bool web_command_id_valid(const char *id);
bool web_command_is_new(const char *last_id, const char *candidate_id);
