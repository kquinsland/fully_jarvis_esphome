#include <math.h> /* round, floor, ceil, trunc */

#include "fully_jarvis_cb2c.h"
#include "esphome/core/log.h"

namespace esphome
{
      namespace fully_jarvis_cb2c
      {

            static const char *TAG = "jarvis.2b2c";
            // In testing w/ oscilloscope, the remote pulls the various control lines down for ~150ms
            static const int btn_delay_time = 150;

            // It appears that the LONGEST possible packet is 9 bytes
            // See: https://github.com/phord/Jarvis#uart-protocol
            static const uint8_t JARVIS_PACKET_MAX_LEN = 9;

            // TODO: is there a setup call I can override?
            // At setup, I need to poke the desk to get it's current preset / units... etc
            void JarvisCB2CSensor::setup()
            {
                  // this->dump_config();
                  //  TODO: Might want to send WAKE 0x29 command to make sure the motor controller is alive
                  //  TODO: might want to see if there is a inquiry message to get the current settings rather than just SET them
                  //           e.g: 0x0E / UNITS is to SET the units... not to inquire what they are. It could be that this does not exist
                  //           and the motor never asks the remote what units it should be using.
                  //
                  //  I am reluctant to expose that functionality to HA because there could be some adverse effects. What happens if
                  //           ha -> esphome tells the motor to be in CM but the remote still thinks its in inches?
                  //////
                  // Default is to have all pins floating high; remote signals a button press by pulling low
                  if (this->hc0_pin != nullptr)
                        this->hc0_pin->digital_write(true);

                  if (this->hc1_pin != nullptr)
                        this->hc1_pin->digital_write(true);

                  if (this->hc2_pin != nullptr)
                        this->hc2_pin->digital_write(true);

                  if (this->hc3_pin != nullptr)
                        this->hc3_pin->digital_write(true);

                  ESP_LOGD(TAG, "Setup: Pins should be high!");
            }

            /*
                  Called every ~16ms. We read bytes until we have a valid packet
            */
            void JarvisCB2CSensor::loop()
            {
                  uint8_t response[JARVIS_PACKET_MAX_LEN];
                  uint8_t peeked;

                  // 0x7E is the End of Message byte
                  while (this->available())
                  {
                        // Get the byte
                        this->peek_byte(&peeked);
                        // ESP_LOGD(TAG, "peeked: %x \t %u", peeked, peeked);

                        if (peeked != 0x7e)
                        {
                              this->read();
                              // There's nothing else for us to do so bail early
                              return;
                        }
                        else
                        {
                              // We still read the byte for completeness then break out of the while loop to read/process the packet we have.
                              this->read();
                              ESP_LOGD(TAG, "EOM");
                              break;
                        }
                  }

                  bool read_success = read_array(response, JARVIS_PACKET_MAX_LEN);
                  if (!read_success)
                  {
                        // Don't need this as there will be plenty of:
                        //          Reading from UART timed out at byte 0!
                        // in the logs
                        ////
                        // ESP_LOGE(TAG, "Failure reading UART bytes!");
                        status_set_warning();

                        // Even though we didn't get anything from the UART, we still should move, if needed
                        this->_adjust_height();
                        return;
                  }
                  // Now we validate the packet
                  if (!this->verify_cb2c_checksum_(response))
                  {
                        ESP_LOGW(TAG, "Checksum didn't match!");
                        status_set_warning();
                        return;
                  }

                  // Checksum is good! Extract the 'command' byte and dispatch
                  uint8_t pkt_type = response[2];
                  switch (pkt_type)
                  {
                  case 1:
                        /*
                              command 1: height report packet looks like this:

                                    [0] 0xf2         (242)
                                    [1] 0xf2         (242)
                                    [2] 0x1          (1)
                                    [3] 0x3          (3)
                                    [4] 0x1          (1)
                                    [5] 0x97         (151)
                                    [6] 0x3          (3)
                                    [7] 0x9f         (159)
                                    [8] 0x7e         (126)

                              There should be 3 params, the first two are the high/low bytes and the third has unknown purpose
                              See: https://github.com/phord/Jarvis#height-report
                        */
                        uint8_t height_hi = response[4];  // 0x01
                        uint8_t height_low = response[5]; // 0x97

                        // 0197 is 407 ... and the display says 40.7 on it!
                        this->current_pos_ = (height_hi << 8) + height_low;
                        ESP_LOGD(TAG, "height: %d", this->current_pos_);

                        // Raw input will be in tenths of $UNIT. EG 407 -> 40.7 inches
                        // _to_mm(407) => 1033.78
                        // 1033.78 * .001 = 1.03378
                        // We will let ESPHome do the truncation for us; we define 4 decimals of accuracy so
                        //   the reading will show as 1.0338m in HA. This is human friendly but still has enough
                        //   precision in it so user can do meter to mm conversion (for whatever reason...) and they'll
                        //   get something pretty accurate.
                        this->height_sensor_->publish_state(_to_mm(this->current_pos_) * .001);
                        break;
                  }

                  // If goto_height() has been called since we last ran
                  this->_adjust_height();
            }

