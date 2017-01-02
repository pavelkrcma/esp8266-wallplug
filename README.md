# esp8266-wallplug
#### ESP based Relay Board
#### Hardware documentation: http://www.electrodragon.com/w/ESP_Relay_Board

### Setup
There is a Wifi Manager with custom parameters. After first power on or if you hold Button1 during boot, a new wifi AP with name esp-xxxxxx appears and LED starts blinking. String after esp- in the wifi AP name is taken from serial number of the chip. You can config both Wifi and MQTT server on the config page. There are no hardcoded values in the code.

### Usage
You can control LED and both relays by MQTT messages.

Topic is constructed under schema:
nodes/(mqtt_user_name)/commands/outlet/[1|2] or nodes/(mqtt_user_name)/commands/led

Message could be either ON, OFF or TOGGLE (uppercase only)
