#ifndef OTA_API_H_
#define OTA_API_H_

#include "esp_http_server.h"

/**
 * @brief Handle OTA firmware binary upload (/api/system/OTA)
 */
esp_err_t POST_OTA_update(httpd_req_t *req);

/**
 * @brief Handle OTA Web UI binary upload (/api/system/OTAWWW)
 */
esp_err_t POST_WWW_update(httpd_req_t *req);

/**
 * @brief Handle direct GitHub OTA firmware download (/api/system/OTA/github)
 */
esp_err_t POST_OTA_github_update(httpd_req_t *req);

#endif /* OTA_API_H_ */
