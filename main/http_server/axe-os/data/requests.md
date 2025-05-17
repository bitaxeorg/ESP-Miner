# Root API path

`api/system/`

## GET /stratum

## PATCH /stratum

<!-- array of objects with URL and port properties -->

```
[
  {
    url: "public-pool.io",
    port: "21496",
    password: "123456"
  },
  {
    url,
    port,
    password
  }
]
```

## Wifi

## GET /wifi

Returns array of SSID strings
["acs-fb", "guest"]

## POST /wifi

{
ssid: "string"
password: "string"
}

## PATCH Overheat mode

`/system`
{"flipscreen":false,"invertscreen":false,"coreVoltage":1150,"frequency":490,"autofanspeed":false,"invertfanpolarity":false,"fanspeed":85,"overheat_mode":0}

## PATCH Auto fan speed

`/system`
autofanspeed: true

## PATCH Auto fan speed

## POST /restart

## Firmware Updates

`/OTA`
Expects .bin file upload
Restrict based on file name on containers acs-esp-miner

`/api/system/OTAWWW`
Expects .bin file upload
Restrict based on file name contains acs-web-app
