# CHCrossword

A crossword for the [CHGame](https://github.com/bateske/CH32SerialBoot) handheld
(CH32X035 RISC-V, 128x128 colour LCD, piezo), in the casino of
[CHBlackjack](https://github.com/bateske/CHBlackjack) and the tables after it:
full 13x13 puzzles with the whole grid on screen, ivory tiles on green felt, a
letter board that slides up to type on with a glove that pokes the keys, and a
score attack on top of the solving. A right word locks in gold, tile by tile,
each a note up the scale; words in a row build a combo up to x5; one letter
that finishes two words is a CROSS!; a wrong word buzzes and shakes the table;
and the last word sets the whole grid off under SOLVED!, with a time against
par and one to three stars to keep.

![Down the grid: words locking, the combo building, a cross](docs/combo.gif)

| Title | The puzzle list | The first words |
|---|---|---|
| ![title](docs/title.gif) | ![list](docs/list.gif) | ![words](docs/words.gif) |
| **A wrong word** | **The last word** | |
| ![wrong](docs/wrong.gif) | ![solved](docs/solved.gif) | |

(Captured from the PC simulator in `tools/chsim`, which runs the real game
and graphics code and renders what the device shows. The game has been built
and checked in the simulator; it has not yet been run on the handheld itself,
and its SD card driver in particular has never met a real card.)

Twenty puzzles are built in - six easy, eight medium, six hard - and more
come on the **SD card**: packs of up to 32 puzzles, up to 15x15, in a folder
named `CHCW`. `packs/BONUS.CWD` is one to start with, and
`tools/puzzles/puz2cwd.py` makes packs from your own Across Lite `.puz`
files. The grids were filled by this project's own grid maker and every clue
written for the game; the framework is CHBlackjack's by way of CHChess and
CHBackgammon, and the small 3x5 lettering is Press Play On Tape's font, as
there. See `NOTICE`.

## Installing

You need the Arduino IDE (2.x) or `arduino-cli`, and:

1. **The CHGame board package, 0.2.4 or later** (Boards Manager URL
   `https://github.com/bateske/CH32SerialBoot/releases/latest/download/package_chgame_index.json`).
2. **The CHGfx library, 1.3.0** from <https://github.com/bateske/CHgfx>.
3. **This repository**, in a folder named `CHCrossword`.

The game needs **link-time optimisation**: pick *Tools > Optimize > Smallest
+ LTO*, and *Tools > USB > Upload only* (the game has no use for USB Serial,
and uploading works as before). That is 46.4 KB of the 50,944-byte
application region, with room left for the two flash pages that hold your
saved puzzle and records. From the command line:

    arduino-cli compile -b CHGame:ch32v:CHGame:opt=oslto,rtlib=nano,periph=game,usb=uploadonly CHCrossword
    arduino-cli upload  -b CHGame:ch32v:CHGame -p COMx CHCrossword

(`python tools/device.py build` does the same.)

For more puzzles, copy `packs/BONUS.CWD` (and any packs of your own) into a
folder named `CHCW` at the top of a microSD card formatted FAT16 or FAT32 -
how cards up to 32 GB come. The game plays without a card.

## Playing

| | On the grid | On the letter board |
|---|---|---|
| **D-pad** | Move to the next open square | Move the glove |
| **A** | Bring up the letter board | Press the key |
| **B** | Turn the word: across / down | Rub out (hold: keep rubbing) |
| **SELECT** | Jump to the next clue not done | Turn the word |
| **START** | Pause | Put the board away |

The clue of the word you are on is under the grid, with its number (`14A`,
`7D`); the word is tinted and the square you are on pulses. Typing moves on
to the word's next empty square and puts the board away when the word is
full. On the puzzle list, left and right change pack when a card with packs
is in.

**The score.** A right word locks: 10 a letter, times the combo. Every third
word in a row without a mistake raises the combo, to x5. A word locked within
eight seconds of the last earns half as much again; a letter that completes
two words at once is a CROSS!, worth both and 50 more times the combo. A
complete wrong word costs 20 and the combo (the letters stay: fix them).
REVEAL LETTER, on the pause menu, costs 50 and the combo, and that word then
pays half. Solving pays 500, two points for every second under par (six
seconds a square), 250 more for revealing nothing and 1,000 instead for a
perfect solve with no wrong word either. Stars: one for solving, two under
par with nothing revealed, three under two-thirds of par with at most three
wrong words.

**Options.** CHECKING OFF plays it straight: no locks, no combo, the grid
judged only when its last square is right (CHECK WORD on the pause menu rubs
out a word's wrong letters, for 25). TYPING STEPS moves square by square
instead of skipping the filled ones. SAVE + QUIT keeps the puzzle, clock and
score for CONTINUE on the title; best times, scores and stars are kept for
the built-in puzzles, and which puzzles are solved for the last two card
packs played.

## How it fits

- **The grid.** 13 squares of 8 pixels is 104: the whole puzzle fits with a
  24-pixel column beside it for the clock and score and three lines below for
  the clue. (15x15 packs use 7-pixel squares.) A square is a 7x7 tile with a
  3x5 letter - M and W are drawn five wide there, since in three columns they
  are an H with its bar out of place - and there is no room for numbers in
  the squares, so the clue line carries them. While the letter board is up
  the grid scrolls to keep the word above it.
- **A puzzle is about 700 bytes.** The black squares are one bit each (half
  of them: the other half is the first turned half a turn), the answers five
  bits a square, and the title and clues Huffman-coded with one fixed table
  (4.5 bits a character), so twenty puzzles are 14 KB. The grid and word
  list are unpacked into RAM when a puzzle starts; clues stay packed and are
  decoded one at a time. The same bytes are a puzzle in flash and in a pack
  on the card (`tools/puzzles/cwformat.py` describes the format and holds
  the reference decoder the tests check the game's against).
- **The card.** A polled, read-only block driver and FAT16/FAT32 reader
  (HypeRunner's, as CHWords cut them down) find `CHCW/*.CWD`, and a
  card puzzle is copied into 2 KB of RAM when it starts, so the card is not
  touched again while you play - pull it out mid-puzzle and nothing happens.
  The card shares its SPI bus with the screen: it is only read between
  frames, and only on the puzzle list.
- **Drawing only what changed.** The play screen is redrawn when something
  on it moves, about once a second otherwise (the clock). The pulsing cursor
  and the shimmer on gold are the palette animating, which costs nothing.
- **Saving** uses the last two pages of the application flash in turn, with
  a CRC, as the other games do (and shares them: saving here replaces
  another CHGame game's save).

## Making puzzles

    python tools/puzzles/newgrid.py --seed 7          # a filled 13x13 grid and its word list
    python tools/puzzles/build_pack.py                # check tools/puzzles/src/*.txt, pack them into the game
    python tools/puzzles/build_pack.py --cwd MINE.CWD --name MINE a.txt b.txt   # a pack for the card
    python tools/puzzles/puz2cwd.py MINE.CWD *.puz    # ... or from Across Lite files
    python tools/puzzles/mkcard.py out/card.img MINE.CWD   # a card image for the simulator

A puzzle source is a text file: a title, a difficulty, the grid (`#` for
black) and a clue for each word (see any file in `tools/puzzles/src`). The
checker insists on what crosswords insist on - symmetry, every letter in two
words, no two-letter words, one piece - and on what the game needs: clues
that fit the clue box in the characters the font has. `newgrid.py` draws a
blank grid, fills it with common words (ENABLE, ranked by the `wordfreq`
package: `pip install wordfreq`), never two forms of one word, and prints it
ready for clues.

## Development

    python tools/check.py                 # puzzles, host tests, every script twice, device build
    python tools/chsim/chdrive.py --sim . tools/scripts/play.txt out/play
    python tools/chsim/chdrive.py --sim . --card out/card.img tools/scripts/card_packs.txt out/card
    python tools/device.py build [--debug] | upload | run SCRIPT OUTDIR

The simulator needs a C++ compiler (`CHSIM_CXX`, zig, clang++ or g++) and
Python with `pillow`. The host tests hold the game's decoder to the Python
reference on every puzzle, play whole puzzles through the rules, and read
packs out of FAT16 and FAT32 images - fragmented files, long names, decoy
entries, files that are not packs - with the card failing at every possible
read. The scripts in `tools/scripts` drive the real game through its debug
protocol (`solve` types each word on the letter board as a player would), in
the simulator or on the board.

## License

Apache License 2.0 (`LICENSE`); the SD card driver and FAT code in `src/sd`
and `tools/puzzles/fatimg.py` are MIT. Credits in `NOTICE`.
