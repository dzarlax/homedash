#include "ui_agentdeck.h"
#include "lvgl.h"
#include "agentdeck.h"
#include <cstring>
#include <cstdio>
#include <ctime>

// Fonts
LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);

// Colors
static const lv_color_t CLR_BG        = lv_color_hex(0x1A1A2E);
static const lv_color_t CLR_CARD      = lv_color_hex(0x16213E);
static const lv_color_t CLR_TEXT      = lv_color_hex(0xE0E0E0);
static const lv_color_t CLR_TEXT_DIM  = lv_color_hex(0x888888);
static const lv_color_t CLR_HIGHLIGHT = lv_color_hex(0x53A8E2);
static const lv_color_t CLR_GREEN     = lv_color_hex(0x4CAF50);
static const lv_color_t CLR_YELLOW    = lv_color_hex(0xFFC107);
static const lv_color_t CLR_ORANGE    = lv_color_hex(0xFF9800);
static const lv_color_t CLR_RED       = lv_color_hex(0xF44336);

// ---------------------------------------------------------------
// Header / status
// ---------------------------------------------------------------
static lv_obj_t *lbl_conn_status = NULL;
static lv_obj_t *dot_conn       = NULL;

// ---------------------------------------------------------------
// State card (top-left)
// ---------------------------------------------------------------
static lv_obj_t *lbl_state   = NULL;
static lv_obj_t *dot_state   = NULL;
static lv_obj_t *lbl_project = NULL;
static lv_obj_t *lbl_model   = NULL;
static lv_obj_t *lbl_tool    = NULL;

// ---------------------------------------------------------------
// Usage card (top-right)
// ---------------------------------------------------------------
static lv_obj_t *bar_5h      = NULL;
static lv_obj_t *lbl_5h_pct  = NULL;
static lv_obj_t *lbl_5h_reset = NULL;
static lv_obj_t *bar_7d      = NULL;
static lv_obj_t *lbl_7d_pct  = NULL;
static lv_obj_t *lbl_7d_reset = NULL;
static lv_obj_t *lbl_tokens  = NULL;
static lv_obj_t *lbl_cost    = NULL;

// ---------------------------------------------------------------
// Session cards (bottom area)
// Two modes: overview (≤3 sessions, tall cards) and action (awaiting, thin strip + options panel)
// ---------------------------------------------------------------

// Overview cards — used when not awaiting or when no options
struct OvCard {
    lv_obj_t *card;
    lv_obj_t *state_dot;
    lv_obj_t *state_lbl;
    lv_obj_t *project_lbl;
    lv_obj_t *model_lbl;
    // Option buttons (visible only in tall mode, focused+awaiting session)
    lv_obj_t *opt_btns[AD_MAX_OPTIONS];
    lv_obj_t *opt_lbls[AD_MAX_OPTIONS];
};
static OvCard s_ov[AD_MAX_SESSIONS];

// Compact chips — used in action mode (thin strip above options panel)
struct CmChip {
    lv_obj_t *chip;
    lv_obj_t *state_dot;
    lv_obj_t *project_lbl;
};
static CmChip s_cm[AD_MAX_SESSIONS];

// Options panel
static lv_obj_t *s_opt_panel     = NULL;
static lv_obj_t *s_opt_title_lbl = NULL;  // "ProjectName"
static lv_obj_t *s_opt_btns[AD_MAX_OPTIONS] = {};
static lv_obj_t *s_opt_lbls[AD_MAX_OPTIONS] = {};

// No-sessions placeholder
static lv_obj_t *s_no_sessions_lbl = NULL;

// Interrupt button (top-right, visible when processing)
static lv_obj_t *btn_interrupt = NULL;

// ---------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------
static void interrupt_btn_cb(lv_event_t *e);
static void opt_btn_panel_cb(lv_event_t *e);
static void opt_btn_card_cb(lv_event_t *e);

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------
static lv_color_t state_color(agentdeck_agent_state_t s)
{
    switch (s) {
    case AD_STATE_PROCESSING: return CLR_HIGHLIGHT;
    case AD_STATE_AWAITING:   return CLR_ORANGE;
    default:                  return CLR_TEXT_DIM;
    }
}

static const char *state_text(agentdeck_agent_state_t s)
{
    switch (s) {
    case AD_STATE_PROCESSING: return "Processing";
    case AD_STATE_AWAITING:   return "Awaiting";
    default:                  return "Idle";
    }
}

