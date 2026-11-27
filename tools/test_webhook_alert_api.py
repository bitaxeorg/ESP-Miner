#!/usr/bin/env python3
"""Host regression: sending an HTTP error must not accept a webhook request."""

from pathlib import Path
import subprocess
import tempfile
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/http_server/webhook_alert_api.c").read_text()
RECEIVE = SOURCE[SOURCE.index("static esp_err_t reject_json_request("):
                 SOURCE.index("static bool validate_optional_bool(")]
PARSE = SOURCE[SOURCE.index("static esp_err_t parse_test_event("):
               SOURCE.index("esp_err_t POST_webhook_alert_test(")]
SERVER = (ROOT / "main/http_server/http_server.c").read_text()
WRAPPER = SERVER[SERVER.index("static esp_err_t rest_api_wrapper("):
                 SERVER.index("esp_err_t HTTP_send_json_error(")]
for method, path, handler in [
    ("GET", "/api/system/alerts", "GET_webhook_alert_settings"),
    ("PATCH", "/api/system/alerts", "PATCH_webhook_alert_settings"),
    ("POST", "/api/system/alerts/test", "POST_webhook_alert_test"),
]:
    route = re.search(r'\{\s*\.uri = "' + re.escape(path) +
                      r'",\s*\.method = HTTP_' + method +
                      r',\s*\.handler = ' + handler + r'\s*\}', SERVER)
    assert route and route.start() < SERVER.index('// Fallbacks / Catch-alls')
assert 'entry.handler = rest_api_wrapper;' in SERVER
STUBS = r'''
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "cJSON.h"
#include "webhook_alert_utils.h"
typedef int esp_err_t;
typedef int httpd_err_code_t;
typedef struct {int content_len; const char *body; void *user_ctx;} httpd_req_t;
typedef enum {WEBHOOK_ALERT_TEST_GENERIC, WEBHOOK_ALERT_TEST_WATCHDOG,
 WEBHOOK_ALERT_TEST_BLOCK_FOUND, WEBHOOK_ALERT_TEST_BEST_DIFFICULTY} WebhookAlertTestEvent;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_TIMEOUT 2
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_401_UNAUTHORIZED 401
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_SOCK_ERR_TIMEOUT -1
#define HTTPD_RESP_USE_STRLEN -1
#define WEBHOOK_ALERT_REQUEST_MAX_LEN 1024
#define WEBHOOK_ALERT_REQUEST_DEADLINE_MS 10000
static int error_responses, response_status, recv_mode;
static int network_result, cors_result, handler_calls;
static int is_network_allowed(httpd_req_t *r) {(void)r; return network_result;}
static int set_cors_headers(httpd_req_t *r) {(void)r; return cors_result;}
static int httpd_resp_send_500(httpd_req_t *r) {(void)r; response_status=500; return ESP_OK;}
static int protected_handler(httpd_req_t *r) {(void)r; handler_calls++; return 73;}
static int64_t clock_us, clock_step;
static int64_t esp_timer_get_time(void) {clock_us += clock_step; return clock_us;}
// ESP-IDF returns ESP_OK when an error response was successfully transmitted.
static int httpd_resp_send_err(httpd_req_t *r, int status, const char *m) {
 (void)r; (void)m; error_responses++; response_status=status; return ESP_OK;
}
static int httpd_resp_set_status(httpd_req_t *r, const char *s) {
 (void)r; assert(strcmp(s,"408 Request Timeout")==0); response_status=408; return ESP_OK;
}
static int httpd_resp_send(httpd_req_t *r, const char *s, int n) {
 (void)r; (void)s; (void)n; error_responses++; return ESP_OK;
}
static int httpd_req_recv(httpd_req_t *r, char *b, int n) {
 if(recv_mode==1) return HTTPD_SOCK_ERR_TIMEOUT;
 if(recv_mode==2) return 0;
 memcpy(b,r->body,n); return n;
}
static void reset(void) {
 error_responses=0; response_status=0; recv_mode=0; clock_us=0; clock_step=100;
}
'''
CASES = r'''
int main(void) {
 httpd_req_t protected_req={.user_ctx=(void *)protected_handler};
 network_result=1;
 rest_api_wrapper(&protected_req);
 assert(response_status==401 && handler_calls==0);
 network_result=0; cors_result=1;
 rest_api_wrapper(&protected_req);
 assert(response_status==500 && handler_calls==0);
 cors_result=0;
 assert(rest_api_wrapper(&protected_req)==73 && handler_calls==1);
 const char *invalid[]={"not-json","[]","{\"event\":7}","{\"event\":\"unknown\"}"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
  reset(); httpd_req_t req={(int)strlen(invalid[i]),invalid[i]}; WebhookAlertTestEvent event;
  assert(parse_test_event(&req,&event)!=ESP_OK);
  assert(error_responses==1 && response_status==400);
 }
 const char *valid[]={"", "{}", "{\"event\":\"generic\"}", "{\"event\":\"watchdog\"}",
  "{\"event\":\"block-found\"}", "{\"event\":\"best-diff\"}"};
 const int expected[]={0,0,0,1,2,3};
 for(size_t i=0;i<sizeof(valid)/sizeof(valid[0]);i++) {
  reset(); httpd_req_t req={(int)strlen(valid[i]),valid[i]}; WebhookAlertTestEvent event;
  assert(parse_test_event(&req,&event)==ESP_OK);
  assert(error_responses==0 && (int)event==expected[i]);
 }
 for(int mode=0;mode<3;mode++) {
  reset(); recv_mode=mode; clock_step=6000000;
  httpd_req_t req={2,"{}"}; cJSON *root=NULL;
  assert(receive_json(&req,&root)!=ESP_OK);
  assert(root==NULL && error_responses==1 && response_status==(mode==2?400:408));
 }
 reset(); clock_step=11000000; httpd_req_t expired={2,"{}"}; cJSON *root=NULL;
 assert(receive_json(&expired,&root)!=ESP_OK);
 assert(root==NULL && error_responses==1 && response_status==408);
 const int sizes[]={0,1024};
 for(size_t i=0;i<2;i++) {
  reset(); httpd_req_t req={sizes[i],""};
  assert(receive_json(&req,&root)!=ESP_OK);
  assert(root==NULL && error_responses==1 && response_status==400);
 }
 puts("Webhook API regression passed: rejects, deadlines, sizes, and valid event selectors");
}
'''

with tempfile.TemporaryDirectory(prefix="webhook-api-test-") as temp:
    executable = Path(temp) / "check"
    command = ["cc", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", "-x", "c", "-",
               "managed_components/espressif__cjson/cJSON/cJSON.c",
               "components/webhook_alert_utils/webhook_alert_utils.c",
               "-I", "managed_components/espressif__cjson/cJSON",
               "-I", "components/webhook_alert_utils/include", "-o", str(executable)]
    subprocess.run(command, input=STUBS + WRAPPER + RECEIVE + PARSE + CASES,
                   text=True, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True)
