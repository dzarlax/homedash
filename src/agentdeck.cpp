#include "agentdeck.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "mdns.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "agentdeck";

static agentdeck_data_t s_data = {};
static esp_websocket_client_handle_t s_ws_client = NULL;
static char s_ws_uri[192] = {};
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

// Timers for retry logic
static uint32_t s_last_mdns_scan_ms = 0;
static uint32_t s_last_reconnect_ms = 0;
static const uint32_t MDNS_SCAN_INTERVAL_MS = 15000;
static const uint32_t RECONNECT_INTERVAL_MS = 10000;

// Outgoing request flags (set by UI core, read by network core)
static volatile int  s_respond_idx = -1;
static volatile bool s_respond_requested = false;
static volatile bool s_interrupt_requested = false;
static volatile bool s_query_usage_requested = false;
static volatile bool s_focus_requested = false;
static char s_focus_session_id[48] = {};

// Periodic usage re-query
static uint32_t s_last_usage_query_ms = 0;
static const uint32_t USAGE_QUERY_INTERVAL_MS = 60000;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Safe string copy for shared data
static void safe_strcpy(char *dst, const char *src, size_t dst_size)
{
    if (!src) { dst[0] = '\0'; return; }
    size_t max = dst_size - 1;
    size_t i = 0;
    while (i < max && src[i]) {
        unsigned char c = (unsigned char)src[i];
        int seq_len = 1;
        if (c >= 0xF0) seq_len = 4;
        else if (c >= 0xE0) seq_len = 3;
        else if (c >= 0xC0) seq_len = 2;
        if (i + seq_len > max) break;
        i += seq_len;
    }
    memcpy(dst, src, i);
    dst[i] = '\0';
}

// ---------------------------------------------------------------
// JSON message parsing
// ---------------------------------------------------------------

static agentdeck_agent_state_t parse_agent_state(const char *s)
{
    if (!s) return AD_STATE_IDLE;
    if (strcmp(s, "processing") == 0) return AD_STATE_PROCESSING;
    if (strncmp(s, "awaiting", 8) == 0) return AD_STATE_AWAITING;
    return AD_STATE_IDLE;
}

static agentdeck_tl_type_t parse_tl_type(const char *s)
{
    if (!s) return AD_TL_OTHER;
    if (strcmp(s, "chat_start") == 0) return AD_TL_CHAT_START;
    if (strcmp(s, "tool_request") == 0) return AD_TL_TOOL_REQUEST;
    if (strcmp(s, "tool_exec") == 0) return AD_TL_TOOL_EXEC;
    if (strcmp(s, "tool_resolved") == 0) return AD_TL_TOOL_RESOLVED;
    if (strcmp(s, "model_call") == 0) return AD_TL_MODEL_CALL;
    if (strcmp(s, "model_response") == 0) return AD_TL_MODEL_CALL;
    if (strcmp(s, "error") == 0) return AD_TL_ERROR;
    return AD_TL_OTHER;
}

