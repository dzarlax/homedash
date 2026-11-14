#pragma once
#include "voice_config.h"
bool voice_config_store_save(const char *base, const char *token);
bool voice_config_store_load(VoiceProvision &out);
bool voice_config_store_clear();
