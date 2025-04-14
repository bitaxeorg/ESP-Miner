## Zabbix template for Bitaxe devices

Bitaxe devices can be monitored by Zabbix, an NMS (Network Management System).
The template `zbx_bitaxe_template.json` is provided, and is compatible with Zabbix 6.0+.

Some basic events/alarms include:
 - Restarts
 - Fan speed too slow
 - Free memory too low
 - Hash rate too low
 - Overheating

### Preparation

Import the Bitaxe template:
 - Configuration/Templates/Import (button)
 - Choose `zbx_bitaxe_template.json`
 - Ensure `Create new` is checked for `Templates` and `Groups` and click import

Create a Host in Zabbix (Configuration/Create host):
 - Name the host (e.g. bitaxe01).
 - Add the host to a group (e.g. mining).
 - Add an interface to the host:
   - SNMPv1 is fine (Zabbix won't actually be trying to contact Bitaxe over SNMP, but needs an interface defined)
   - Set the IP address of the host to your Bitaxe's IP address
 - Add the recently imported `Bitaxe` template to the host
 - Add the host

### Notifications

The Zabbix notification feature can be used to notify of an event occuring with the Bitaxe.  For example, Zabbix can be configured to send an email if an unexpected restart occurs.

See https://www.zabbix.com/documentation/6.0/en/manual/config/notifications