static void handle_state_update(cJSON *root)
{
    taskENTER_CRITICAL(&s_data_mux);

    cJSON *v;
    v = cJSON_GetObjectItem(root, "state");
    if (v && cJSON_IsString(v)) s_data.agent_state = parse_agent_state(v->valuestring);

    v = cJSON_GetObjectItem(root, "projectName");
    if (v && cJSON_IsString(v)) safe_strcpy(s_data.project_name, v->valuestring, AD_PROJECT_LEN);

    v = cJSON_GetObjectItem(root, "modelName");
    if (v && cJSON_IsString(v)) safe_strcpy(s_data.model_name, v->valuestring, AD_MODEL_LEN);

    v = cJSON_GetObjectItem(root, "currentTool");
    if (v && cJSON_IsString(v)) safe_strcpy(s_data.current_tool, v->valuestring, AD_TOOL_LEN);

    // Options
    cJSON *opts = cJSON_GetObjectItem(root, "options");
    s_data.option_count = 0;
    if (opts && cJSON_IsArray(opts)) {
        int n = cJSON_GetArraySize(opts);
        if (n > AD_MAX_OPTIONS) n = AD_MAX_OPTIONS;
        for (int i = 0; i < n; i++) {
            cJSON *opt = cJSON_GetArrayItem(opts, i);
            if (!opt) continue;
            agentdeck_option_t *o = &s_data.options[s_data.option_count];
            memset(o, 0, sizeof(*o));
            cJSON *lbl = cJSON_GetObjectItem(opt, "label");
            if (lbl && cJSON_IsString(lbl)) safe_strcpy(o->label, lbl->valuestring, AD_OPTION_LEN);
            cJSON *idx = cJSON_GetObjectItem(opt, "index");
            if (idx) o->index = idx->valueint;
            cJSON *rec = cJSON_GetObjectItem(opt, "recommended");
            if (rec) o->recommended = cJSON_IsTrue(rec);
            s_data.option_count++;
        }
    }

    // Sync focused session entry in the grid
    for (int i = 0; i < s_data.session_count; i++) {
        if (s_data.sessions[i].focused) {
            s_data.sessions[i].state = s_data.agent_state;
            if (s_data.project_name[0])
                safe_strcpy(s_data.sessions[i].project_name,
                            s_data.project_name, AD_PROJECT_LEN);
            if (s_data.model_name[0])
                safe_strcpy(s_data.sessions[i].model_name,
                            s_data.model_name, AD_MODEL_LEN);
            break;
        }
    }

    s_data.last_update_ts = (uint32_t)time(NULL);
    taskEXIT_CRITICAL(&s_data_mux);
}

