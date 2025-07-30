#ifndef WIFI_MODULE_H_
#define WIFI_MODULE_H_

typedef struct
{
    // The SSID of the WiFi network to which the system is connected.
    char ssid[32];

    // Status information about the current state of the WiFi connection.
    char wifi_status[256];

    // A string representing the IP address currently assigned to the device.
    char ip_addr_str[16]; // IP4ADDR_STRLEN_MAX

    // The SSID of the access point when operating in Access Point mode.
    char ap_ssid[32];

    // A flag indicating if the device is enabled as an access point.
    bool ap_enabled;

    // A flag indicating whether the device is connected to a network.
    bool is_connected;
}WifiSettings;

extern WifiSettings WIFI_MODULE;
#endif