#ifndef USB_NET_H_
#define USB_NET_H_

/**
 * @brief Initialize USB NCM (Network Control Model) network interface and CDC ACM for serial communication
 *
 * This function initializes TinyUSB with NCM support and creates an
 * esp_netif interface for Ethernet-over-USB connectivity. The device will
 * appear as an ethernet adapter to the host computer.
 * 
 * Additionally, it initializes CDC ACM (Communication Device Class Abstract Control Model)
 * for serial communication, allowing logging over USB serial connection.
 *
 * @param GLOBAL_STATE Pointer to the global state structure
 */
void usb_net_init(void * GLOBAL_STATE);

#endif /* USB_NET_H_ */
