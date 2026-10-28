# TC Wiper

A Total Commander file system plugin. Its panel shows one entry, *Move here to securely wipe*.
Files and folders you **move (F6)** into it are overwritten with random data, renamed
several times to random names, and deleted. Nothing is stored anywhere.

## Install

1. Run `build.cmd` (or use the ready file in `dist\`).
2. In Total Commander, open `dist\TCWiper.zip` (double-click it or press Ctrl+PgDn on it)
   and confirm the plugin installation.
3. Open *Network Neighborhood* (`\\\`) in one panel and enter **Secure Wipe**.

The zip has both builds: `TCWiper.wfx` (32-bit TC) and `TCWiper.wfx64` (64-bit TC).

## Use

- Select files/folders in the other panel and press **F6**, then Enter in Total Commander's
  own *Upload* dialog (that dialog belongs to TC and cannot be changed by a plugin).
- TC Wiper then shows how many files and folders will be erased and the first 5 selected
  names. Nothing is touched until you click **Yes**.
- A progress window with a **Cancel** button is shown while erasing. Cancel leaves the
  remaining files untouched.
- **F5 (Copy) never wipes**; it shows a hint instead.
- Double-click the placeholder entry for a short help text.

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

## Limits (be honest with yourself)

- **SSDs / flash drives**: the drive may write new data to different cells, so old data can
  survive internally. Full-disk encryption (BitLocker) is the real protection there.
- **Compressed, encrypted (EFS) or sparse files**: old data may remain elsewhere on disk.
  The summary tells you when this happened.
- Copies outside the file are not touched: shadow copies / restore points, cloud-sync
  versions, the page file, backups.
- Folder names are deleted by Total Commander normally, without scrambling.

## Trying it safely

Run `make_test_files.ps1` to create `%USERPROFILE%\WipeTest` with throwaway files, and
practise on that folder first.

## Developer notes

- Source: `src\tcwiper.cpp`, exports in `src\tcwiper.def`, API constants in `src\wfx_api.h`.
- `build.cmd test` also builds and runs `tests\test_harness.cpp` against a test build
  (message boxes auto-answered) for both 32-bit and 64-bit.
- Requires Visual Studio 2022 Build Tools with the C++ workload.
