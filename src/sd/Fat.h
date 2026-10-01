// Fat: read-only FAT16/FAT32 on top of sd::read (clean-room, MIT; from
// HypeRunner, see NOTICE).
//
// Written from the Microsoft FAT specification (BPB fields, FAT type from
// the cluster count, 8.3 directory entries, end-of-chain marks) and the MBR
// partition table layout. It only turns a file name into LBA runs, once, while
// booting: after that files are read and written as raw card blocks through
// their run lists, so there is no FAT write code, no cache and no file
// object. Every call borrows the caller's 512 B buffer (4-byte aligned) and
// clobbers it; the volume state is 20 B.
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

// A file in the root directory by its 8.3 name as the directory stores it:
// 11 characters, capitals, the name and the extension padded with spaces
// ("WORDS   DIC"). Long-name entries are skipped.
int8_t find(const char *name, File &f, uint8_t *buf);

// Walks f's cluster chain once and returns its extents: up to maxRuns runs
// of consecutive blocks, the last one trimmed to the file's size (a file of
// 0 bytes has 0 runs). Returns the run count or an error. The walk is bounded
// by the file's size and the chain must end right there, so a looped or
// truncated FAT gives E_CHAIN instead of a hang.
int8_t runs(const File &f, Run *out, uint8_t maxRuns, uint8_t *buf);

}  // namespace fat
