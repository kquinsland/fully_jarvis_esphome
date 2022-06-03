import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.components import sensor
from esphome.const import CONF_ID, CONF_HEIGHT, ICON_RULER, DEVICE_CLASS_EMPTY, STATE_CLASS_MEASUREMENT, UNIT_METER


DEPENDENCIES = ['uart']
AUTO_LOAD = ['sensor']

jarvis_cb2c_ns = cg.esphome_ns.namespace('fully_jarvis_cb2c')
JarvisCB2CSensor = jarvis_cb2c_ns.class_('JarvisCB2CSensor', cg.Component, sensor.Sensor, uart.UARTDevice)

# The gpio pins remote manipulates to indicate a button press
HC_0_PIN = "hc0_pin"
HC_1_PIN = "hc1_pin"
HC_2_PIN = "hc2_pin"
HC_3_PIN = "hc3_pin"


CONFIG_SCHEMA = cv.COMPONENT_SCHEMA.extend(
    {
        # UID for the component
        cv.GenerateID(): cv.declare_id(JarvisCB2CSensor),
        # GPIO pins
        cv.Optional(HC_0_PIN): pins.gpio_output_pin_schema,
        cv.Optional(HC_1_PIN): pins.gpio_output_pin_schema,
        cv.Optional(HC_2_PIN): pins.gpio_output_pin_schema,
        cv.Optional(HC_3_PIN): pins.gpio_output_pin_schema,

        # Desk elevation
        cv.Optional(CONF_HEIGHT): sensor.sensor_schema(
            unit_of_measurement=UNIT_METER,
            icon=ICON_RULER,
            # Unit works in mm/tenths of inches. The measure is reported using a high/low byte
            #   with one decimal of accuracy. Since we are reporting distance in METERS, we indicate 3
            #    digits of precision. E.G.: Desk might be at 25.9 inches which is 0.65786 meter. We would
            #     report this as .658 meter
            accuracy_decimals=4,
            device_class=DEVICE_CLASS_EMPTY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if HC_0_PIN in config:
        pin = await cg.gpio_pin_expression(config[HC_0_PIN])
        cg.add(var.set_hc0_pin(pin))
    if HC_1_PIN in config:
        pin = await cg.gpio_pin_expression(config[HC_1_PIN])
        cg.add(var.set_hc1_pin(pin))
    if HC_2_PIN in config:
        pin = await cg.gpio_pin_expression(config[HC_2_PIN])
        cg.add(var.set_hc2_pin(pin))
    if HC_3_PIN in config:
        pin = await cg.gpio_pin_expression(config[HC_3_PIN])
        cg.add(var.set_hc3_pin(pin))

    if CONF_HEIGHT in config:
        sens = await sensor.new_sensor(config[CONF_HEIGHT])
        cg.add(var.set_height_sensor(sens))
