#include <stdlib.h>
#include <stddef.h>
#include <cstring>
static bool fail_entropy = false;
void voice_pair_test_entropy_fail(bool fail) { fail_entropy = fail; }
bool wifi_pairing_fill_entropy(void *p, size_t n) {
  if (fail_entropy) { std::memset(p,0,n); return false; }
  arc4random_buf(p,n); return true;
}
