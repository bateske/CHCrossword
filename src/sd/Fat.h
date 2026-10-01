// Fat: read-only FAT16/FAT32 on top of sd::read (clean-room, MIT; from
// HypeRunner, see NOTICE).
//
// Written from the Microsoft FAT specification (BPB fields, FAT type from
// the cluster count, 8.3 directory entries, end-of-chain marks) and the MBR
// partition table layout. It only turns a file name into LBA runs: after
// that a file is read as raw card blocks through its run list, so there is
// no FAT write code, no cache and no file object. Every call borrows the
// caller's 512 B buffer (4-byte aligned) and clobbers it; the volume state
// is 24 B. Here it also finds a folder in the root directory and the files
// in it that match a pattern (the packs: CHCW/*.CWD).
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

// A folder in the root directory by its 8.3 name as the directory stores
// it: 11 characters, capitals, the name and the extension padded with
// spaces ("CHCW       "). Long-name entries are skipped.
int8_t folder(const char *name, File &dir, uint8_t *buf);

// The (skip + 1)th file in a folder whose short name fits a pattern of 11
// characters, '?' standing for any ("????????CWD"). Its 11-character name
// is copied to nameOut (no NUL). Folders, hidden files and volume labels
// never match. E_NOTFOUND: there are no more.
int8_t match(const File &dir, const char *pattern, uint8_t skip, File &f, char *nameOut, uint8_t *buf);

// Walks f's cluster chain once and returns its extents: up to maxRuns runs
// of consecutive blocks, the last one trimmed to the file's size (a file of
// 0 bytes has 0 runs). Returns the run count or an error. The walk is bounded
// by the file's size and the chain must end right there, so a looped or
// truncated FAT gives E_CHAIN instead of a hang.
int8_t runs(const File &f, Run *out, uint8_t maxRuns, uint8_t *buf);

}  // namespace fat
