#pragma GCC optimize("Os", "no-ipa-sra")   // cold code: size over speed
#include <Arduino.h>
#include <string.h>
#include <stddef.h>
#include <CHGfx.h>
#include "../../config.h"
#include "../RamFunc.h"
#include "Save.h"

namespace save {

#if CHCW_LEAN
// A build without saving (config.h).
bool available() { return false; }
bool load(Options &, Progress &, bool &hasGame) { hasGame = false; return false; }
bool loadGame(game::Record &) { return false; }
bool store(const Options &, const Progress &, const game::Record *) { return false; }
#else
static const uint32_t MAGIC = 0x57434843u;       // "CHCW"
static const uint8_t VERSION = 1;
static const uint32_t PAGE = 256;
static const uint32_t PAGE_A = 0xF500, PAGE_B = 0xF600;   // metadata page is 0xF700

struct Record {
    uint32_t magic;
    uint8_t  version, hasGame;
    uint16_t seq;
    Options  opt;
    game::Record game;
    Progress prog;
    uint32_t crc;
};
static_assert(sizeof(Record) <= PAGE, "save record must fit one flash page");
// The CRC covers everything before it (the record may have padding after it).
static const uint32_t CRC_OVER = offsetof(Record, crc);

static uint16_t lastSeq = 0;
static bool broken = false;

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static bool valid(const Record *r) {
    return r->magic == MAGIC && r->version == VERSION &&
           r->crc == crc32((const uint8_t *)r, CRC_OVER);
}

#ifndef CHSIM
extern "C" uint32_t _data_lma, _data_vma, _edata;

static uint32_t imageEnd() {
    return (uint32_t)&_data_lma + ((uint32_t)&_edata - (uint32_t)&_data_vma);
}

// Flash controller, mirrored from CH32SerialBoot/bootloader/src/flash.c.
// Must run from SRAM, with interrupts off (the vector table is in flash).
#define CR_STRT 0x00000040u
#define CR_FLOCK 0x00008000u
#define CR_PAGE_PG 0x00010000u
#define CR_PAGE_ER 0x00020000u
#define CR_BUF_LOAD 0x00040000u
#define CR_BUF_RST 0x00080000u
#define SR_BSY 0x00000001u
#define PROG(a) ((a) + 0x08000000u)

RAMFUNC(save) static void pageWrite(uint32_t addr, const uint32_t *w) {
    uint32_t irq;
    __asm volatile("csrr %0, 0x800" : "=r"(irq));
    __asm volatile("csrw 0x800, %0" : : "r"(irq & ~0x88u));
    FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
    FLASH->MODEKEYR = 0x45670123u; FLASH->MODEKEYR = 0xCDEF89ABu;
    FLASH->CTLR |= CR_PAGE_ER;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_ER;
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->CTLR |= CR_BUF_RST;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    for (uint32_t i = 0; i < PAGE / 4; i++) {
        FLASH->CTLR |= CR_PAGE_PG;
        *(volatile uint32_t *)(PROG(addr) + i * 4) = w[i];
        FLASH->CTLR |= CR_BUF_LOAD;
        while (FLASH->STATR & SR_BSY) {}
        FLASH->CTLR &= ~CR_PAGE_PG;
    }
    FLASH->CTLR |= CR_PAGE_PG;
    FLASH->ADDR = PROG(addr);
    FLASH->CTLR |= CR_STRT;
    while (FLASH->STATR & SR_BSY) {}
    FLASH->CTLR &= ~CR_PAGE_PG;
    FLASH->CTLR |= CR_FLOCK;
    __asm volatile("csrw 0x800, %0" : : "r"(irq));
}

// Two pages when the image leaves room for them, else just the last one.
static bool twoPages() { return imageEnd() <= PAGE_A; }
bool available() { return !broken && imageEnd() <= PAGE_B; }

static const Record *page(uint32_t a) { return (const Record *)a; }

static bool writePage(uint32_t addr, const uint8_t *buf) {
    pageWrite(addr, (const uint32_t *)buf);
    return memcmp((const void *)addr, buf, PAGE) == 0;
}
#else
// Simulator: in-memory "flash" so save/continue flows can be scripted.
static uint8_t simFlash[2][PAGE];
bool available() { return !broken; }
static bool twoPages() { return true; }
static const Record *page(uint32_t a) { return (const Record *)simFlash[a == PAGE_B]; }
static bool writePage(uint32_t addr, const uint8_t *buf) {
    memcpy(simFlash[addr == PAGE_B], buf, PAGE);
    return true;
}
#endif

static const Record *best() {
    if (!available()) return nullptr;
    const Record *a = page(PAGE_A), *b = page(PAGE_B);
    bool va = twoPages() && valid(a), vb = valid(b);
    if (va && vb) return (int16_t)(a->seq - b->seq) > 0 ? a : b;
    return va ? a : (vb ? b : nullptr);
}

bool load(Options &o, Progress &g, bool &hasGame) {
    hasGame = false;
    const Record *r = best();
    if (!r) return false;
    lastSeq = r->seq;
    o = r->opt;
    g = r->prog;
    hasGame = r->hasGame != 0;
    return true;
}

bool loadGame(game::Record &g) {
    const Record *r = best();
    if (!r || !r->hasGame) return false;
    g = r->game;
    return true;
}

bool store(const Options &o, const Progress &g, const game::Record *game) {
    if (!available()) return false;
    uint8_t *buf = gfx_chunkScratch();          // idle between gfx_wait() and the next flush
    memset(buf, 0xFF, PAGE);
    Record &rec = *(Record *)buf;
    rec.magic = MAGIC;
    rec.version = VERSION;
    rec.seq = (uint16_t)(lastSeq + 1);
    rec.opt = o;
    rec.prog = g;
    rec.hasGame = game ? 1 : 0;
    if (game) rec.game = *game;
    rec.crc = crc32(buf, CRC_OVER);
    uint32_t addr = ((rec.seq & 1) || !twoPages()) ? PAGE_B : PAGE_A;   // alternate pages
    if (!writePage(addr, buf)) { broken = true; return false; }
    lastSeq = rec.seq;
    return true;
}
#endif

}  // namespace save
