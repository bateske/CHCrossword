// SdSpi.cpp - see SdSpi.h. From HypeRunner's src/sd/SdSpi.cpp (MIT, clean
// room: written from the SD Physical Layer Simplified Specification, chapter
// 7, and the CH32X035 reference manual), reduced to polled single-block
// reads. The simulator's stand-in is tools/chsim/host/sd_host.cpp.
#pragma GCC optimize("Os", "no-ipa-sra")
#if !defined(CHSIM) && !defined(CHTEST)
#include <Arduino.h>
#include "SdSpi.h"

namespace sd {

// SPI1: master, software NSS held high, mode 0, 8-bit, MSB first.
static const uint32_t SPI_MASTER = (1u << 2) | (1u << 8) | (1u << 9);   // MSTR | SSI | SSM
static const uint32_t SPE = 1u << 6, RXNE = 1u << 0, BSY = 1u << 7;
static const uint8_t BR_IDENT = 7;              // 48 MHz / 256 = 187.5 kHz
static const uint8_t BR_RUN = 1;                // 12 MHz: polled bytes are slower than the wire anyway
static const uint32_t TOKEN_US = 300000, BUSY_US = 300000, INIT_US = 1000000;

static uint8_t br;
static bool hc;                                 // block addressing (SDHC/SDXC)
static uint16_t lcdCtlr1;                       // CHGfx's SPI1 setup, put back afterwards

static uint8_t xfer(uint8_t b) {
    SPI1->DATAR = b;
    while (!(SPI1->STATR & RXNE)) {}
    return (uint8_t)SPI1->DATAR;
}

// BR and the frame size may change only while SPE is clear.
static void spiSet(uint32_t ctlr1) {
    while (SPI1->STATR & BSY) {}
    SPI1->CTLR1 = (uint16_t)(ctlr1 & ~SPE);
    SPI1->CTLR1 = (uint16_t)ctlr1;
}

static void claim() {
    GPIOA->BSHR = 1u << 4;                      // the panel deselected
    lcdCtlr1 = (uint16_t)SPI1->CTLR1;
    spiSet(SPI_MASTER | SPE | ((uint32_t)br << 3));
    (void)SPI1->DATAR;                          // the panel never reads: RXNE and OVR are set
    (void)SPI1->STATR;
    GPIOB->BCR = 1u << 11;                      // card selected
}

// The card drives DO until it sees a clock with CS high.
static void release() {
    GPIOB->BSHR = 1u << 11;
    xfer(0xFF);
    spiSet(lcdCtlr1);
}

// Clocks 0xFF until a start token arrives (tok: anything but 0xFF) or busy
// ends (!tok: 0xFF), or us runs out. Returns the last byte.
static uint8_t wait(uint32_t us, bool tok) {
    uint32_t t0 = micros();
    uint8_t b;
    do b = xfer(0xFF); while ((b == 0xFF) == tok && micros() - t0 <= us);
    return b;
}

// 48-bit command frame: 01 + index, the argument MSB first, CRC7 and the end
// bit. Only CMD0 and CMD8 are checked by a card in SPI mode (CRC is left
// off): their CRCs are the two constants. R1 arrives within 8 bytes; bit 7
// set means nothing answered.
static uint8_t cmd(uint8_t c, uint32_t arg) {
    xfer((uint8_t)(0x40 | c));
    for (int sh = 24; sh >= 0; sh -= 8) xfer((uint8_t)(arg >> sh));
    xfer(c == 8 ? 0x87 : 0x95);
    uint8_t r, k = 9;
    do r = xfer(0xFF); while ((r & 0x80) && --k);
    return r;
}

static uint32_t rd32() {
    uint32_t r = 0;
    for (uint8_t i = 0; i < 4; i++) r = (r << 8) | xfer(0xFF);
    return r;
}

// Identification at 187.5 kHz with CS low (spec figure 7-2).
static bool ident() {
    uint8_t r, k = 20;
    wait(BUSY_US, false);
    while (cmd(0, 0) != 0x01)                   // GO_IDLE_STATE: enter SPI mode
        if (!--k) return false;
    bool v2 = false;
    r = cmd(8, 0x1AA);                          // SEND_IF_COND 2.7-3.6 V, check pattern 0xAA
    if (!(r & 0x04)) {                          // not an illegal command: v2.00 or later
        if (r != 0x01 || (rd32() & 0xFFF) != 0x1AA) return false;
        v2 = true;
    }
    uint32_t t0 = micros();
    do {                                        // ACMD41 (HCS on v2) until idle clears
        r = cmd(55, 0);
        if (r <= 1) r = cmd(41, v2 ? 1ul << 30 : 0);
        if (r > 1 || micros() - t0 > INIT_US) return false;
    } while (r);
    hc = false;
    if (v2) {                                   // READ_OCR: CCS = block addressing
        if (cmd(58, 0)) return false;
        hc = (rd32() >> 30) & 1;
    }
    return hc || cmd(16, 512) == 0;             // SDSC: 512-byte blocks
}

bool init() {
    GPIOA->BSHR = 1u << 6;                      // MISO pulled up: an empty slot reads 0xFF
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xFul << 24)) | (0x8ul << 24);
    br = BR_IDENT;
    claim();
    GPIOB->BSHR = 1u << 11;
    for (uint8_t k = 0; k < 10; k++) xfer(0xFF);    // >= 74 clocks with CS high
    GPIOB->BCR = 1u << 11;
    bool ok = ident();
    release();
    br = BR_RUN;
    return ok;
}

bool read(uint32_t lba, uint8_t *dst) {
    claim();
    bool ok = wait(BUSY_US, false) == 0xFF && cmd(17, hc ? lba : lba << 9) == 0 && wait(TOKEN_US, true) == 0xFE;
    if (ok) {
        for (uint16_t i = 0; i < 512; i++) dst[i] = xfer(0xFF);
        xfer(0xFF);                             // the CRC16, unused
        xfer(0xFF);
    }
    release();
    return ok;
}

}  // namespace sd
#endif
