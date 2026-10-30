#pragma once

// UI copy catalog. Content supplied by Bridge (names, events, tasks and news)
// is deliberately not passed through this catalog.
void ui_strings_set_locale(const char *locale);
const char *ui_strings_locale(void);
const char *ui_text(const char *russian);
const char *ui_day_name(int weekday_monday_first);
const char *ui_month_name(int month_zero_based);
const char *ui_month_name_full(int month_zero_based);
