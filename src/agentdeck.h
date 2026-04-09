#pragma once

#include <stdint.h>
#include <stdbool.h>

#define AD_MAX_TIMELINE   12
#define AD_MAX_SESSIONS   6
#define AD_PROJECT_LEN    48
#define AD_MODEL_LEN      32
#define AD_TOOL_LEN       48
#define AD_DETAIL_LEN     128
#define AD_MAX_OPTIONS    5
#define AD_OPTION_LEN     80

enum agentdeck_conn_state_t {
    AD_CONN_DISABLED,       // Not initialized yet
    AD_CONN_SEARCHING,      // mDNS scanning
    AD_CONN_CONNECTING,     // Found host, WebSocket connecting
    AD_CONN_CONNECTED,      // WebSocket open
    AD_CONN_RECONNECTING,   // Lost connection, retrying
};

enum agentdeck_agent_state_t {
    AD_STATE_IDLE,
    AD_STATE_PROCESSING,
    AD_STATE_AWAITING,
};

enum agentdeck_tl_type_t {
    AD_TL_CHAT_START,
    AD_TL_TOOL_REQUEST,
    AD_TL_TOOL_EXEC,
    AD_TL_TOOL_RESOLVED,
    AD_TL_MODEL_CALL,
    AD_TL_ERROR,
    AD_TL_OTHER,
};

struct agentdeck_timeline_entry_t {
    agentdeck_tl_type_t type;
    uint32_t            timestamp;
    char                detail[AD_DETAIL_LEN];
};

struct agentdeck_usage_t {
    float    five_hour_pct;     // 0.0 - 100.0, -1 = no data
    float    seven_day_pct;
    char     five_hour_resets[16];  // "2h 15m"
    char     seven_day_resets[16];
    uint32_t input_tokens;
    uint32_t output_tokens;
    float    estimated_cost_usd;
    bool     valid;
};

struct agentdeck_option_t {
    char label[AD_OPTION_LEN];
    int  index;
    bool recommended;
};

struct agentdeck_session_t {
    char                    id[48];
    char                    project_name[AD_PROJECT_LEN];
    char                    model_name[AD_MODEL_LEN];
    int                     port;
    bool                    alive;
    bool                    focused;   // we're watching this session
    agentdeck_agent_state_t state;
};

struct agentdeck_data_t {
    agentdeck_conn_state_t  conn_state;
    agentdeck_agent_state_t agent_state;
    char                    project_name[AD_PROJECT_LEN];
    char                    model_name[AD_MODEL_LEN];
    char                    current_tool[AD_TOOL_LEN];

    agentdeck_option_t      options[AD_MAX_OPTIONS];
    int                     option_count;

    agentdeck_usage_t       usage;

    agentdeck_timeline_entry_t timeline[AD_MAX_TIMELINE];
    int                     timeline_count;
    int                     timeline_head;   // index of newest entry

    // Multi-session overview (from sessions_list broadcasts)
    agentdeck_session_t     sessions[AD_MAX_SESSIONS];
    int                     session_count;

    uint32_t                last_update_ts;
};

// Init mDNS discovery + WebSocket. Call after WiFi connected.
void agentdeck_init(void);

// Non-blocking poll: reconnect, mDNS retry, process outgoing.
void agentdeck_poll(void);

// Read-only access to shared state.
const agentdeck_data_t *agentdeck_get_data(void);

// Outgoing commands (called from UI core, read by network core).
void agentdeck_send_respond(int option_index);
void agentdeck_send_interrupt(void);
void agentdeck_send_query_usage(void);
