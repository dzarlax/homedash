#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_CONNECT 0x7002
const char *esp_err_to_name(esp_err_t);
typedef struct mock_http *esp_http_client_handle_t;
enum { HTTP_EVENT_ERROR, HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_HEADER };
enum { HTTP_METHOD_GET, HTTP_METHOD_POST };
struct esp_http_client_event_t { int event_id; void *user_data; void *data; int data_len; char *header_key; char *header_value; };
struct esp_http_client_config_t {
 const char *url; int method; int timeout_ms; bool disable_auto_redirect;
 esp_err_t (*crt_bundle_attach)(void *);
 esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data;
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t,const char *,int);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_errno(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
