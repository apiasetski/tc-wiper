# TC Wiper

**Version 1.0.0 beta** · Secure wipe plugin (WFX) for [Total Commander](https://www.ghisler.com/), 32-bit and 64-bit.

Its panel shows *Move here to securely wipe* plus two readme files. Files and folders you
**move (F6)** into it are overwritten with random data, renamed several times to random
names, and deleted. Nothing is stored anywhere.

> **Beta:** tested by the author only. Try it on test files first. Erased files cannot be
> restored. Provided as is, without warranty.

*Developed using Claude Opus 5.5 Medium (Anthropic).*

## Install

1. Download `tcwiper_1.0.0_beta.zip` (or build it yourself with `build.cmd`; it lands in `dist\`).
2. In Total Commander, open the zip (Enter or Ctrl+PgDn on it) and confirm the plugin installation.
3. Open *Network Neighborhood* (`\\\`) in one panel and enter **Secure Wipe**.

The zip contains both builds: `TCWiper.wfx` (32-bit TC) and `TCWiper.wfx64` (64-bit TC).
Each zip has a matching `.sha256` checksum file.

## Use

- Select files/folders in the other panel and press **F6**, then Enter in Total Commander's
  own *Upload* dialog (that dialog belongs to TC and cannot be changed by a plugin).
- TC Wiper then shows how many files and folders will be erased and the first 5 selected
  names. Nothing is touched until you click **Yes**.
- A progress window with a **Cancel** button is shown while erasing. Cancel leaves the
  remaining files untouched.
- **F5 (Copy) never wipes**; it shows a hint instead.
- Enter on the placeholder entry shows a short help text with version and license.
- **Readme.txt** (English) and **Readme_ru.txt** (Russian) in the plugin panel explain in
  detail how files are erased and how secure that is on HDDs and SSDs. F3 views them, F5
  copies them out.

## Safety rules built in

- Confirmation before every wipe operation.
- Windows and Program Files folders are refused.
- Files with other hard links are refused (wiping would destroy the other copies too).
- Symbolic links: only the link is removed, the target file is left alone.
- Files in use by another program are reported and left alone.

## What it does per file

1. Overwrites the contents and every alternate data stream with random data (one pass,
   as recommended by NIST SP 800-88), including the unused tail of the last cluster.
2. Flushes to disk, truncates to zero length.
3. Renames the file 3 times to random names, then deletes it.

Emptied folders are renamed to random names and removed too.

## Limits

- **SSDs / flash drives**: the drive writes new data to different cells, so old data can
  survive inside the flash chips. Full-disk encryption (BitLocker, VeraCrypt) is the real
  protection there.
- **Compressed, encrypted (EFS) or sparse files**: old data may remain elsewhere on disk.
  The summary tells you when this happened.
- Copies outside the file are not touched: older versions saved by programs, shadow copies,
  cloud-sync versions, the page file, backups.

## Trying it safely

Run `make_test_files.ps1` to create `%USERPROFILE%\WipeTest` with throwaway files, and
practise on that folder first.

## Bugs and feedback

Please report problems in [GitHub Issues](https://github.com/apiasetski/tc-wiper/issues),
with your Windows version, Total Commander version and 32/64-bit.

## Developer notes

- Source: `src\tcwiper.cpp`, exports in `src\tcwiper.def`, API constants in `src\wfx_api.h`.
- Version, author and license strings: `src\version.h` (single place; `package.ps1` reads it).
- Readme texts: `docs\TCWiper-Readme.txt` and `docs\TCWiper-Readme.ru.txt`, embedded into
  the plugin when building. Catalog descriptions: `docs\listing-en.txt`, `docs\listing-ru.txt`.
- `build.cmd` builds both plugins and the release zip; `build.cmd test` also runs
  `tests\test_harness.cpp` against a test build (message boxes auto-answered) for 32 and 64 bit.
- The icon (`src\tcwiper.ico`, embedded via `src\tcwiper.rc`) is drawn by
  `tools\make_icon.ps1`; rerun it after changing the design.
- Requires Visual Studio 2022 Build Tools with the C++ workload.

## License

Copyright (C) 2026 Andrei Piasetski.
Licensed under the GNU General Public License v3.0 or later. See [LICENSE.txt](LICENSE.txt).
