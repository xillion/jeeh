// Show incoming wireless RF69 packets on USART1.

#include <jee.h>
#include <jee/spi-rf69.h>

UartBufDev< PinA<9>, PinA<10> > console;  // buffering avoids missed packets

void printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); veprintf(console.putc, fmt, ap); va_end(ap);
}

RF69< PinA<7>, PinA<6>, PinA<5>, PinA<4> > rf;  // default SPI1 pins

PinC<13> led;

int main () {
    led.mode(Pinmode::out);
    rf.init(63, 42, 8683);  // node 63, group 42, 868.3 MHz

    while (true) {
        uint8_t rxBuf [64];
        auto rxLen = rf.receive(rxBuf, sizeof rxBuf);

        if (rxLen >= 0) {
            led.toggle();

            printf("RF69 #%d: ", rxLen);
            for (int i = 0; i < rxLen; ++i)
                printf("%02x", rxBuf[i]);
            printf("\n");
        }
    }
}

// sample output:
//
//  RF69 #9: C0018101A15DAB8080
//  RF69 #6: C00181808080
//  RF69 #9: C0018101A25DAC8080