static void handle_usage_update(cJSON *root)
{
    taskENTER_CRITICAL(&s_data_mux);

    cJSON *v;
    v = cJSON_GetObjectItem(root, "fiveHourPercent");
    s_data.usage.five_hour_pct = (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : -1.0f;

    v = cJSON_GetObjectItem(root, "sevenDayPercent");
    s_data.usage.seven_day_pct = (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : -1.0f;

    v = cJSON_GetObjectItem(root, "fiveHourResetsAt");
    if (v && cJSON_IsString(v)) safe_strcpy(s_data.usage.five_hour_resets, v->valuestring, sizeof(s_data.usage.five_hour_resets));

    v = cJSON_GetObjectItem(root, "sevenDayResetsAt");
    if (v && cJSON_IsString(v)) safe_strcpy(s_data.usage.seven_day_resets, v->valuestring, sizeof(s_data.usage.seven_day_resets));

    v = cJSON_GetObjectItem(root, "inputTokens");
    s_data.usage.input_tokens = (v && cJSON_IsNumber(v)) ? (uint32_t)v->valuedouble : 0;

    v = cJSON_GetObjectItem(root, "outputTokens");
    s_data.usage.output_tokens = (v && cJSON_IsNumber(v)) ? (uint32_t)v->valuedouble : 0;

    v = cJSON_GetObjectItem(root, "estimatedCostUsd");
    s_data.usage.estimated_cost_usd = (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : 0;

    s_data.usage.valid = true;
    taskEXIT_CRITICAL(&s_data_mux);
}

// Parse a TimelineEntry object (shared helper)
static void push_timeline_entry(cJSON *entry)
{
    if (!entry) return;

    int idx = (s_data.timeline_head + 1) % AD_MAX_TIMELINE;
    agentdeck_timeline_entry_t *e = &s_data.timeline[idx];
    memset(e, 0, sizeof(*e));

    cJSON *v;
    v = cJSON_GetObjectItem(entry, "type");
    e->type = (v && cJSON_IsString(v)) ? parse_tl_type(v->valuestring) : AD_TL_OTHER;

    // ts is in milliseconds (Unix epoch ms → seconds)
    v = cJSON_GetObjectItem(entry, "ts");
    e->timestamp = (v && cJSON_IsNumber(v))
        ? (uint32_t)(v->valuedouble / 1000.0)
        : (uint32_t)time(NULL);

    v = cJSON_GetObjectItem(entry, "raw");
    if (v && cJSON_IsString(v)) safe_strcpy(e->detail, v->valuestring, AD_DETAIL_LEN);

    s_data.timeline_head = idx;
    if (s_data.timeline_count < AD_MAX_TIMELINE) s_data.timeline_count++;
}

// {"type":"timeline_event", "entry": {...}}
static void handle_timeline_event(cJSON *root)
{
    cJSON *entry = cJSON_GetObjectItem(root, "entry");
    if (!entry) return;

    taskENTER_CRITICAL(&s_data_mux);
    push_timeline_entry(entry);
    taskEXIT_CRITICAL(&s_data_mux);
}

// {"type":"timeline_history", "entries": [...]}  — sent on connect
static void handle_timeline_history(cJSON *root)
{
    cJSON *entries = cJSON_GetObjectItem(root, "entries");
    if (!entries || !cJSON_IsArray(entries)) return;

    taskENTER_CRITICAL(&s_data_mux);
    s_data.timeline_head = 0;
    s_data.timeline_count = 0;

    int n = cJSON_GetArraySize(entries);
    int start = (n > AD_MAX_TIMELINE) ? n - AD_MAX_TIMELINE : 0;
    for (int i = start; i < n; i++) {
        push_timeline_entry(cJSON_GetArrayItem(entries, i));
    }
    taskEXIT_CRITICAL(&s_data_mux);
}

// {"type":"sessions_list", "sessions": [...]}  — sent on connect + periodically
static void handle_sessions_list(cJSON *root)
{
    cJSON *sessions_arr = cJSON_GetObjectItem(root, "sessions");
    if (!sessions_arr || !cJSON_IsArray(sessions_arr)) return;

    int n = cJSON_GetArraySize(sessions_arr);

    // Pick most interesting alive session for auto-focus: processing > awaiting > idle
    cJSON *best = NULL;
    int best_score = -1;
    for (int i = 0; i < n; i++) {
        cJSON *sess = cJSON_GetArrayItem(sessions_arr, i);
        if (!sess) continue;
        cJSON *alive_node = cJSON_GetObjectItem(sess, "alive");
        if (!alive_node || !cJSON_IsTrue(alive_node)) continue;

        cJSON *state_node = cJSON_GetObjectItem(sess, "state");
        int score = 1;
        if (state_node && cJSON_IsString(state_node)) {
            if (strcmp(state_node->valuestring, "processing") == 0)        score = 3;
            else if (strncmp(state_node->valuestring, "awaiting", 8) == 0) score = 2;
        }
        if (score > best_score) { best_score = score; best = sess; }
    }

    taskENTER_CRITICAL(&s_data_mux);

    // Populate sessions array for the UI grid
    s_data.session_count = 0;
    int limit = n < AD_MAX_SESSIONS ? n : AD_MAX_SESSIONS;
    for (int i = 0; i < limit; i++) {
        cJSON *sess = cJSON_GetArrayItem(sessions_arr, i);
        if (!sess) continue;
        agentdeck_session_t *sd = &s_data.sessions[s_data.session_count];
        memset(sd, 0, sizeof(*sd));

        cJSON *v;
        v = cJSON_GetObjectItem(sess, "id");
        if (v && cJSON_IsString(v))
            strncpy(sd->id, v->valuestring, sizeof(sd->id) - 1);

        v = cJSON_GetObjectItem(sess, "projectName");
        if (v && cJSON_IsString(v))
            safe_strcpy(sd->project_name, v->valuestring, AD_PROJECT_LEN);

        v = cJSON_GetObjectItem(sess, "modelName");
        if (v && cJSON_IsString(v))
            safe_strcpy(sd->model_name, v->valuestring, AD_MODEL_LEN);

        v = cJSON_GetObjectItem(sess, "port");
        if (v && cJSON_IsNumber(v)) sd->port = (int)v->valuedouble;

        v = cJSON_GetObjectItem(sess, "alive");
        sd->alive = (v && cJSON_IsTrue(v));

        v = cJSON_GetObjectItem(sess, "state");
        sd->state = (v && cJSON_IsString(v))
            ? parse_agent_state(v->valuestring) : AD_STATE_IDLE;

        sd->focused = (sd->id[0] && strcmp(sd->id, s_focus_session_id) == 0);

        s_data.session_count++;
    }

    if (best) {
        cJSON *v;
        // State — only update top-level if no focused session yet
        v = cJSON_GetObjectItem(best, "state");
        if (v && cJSON_IsString(v) && !s_focus_session_id[0])
            s_data.agent_state = parse_agent_state(v->valuestring);

        // Project name (prefer session name over daemon "AgentDeck")
        v = cJSON_GetObjectItem(best, "projectName");
        if (v && cJSON_IsString(v) && v->valuestring[0])
            safe_strcpy(s_data.project_name, v->valuestring, AD_PROJECT_LEN);

        v = cJSON_GetObjectItem(best, "modelName");
        if (v && cJSON_IsString(v) && v->valuestring[0])
            safe_strcpy(s_data.model_name, v->valuestring, AD_MODEL_LEN);
    }

    taskEXIT_CRITICAL(&s_data_mux);

    // Auto-focus best session (so daemon relays its real-time state_update)
    if (best) {
        cJSON *v = cJSON_GetObjectItem(best, "id");
        if (v && cJSON_IsString(v) && v->valuestring[0]
            && strcmp(s_focus_session_id, v->valuestring) != 0) {
            strncpy(s_focus_session_id, v->valuestring, sizeof(s_focus_session_id) - 1);
            s_focus_session_id[sizeof(s_focus_session_id) - 1] = '\0';
            s_focus_requested = true;
            ESP_LOGI(TAG, "sessions_list: auto-focusing %s", s_focus_session_id);
        }
    }
}

// ---------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        s_data.conn_state = AD_CONN_CONNECTED;
        s_query_usage_requested = true;
        s_last_usage_query_ms = now_ms();
        s_focus_session_id[0] = '\0';  // Reset focus on reconnect
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01 && data->data_len > 0) {  // text frame
            cJSON *root = cJSON_ParseWithLength(data->data_ptr, data->data_len);
            if (root) {
                cJSON *type = cJSON_GetObjectItem(root, "type");
                if (type && cJSON_IsString(type)) {
                    const char *t = type->valuestring;
                    if (strcmp(t, "state_update") == 0)         handle_state_update(root);
                    else if (strcmp(t, "usage_update") == 0)    handle_usage_update(root);
                    else if (strcmp(t, "timeline_event") == 0)  handle_timeline_event(root);
                    else if (strcmp(t, "timeline_history") == 0) handle_timeline_history(root);
                    else if (strcmp(t, "sessions_list") == 0)   handle_sessions_list(root);
                    else ESP_LOGD(TAG, "Unhandled message type: %s", t);
                }
                cJSON_Delete(root);
            }
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        s_data.conn_state = AD_CONN_RECONNECTING;
        s_last_reconnect_ms = now_ms();
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        s_data.conn_state = AD_CONN_RECONNECTING;
        s_last_reconnect_ms = now_ms();
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------
// mDNS discovery
// ---------------------------------------------------------------

// Fetch pairing token from bridge /health endpoint
static char s_health_buf[512];
static int  s_health_len = 0;

static esp_err_t health_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int remaining = (int)sizeof(s_health_buf) - 1 - s_health_len;
        int copy = evt->data_len < remaining ? evt->data_len : remaining;
        if (copy > 0) {
            memcpy(s_health_buf + s_health_len, evt->data, copy);
            s_health_len += copy;
        }
    }
    return ESP_OK;
}

static bool fetch_token_from_health(const char *ip, int port, char *out_token, size_t token_size)
{
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/health", ip, port);

    s_health_len = 0;
    s_health_buf[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 1500;
    cfg.event_handler = health_event_handler;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || s_health_len <= 0) return false;
    s_health_buf[s_health_len] = '\0';

    cJSON *root = cJSON_Parse(s_health_buf);
    if (!root) return false;

    cJSON *pt = cJSON_GetObjectItem(root, "pairingToken");
    bool ok = false;
    if (pt && cJSON_IsString(pt) && pt->valuestring[0]) {
        size_t tlen = strlen(pt->valuestring);
        if (tlen < token_size) {
            memcpy(out_token, pt->valuestring, tlen + 1);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

// Fallback: scan known IP + port range via /health when mDNS fails
static bool http_fallback_discover(void)
{
    // Try the LAN gateway/known host on daemon port range
    static const char *known_hosts[] = { "192.168.50.220", NULL };
    static const int ports[] = { 9123, 9120 };

    for (int h = 0; known_hosts[h]; h++) {
        for (int p = 0; p < sizeof(ports)/sizeof(ports[0]); p++) {
            char health_token[48] = {};
            if (fetch_token_from_health(known_hosts[h], ports[p], health_token, sizeof(health_token))) {
                if (health_token[0]) {
                    snprintf(s_ws_uri, sizeof(s_ws_uri), "ws://%s:%d/?token=%s", known_hosts[h], ports[p], health_token);
                } else {
                    snprintf(s_ws_uri, sizeof(s_ws_uri), "ws://%s:%d/", known_hosts[h], ports[p]);
                }
                ESP_LOGI(TAG, "HTTP fallback: found AgentDeck at %s:%d", known_hosts[h], ports[p]);
                return true;
            }
        }
    }
    ESP_LOGD(TAG, "HTTP fallback: no AgentDeck found");
    return false;
}

static bool mdns_discover(void)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_agentdeck", "_tcp", 2000, 3, &results);
    if (err != ESP_OK || !results) {
        ESP_LOGI(TAG, "mDNS: no results, trying HTTP fallback");
        return http_fallback_discover();
    }

    // Use first result
    mdns_result_t *r = results;
    if (r->addr && r->port > 0) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&r->addr->addr.u_addr.ip4));

        // Try token from mDNS TXT records first
        const char *txt_token = NULL;
        for (size_t i = 0; i < r->txt_count; i++) {
            if (r->txt[i].key && strcmp(r->txt[i].key, "token") == 0) {
                txt_token = r->txt[i].value;
                break;
            }
        }

        char health_token[48] = {};
        if (txt_token && txt_token[0]) {
            snprintf(s_ws_uri, sizeof(s_ws_uri), "ws://%s:%d/?token=%s", ip_str, r->port, txt_token);
            ESP_LOGI(TAG, "mDNS: found AgentDeck at %s:%d (token from TXT)", ip_str, r->port);
        } else if (fetch_token_from_health(ip_str, r->port, health_token, sizeof(health_token))) {
            snprintf(s_ws_uri, sizeof(s_ws_uri), "ws://%s:%d/?token=%s", ip_str, r->port, health_token);
            ESP_LOGI(TAG, "mDNS: found AgentDeck at %s:%d (token from /health)", ip_str, r->port);
        } else {
            snprintf(s_ws_uri, sizeof(s_ws_uri), "ws://%s:%d/", ip_str, r->port);
            ESP_LOGW(TAG, "mDNS: found AgentDeck at %s:%d (NO token — may be rejected)", ip_str, r->port);
        }
        mdns_query_results_free(results);
        return true;
    }

    mdns_query_results_free(results);
    return false;
}

