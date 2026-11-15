#pragma once
#include <stddef.h>
#include <stdint.h>
// Generic radio lease helper. The platform must use the SAME lock around any
// radio-stop operation. This helper emits no bytes if readiness changes.
namespace voice_pair_entropy {
struct Ops { void *ctx; bool (*lock)(void *); void (*unlock)(void *); bool (*ready)(void *); void (*fill)(void *,void *,size_t); };
inline bool fill(const Ops &ops,void *out,size_t n) {
  if(!out || !n || n>64) return false;
  auto clear=[&](){volatile uint8_t *p=static_cast<volatile uint8_t *>(out);for(size_t i=0;i<n;++i)p[i]=0;};
  if(!ops.lock||!ops.unlock||!ops.ready||!ops.fill||!ops.lock(ops.ctx)){clear();return false;}
  bool ok=ops.ready(ops.ctx);if(ok){ops.fill(ops.ctx,out,n);ok=ops.ready(ops.ctx);}ops.unlock(ops.ctx);
  if(!ok)clear();return ok;
}
}