            /*
             * Helpers
             */
            void JarvisCB2CSensor::_adjust_height()
            {
                  if (this->target_pos_ == -1)
                  {
                        ESP_LOGD(TAG, "_adjust_height. Nothing to do");
                        return;
                  }

                  /*
                        The motor controller has a smooth start/stop that it applies on top of user button presses.
                        This means that we will never have precise control over the exact postion of the desk.
                        We can measure the current height and calculate how far we need to move to get to the
                              correct height... but how long do we hold the button for?

                        I am sure that there's a very complicated way to figure this out involving some heuristics to figure out the acceleration of the desk
                              and then the derivitive of that. Once the acceleration slows down to a certain point, we can stop pressing the button and let
                              the soft slow from the motor driver get us the rest of the way. In theory I could do a lot of testing to figure out exactly at
                              what point I need to let go of the travel button for the desk to finish moving on it's own and arrive at the PRECISE height
                              that the user asked for.

                        Or I could do something else with my time.
                        I did some pretty crude testing (read: a few samples in one session) and was within a few mm every time.
                        Request 65.0? get 65.024
                        Request 75.1? get 75.184
                        Request 82.3? get 82.5

                        SO pretty close, all things considered!
                  */
                  double delta = this->target_pos_ - _to_mm(this->current_pos_);
                  ESP_LOGD(TAG, "_adjust_height. delta: %.2f", delta);
                  if (abs(delta) < 10)
                  {
                        ESP_LOGD(TAG, "_adjust_height. Close enough... not moving!");
                        _stop_and_release_all_buttons();
                        // Indicate that there's nothing to do
                        this->target_pos_ = -1;
                        return;
                  }

                  // Desk needs to up
                  if (delta > 0)
                  {
                        ESP_LOGD(TAG, "DESK UP!");
                        if (this->hc1_pin != nullptr)
                              this->hc1_pin->digital_write(false);
                  }
                  else if (delta < 0)
                  {
                        ESP_LOGD(TAG, "DESK DOWN!");
                        if (this->hc0_pin != nullptr)
                              this->hc0_pin->digital_write(false);
                  }
            }