// ---------------------------------------------------------------
// WebSocket connect / reconnect
// ---------------------------------------------------------------

static void ws_connect(void)
{
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = s_ws_uri;
    ws_cfg.buffer_size = 2048;
    ws_cfg.task_stack = 4096;
    ws_cfg.pingpong_timeout_sec = 30;
    ws_cfg.ping_interval_sec = 15;

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        s_data.conn_state = AD_CONN_SEARCHING;
        return;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);

    s_data.conn_state = AD_CONN_CONNECTING;
    esp_err_t err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        s_data.conn_state = AD_CONN_SEARCHING;
    }
}

// ---------------------------------------------------------------
// Outgoing messages
// ---------------------------------------------------------------

static void ws_send_json(cJSON *msg)
{
    if (!s_ws_client || !esp_websocket_client_is_connected(s_ws_client)) return;
    char *str = cJSON_PrintUnformatted(msg);
    if (str) {
        esp_websocket_client_send_text(s_ws_client, str, strlen(str), pdMS_TO_TICKS(1000));
        free(str);
    }
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

void agentdeck_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_data.conn_state = AD_CONN_SEARCHING;
    s_data.usage.five_hour_pct = -1.0f;
    s_data.usage.seven_day_pct = -1.0f;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    // Try initial discovery (HTTP fallback first — mDNS unreliable from ESP32 to system dns-sd)
    if (http_fallback_discover() || mdns_discover()) {
        ws_connect();
    }
    s_last_mdns_scan_ms = now_ms();
}

