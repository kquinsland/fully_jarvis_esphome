# ESPHome component

Below is a representative example config based on the one I use. I have included most of my `# TODO: ...` and similar notes made during the development / reverse engineering process for completion.

If not using the PCB with support for 10 PWM channels, you can safely remove the `light:` components and the related `output` components.

```yaml
esphome:
  # Note: this will be the hostname that device request during the DHCP dance... but appears to be broken on ESP32 devices
  # See: https://github.com/esphome/issues/issues/125
  name: ${hostname}
  platform: ESP32
  board: mhetesp32minikit

# Enable logging
logger:
  # Set to DEBUG for loads of messages about uart packets
  ##
  level: INFO
  # We prevent the constant stream of "[E][uart:015]: Reading from UART timed out at byte 0!" messages"
  logs:
    uart: NONE

# See: https://esphome.io/components/external_components.html
##
external_components:
  - source:
      type: git
      url: https://github.com/kquinsland/fully_jarvis_esphome/components/
    components:
      - fully_jarvis_cb2c

# Expose a raw number entry so we can assign an arbitrary height (within some precision limits; see c++ code)
number:
  - platform: template
    id: int_desk_height
    name: Desk Height
    icon: "mdi:human-male-height-variant"
    entity_category: config
    unit_of_measurement: cm
    # The 3 stage model goes from 24.5 inches to 50 inches (without top) which is 62-127cm
    min_value: 63
    # Likewise, we don't want to linger for more than 30s
    max_value: 127
    # Desk measures height accurate to ~1mm
    step: .1
    # Called when HA sets the number
    set_action:
      then:
        - logger.log:
            format: "Desk Height Set to %.1f"
            args: [x]
        - lambda: |-
            // goto_height() needs at most 1 decimal
            // multiply whatever the user gives us by 10 so the single decimal point is to the left of the decimal
            // then turn it into an int to drop all decimal
            double h = (int) (x*10);
            // and shift back to get the single decimal
            h = h /10;
            id(desk).goto_height(h);

  # We expose buttons to HA to exactly mimic the front panel
  - platform: template
    id: inp_preset_1
    name: "Desk Height Preset 1"
    icon: "mdi:numeric-1-box"
    entity_category: "config"
    on_press:
      then:
        - logger.log:
            format: "Preset selected: %u"
            args: ["1"]
        - lambda: |-
            id(desk).goto_preset(1);
  - platform: template
    id: inp_preset_2
    name: "Desk Height Preset 2"
    icon: "mdi:numeric-2-box"
    entity_category: "config"
    on_press:
      then:
        - logger.log:
            format: "Preset selected: %u"
            args: ["2"]
        - lambda: |-
            id(desk).goto_preset(2);
  - platform: template
    id: inp_preset_3
    name: "Desk Height Preset 3"
    icon: "mdi:numeric-3-box"
    entity_category: "config"
    on_press:
      then:
        - logger.log:
            format: "Preset selected: %u"
            args: ["3"]
        - lambda: |-
            id(desk).goto_preset(3);

  - platform: template
    id: inp_preset_4
    name: "Desk Height Preset 4"
    icon: "mdi:numeric-4-box"
    entity_category: "config"
    on_press:
      then:
        - logger.log:
            format: "Preset selected: %u"
            args: ["4"]
        - lambda: |-
            id(desk).goto_preset(4);

# This works! If i keep clicking it, the remote will activate. The buttons come on immediately, screen is blank
#   but the remote appears to have a failsafe because after a few seconds, the "fully" logo shows up and stays
#   on the screen until the wake packet stops getting sent
##
  # - platform: template
  #   id: inp_wake
  #   name: "Wake Remote"
  #   icon: "mdi:arrow-down-bold-box"
  #   entity_category: "config"
  #   on_press:
  #     then:
  #       - lambda: |-
  #           //id(desk).do_null();
  #           id(desk).do_wake();


# See: https://esphome.io/components/light/rgbww.html
light:
  - platform: rgbww
    name: "Lights Channel 1"
    id: led_center
    red: pwm_rd_1
    green: pwm_gn_1
    blue: pwm_bu_1
    cold_white: pwm_cw_1
    warm_white: pwm_ww_1
    cold_white_color_temperature: 6500 K
    warm_white_color_temperature: 2400 K
    # We tell HA that we support a few built in effects
    # See: https://esphome.io/components/light/index.html#light-effects
    effects:
      - pulse:
      - random:
      - strobe:

##
# In PCB Bring up, there is something wrong with the BLUE and RED channels on the second bank.
# Somehow, there's no ground connection so the mosfets are ineffective. I was able to fix this with bodge wires
#   for the BL2 and RD2 channels
###
  - platform: rgbww
    name: "Lights Channel 2"
    id: led_side
    red: pwm_rd_2
    green: pwm_gn_2
    blue: pwm_bu_2
    cold_white: pwm_cw_2
    warm_white: pwm_ww_2
    cold_white_color_temperature: 6500 K
    warm_white_color_temperature: 2400 K
    # We tell HA that we support a few built in effects
    # See: https://esphome.io/components/light/index.html#light-effects
    effects:
      - pulse:
      - random:
      - strobe:

output:
  - platform: ledc
    id: pwm_rd_1
    pin: GPIO5

  - platform: ledc
    id: pwm_gn_1
    pin: GPIO12

  - platform: ledc
    id: pwm_bu_1
    pin: GPIO13

  - platform: ledc
    id: pwm_ww_1
    pin: GPIO00

  - platform: ledc
    id: pwm_cw_1
    pin: GPIO4

  - platform: ledc
    id: pwm_rd_2
    pin: GPIO25

  - platform: ledc
    id: pwm_gn_2
    pin: GPIO26

  - platform: ledc
    id: pwm_bu_2
    pin: GPIO27

  - platform: ledc
    id: pwm_ww_2
    pin: GPIO14

  - platform: ledc
    id: pwm_cw_2
    pin: GPIO23

  # I don't understand WHY, but we NEED to set up the GPIO here as outputs before calls to digital_write()
  #   in the custom component will work. Can also do this as template switch but this is cleaner.
  - platform: gpio
    id: out18
    pin: GPIO18
  - platform: gpio
    id: out19
    pin: GPIO19
  - platform: gpio
    id: out32
    pin: GPIO32
  - platform: gpio
    id: out33
    pin: GPIO33


# Testing the jarvis desk integration
# The remote/controller use 6 pins but only two of them are serial like
# @phord calls them DTX and HTX. I have called them the same
#   - DTX: Serial messages from controller to handset (remote)
#   - HTX: Serial messages from handset to controller (motor driver)
uart:
  # The ESP32 has 3 UART; 0 is programming / debugging, 1 is in use (flash) so we will use 2
  id: desk_uart
  # We want to impersonate the remote/handset by injecting via the HTX pin which is mapped to 17
  tx_pin: GPIO17
  # Likewise, if the motor controller sends anything to the remote, we _also_ want to hear it.
  rx_pin: GPIO16
  # bog standard 9600 8n1
  baud_rate: 9600

# Load up __init.py__
fully_jarvis_cb2c:
    id: desk
    # This is read from the UART
    height:
      name: "Desk Height"
    # The GPIO we manipulate to simulate pressing buttons
    hc0_pin: GPIO18
    hc1_pin: GPIO19
    hc2_pin: GPIO32
    hc3_pin: GPIO33

```