            void JarvisCB2CSensor::goto_height(double ht_in_cm)
            {
                  /*
                        There does not appear to be a way to command the desk to a specific height.
                        Instead, we must manually manipulate the GPIO to similar a user holding the up/down buttons
                              and monitor the height while we do so.
                  */
                  // Validate the requested height
                  // The 3 stage model goes from 24.5 inches to 50 inches (without top) which is 62-127cm
                  if ((int)ht_in_cm < 62. || (int)ht_in_cm > 127)
                  {
                        ESP_LOGE(TAG, "Can't go to requested height as it's out of bounds. h: %.2f", ht_in_cm);
                        return;
                  }
                  ESP_LOGD(TAG, "goto_height. 1 requested height of: %.1f (cm). Currently at (uint16) %u ", ht_in_cm, this->current_pos_);

                  float current_height_in_mm = _to_mm(this->current_pos_);
                  ESP_LOGD(TAG, "goto_height. current_height_in_mm: %.2f", current_height_in_mm);

                  double desired_ht_in_mm = ht_in_cm * 10;
                  ESP_LOGD(TAG, "goto_height. desired_ht_in_mm: %.2f", desired_ht_in_mm);

                  if (current_height_in_mm == desired_ht_in_mm)
                  {
                        ESP_LOGD(TAG, "requested height and current height match. Nothing to do.");
                        return;
                  }
                  // Otherwise, record the new desired position, we will do the movement on loop()
                  this->target_pos_ = desired_ht_in_mm;
                  ESP_LOGD(TAG, "goto_height. target_pos_: %.2f", this->target_pos_);
            }

            void JarvisCB2CSensor::goto_preset(int p)
            {
                  if (!p || p > 4)
                  {
                        ESP_LOGE(TAG, "Called with invalid preset number: %i", p);
                  }

                  // If desk was in the process of moving to a height, stop moving
                  _stop_and_release_all_buttons();

                  switch (p)
                  {
                  case 1:
                        if (this->hc0_pin != nullptr)
                              this->hc0_pin->digital_write(false);

                        if (this->hc1_pin != nullptr)
                              this->hc1_pin->digital_write(false);

                        delay(btn_delay_time);

                        if (this->hc0_pin != nullptr)
                              this->hc0_pin->digital_write(true);

                        if (this->hc1_pin != nullptr)
                              this->hc1_pin->digital_write(true);
                        break;
                  case 2:
                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(false);

                        delay(btn_delay_time);

                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(true);
                        break;
                  case 3:
                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(false);
                        if (this->hc0_pin != nullptr)
                              this->hc0_pin->digital_write(false);

                        delay(btn_delay_time);

                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(true);
                        if (this->hc0_pin != nullptr)
                              this->hc0_pin->digital_write(true);

                        break;
                  case 4:
                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(false);

                        if (this->hc1_pin != nullptr)
                              this->hc1_pin->digital_write(false);

                        delay(btn_delay_time);

                        if (this->hc2_pin != nullptr)
                              this->hc2_pin->digital_write(true);
                        if (this->hc1_pin != nullptr)
                              this->hc1_pin->digital_write(true);

                        break;
                  }
            }

            void JarvisCB2CSensor::do_wake()
            {
                  ESP_LOGD(TAG, "doing wake");
                  uint8_t pkt[] = {
                      0xF1, // address of motor
                      0xF1, // address of motor
                      0x29, // WAKE command
                      0x0,  // 0 params
                      0x29, // checksum of 0x29+0 is ... 0x29 :)
                      0x7e  // end of message
                  };
                  this->write_array(pkt, 6);
            }

            void JarvisCB2CSensor::_stop_and_release_all_buttons()
            {
                  ESP_LOGD(TAG, "Stop All!");
                  this->target_pos_ = -1;

                  // GPIO are active low, idle high
                  if (this->hc0_pin != nullptr)
                        this->hc0_pin->digital_write(true);
                  if (this->hc1_pin != nullptr)
                        this->hc1_pin->digital_write(true);
                  if (this->hc2_pin != nullptr)
                        this->hc2_pin->digital_write(true);
                  if (this->hc3_pin != nullptr)
                        this->hc3_pin->digital_write(true);
            }

            void JarvisCB2CSensor::do_null()
            {
                  ESP_LOGD(TAG, "doing null wake");
                  uint8_t pkt[] = {
                      0x00};
                  this->write_array(pkt, 1);
            }

