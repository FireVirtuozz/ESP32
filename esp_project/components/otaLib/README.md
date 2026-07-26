# OTA Library

Over The Air Updates

OTA HTTPS

Edit partitions to a custom partition in menuconfig

```
# Name, Type, SubType, Offset, Size, Flags
nvs,      data, nvs,       , 0x6000
otadata,  data, ota,       , 0x2000
phy_init, data, phy,       , 0x1000
factory,  app,  factory,   , 1M
ota_0,    app,  ota_0,     , 1M
ota_1,    app,  ota_1,     , 1M
spiffs,   data, spiffs,    , 1M
```

--> ota 0 & 1 & data : for OTA
--> Spiffs : web pages
--> nvs : wifi storage
--> phy_init : for wifi phy
--> factory : for base app