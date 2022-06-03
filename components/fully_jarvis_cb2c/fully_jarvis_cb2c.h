#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/hal.h"

namespace esphome
{
  namespace fully_jarvis_cb2c
  {

    class JarvisCB2CSensor : public Component, public sensor::Sensor, public uart::UARTDevice
    {
    public:
      float get_setup_priority() const override { return setup_priority::LATE; }
      void setup() override;
      void loop() override;
      void dump_config() override;

      void set_height_sensor(sensor::Sensor *sensor) { this->height_sensor_ = sensor; }

      // GPIO we'll need to implement some functionality
      void set_hc0_pin(GPIOPin *pin) { this->hc0_pin = pin; }
      void set_hc1_pin(GPIOPin *pin) { this->hc1_pin = pin; }
      void set_hc2_pin(GPIOPin *pin) { this->hc2_pin = pin; }
      void set_hc3_pin(GPIOPin *pin) { this->hc3_pin = pin; }

      // Functions to call from lambda
      void goto_preset(int p);
      void goto_height(double h);

      void do_wake();
      void do_null();
      void do_m();

      void do_manual_move(char direction);

    protected:
      sensor::Sensor *height_sensor_{nullptr};

      GPIOPin *hc0_pin{nullptr};
      GPIOPin *hc1_pin{nullptr};
      GPIOPin *hc2_pin{nullptr};
      GPIOPin *hc3_pin{nullptr};

      // We'll need to keep track of position for manual control
      int16_t current_pos_{0};
      double target_pos_{-1};

      bool verify_cb2c_checksum_(uint8_t *ptr);

      // Util functions
      static float _to_mm(int16_t h);
      static void _dump_packet(uint8_t *ptr);

      void _adjust_height();

      void _stop_and_release_all_buttons();
    };

  } // namespace fully_jarvis_cb2c
} // namespace esphome
