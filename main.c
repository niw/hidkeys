#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "usbdrv.h"
#include "keyboard.h"

static void hardwareInit(void) {
    volatile uchar i, j;
    // activate all pull-ups
    PORTB = 0xFF;
    // all pins input
    DDRB = 0;
    // activate pull-ups except on USB lines
    PORTD = 0b11111010;
    // all pins input except USB (-> USB reset)
    DDRD = 0b00000101;
    // USB Reset by device only required on Watchdog Reset
    j = 0;
    while(--j) {
        // delay >10ms for USB reset
        i = 0;
        while(--i);
    }
    // remove USB reset condition
    DDRD = 0b00000000;
    // timer 0 prescaler: 1024
    TCCR0 = 0x05;
}

// The following function returns an index for the first key pressed. It
// returns 0 if no key is pressed.
static uchar keyPressed(void) {
    uchar i, mask;

    for(i = 0, mask = 1; i < 8; i++) {
        if ((PINB & mask) == 0) {
            return i + 1;
        }
        mask = mask << 1;
    }

    return 0;
}

// USB report descriptor
PROGMEM char usbHidReportDescriptor[35] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xa1, 0x01, // COLLECTION (Application)
    0x05, 0x07, //   USAGE_PAGE (Keyboard)
    0x19, 0xe0, //   USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7, //   USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x95, 0x08, //   REPORT_COUNT (8)
    0x81, 0x02, //   INPUT (Data,Var,Abs)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x25, 0x65, //   LOGICAL_MAXIMUM (101)
    0x19, 0x00, //   USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65, //   USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00, //   INPUT (Data,Ary,Abs)
    0xc0        // END_COLLECTION
};

// See keyboard.h
static const uchar keyReport[9][2] PROGMEM = {
    {0, 0},     // No press
    {0, KEY_A}, // 0
    {0, KEY_B}, // 1
    {0, KEY_C}, // 2
    {0, KEY_D}, // 3
    {0, KEY_E}, // 4
    {0, KEY_F}, // 5
    {0, KEY_G}, // 6
    {0, KEY_H}  // 7
};

// buffer for HID reports
static uchar reportBuffer[2];

// in 4 ms units
static uchar idleRate;


static void buildReport(uchar key) {
    // This (not so elegant) cast saves us 10 bytes of program memory.
    *(int *)reportBuffer = pgm_read_word(keyReport[key]);
}

uchar usbFunctionSetup(uchar data[8]) {
    usbRequest_t *rq = (void *)data;

    // data to transmit next, ROM or RAM address.
    usbMsgPtr = reportBuffer;

    // class request type
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
        if(rq->bRequest == USBRQ_HID_GET_REPORT) {
            // wValue: ReportType (highbyte), ReportID (lowbyte)
            // we only have one report type, so don't look at wValue
            buildReport(keyPressed());
            return sizeof(reportBuffer);
        } else if(rq->bRequest == USBRQ_HID_GET_IDLE) {
            usbMsgPtr = &idleRate;
            return 1;
        } else if(rq->bRequest == USBRQ_HID_SET_IDLE) {
            idleRate = rq->wValue.bytes[1];
        }
    } else {
        // no vendor specific requests implemented.
    }

    return 0;
}

int main(void) {
    uchar key, lastKey = 0, keyDidChange = 0;
    uchar idleCounter = 0;

    wdt_enable(WDTO_2S);
    hardwareInit();
    usbInit();
    sei();

    for(;;) {
        wdt_reset();
        usbPoll();

        key = keyPressed();

        if(lastKey != key) {
            lastKey = key;
            keyDidChange = 1;
        }

        // 22 ms timer
        if(TIFR & (1<<TOV0)) {
            TIFR = 1<<TOV0;
            if(idleRate != 0) {
                if(idleCounter > 4) {
                    // 22 ms in units of 4 ms
                    idleCounter -= 5;
                } else {
                    idleCounter = idleRate;
                    keyDidChange = 1;
                }
            }
        }

        if(keyDidChange && usbInterruptIsReady()) {
            keyDidChange = 0;
            // use last key and not current key status in order to avoid lost
            // changes in key status.
            buildReport(lastKey);
            usbSetInterrupt(reportBuffer, sizeof(reportBuffer));
        }
    }
    return 0;
}

// vim:set ts=4 sw=4 expandtab:
