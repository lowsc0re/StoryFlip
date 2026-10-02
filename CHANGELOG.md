# Changelog

All notable changes to StoryFlip are documented here.

**StoryFlip 0.4.2 is the first public release.**  
Earlier versions listed below were private development milestones and are included for project history.

## [0.5.0] - 2026-10-02

### Added

- Eight-entry main menu with **Categories** and **Replay**.
- User-defined categories with create, rename and delete actions.
- Multiple category assignments per NFC file.
- Category statistics with assigned-file counts and category file lists.
- Folder favorites.
- Separate and combined favorites views.
- Persistent Replay for the last successfully started NFC file.

### Changed

- Final main-menu order:
  **Collection, Favorites, Categories, Search / Replay, Recent, Statistics, Settings**.
- Recent history now shows up to 50 distinct NFC files.
- Emulation supports multi-line filenames.
- Ratings are displayed with star icons.
- Folder favorites open directly in the saved folder.
- Index diagnostics now shows the saved index build timestamp.
- Updated artwork and UI text for StoryFlip 0.5.0.

## [0.4.2] - 2026-09-28

### First public release

- Updated collection terminology and interface text across English, German and French.
- Added new StoryFlip splash screens for all three languages and a new collection icon.
- Changed the default collection root for new installations to `/ext/nfc/StoryFlip/`.
- Existing saved root paths remain unchanged after updating.
- Improved the main-menu layout by moving all six tiles, icons and shadows.
- Reworked the four selection markers into equal three-pixel corner shapes.
- Updated source, tests and documentation for the public release.

## [0.4.1]

### Fixed

- Improved vertical alignment of list and menu text.
- Applied the same alignment adjustment to favorite and rating indicators while preserving icon and selection-bar positions.

## [0.4.0]

### Added

- Dedicated folder and NFC-file icons in the collection browser.
- Separate About screen with the app name, version and a short AI-assisted development note.
- Custom PNG artwork for all six main-menu icons, including inverted rendering while selected.

### Changed

- Increased visible list and menu entries from three to four using 12-pixel row spacing.
- Switched normal menu entries to regular text while keeping headings and the folder-selection action bold.
- Replaced the scrolling main-menu label with a static centered label using the available width.
- Reordered settings with Language first and About last.
- Revised settings labels across all three supported languages.
- Simplified the About screen.

### Fixed

- Made the emulation screen static and disabled periodic UI updates while emulation is active.
- Ignored navigation keys during emulation. Back stops emulation and restores normal UI updates.
- Removed unnecessary periodic redraws from the main menu.
- Preserved the correct settings selection when returning from language selection, root selection or About.

## [0.3.0]

### Added

- English, German and French interfaces with a persistent language setting.
- Language-specific splash screens and fonts supporting umlauts and accented characters.
- In-app collection-root picker with an explicit folder-selection action.
- Option to rebuild the search index after changing the collection root.
- Metadata path updates after a successful index rebuild when exact filenames match.

### Changed

- Added a bounded cache for up to three folders to improve back-navigation performance and restore the previous selection.
- Removed temporary list writes during normal folder browsing.
- Added streaming fallback for folders too large for the cache.
- Cached favorite and rating indicators and refreshed them when metadata changes.
- Increased menu row size and added shadows to the main-menu tiles.

### Fixed

- Preserved favorites, ratings, play counts and history when indexed files move without being renamed.
- Corrected navigation at the SD-card root in the folder picker.
- Preserved the previous valid saved state if a write fails during synchronization.

## [0.2.0]

### Added

- First compiled FAP built against the matching Momentum SDK with API 87.1.
- Direct SLIX emulation inside StoryFlip with Back returning to the file list.
- Reproducible build script.
- Host-side tests for storage, search, statistics and emulation control.
- Explicit handling of missing files, invalid NFC data, unsupported protocols and unavailable NFC resources.

### Changed

- Replaced prototype TSV metadata storage with paired binary snapshots and CRC validation.
- Reworked browsing and indexing to support larger collections without a fixed 512- or 1,024-entry limit.
- Made recursive search indexing incremental and cancellable while preserving the previous index on cancellation.
- Counted a start only after loading, validation and listener startup complete.
- Added a sequence number to keep recent entries and statistics ordered correctly when starts share a timestamp.
- Established the collection-first, three-column main-menu layout.

### Notes

- Emulation operates on a temporary copy and does not write changes back to the original NFC file.
- Metadata from the initial TSV prototype is not imported automatically.

## [0.1.0]

### Added

- Initial source prototype for a native Flipper Zero collection manager.
- Six-tile main menu with collection browsing, recent entries, favorites, statistics, search and settings.
- Folder-by-folder browsing without automatic recursive scanning.
- Filename-based metadata with favorites, ratings, play counts and the five most recent unique entries.
- Top 50 view and rating filters.
- Explicitly generated recursive search index.
- Context actions opened by holding Right.
- Experimental TSV metadata storage.
- Handoff of a selected NFC file to the firmware NFC app.

### Notes

- This version was an uncompiled implementation baseline.
- Direct in-app SLIX emulation followed in 0.2.0.
