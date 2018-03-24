#include <stdarg.h>
#include <stdint.h>

#define MMIO32(x) (*(volatile uint32_t*) (x))

struct Periph {
    constexpr static uint32_t gpio = 0x40010800U;
    constexpr static uint32_t rcc  = 0x40021000U;
};

// GPIO

template<char port>
struct Port {
    constexpr static uint32_t base = Periph::gpio + 0x400 * (port-'A');
    constexpr static uint32_t crl  = base + 0x00;
    constexpr static uint32_t crh  = base + 0x04;
    constexpr static uint32_t idr  = base + 0x08;
    constexpr static uint32_t odr  = base + 0x0C;
    constexpr static uint32_t bsrr = base + 0x10;
    constexpr static uint32_t brr  = base + 0x14;
};

enum class Pinmode {
    in_analog        = 0b0000,
    in_float         = 0b0100,
    in_pulldown      = 0b1000,  // pseudo mode, also clears output
    in_pullup        = 0b1100,  // pseudo mode, also sets output

    out_10mhz        = 0b0001,
    out_od_10mhz     = 0b0101,
    alt_out_10mhz    = 0b1001,
    alt_out_od_10mhz = 0b1101,

    out_2mhz         = 0b0010,
    out_od_2mhz      = 0b0110,
    alt_out_2mhz     = 0b1010,
    alt_out_od_2mhz  = 0b1110,

    out              = 0b0011,
    out_od           = 0b0111,
    alt_out          = 0b1011,
    alt_out_od       = 0b1111,
};

template<char port,int pin>
struct Pin {
    typedef Port<port> gpio;
    constexpr static uint16_t mask = 1U << pin;

    static void mode (Pinmode m) {
        // enable GPIOx and AFIO clocks
        MMIO32(Periph::rcc + 0x18) |= (1 << (port-'A'+2)) | (1<<0);

        auto mval = static_cast<int>(m);
        if (mval == 0b1000 || mval == 0b1100) {
            MMIO32(gpio::bsrr) = mval & 0b0100 ? mask : mask << 16;
            mval = 0b1000;
        }

        constexpr uint32_t cr = pin & 8 ? gpio::crh : gpio::crl;
        constexpr int shift = 4 * (pin & 7);
        MMIO32(cr) = (MMIO32(cr) & ~(0xF << shift)) | (mval << shift);
    }

    static int read () {
        return mask & MMIO32(gpio::idr) ? 1 : 0;
    }

    static void write (int v) {
        // MMIO32(v ? gpio::bsrr : gpio::brr) = mask;
        // this is slightly faster when v is not known at compile time:
        MMIO32(gpio::bsrr) = v ? mask : mask << 16;
    }

    // shorthand
    operator int () const { return read(); }
    void operator= (int v) const { write(v); }

    static void toggle () {
        // both versions below are non-atomic, they access and set in two steps
        // this is smaller and faster (1.6 vs 1.2 MHz on F103 @ 72 MHz):
        // MMIO32(gpio::odr) ^= mask;
        // but this code is safer, because it can't interfere with nearby pins:
        MMIO32(gpio::bsrr) = mask & MMIO32(gpio::odr) ? mask << 16 : mask;
    }
};

// USART1

template< typename TX, typename RX >
class UartDev {
    constexpr static uint32_t base = 0x40013800;  // TODO only USART1 for now
    constexpr static uint32_t sr  = base + 0x00;
    constexpr static uint32_t dr  = base + 0x04;
    constexpr static uint32_t brr = base + 0x08;
    constexpr static uint32_t cr1 = base + 0x0C;

public:
    UartDev () {
        tx.mode(Pinmode::alt_out);
        rx.mode(Pinmode::in_float);

        MMIO32(Periph::rcc + 0x18) |= 1 << 14; // enable USART1 clock
        MMIO32(brr) = 70;  // 115200 baud @ 8 MHz
        MMIO32(cr1) = (1<<13) | (1<<3) | (1<<2);  // UE TE RE
    }

