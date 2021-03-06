/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 hathach for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "tick.h"
#include "shared-bindings/microcontroller/Processor.h"
#include "shared-module/usb_midi/__init__.h"
#include "supervisor/port.h"
#include "supervisor/usb.h"
#include "lib/utils/interrupt_char.h"
#include "lib/mp-readline/readline.h"

#include "tusb.h"

// Serial number as hex characters. This writes directly to the USB
// descriptor.
extern uint16_t usb_serial_number[1 + COMMON_HAL_MCU_PROCESSOR_UID_LENGTH * 2];

void load_serial_number(void) {
    // create serial number based on device unique id
    uint8_t raw_id[COMMON_HAL_MCU_PROCESSOR_UID_LENGTH];
    common_hal_mcu_processor_get_uid(raw_id);

    const char nibble_to_hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                                    'A', 'B', 'C', 'D', 'E', 'F'};
    for (int i = 0; i < COMMON_HAL_MCU_PROCESSOR_UID_LENGTH; i++) {
        for (int j = 0; j < 2; j++) {
            uint8_t nibble = (raw_id[i] >> (j * 4)) & 0xf;
            // Strings are UTF-16-LE encoded.
            usb_serial_number[1 + i * 2 + j] = nibble_to_hex[nibble];
        }
    }
}

bool usb_enabled(void) {
    return tusb_inited();
}

void usb_init(void) {
    init_usb_hardware();
    load_serial_number();

    tusb_init();

#if MICROPY_KBD_EXCEPTION
    // Set Ctrl+C as wanted char, tud_cdc_rx_wanted_cb() callback will be invoked when Ctrl+C is received
    // This callback always got invoked regardless of mp_interrupt_char value since we only set it once here
    tud_cdc_set_wanted_char(CHAR_CTRL_C);
#endif

    usb_midi_init();
}

void usb_background(void) {
    if (usb_enabled()) {
        tud_task();
        tud_cdc_write_flush();
    }
}

//--------------------------------------------------------------------+
// tinyusb callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
}

uint32_t tusb_hal_millis(void) {
    uint64_t ms;
    uint32_t us;
    current_tick(&ms, &us);
    return (uint32_t) ms;
}


// Invoked when cdc when line state changed e.g connected/disconnected
// Use to reset to DFU when disconnect with 1200 bps
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void) itf; // interface ID, not used

    // DTR = false is counted as disconnected
    if ( !dtr )
    {
        cdc_line_coding_t coding;
        tud_cdc_get_line_coding(&coding);

        if ( coding.bit_rate == 1200 )
        {
            reset_to_bootloader();
        }
    }
}

#if MICROPY_KBD_EXCEPTION

/**
 * Callback invoked when received an "wanted" char.
 * @param itf           Interface index (for multiple cdc interfaces)
 * @param wanted_char   The wanted char (set previously)
 */
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
    (void) itf; // not used

    // Workaround for using lib/utils/interrupt_char.c
    // Compare mp_interrupt_char with wanted_char and ignore if not matched
    if (mp_interrupt_char == wanted_char) {
        tud_cdc_read_flush();    // flush read fifo
        mp_keyboard_interrupt();
    }
}

#endif
