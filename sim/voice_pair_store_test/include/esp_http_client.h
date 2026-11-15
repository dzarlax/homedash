#pragma once
#include <stddef.h>
#include "nvs.h"
enum { HTTP_EVENT_ON_DATA=1, HTTP_METHOD_POST=1 };
struct esp_http_client_event_t {int event_id;void *user_data;void *data;int data_len;};
typedef esp_err_t (*esp_http_event_cb_t)(esp_http_client_event_t *);
struct esp_http_client_config_t {const char *url;int method;int timeout_ms;int (*crt_bundle_attach)(void *);bool skip_cert_common_name_check;bool disable_auto_redirect;esp_http_event_cb_t event_handler;void *user_data;};
struct mock_http_client; typedef mock_http_client *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t,const char *,int);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
