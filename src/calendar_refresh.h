#pragma once
#include <stdint.h>
struct calendar_refresh_t {
 int year=0, month=0, day=0;
 int64_t completed_ms=0;
 bool attempted=false, failed=false;
 void complete(int y,int m,int d,int64_t now,bool error) {year=y;month=m;day=d;completed_ms=now;attempted=true;failed=error;}
 bool due(int64_t now) const {return attempted && now>=completed_ms && now-completed_ms >= (failed?30000:120000);}
};
