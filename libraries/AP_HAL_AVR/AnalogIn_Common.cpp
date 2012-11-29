
#include <avr/io.h>
#include <avr/interrupt.h>

#include <AP_HAL.h>
#include "AnalogIn.h"
using namespace AP_HAL_AVR;

extern const AP_HAL::HAL& hal;

/* CHANNEL_READ_REPEAT: how many reads on a channel before using the value.
 * This seems to be determined empirically */
#define CHANNEL_READ_REPEAT 2

/* Static variable instances */
ADCSource* AVRAnalogIn::_channels[AVR_INPUT_MAX_CHANNELS] = {NULL};
int AVRAnalogIn::_num_channels = 0;
int AVRAnalogIn::_active_channel = 0;
int AVRAnalogIn::_channel_repeat_count = 0;

AVRAnalogIn::AVRAnalogIn() :
    _vcc(ADCSource(ANALOG_INPUT_BOARD_VCC))
{}

void AVRAnalogIn::init(void* machtnichts) {
    /* Register AVRAnalogIn::_timer_event with the scheduler. */
    hal.scheduler->register_timer_process(_timer_event);
    /* Register each private channel with AVRAnalogIn. */
    _register_channel(&_vcc);
}

ADCSource* AVRAnalogIn::_find_or_create_channel(int pin) {
    for(int i = 0; i < _num_channels; i++) {
        if (_channels[i]->_pin == pin) {
            return _channels[i];
        }
    }
    return _create_channel(pin);
}

ADCSource* AVRAnalogIn::_create_channel(int chnum) {
    ADCSource *ch = new ADCSource(chnum);
    _register_channel(ch);
    return ch;
}

void AVRAnalogIn::_register_channel(ADCSource* ch) {
    if (_num_channels >= AVR_INPUT_MAX_CHANNELS) {
        for(;;) {
            hal.console->print_P(PSTR(
                "Error: AP_HAL_AVR::AVRAnalogIn out of channels\r\n"));
            hal.scheduler->delay(1000);
        }
    }
    _channels[_num_channels] = ch;
    /* Need to lock to increment _num_channels as it is used
     * by the interrupt to access _channels */
    cli();
    _num_channels++;
    sei();

    if (_num_channels == 1) {
        /* After registering the first channel, we can enable the ADC */
        PRR0 &= ~_BV(PRADC);
        ADCSRA |= _BV(ADEN);
    }
}

void AVRAnalogIn::_timer_event(uint32_t t) {

    if (_channels[_active_channel]->_pin == ANALOG_INPUT_NONE) {
        goto next_channel;
    }

    if (ADCSRA & _BV(ADSC)) {
        /* ADC Conversion is still running - this should not happen, as we
         * are called at 1khz. */
        return;
    }

    if (_num_channels == 0) {
        /* No channels are registered - nothing to be done. */
        return;
    }

    _channel_repeat_count++;
    if (_channel_repeat_count < CHANNEL_READ_REPEAT) {
        /* Start a new conversion, throw away the current conversion */
        ADCSRA |= _BV(ADSC);
        return;
    } else {
        _channel_repeat_count = 0;
    }

    /* Read the conversion registers. */
    {
        uint8_t low = ADCL;
        uint8_t high = ADCH;
        uint16_t sample = low | (((uint16_t)high) << 8);
        /* Give the active channel a new sample */
        _channels[_active_channel]->new_sample( sample );
    }
next_channel:
    /* Move to the next channel */
    _active_channel = (_active_channel + 1) % _num_channels;
    /* Setup the next channel's conversion */
    _channels[_active_channel]->setup_read();
    /* Start conversion */
    ADCSRA |= _BV(ADSC);
}


AP_HAL::AnalogSource* AVRAnalogIn::channel(int ch) {
    if (ch == ANALOG_INPUT_BOARD_VCC) {
            return &_vcc;
    } else {
        return _find_or_create_channel(ch);
    }
}

