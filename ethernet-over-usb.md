# Ethernet-over-USB

This provides a direct wired connection via the USB port, useful for environments where WiFi is unavailable or undesirable.

## Switching Network Modes

Use the API to switch between WiFi and USB modes:

```bash
# Switch to Ethernet-over-USB mode
curl -X PATCH http://YOUR-BITAXE-IP/api/system \
     -H "Content-Type: application/json" \
     -d '{"networkMode": "usb"}'

# Restart to apply
curl -X POST http://YOUR-BITAXE-IP/api/system/restart

# Switch back to WiFi mode
curl -X PATCH http://YOUR-BITAXE-IP/api/system \
     -H "Content-Type: application/json" \
     -d '{"networkMode": "wifi"}'
```

## Host Computer Setup (Linux)

When using Ethernet-over-USB mode, your computer needs to provide network connectivity to the device.

### Option 1: NetworkManager GUI (Easiest)
1. Connect the Bitaxe via USB.
2. Wait for the "Ethernet-over-USB" interface to appear in Network Settings.
3. Edit the connection → IPv4 Settings.
4. Set Method to **"Shared to other computers"**.
5. Save and reconnect.

The device will automatically get an IP address (typically `10.42.0.X`), and you can access the web UI at that address.

### Option 2: NetworkManager CLI
```bash
# Find the USB interface
nmcli device status | grep ethernet

# Create and activate shared connection (replace usb0 with your interface)
nmcli connection add type ethernet ifname usb0 con-name "USB-Miner" \
  ipv4.method shared
nmcli connection up "USB-Miner"
```

### Option 3: Manual DHCP Server
```bash
# Install dnsmasq
sudo apt install dnsmasq

# Configure interface
sudo ip addr add 192.168.7.1/24 dev usb0
sudo ip link set usb0 up

# Start DHCP server
sudo dnsmasq --interface=usb0 --dhcp-range=192.168.7.2,192.168.7.254 --no-daemon
```

## Technical Details

- **Protocol**: USB NCM (Network Control Model)
- **Device Class**: CDC NCM (Communications Device Class)
- **USB Descriptors**:
  - Vendor ID: 0x303A (Espressif)
  - Manufacturer: "ESP-Miner"
  - Product: "Bitaxe [family] [model] ([hostname])"
  - Serial: [MAC address]

## Limitations

- The ESP32-S3 has a single USB port used for both flashing/logging and Ethernet-over-USB.
- When Ethernet-over-USB is active, USB serial logging is interrupted briefly when switching over from the ROM USB PHY to TinyUSB.
- To flash the device, it first needs to be put in bootloader mode by holding the BOOT button and resetting the device.
- Windows 10 may require manual driver installation for NCM support (see Windows 10 Setup section).

## Windows 10 Setup

To share your Windows 10 PC's internet connection to a device via USB using the Network Control Model (NCM) protocol, you need to enable Internet Connection Sharing (ICS) after ensuring the correct driver is installed for the connected device.

### Step 1: Ensure the Correct USB NCM Driver is Installed
When you connect a device (like an Android phone or development board) and enable "USB tethering" on the device side, Windows should recognize it. If it doesn't appear correctly under Network adapters, you may need to manually update the driver to use the built-in Microsoft driver.

1. Open Device Manager: Right-click the Windows Start menu and select Device Manager.
2. Locate the device: Look for the device under the "Other devices" category (it might appear as "CDC NCM" or an unknown device).
3. Update the driver: Right-click the entry and select "Update Driver".
4. Browse for drivers: Select "Browse my computer for drivers", then "Let me pick from a list of available drivers on my computer".
5. Select Network adapters: From the list of Common hardware types, scroll down and select "Network adapters", then click Next.
6. Select Microsoft and UsbNcm Host Device: Under Manufacturer, select "Microsoft". Under Model, select "UsbNcm Host Device" and click Next.
7. Confirm installation: Click Yes if a warning message appears. The device should now appear as a "UsbNcm Host Device" under the "Network adapters" category.

### Step 2: Configure Internet Connection Sharing (ICS)
Once the device is correctly recognized as a network adapter, you can share your computer's active internet connection with it.

1. Open Network Connections: Right-click the network icon in your system tray and select "Open Network & Internet settings". In the settings window, click "Change adapter options" under "Advanced network settings".
2. Identify your internet source: In the "Network Connections" window, right-click the network adapter that has the active internet access (e.g., your Wi-Fi or Ethernet connection).
3. Open properties: Select "Properties" from the context menu.
4. Enable sharing: Go to the "Sharing" tab.
5. Check the box for "Allow other network users to connect through this computer's Internet connection".
6. Select the NCM connection: In the dropdown menu under that checkbox, select the new USB network connection you just installed (labeled something like "Ethernet X" with "UsbNcm Host Device" underneath).
7. Confirm: Click OK. You may receive an alert about enabling ICS; click Yes to continue.

Your connected device should now be able to use your Windows 10 PC's internet connection. You may need to unplug and replug the USB cable or disable/re-enable USB tethering on the connected device for the changes to take effect.
