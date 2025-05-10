# ESP32 CORS Configuration Guide

This guide explains how to add CORS (Cross-Origin Resource Sharing) headers to your ESP32 HTTP server to allow web applications to make requests to your device's API endpoints.

## Why CORS Is Needed

Modern web browsers implement a security feature called Same-Origin Policy, which restricts web pages from making requests to a different domain than the one that served the web page. CORS is a mechanism that allows servers to indicate which origins are permitted to access their resources.

When you're developing a web application that needs to fetch data from your ESP32 device, you'll encounter CORS errors if the ESP32 doesn't include the appropriate CORS headers in its responses.

## Option 1: Modifying the ESP32 Firmware (Recommended)

The most reliable solution is to modify your ESP32 firmware to include CORS headers in responses.

### For ESP-IDF HTTP Server

If you're using the ESP-IDF `esp_http_server` component:

```c
// Add these lines to your request handler functions
httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept");

// If you need to support credentials (cookies, auth), use this instead of "*"
// httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "http://your-web-app-domain.com");
// httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
```

### Example Integration for API Endpoints

Here's an example of how to modify your API endpoint handlers:

```c
esp_err_t system_info_get_handler(httpd_req_t *req)
{
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept");

    // Set content type
    httpd_resp_set_type(req, "application/json");

    // Create JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", "1.0.0");
    cJSON_AddStringToObject(root, "device", "ESP32");
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);

    // Add more system info...

    // Convert to string and send
    const char *json_str = cJSON_Print(root);
    httpd_resp_sendstr(req, json_str);

    // Free resources
    free((void *)json_str);
    cJSON_Delete(root);

    return ESP_OK;
}
```

### Handling OPTIONS Requests (CORS Preflight)

Modern browsers send an OPTIONS request (called a "preflight request") before sending the actual request for certain types of cross-origin requests. You need to handle these:

```c
esp_err_t options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept, Authorization, X-Requested-With");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");

    // Respond with empty body and 200 OK status
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Then register this handler for OPTIONS method
httpd_uri_t options_uri = {
    .uri = "/api/*",  // Match all API endpoints
    .method = HTTP_OPTIONS,
    .handler = options_handler,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &options_uri);
```

## Option 2: Centralized CORS Handling

For a more maintainable approach, you can create a utility function to add CORS headers to all responses:

```c
void add_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Accept, Authorization");
}

// Then in your handlers
esp_err_t some_handler(httpd_req_t *req)
{
    // Add CORS headers
    add_cors_headers(req);

    // Rest of your handler code
    // ...

    return ESP_OK;
}
```

## Option 3: Using a Development Proxy (Alternative)

If you can't modify the ESP32 firmware, you can set up a development proxy server that adds CORS headers to the responses. This is implemented in the current project using Vite's proxy configuration.

## Checking Your CORS Configuration

After implementing CORS headers, test your API endpoint with a web browser. You can use the browser's developer tools to confirm that the CORS headers are present in the response.

1. Open your browser's developer tools (F12)
2. Go to the Network tab
3. Make a request to your ESP32 API endpoint
4. Select the request and look at the response headers
5. Verify that the `Access-Control-Allow-Origin` header is present

## Common CORS Issues

1. **Credentials not working**: If you're using cookies or authentication, you need to set `Access-Control-Allow-Credentials: true` and specify the exact origin instead of `*` in `Access-Control-Allow-Origin`.

2. **Custom headers not working**: Make sure the custom headers are included in the `Access-Control-Allow-Headers` list.

3. **Preflight requests failing**: Ensure you have an OPTIONS handler that returns the appropriate CORS headers.

## IP Address Restrictions and Network Configuration

Even with proper CORS headers, you might encounter connectivity issues due to IP address restrictions or network configuration. Here are some common issues and solutions:

### ESP32 IP Address Configuration

1. **Static IP vs DHCP**: If your ESP32 is using DHCP, its IP address might change. For development, consider assigning a static IP.

   ```c
   // Example of setting a static IP in ESP-IDF
   esp_netif_ip_info_t ip_info;
   IP4_ADDR(&ip_info.ip, 192, 168, 1, 100);      // Static IP
   IP4_ADDR(&ip_info.gw, 192, 168, 1, 1);        // Gateway
   IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // Netmask
   esp_netif_set_ip_info(netif, &ip_info);
   ```

2. **Verifying ESP32 IP**: You can print the IP address to the serial console during startup:

   ```c
   esp_netif_ip_info_t ip_info;
   esp_netif_get_ip_info(netif, &ip_info);
   ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&ip_info.ip));
   ```

### Access Control Lists

Some ESP32 applications implement IP-based access control lists (ACLs) that only allow connections from specific IP addresses or subnets:

1. **Checking for Access Control**: Look for code that examines the client's IP address and conditionally allows/denies connections:

   ```c
   // Example of IP filtering
   esp_err_t handler(httpd_req_t *req)
   {
       // Get client's IP address
       struct sockaddr_in6 addr;
       socklen_t addr_size = sizeof(addr);
       httpd_req_get_sockaddr(req, (struct sockaddr *)&addr, &addr_size);

       // Check if client is allowed
       if (!is_ip_allowed(&addr)) {
           return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access denied");
       }

       // Continue with request handling
       // ...
   }
   ```

2. **Modifying Access Control**: If you find such code, modify it to allow your development machine's IP address:

   ```c
   bool is_ip_allowed(const struct sockaddr_in6 *addr)
   {
       // Allow access from specific IPs (add your development machine here)
       const char *allowed_ips[] = {"192.168.1.5", "10.0.0.2", NULL};

       char ip_str[INET6_ADDRSTRLEN];
       inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, sizeof(ip_str));

       for (int i = 0; allowed_ips[i] != NULL; i++) {
           if (strcmp(ip_str, allowed_ips[i]) == 0) {
               return true;
           }
       }

       return false; // Deny all other IPs
   }
   ```

### Network Configuration Issues

1. **Different Subnets**: Ensure your development machine and ESP32 are on the same subnet. If they're on different subnets, you might need to configure routing.

2. **Firewall Settings**: Check firewall settings on your development machine. The firewall might be blocking outgoing connections to the ESP32.

3. **Wi-Fi vs Ethernet**: If your ESP32 is on Wi-Fi and your computer is on Ethernet (or vice versa), verify that your router allows communication between these networks.

4. **Router Isolation Settings**: Some routers have "AP Isolation" or "Client Isolation" enabled, which prevents devices on the same network from communicating with each other.

### Testing Network Connectivity

Use these commands to test basic network connectivity to your ESP32:

1. **Ping**: Test if the ESP32 is reachable:

   ```bash
   ping 10.1.1.50  # Replace with your ESP32's IP
   ```

2. **Telnet**: Test if the HTTP server port is open:

   ```bash
   telnet 10.1.1.50 80  # Tests if port 80 is accessible
   ```

3. **Curl**: Test if the API endpoint is responding:
   ```bash
   curl -v http://10.1.1.50/api/system/info
   ```

## Security Considerations

While using `Access-Control-Allow-Origin: "*"` is convenient for development, it allows any website to make requests to your ESP32. For production environments, consider:

1. Restricting allowed origins to specific domains:

   ```c
   httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "https://your-trusted-site.com");
   ```

2. Implementing proper authentication and authorization for your API endpoints.

## Additional Resources

- [ESP32 HTTP Server Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)
- [MDN Web Docs: CORS](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS)
- [ESP-IDF HTTP Server Examples](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server)
- [ESP-IDF Network Interface Examples](https://github.com/espressif/esp-idf/tree/master/examples/network)