    static void puts (const char* s) {
        while (*s)
            putc(*s++);
    }

    static void putc (int c) {
        while (!writable())
            ;
        MMIO32(dr) = (uint8_t) c;
    }

    static int getc () {
        while (!readable())
            ;
        return MMIO32(dr);
    }

    static bool writable () {
        return (MMIO32(sr) & 0x80) != 0;
    }

    static bool readable () {
        return (MMIO32(sr) & 0x20) != 0;
    }

    static TX tx;
    static RX rx;
};

template< typename TX, typename RX >
TX UartDev<TX,RX>::tx;

template< typename TX, typename RX >
RX UartDev<TX,RX>::rx;

// SPI, bit-banged on any GPIO pins

template< typename MO, typename MI, typename CK, typename SS >
class SpiDev {
public:
    SpiDev () {
        nss = 1;
        nss.mode(Pinmode::out);
        sclk = 0;
        sclk.mode(Pinmode::out);
        miso.mode(Pinmode::in_float);
        mosi.mode(Pinmode::out);
    }

    static void enable () {
        nss = 0;
    }

    static void disable () {
        nss = 1;
    }

    static uint8_t transfer (uint8_t v) {
        for (int i = 0; i < 8; ++i) {
            mosi = v & 0x80;
            sclk = 1;
            v <<= 1;
            v |= miso;
            sclk = 0;
        }
        return v;
    }

    static uint8_t rwReg (uint8_t cmd, uint8_t val) {
        enable();
        transfer(cmd);
        uint8_t r = transfer(val);
        disable();
        return r;
    }

    static MO mosi;
    static MI miso;
    static CK sclk;
    static SS nss;
};

template< typename MO, typename MI, typename CK, typename SS >
MO SpiDev<MO,MI,CK,SS>::mosi;

template< typename MO, typename MI, typename CK, typename SS >
MI SpiDev<MO,MI,CK,SS>::miso;

template< typename MO, typename MI, typename CK, typename SS >
CK SpiDev<MO,MI,CK,SS>::sclk;

template< typename MO, typename MI, typename CK, typename SS >
SS SpiDev<MO,MI,CK,SS>::nss;

// I2C, bit-banged on any GPIO pins

template< typename SDA, typename SCL >
class I2cDev {
    static void hold () {
        // TODO make configurabe, this is ≈ 360 kHz for STM32F1 @ 72 MHz
        for (int i = 0; i < 5; ++i)
            __asm("");
    }

    static void sclHi () { scl = 1; hold(); }
    static void sclLo () { scl = 0; hold(); }

public:
    I2cDev () {
        sda = 1;
        sda.mode(Pinmode::out_od);
        scl = 1;
        scl.mode(Pinmode::out_od);
    }

    static uint8_t start(int addr) {
        sclLo();
        sclHi();
        sda = 0;
        return write(addr);
    }

    static void stop() {
        sda = 0;
        sclHi();
        sda = 1;
    }

    static bool write(int data) {
        sclLo();
        for (int mask = 0x80; mask != 0; mask >>= 1) {
            sda = data & mask;
            sclHi();
            sclLo();
        }
        sda = 1;
        sclHi();
        bool ack = !sda;
        sclLo();
        return ack;
    }

    static int read(bool last) {
        int data = 0;
        for (int mask = 0x80; mask != 0; mask >>= 1) {
            sclHi();
            if (sda)
                data |= mask;
            sclLo();
        }
        sda = last;
        sclHi();
        sclLo();
        if (last)
            stop();
        sda = 1;
        return data;
    }

    static SDA sda;
    static SCL scl;
};

template< typename SDA, typename SCL >
SDA I2cDev<SDA,SCL>::sda;

template< typename SDA, typename SCL >
SCL I2cDev<SDA,SCL>::scl;

// formatted OUTPUT

extern void putInt (void (*emit)(int), int val, int base =10, int width =0, char fill =' ');
extern void veprintf(void (*emit)(int), const char* fmt, va_list ap);