            void JarvisCB2CSensor::do_m()
            {
                  ESP_LOGD(TAG, "doing m button press");
                  if (this->hc3_pin != nullptr)
                        this->hc3_pin->digital_write(false);
                  if (this->hc0_pin != nullptr)
                        this->hc0_pin->digital_write(false);
                  delay(100);
                  if (this->hc3_pin != nullptr)
                        this->hc3_pin->digital_write(true);
                  if (this->hc0_pin != nullptr)
                        this->hc0_pin->digital_write(true);
            }

            // TODO: almost certainly want to but a max timeout in here
            // What happens if I click the UP button and then HA crashes? I
            void JarvisCB2CSensor::do_manual_move(char direction)
            {
                  switch (direction)
                  {
                  case 'u':
                        ESP_LOGD(TAG, "manual move up");
                        break;

                  case 'd':
                        ESP_LOGD(TAG, "manual move down");
                        break;
                  }
            }

            float JarvisCB2CSensor::_to_mm(int16_t h)
            {

                  float conv = 0;
                  /*
                        TODO: I should probably send the bytes to the controller to ask it which units it's configured with.
                        From testing, though, desk controller seems to report these values::
                              Height from 240..530 is in inches
                              Height from 650..1290 is in mm
                  */
                  if (h < 600)
                  {
                        // 1 inch is 25.4mm, so 1 tenth of an inch is 2.54mm
                        // 407 * 2.54 = 1033.78
                        conv = h * 2.54;
                  }
                  // Print the raw h and with the proper tenths formatting and the converted value
                  ESP_LOGD(TAG, "Converted h(%d) => %f to %f mm", h, (h * .1), conv);
                  return conv;
            }

            bool JarvisCB2CSensor::verify_cb2c_checksum_(uint8_t *ptr)
            {
                  _dump_packet(ptr);
                  // byte 0,1 are address
                  // byte 2 is the command
                  // byte 3 is the number of parameters
                  uint8_t param_len = ptr[3];
                  // ESP_LOGD(TAG, "param_len: %u", param_len);

                  // Once we know paramater length, we know where the checksum _should_ be:
                  // It'll be the byte right after $param_len bytes beyond the 4th byte
                  uint8_t expected_sum_byte = (3 + (param_len + 1));
                  // ESP_LOGD(TAG, "expected_sum_byte: %u", expected_sum_byte);
                  // ESP_LOGD(TAG, "expected_sum: %u", ptr[expected_sum_byte]);

                  // Checksum is the 8 bit sum of all bytes after the first 2 up to the parameters.
                  // After the first two bytes we know there will be 2 more bytes and optionally up to 3 more.
                  // This excludes the last 2 bytes which should be checksum and EOM
                  uint8_t calc_sum = 0;
                  for (int i = 2; i < 4 + param_len; i++)
                  {
                        // ESP_LOGD(TAG, "ptr[%i]: %x", i, ptr[i]);
                        calc_sum += ptr[i];
                  }
                  // ESP_LOGD(TAG, "calc_sum: %u", calc_sum);
                  return calc_sum == ptr[expected_sum_byte];
            }

            void JarvisCB2CSensor::_dump_packet(uint8_t *ptr)
            {
                  return;
                  // ESP_LOGD(TAG, "Got Bytes:");
                  // for (int i = 0; i < JARVIS_PACKET_MAX_LEN; i++)
                  // {
                  //       ESP_LOGD(TAG, "[%i] 0x%x \t (%u)", i, ptr[i], ptr[i]);
                  // }
            }

            void JarvisCB2CSensor::dump_config()
            {
                  ESP_LOGCONFIG(TAG, "JarvisCB2CSensor desk:");
                  LOG_SENSOR("", "Height", this->height_sensor_);
                  LOG_PIN("hc0_pin: ", this->hc0_pin);
                  LOG_PIN("hc1_pin: ", this->hc1_pin);
                  LOG_PIN("hc2_pin: ", this->hc2_pin);
                  LOG_PIN("hc3_pin: ", this->hc3_pin);
            }

      } // namespace fully_jarvis_cb2c
} // namespace esphome