void agentdeck_poll(void)
{
    if (!wifi_is_connected()) {
        s_data.conn_state = AD_CONN_SEARCHING;
        return;
    }

    uint32_t now = now_ms();

    // Process outgoing commands
    if (s_respond_requested) {
        s_respond_requested = false;
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "select_option");
        cJSON_AddNumberToObject(msg, "index", s_respond_idx);
        ws_send_json(msg);
        cJSON_Delete(msg);
    }
    if (s_interrupt_requested) {
        s_interrupt_requested = false;
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "interrupt");
        ws_send_json(msg);
        cJSON_Delete(msg);
    }
    if (s_query_usage_requested) {
        s_query_usage_requested = false;
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "query_usage");
        ws_send_json(msg);
        cJSON_Delete(msg);
    }
    if (s_focus_requested) {
        s_focus_requested = false;
        if (s_focus_session_id[0]) {
            cJSON *msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "type", "focus_session");
            cJSON_AddStringToObject(msg, "sessionId", s_focus_session_id);
            ws_send_json(msg);
            cJSON_Delete(msg);
            ESP_LOGI(TAG, "Sent focus_session: %s", s_focus_session_id);
        }
    }

    // Periodic usage re-query (daemon doesn't push usage unsolicited)
    if (s_data.conn_state == AD_CONN_CONNECTED &&
        now - s_last_usage_query_ms >= USAGE_QUERY_INTERVAL_MS) {
        s_last_usage_query_ms = now;
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "query_usage");
        ws_send_json(msg);
        cJSON_Delete(msg);
    }

    // Retry logic
    switch (s_data.conn_state) {
    case AD_CONN_SEARCHING:
        if (now - s_last_mdns_scan_ms >= MDNS_SCAN_INTERVAL_MS) {
            s_last_mdns_scan_ms = now;
            if (http_fallback_discover() || mdns_discover()) {
                ws_connect();
            }
        }
        break;

    case AD_CONN_RECONNECTING:
        if (now - s_last_reconnect_ms >= RECONNECT_INTERVAL_MS) {
            s_last_reconnect_ms = now;
            if (http_fallback_discover() || mdns_discover()) {
                ws_connect();
            } else {
                s_data.conn_state = AD_CONN_SEARCHING;
            }
        }
        break;

    default:
        break;
    }
}

const agentdeck_data_t *agentdeck_get_data(void)
{
    return &s_data;
}

void agentdeck_send_respond(int option_index)
{
    s_respond_idx = option_index;
    s_respond_requested = true;
}

void agentdeck_send_interrupt(void)
{
    s_interrupt_requested = true;
}

void agentdeck_send_query_usage(void)
{
    s_query_usage_requested = true;
}