static lv_color_t bar_color(float pct)
{
    if (pct < 60) return CLR_GREEN;
    if (pct < 85) return CLR_YELLOW;
    return CLR_RED;
}

static void format_tokens(char *buf, size_t sz, uint32_t tokens)
{
    if (tokens >= 1000000)
        snprintf(buf, sz, "%.1fM", tokens / 1000000.0f);
    else if (tokens >= 1000)
        snprintf(buf, sz, "%.0fK", tokens / 1000.0f);
    else
        snprintf(buf, sz, "%u", (unsigned)tokens);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, CLR_CARD, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// ---------------------------------------------------------------
// Create page
// ---------------------------------------------------------------
void ui_agentdeck_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parent, CLR_BG, 0);

    // ---- HEADER ----
    lv_obj_t *lbl_title = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl_title, &font_montserrat_24_cyr, 0);
    lv_obj_set_style_text_color(lbl_title, CLR_HIGHLIGHT, 0);
    lv_label_set_text(lbl_title, "AgentDeck");
    lv_obj_set_pos(lbl_title, 20, 8);

    dot_conn = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_conn);
    lv_obj_set_size(dot_conn, 10, 10);
    lv_obj_set_style_radius(dot_conn, 5, 0);
    lv_obj_set_style_bg_opa(dot_conn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot_conn, CLR_RED, 0);
    lv_obj_set_pos(dot_conn, 200, 15);

    lbl_conn_status = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl_conn_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_conn_status, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_conn_status, "Searching...");
    lv_obj_set_pos(lbl_conn_status, 218, 11);

    btn_interrupt = lv_btn_create(parent);
    lv_obj_set_size(btn_interrupt, 120, 32);
    lv_obj_set_pos(btn_interrupt, 880, 5);
    lv_obj_set_style_bg_color(btn_interrupt, CLR_RED, 0);
    lv_obj_set_style_radius(btn_interrupt, 6, 0);
    lv_obj_add_flag(btn_interrupt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn_interrupt, interrupt_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_btn = lv_label_create(btn_interrupt);
    lv_label_set_text(lbl_btn, "Interrupt");
    lv_obj_set_style_text_color(lbl_btn, lv_color_white(), 0);
    lv_obj_center(lbl_btn);

    // ---- STATE CARD (top-left) ----
    lv_obj_t *state_card = make_card(parent, 5, 45, 410, 160);

    dot_state = lv_obj_create(state_card);
    lv_obj_remove_style_all(dot_state);
    lv_obj_set_size(dot_state, 14, 14);
    lv_obj_set_style_radius(dot_state, 7, 0);
    lv_obj_set_style_bg_opa(dot_state, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot_state, CLR_TEXT_DIM, 0);
    lv_obj_set_pos(dot_state, 0, 3);

    lbl_state = lv_label_create(state_card);
    lv_obj_set_style_text_font(lbl_state, &font_montserrat_24_cyr, 0);
    lv_obj_set_style_text_color(lbl_state, CLR_TEXT, 0);
    lv_label_set_text(lbl_state, "Idle");
    lv_obj_set_pos(lbl_state, 22, 0);

    lbl_project = lv_label_create(state_card);
    lv_obj_set_style_text_font(lbl_project, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(lbl_project, CLR_TEXT, 0);
    lv_label_set_text(lbl_project, "Project: ---");
    lv_obj_set_pos(lbl_project, 0, 40);

    lbl_model = lv_label_create(state_card);
    lv_obj_set_style_text_font(lbl_model, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(lbl_model, CLR_TEXT, 0);
    lv_label_set_text(lbl_model, "Model: ---");
    lv_obj_set_pos(lbl_model, 0, 65);

    lbl_tool = lv_label_create(state_card);
    lv_obj_set_style_text_font(lbl_tool, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(lbl_tool, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_tool, "");
    lv_obj_set_pos(lbl_tool, 0, 90);
    lv_obj_set_width(lbl_tool, 390);
    lv_label_set_long_mode(lbl_tool, LV_LABEL_LONG_DOT);

    // ---- USAGE CARD (top-right) ----
    lv_obj_t *usage_card = make_card(parent, 425, 45, 594, 160);

    lv_obj_t *lbl_5h_title = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_5h_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_5h_title, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_5h_title, "5h Usage");
    lv_obj_set_pos(lbl_5h_title, 0, 0);

    bar_5h = lv_bar_create(usage_card);
    lv_obj_set_size(bar_5h, 400, 18);
    lv_obj_set_pos(bar_5h, 0, 20);
    lv_bar_set_range(bar_5h, 0, 100);
    lv_bar_set_value(bar_5h, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_5h, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_5h, CLR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_5h, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_5h, 4, LV_PART_INDICATOR);

    lbl_5h_pct = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_5h_pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_5h_pct, CLR_TEXT, 0);
    lv_label_set_text(lbl_5h_pct, "---");
    lv_obj_set_pos(lbl_5h_pct, 410, 20);

    lbl_5h_reset = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_5h_reset, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_5h_reset, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_5h_reset, "");
    lv_obj_set_pos(lbl_5h_reset, 0, 42);

    lv_obj_t *lbl_7d_title = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_7d_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_7d_title, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_7d_title, "7d Usage");
    lv_obj_set_pos(lbl_7d_title, 0, 60);

    bar_7d = lv_bar_create(usage_card);
    lv_obj_set_size(bar_7d, 400, 18);
    lv_obj_set_pos(bar_7d, 0, 80);
    lv_bar_set_range(bar_7d, 0, 100);
    lv_bar_set_value(bar_7d, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_7d, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_7d, CLR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_7d, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_7d, 4, LV_PART_INDICATOR);

    lbl_7d_pct = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_7d_pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_7d_pct, CLR_TEXT, 0);
    lv_label_set_text(lbl_7d_pct, "---");
    lv_obj_set_pos(lbl_7d_pct, 410, 80);

    lbl_7d_reset = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_7d_reset, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_7d_reset, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_7d_reset, "");
    lv_obj_set_pos(lbl_7d_reset, 0, 102);

    lbl_tokens = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_tokens, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tokens, CLR_TEXT_DIM, 0);
    lv_label_set_text(lbl_tokens, "");
    lv_obj_set_pos(lbl_tokens, 0, 125);

    lbl_cost = lv_label_create(usage_card);
    lv_obj_set_style_text_font(lbl_cost, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_cost, CLR_HIGHLIGHT, 0);
    lv_label_set_text(lbl_cost, "");
    lv_obj_set_pos(lbl_cost, 350, 125);

    // ---- SESSION CARDS: OVERVIEW MODE (pre-created, positioned in update) ----
    for (int i = 0; i < AD_MAX_SESSIONS; i++) {
        OvCard &c = s_ov[i];
        // Initial placeholder size; repositioned in update
        c.card = make_card(parent, 5, 220, 328, 355);
        lv_obj_add_flag(c.card, LV_OBJ_FLAG_HIDDEN);

        c.state_dot = lv_obj_create(c.card);
        lv_obj_remove_style_all(c.state_dot);
        lv_obj_set_size(c.state_dot, 12, 12);
        lv_obj_set_style_radius(c.state_dot, 6, 0);
        lv_obj_set_style_bg_opa(c.state_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c.state_dot, CLR_TEXT_DIM, 0);
        lv_obj_set_pos(c.state_dot, 0, 6);

        c.state_lbl = lv_label_create(c.card);
        lv_obj_set_style_text_font(c.state_lbl, &font_montserrat_16_cyr, 0);
        lv_obj_set_style_text_color(c.state_lbl, CLR_TEXT, 0);
        lv_label_set_text(c.state_lbl, "Idle");
        lv_obj_set_pos(c.state_lbl, 20, 3);

        c.project_lbl = lv_label_create(c.card);
        lv_obj_set_style_text_font(c.project_lbl, &font_montserrat_24_cyr, 0);
        lv_obj_set_style_text_color(c.project_lbl, CLR_TEXT, 0);
        lv_label_set_text(c.project_lbl, "---");
        lv_obj_set_pos(c.project_lbl, 0, 30);
        lv_obj_set_width(c.project_lbl, 300);
        lv_label_set_long_mode(c.project_lbl, LV_LABEL_LONG_DOT);

        c.model_lbl = lv_label_create(c.card);
        lv_obj_set_style_text_font(c.model_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(c.model_lbl, CLR_TEXT_DIM, 0);
        lv_label_set_text(c.model_lbl, "");
        lv_obj_set_pos(c.model_lbl, 0, 65);

        // Option buttons (tall mode only; hidden by default)
        for (int j = 0; j < AD_MAX_OPTIONS; j++) {
            c.opt_btns[j] = lv_btn_create(c.card);
            lv_obj_set_size(c.opt_btns[j], lv_pct(100), 44);
            lv_obj_set_pos(c.opt_btns[j], 0, 100 + j * 52);
            lv_obj_set_style_radius(c.opt_btns[j], 6, 0);
            lv_obj_set_style_bg_color(c.opt_btns[j], lv_color_hex(0x2A4070), 0);
            lv_obj_add_event_cb(c.opt_btns[j], opt_btn_card_cb,
                                LV_EVENT_CLICKED, (void *)(intptr_t)j);
            lv_obj_add_flag(c.opt_btns[j], LV_OBJ_FLAG_HIDDEN);

            c.opt_lbls[j] = lv_label_create(c.opt_btns[j]);
            lv_obj_set_style_text_font(c.opt_lbls[j], &font_montserrat_16_cyr, 0);
            lv_obj_set_style_text_color(c.opt_lbls[j], CLR_TEXT, 0);
            lv_label_set_long_mode(c.opt_lbls[j], LV_LABEL_LONG_DOT);
            lv_obj_set_width(c.opt_lbls[j], LV_PCT(90));
            lv_obj_align(c.opt_lbls[j], LV_ALIGN_LEFT_MID, 8, 0);
            lv_label_set_text(c.opt_lbls[j], "");
        }
    }

    // ---- SESSION CHIPS: ACTION MODE (compact strip) ----
    for (int i = 0; i < AD_MAX_SESSIONS; i++) {
        CmChip &ch = s_cm[i];
        ch.chip = make_card(parent, 5, 220, 160, 88);
        lv_obj_set_style_border_width(ch.chip, 0, 0);
        lv_obj_add_flag(ch.chip, LV_OBJ_FLAG_HIDDEN);

        ch.state_dot = lv_obj_create(ch.chip);
        lv_obj_remove_style_all(ch.state_dot);
        lv_obj_set_size(ch.state_dot, 10, 10);
        lv_obj_set_style_radius(ch.state_dot, 5, 0);
        lv_obj_set_style_bg_opa(ch.state_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ch.state_dot, CLR_TEXT_DIM, 0);
        lv_obj_set_pos(ch.state_dot, 0, 4);

        ch.project_lbl = lv_label_create(ch.chip);
        lv_obj_set_style_text_font(ch.project_lbl, &font_montserrat_16_cyr, 0);
        lv_obj_set_style_text_color(ch.project_lbl, CLR_TEXT, 0);
        lv_label_set_text(ch.project_lbl, "---");
        lv_obj_set_pos(ch.project_lbl, 0, 22);
        lv_obj_set_width(ch.project_lbl, 130);
        lv_label_set_long_mode(ch.project_lbl, LV_LABEL_LONG_DOT);
    }

    // ---- OPTIONS PANEL (action mode, below compact strip) ----
    s_opt_panel = make_card(parent, 5, 320, 1014, 258);
    lv_obj_set_style_border_width(s_opt_panel, 2, 0);
    lv_obj_set_style_border_color(s_opt_panel, CLR_ORANGE, 0);
    lv_obj_add_flag(s_opt_panel, LV_OBJ_FLAG_HIDDEN);

    s_opt_title_lbl = lv_label_create(s_opt_panel);
    lv_obj_set_style_text_font(s_opt_title_lbl, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(s_opt_title_lbl, CLR_ORANGE, 0);
    lv_label_set_text(s_opt_title_lbl, "Awaiting input");
    lv_obj_set_pos(s_opt_title_lbl, 0, 0);

    for (int j = 0; j < AD_MAX_OPTIONS; j++) {
        s_opt_btns[j] = lv_btn_create(s_opt_panel);
        lv_obj_set_size(s_opt_btns[j], lv_pct(100), 38);
        lv_obj_set_pos(s_opt_btns[j], 0, 28 + j * 46);
        lv_obj_set_style_radius(s_opt_btns[j], 6, 0);
        lv_obj_set_style_bg_color(s_opt_btns[j], lv_color_hex(0x2A4070), 0);
        lv_obj_add_event_cb(s_opt_btns[j], opt_btn_panel_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)j);
        lv_obj_add_flag(s_opt_btns[j], LV_OBJ_FLAG_HIDDEN);

        s_opt_lbls[j] = lv_label_create(s_opt_btns[j]);
        lv_obj_set_style_text_font(s_opt_lbls[j], &font_montserrat_16_cyr, 0);
        lv_obj_set_style_text_color(s_opt_lbls[j], CLR_TEXT, 0);
        lv_label_set_long_mode(s_opt_lbls[j], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_opt_lbls[j], LV_PCT(90));
        lv_obj_align(s_opt_lbls[j], LV_ALIGN_LEFT_MID, 8, 0);
        lv_label_set_text(s_opt_lbls[j], "");
    }

    // ---- NO SESSIONS PLACEHOLDER ----
    s_no_sessions_lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(s_no_sessions_lbl, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(s_no_sessions_lbl, CLR_TEXT_DIM, 0);
    lv_label_set_text(s_no_sessions_lbl, "No active sessions");
    lv_obj_set_pos(s_no_sessions_lbl, 400, 380);
    lv_obj_add_flag(s_no_sessions_lbl, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------

// Card x-position for column i out of count cards in 1014px width
static int card_x(int i, int count, int card_w)
{
    int total = count * card_w + (count - 1) * 8;
    int left = (1014 - total) / 2;
    return left + i * (card_w + 8);
}

static void hide_all_cards()
{
    for (int i = 0; i < AD_MAX_SESSIONS; i++) {
        lv_obj_add_flag(s_ov[i].card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cm[i].chip, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_opt_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_no_sessions_lbl, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------
// Update page from data
// ---------------------------------------------------------------
void ui_agentdeck_update(const agentdeck_data_t *data)
{
    if (!data) return;

    // ---- Connection status ----
    switch (data->conn_state) {
    case AD_CONN_CONNECTED:
        lv_obj_set_style_bg_color(dot_conn, CLR_GREEN, 0);
        lv_label_set_text(lbl_conn_status, "Connected");
        break;
    case AD_CONN_CONNECTING:
        lv_obj_set_style_bg_color(dot_conn, CLR_YELLOW, 0);
        lv_label_set_text(lbl_conn_status, "Connecting...");
        break;
    case AD_CONN_RECONNECTING:
        lv_obj_set_style_bg_color(dot_conn, CLR_YELLOW, 0);
        lv_label_set_text(lbl_conn_status, "Reconnecting...");
        break;
    default:
        lv_obj_set_style_bg_color(dot_conn, CLR_RED, 0);
        lv_label_set_text(lbl_conn_status, "Searching...");
        break;
    }

    // ---- Agent state (focused session) ----
    lv_label_set_text(lbl_state, state_text(data->agent_state));
    lv_obj_set_style_bg_color(dot_state, state_color(data->agent_state), 0);

    if (data->agent_state != AD_STATE_IDLE && data->conn_state == AD_CONN_CONNECTED)
        lv_obj_clear_flag(btn_interrupt, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(btn_interrupt, LV_OBJ_FLAG_HIDDEN);

    char buf[128];
    snprintf(buf, sizeof(buf), "Project: %s",
             data->project_name[0] ? data->project_name : "---");
    lv_label_set_text(lbl_project, buf);

    snprintf(buf, sizeof(buf), "Model: %s",
             data->model_name[0] ? data->model_name : "---");
    lv_label_set_text(lbl_model, buf);

    if (data->current_tool[0] && data->agent_state != AD_STATE_IDLE) {
        snprintf(buf, sizeof(buf), "Tool: %s", data->current_tool);
        lv_label_set_text(lbl_tool, buf);
    } else {
        lv_label_set_text(lbl_tool, "");
    }

    // ---- Usage ----
    if (data->usage.valid) {
        float pct5 = data->usage.five_hour_pct;
        if (pct5 >= 0) {
            lv_bar_set_value(bar_5h, (int32_t)pct5, LV_ANIM_ON);
            lv_obj_set_style_bg_color(bar_5h, bar_color(pct5), LV_PART_INDICATOR);
            snprintf(buf, sizeof(buf), "%.0f%%", pct5);
            lv_label_set_text(lbl_5h_pct, buf);
        } else {
            lv_bar_set_value(bar_5h, 0, LV_ANIM_OFF);
            lv_label_set_text(lbl_5h_pct, "---");
        }
        if (data->usage.five_hour_resets[0]) {
            snprintf(buf, sizeof(buf), "Resets: %s", data->usage.five_hour_resets);
            lv_label_set_text(lbl_5h_reset, buf);
        }

        float pct7 = data->usage.seven_day_pct;
        if (pct7 >= 0) {
            lv_bar_set_value(bar_7d, (int32_t)pct7, LV_ANIM_ON);
            lv_obj_set_style_bg_color(bar_7d, bar_color(pct7), LV_PART_INDICATOR);
            snprintf(buf, sizeof(buf), "%.0f%%", pct7);
            lv_label_set_text(lbl_7d_pct, buf);
        } else {
            lv_bar_set_value(bar_7d, 0, LV_ANIM_OFF);
            lv_label_set_text(lbl_7d_pct, "---");
        }
        if (data->usage.seven_day_resets[0]) {
            snprintf(buf, sizeof(buf), "Resets: %s", data->usage.seven_day_resets);
            lv_label_set_text(lbl_7d_reset, buf);
        }

        char in_buf[16], out_buf[16];
        format_tokens(in_buf, sizeof(in_buf), data->usage.input_tokens);
        format_tokens(out_buf, sizeof(out_buf), data->usage.output_tokens);
        snprintf(buf, sizeof(buf), "Tokens: %s in / %s out", in_buf, out_buf);
        lv_label_set_text(lbl_tokens, buf);

        if (data->usage.estimated_cost_usd > 0) {
            snprintf(buf, sizeof(buf), "$%.2f", data->usage.estimated_cost_usd);
            lv_label_set_text(lbl_cost, buf);
        }
    }

    // ---- Session cards (bottom area) ----
    hide_all_cards();

    int count = data->session_count;

    if (count == 0) {
        // No sessions — show placeholder
        lv_obj_clear_flag(s_no_sessions_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Determine if we should show action mode (focused session awaiting with options)
    bool action_mode = (data->option_count > 0 &&
                        data->agent_state == AD_STATE_AWAITING &&
                        data->conn_state == AD_CONN_CONNECTED);

    if (action_mode) {
        // ---- ACTION MODE: compact chips + options panel ----

        // Layout compact chips
        int chip_count = count < AD_MAX_SESSIONS ? count : AD_MAX_SESSIONS;
        // Chips: up to 6 in one row, chip_w = (1014 - gaps) / chip_count
        int chip_w = (1014 - (chip_count - 1) * 8) / chip_count;
        if (chip_w > 240) chip_w = 240;  // cap at 240px for readability
        int chip_h = 88;

        for (int i = 0; i < chip_count; i++) {
            const agentdeck_session_t &s = data->sessions[i];
            CmChip &ch = s_cm[i];

            int cx = 5 + i * (chip_w + 8);
            lv_obj_set_pos(ch.chip, cx, 220);
            lv_obj_set_size(ch.chip, chip_w, chip_h);

            // Highlight focused chip
            if (s.focused) {
                lv_obj_set_style_border_width(ch.chip, 2, 0);
                lv_obj_set_style_border_color(ch.chip, CLR_ORANGE, 0);
            } else {
                lv_obj_set_style_border_width(ch.chip, 0, 0);
            }

            lv_obj_set_style_bg_color(ch.state_dot, state_color(s.state), 0);
            lv_label_set_text(ch.project_lbl,
                              s.project_name[0] ? s.project_name : "---");
            lv_obj_set_width(ch.project_lbl, chip_w - 20);

            lv_obj_clear_flag(ch.chip, LV_OBJ_FLAG_HIDDEN);
        }

        // Options panel
        lv_obj_clear_flag(s_opt_panel, LV_OBJ_FLAG_HIDDEN);

        // Title: focused session project name
        const char *proj = data->project_name[0] ? data->project_name : "Session";
        snprintf(buf, sizeof(buf), "%s — awaiting", proj);
        lv_label_set_text(s_opt_title_lbl, buf);

        for (int j = 0; j < AD_MAX_OPTIONS; j++) {
            if (j < data->option_count) {
                const agentdeck_option_t &opt = data->options[j];
                // Mark recommended option
                if (opt.recommended) {
                    lv_obj_set_style_bg_color(s_opt_btns[j],
                                              lv_color_hex(0x1A5276), 0);
                    snprintf(buf, sizeof(buf), "✓ %s", opt.label);
                } else {
                    lv_obj_set_style_bg_color(s_opt_btns[j],
                                              lv_color_hex(0x2A4070), 0);
                    snprintf(buf, sizeof(buf), "%s", opt.label);
                }
                lv_label_set_text(s_opt_lbls[j], buf);
                lv_obj_clear_flag(s_opt_btns[j], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_opt_btns[j], LV_OBJ_FLAG_HIDDEN);
            }
        }

    } else {
        // ---- OVERVIEW MODE: session cards ----

        // Layout: 1-3 sessions = 1 row, full height; 4-6 = 2 rows
        bool two_rows = (count > 3);
        int card_h = two_rows ? 175 : 355;
        int per_row = two_rows ? 3 : count;
        int row2_count = two_rows ? (count - per_row) : 0;

        // Row 1 card width: evenly divide 1014px
        int cw1 = (1014 - (per_row - 1) * 8) / per_row;
        // Row 2 card width
        int cw2 = (row2_count > 0)
            ? (1014 - (row2_count - 1) * 8) / row2_count
            : 0;

        for (int i = 0; i < count && i < AD_MAX_SESSIONS; i++) {
            const agentdeck_session_t &s = data->sessions[i];
            OvCard &c = s_ov[i];

            int row = (i < per_row) ? 0 : 1;
            int col = (i < per_row) ? i : (i - per_row);
            int cw  = (row == 0) ? cw1 : cw2;
            int row_count = (row == 0) ? per_row : row2_count;

            int cx = card_x(col, row_count, cw);
            int cy = 220 + row * (card_h + 8);

            lv_obj_set_pos(c.card, cx, cy);
            lv_obj_set_size(c.card, cw, card_h);

            // Border: highlight focused session
            if (s.focused) {
                lv_obj_set_style_border_width(c.card, 2, 0);
                lv_obj_set_style_border_color(c.card, CLR_HIGHLIGHT, 0);
            } else {
                lv_obj_set_style_border_width(c.card, 0, 0);
            }

            lv_obj_set_style_bg_color(c.state_dot, state_color(s.state), 0);
            lv_label_set_text(c.state_lbl, state_text(s.state));
            lv_obj_set_style_text_color(c.state_lbl, state_color(s.state), 0);

            lv_label_set_text(c.project_lbl,
                              s.project_name[0] ? s.project_name : "---");
            lv_obj_set_width(c.project_lbl, cw - 24);

            snprintf(buf, sizeof(buf), "%s  :%d",
                     s.model_name[0] ? s.model_name : "---", s.port);
            lv_label_set_text(c.model_lbl, buf);

            // Option buttons: only for focused session in tall mode
            bool show_opts = s.focused && !two_rows &&
                             s.state == AD_STATE_AWAITING &&
                             data->option_count > 0;

            for (int j = 0; j < AD_MAX_OPTIONS; j++) {
                if (show_opts && j < data->option_count) {
                    const agentdeck_option_t &opt = data->options[j];
                    if (opt.recommended) {
                        lv_obj_set_style_bg_color(c.opt_btns[j],
                                                  lv_color_hex(0x1A5276), 0);
                        snprintf(buf, sizeof(buf), "✓ %s", opt.label);
                    } else {
                        lv_obj_set_style_bg_color(c.opt_btns[j],
                                                  lv_color_hex(0x2A4070), 0);
                        snprintf(buf, sizeof(buf), "%s", opt.label);
                    }
                    lv_label_set_text(c.opt_lbls[j], buf);
                    lv_obj_set_width(c.opt_btns[j], cw - 20);
                    lv_obj_clear_flag(c.opt_btns[j], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(c.opt_btns[j], LV_OBJ_FLAG_HIDDEN);
                }
            }

            lv_obj_clear_flag(c.card, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------
static void interrupt_btn_cb(lv_event_t *e)
{
    (void)e;
    agentdeck_send_interrupt();
}

// Option button in options panel (action mode)
static void opt_btn_panel_cb(lv_event_t *e)
{
    int j = (int)(intptr_t)lv_event_get_user_data(e);
    // j is the array index; we need the actual option.index
    // agentdeck_get_data() gives us the options
    const agentdeck_data_t *d = agentdeck_get_data();
    if (d && j < d->option_count)
        agentdeck_send_respond(d->options[j].index);
}

// Option button inside overview card (tall mode)
static void opt_btn_card_cb(lv_event_t *e)
{
    int j = (int)(intptr_t)lv_event_get_user_data(e);
    const agentdeck_data_t *d = agentdeck_get_data();
    if (d && j < d->option_count)
        agentdeck_send_respond(d->options[j].index);
}
