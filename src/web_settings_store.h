#pragma once
#include "web_settings.h"
#include "nvs_config.h"

bool web_settings_store_load(web_preferences_t *out);
// True only for a durable locale record matching these preferences.
bool web_settings_store_locale_confirmed(const web_preferences_t *prefs);
bool web_settings_store_save(const web_preferences_t *prefs);
bool web_settings_store_command_begin(const char *id, const char *action,
                                      const char *boot_id, const char *firmware);
bool web_settings_store_command_finish(const char *id, const char *state,
                                       const char *error);
bool web_settings_store_command_result(char *id, size_t id_size,
                                       char *state, size_t state_size,
                                       char *error, size_t error_size);
bool web_settings_store_command_id(char *id, size_t size);
bool web_settings_store_clear_preferences(void);
bool web_settings_store_recover_on_boot(const char *boot_id, const char *firmware,
                                        bool config_present);
bool web_settings_store_complete_reset_after_recovery(void);
bool web_settings_store_network_trial_begin(const device_config_t *old_config,
                                            bool had_config,
                                            const web_preferences_t *old_preferences,
                                            bool had_preferences);
bool web_settings_store_network_trial_mark_committed(void);
bool web_settings_store_network_trial_read(device_config_t *old_config,
                                           bool *had_config,
                                           web_preferences_t *old_preferences,
                                           bool *had_preferences,
                                           bool *committed);
bool web_settings_store_network_trial_clear(void);
bool web_settings_store_network_trial_recover_pending(void);
bool web_settings_store_sync_allowed(void);
