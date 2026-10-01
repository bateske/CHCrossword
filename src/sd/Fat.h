// CHSd 1.0.0 (generated: edit CHSd/src/Fat.h, then run CHSd/tools/vendor.py)
// Fat: read-only FAT16/FAT32 on top of sd::read (CHSd; clean-room, MIT,
// from HypeRunner: see NOTICE).
//
// Written from the Microsoft FAT specification (BPB fields, FAT type from
// the cluster count, 8.3 directory entries, end-of-chain marks) and the MBR
// partition table layout. It only turns a file name into LBA runs: after
// that a file is read as raw card blocks through its run list, so there is
// no FAT write code, no cache and no file object. Every call borrows the
// caller's 512 B buffer (4-byte aligned) and clobbers it; the volume state
// is 24 B. What a game does not call is left out of its image by the linker.
//
// Names are 8.3 short names as the directory stores them: 11 characters,
// capitals, the name and the extension padded with spaces ("WORDS   DIC"),
// and '?' stands for any character. Long-name entries, volume labels and
// hidden entries are skipped; a file never matches a folder name, nor a
// folder a file name.
#pragma once
#include <stdint.h>

namespace fat {

enum Err : int8_t {
    OK = 0,
    E_READ = -1,             // the card did not deliver a block
    E_NOFS = -10,            // no FAT16/FAT32 volume: no partition, bad BPB, FAT12
    E_EXFAT = -11,           // exFAT (or NTFS) volume: reformat the card as FAT32
    E_NOTFOUND = -12,        // no such file or directory (or not mounted)
    E_FRAG = -13,            // more runs than the caller has room for
    E_CHAIN = -14,           // cluster chain broken, looped, or not the file's length
};

struct Run { uint32_t lba, blocks; };
struct File { uint32_t cluster, size; };      // first cluster, size in bytes

// Finds the volume: a superfloppy boot sector at LBA 0, else the first MBR
// partition of type 01/04/06/0B/0C/0E. An exFAT boot sector at LBA 0, or an
// MBR with a type 07 partition and no FAT one, is E_EXFAT.
int8_t mount(uint8_t *buf);

// A file in the root directory.
int8_t find(const char *name, File &f, uint8_t *buf);

// A folder in the root directory.
int8_t folder(const char *name, File &dir, uint8_t *buf);

// The (skip + 1)th file in a folder whose name fits pattern ("????????CWD").
// Its 11-character name is copied to nameOut (no NUL) unless that is null.
// E_NOTFOUND: there are no more.
int8_t match(const File &dir, const char *pattern, uint8_t skip, File &f, char *nameOut, uint8_t *buf);

// Walks f's cluster chain once and returns its extents: up to maxRuns runs
// of consecutive blocks, the last one trimmed to the file's size (a file of
// 0 bytes has 0 runs). Returns the run count or an error. The walk is bounded
// by the file's size and the chain must end right there, so a looped or
// truncated FAT gives E_CHAIN instead of a hang.
int8_t runs(const File &f, Run *out, uint8_t maxRuns, uint8_t *buf);

// The usual way in: sd::init(), mount(), find(name) and runs(), in one go.
// Returns the run count; 0 if any step failed or the file is empty. (A game
// that tells the player why there is no card calls the steps itself.)
uint8_t open(const char *name, Run *out, uint8_t maxRuns, uint8_t *buf);

// Block k of a file (0 = its first 512 bytes) through its runs, into dst.
// False: past the end, or the card did not deliver it.
bool read(const Run *run, uint32_t nRuns, uint32_t k, uint8_t *dst);

}  // namespace fat
