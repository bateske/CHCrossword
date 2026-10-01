// Host tests of the card side: src/sd/Fat.cpp and src/pack/Pack.cpp reading
// packs out of real FAT images (made by run_tests.py with tools/puzzles/
// mkcard.py: FAT16 and FAT32, long names, decoy entries, fragmented files,
// files that are not packs), and what happens when the card stops answering.
//
//   test_card IMAGE EXPECT        EXPECT: lines "name id count title|title|..."
//   test_card --none              no card at all
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../src/game/Puzzle.h"
#include "../../src/pack/Pack.h"
#include "../../src/sd/SdSpi.h"

static long checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

// --- The card: an image in memory ------------------------------------------------
static std::vector<uint8_t> image;
static long reads, failAfter = -1;      // failAfter >= 0: that many more reads work

namespace sd {
bool init() { return !image.empty() && failAfter != 0; }
bool read(uint32_t lba, uint8_t *dst) {
    if (image.empty() || (uint64_t)lba * 512 + 512 > image.size()) return false;
    if (failAfter == 0) return false;
    if (failAfter > 0) failAfter--;
    reads++;
    memcpy(dst, image.data() + (size_t)lba * 512, 512);
    return true;
}
}  // namespace sd

struct XPack { std::string name; uint32_t id; int count; std::vector<std::string> titles; };

static std::vector<XPack> readExpect(const char *path) {
    std::vector<XPack> out;
    FILE *f = fopen(path, "r");
    if (!f) return out;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        XPack x;
        char name[32], titles[3800];
        unsigned id;
        int count;
        if (sscanf(line, "%31s %u %d %3799[^\n]", name, &id, &count, titles) != 4) continue;
        x.name = name; x.id = id; x.count = count;
        for (char *t = strtok(titles, "|"); t; t = strtok(nullptr, "|")) x.titles.push_back(t);
        out.push_back(x);
    }
    fclose(f);
    return out;
}

// Everything on the card, against what was put there. Returns sd reads used.
static void walk(const std::vector<XPack> &want) {
    pack::scan();
    CHECK(pack::card() == pack::CARD_OK);
    CHECK(pack::count() == want.size() + 1);
    std::vector<bool> seen(want.size());
    for (uint8_t p = 1; p < pack::count(); p++) {
        CHECK(pack::select(p));
        size_t k = 0;
        while (k < want.size() && want[k].id != pack::id()) k++;
        CHECK(k < want.size());
        if (k == want.size()) continue;
        CHECK(!seen[k]);
        seen[k] = true;
        const XPack &x = want[k];
        std::string name = pack::name();
        for (auto &ch : name) if (ch == ' ') ch = '_';      // (as the expectations file writes it)
        CHECK(x.name == name && x.count == pack::puzzles());
        for (int i = 0; i < x.count; i++) {
            uint8_t size, diff;
            char title[puz::TITLE_MAX + 1];
            CHECK(pack::peek((uint8_t)i, size, diff, title));
            CHECK(x.titles[i] == title);
            CHECK(pack::open((uint8_t)i));
            CHECK(x.titles[i] == puz::title && puz::n == size && puz::difficulty == diff);
            char clue[puz::CLUE_MAX];
            for (int w = 0; w < puz::nWords; w++) { puz::clue((uint8_t)w, clue); CHECK(clue[0]); }
        }
        CHECK(!pack::peek((uint8_t)x.count, *(new uint8_t), *(new uint8_t), new char[32]));
        CHECK(pack::find(x.id) && pack::current() == p);
    }
    for (size_t k = 0; k < want.size(); k++) CHECK(seen[k]);
    CHECK(!pack::find(0x12345678));
    CHECK(pack::current() == 0 && pack::puzzles() > 0);
    // The built-in pack is still there and still opens.
    CHECK(pack::select(0) && pack::open(0));
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--none")) {
        pack::scan();
        CHECK(pack::card() == pack::CARD_NONE && pack::count() == 1 && pack::select(0) && pack::open(0));
        CHECK(!pack::select(1));
        printf("%ld checks, %ld failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    image.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(image.data(), 1, image.size(), f) != image.size()) return 2;
    fclose(f);
    std::vector<XPack> want = readExpect(argv[2]);
    if (argc > 3) {
        // A card with nothing for the game on it: the state it should report.
        pack::scan();
        CHECK(pack::count() == 1);
        CHECK(pack::card() == (pack::Card)atoi(argv[3]));
        CHECK(pack::select(0) && pack::open(0));
        printf("%ld checks, %ld failed\n", checks, fails);
        return fails ? 1 : 0;
    }
    reads = 0;
    walk(want);
    long total = reads;
    // The card pulled after k reads, for every k: nothing hangs or runs
    // off, a failed call says so, and the built-in pack always works.
    for (long k = 0; k < total; k += (total > 400 ? 7 : 1)) {
        failAfter = k;
        pack::scan();
        for (uint8_t p = 1; p < pack::MAX_PACKS; p++) {
            if (!pack::select(p)) continue;
            for (uint8_t i = 0; i < pack::puzzles(); i++) {
                uint8_t size, diff;
                char title[puz::TITLE_MAX + 1];
                if (!pack::peek(i, size, diff, title)) break;
                if (!pack::open(i)) break;
            }
        }
        failAfter = -1;
        if (pack::card() != pack::CARD_OK) CHECK(pack::count() == 1);
        CHECK(pack::select(0) && pack::open(0) && pack::current() == 0);
    }
    failAfter = -1;
    walk(want);
    printf("%ld checks, %ld failed (%ld card reads for a full walk)\n", checks, fails, total);
    return fails ? 1 : 0;
}
