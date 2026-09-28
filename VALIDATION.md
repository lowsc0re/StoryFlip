# Validation Report: StoryFlip 0.4.2

**Report date:** 2026-09-28

## Build

* Momentum commit: `d3f89dfe2ef6b01839201598e9be1590cba80322`
* SDK version: `mntm-dev-d3f89dfe`
* Firmware API: **87.1**
* Hardware target: **7**
* ARM compiler: `arm-none-eabi-gcc (GCC) 12.3.1 20230626`
* SDK compiler flags were kept unchanged, including `-Wall -Wextra -Werror`
* Compilation, linking, FAP metadata generation, FastFAP preparation and APPCHK: successful
* No non-exported firmware functions are imported
* No private firmware structure was copied for SLIX emulation
* SDK SHA256: `238db260ee7e8f6e9a79dd1c760e07a832351a7d92d896b4cb19082f8548b2c1`
* Application manifest author: `lowsc0re`

## Development test collection

A private SLIX NFC test collection was used for validation. The collection itself is not included in the repository.

The validation set covered nested directories, root-level files, long filenames and a representative range of SLIX NFC files.

Duplicate-filename behavior was additionally tested using synthetic data.

The test collection is not a requirement or limitation of StoryFlip.

## Host tests using the actual C implementation

`sf_store.c` and `sf_browser.c` are compiled directly with GCC for host-side tests.

For selected core functions in `storyflip.c`, the unchanged function bodies are extracted automatically for controlled host testing. For host-side tests, file-system and NFC hardware interactions are simulated with controlled test implementations.

These tests are **not** an execution of the ARM FAP on a physical Flipper Zero.

AddressSanitizer and UndefinedBehaviorSanitizer reported no errors in the successful test runs. LeakSanitizer was unavailable in the test environment and was disabled.

### UI and settings

Passed:

* all nine settings positions
* language switching and About-screen return behavior
* emulation disables the normal UI tick
* navigation keys do not trigger UI updates while emulation is active
* Back stops emulation and restores the UI tick
* main menu and emulation screen remain pixel-identical between simulated ticks
* four visible list entries render on a 128x64 canvas
* language strings containing accents render in the host preview
* all six main-menu PNG assets are embedded unchanged
* folder and NFC list icons are embedded and invert correctly inside the selection bar

### Configuration and browser behavior

Passed:

* root folder and all three languages survive restart
* older configuration without a language value starts in English
* root selection is persisted
* a search-index rebuild is offered after changing root
* Back exits root selection at the SD-card root
* cached back-navigation avoids unnecessary directory reads and restores selection
* favorite and rating display refreshes after metadata generation changes
* large directories remain fully accessible when they exceed the browser cache
* repeated navigation respects the 24 KiB browser name-data budget
* normal browsing reads only the currently opened directory
* browser and search-index construction do not open NFC payload contents

### Persistence and search

Passed:

* 2,128 simulated interrupted-write positions retain the previous valid metadata generation
* CRC failure in the newest generation falls back to the previous valid slot
* the same filename at a new path remains one logical metadata identity
* favorites, ratings and history can be reset independently
* 2,000 metadata records operate without a 512-entry limit
* overlong strings are rejected instead of silently truncated
* recursive indexing covered the complete private validation set
* stored index paths resolve to existing files
* search filtering, Top 50, recent list, favorite filter and rating filter
* equal timestamps remain correctly ordered through the internal start sequence
* cancelling an index rebuild leaves the previous index usable
* simulated load failure, unsupported protocol, NFC initialization failure and low memory do not count a start
* simulated successful listener start increments the counter once and releases test objects on stop

### Source-file integrity

All NFC files in the private validation set remained byte-identical after the host tests.

The tests can be run on Linux with GCC from the project directory:

```bash
python3 tests/run_tests.py --collection /path/to/private-collection.zip
```

Without `--collection`, the standalone persistence tests can still run.

## UI rendering

The actual StoryFlip drawing functions were rendered on the host using U8g2 code and the embedded fonts from the pinned Momentum commit.

The following views were visually checked in all three languages:

* main menu
* file list
* root-folder selection
* settings
* language selection
* confirmation dialog
* emulation screen
* splash screen

The host adapters model canvas drawing operations, not physical device input.

## Firmware compatibility

StoryFlip has only been used and tested with Momentum Firmware:

* Momentum mainline `mntm-012` from 2026-01-01
* Momentum dev `d3f89dfe` from 2026-08-18

The 0.4.2 FAP is built against `d3f89dfe`.

## On-device validation

The complete StoryFlip feature set was tested on physical Flipper Zero hardware using Momentum Firmware.

The on-device validation included:

* application startup
* main-menu navigation
* collection browsing
* folder navigation
* root-folder selection
* favorites
* ratings
* recent history
* statistics
* Top 50
* search and search-index rebuilds
* metadata cleanup
* settings
* language switching
* context-menu actions
* NFC file information
* file deletion with confirmation
* SLIX emulation
* returning from emulation with Back
* persistence of settings and metadata across restarts

The host-side tests additionally cover controlled failure scenarios and persistence edge cases that are impractical to reproduce manually on the device.

## Release checks

Passed:

* consistent StoryFlip terminology across all three UI languages
* default root `/ext/nfc/StoryFlip`
* existing saved configuration remains compatible
* splash images and collection tile are embedded
* menu tiles and shadows use the corrected vertical position
* selection corners use three pixels each, forming equal-length L-shaped arms
* About displays version 0.4.2

The SDK binary version field stores only major and minor values, so that field remains `0.4` even though the application UI and package version are 0.4.2.

## Distributed FAP

```text
SHA256: f0af5192e2bbdaa9d43ff83bf455ce22dcb20956ecdbe72b61b981c9d6f0540d
```
