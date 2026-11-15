#include "../src/voice_push_client.h"
#include <cassert>
int main() {
    using C = VoiceBootChoice;
    assert(voice_push_boot_retry(true));
    assert(!voice_push_boot_retry(false));
    assert(voice_push_boot_choice(true, false, false, false, false, false, false)==C::Legacy);
    assert(voice_push_boot_choice(false, true, false, false, false, false, false)==C::Legacy);
    assert(voice_push_boot_choice(false, true, false, false, false, true, false)==C::Disabled);
    assert(voice_push_boot_choice(false, true, false, true, false, false, false)==C::Disabled);
    assert(voice_push_boot_choice(false, true, false, false, true, false, false)==C::Disabled);
    assert(voice_push_boot_choice(false, false, false, false, false, false, false)==C::Disabled);
    assert(voice_push_boot_choice(false, true, true, true, false, false, false)==C::Disabled);
    assert(voice_push_boot_choice(false, true, true, false, true, true, false)==C::Active);
    assert(voice_push_boot_choice(false, true, true, false, false, true, true)==C::Disabled);
}
