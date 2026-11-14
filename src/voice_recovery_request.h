#pragma once
#include "voice_config.h"
// body is NUL-terminated and length excludes the NUL. Outputs are always initialized.
bool voice_recovery_parse(const char *body, size_t length, VoiceProvision &out, bool &disable);
