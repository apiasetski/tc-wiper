===============================================================================
 TC WIPER - HOW FILES ARE ERASED, AND HOW SECURE IT REALLY IS
===============================================================================

 TC Wiper 1.0.0 beta - secure wipe plugin for Total Commander
 Copyright (C) 2026 Andrei Piasetski
 License: GNU General Public License v3.0 or later (see LICENSE.txt)
 Source code and bug reports: https://github.com/apiasetski/tc-wiper

 Developed using Claude Opus 5.5 Medium (Anthropic).

 BETA: this version has been tested by its author only. Try it on test
 files first. Erased files cannot be restored.

This text explains exactly what TC Wiper does to your files, why it works
very well on classic hard disks, why no file-level tool can give the same
guarantee on SSDs and flash memory, and which copies of your data can exist
outside the file itself. Read it once, so you know what "erased" means in
your situation.


-------------------------------------------------------------------------------
 1. THE SHORT VERSION
-------------------------------------------------------------------------------

 * On a normal internal HARD DISK (HDD), a file erased by TC Wiper cannot be
   recovered by undelete software or by data-recovery labs.

 * On an SSD, USB stick, memory card or NVMe drive, TC Wiper defeats
   undelete software and normal recovery tools. It CANNOT guarantee that the
   flash chips inside the drive hold no old copy. A lab that removes and
   reads the chips directly may find fragments for some time.

 * In BOTH cases, copies of the data that live OUTSIDE the file are not
   touched: older versions saved by programs, temporary files, shadow copies
   and restore points, cloud sync and backups, the page file, and so on.
   See section 6.

 * The strongest protection is full-disk encryption (BitLocker, VeraCrypt)
   switched on BEFORE the sensitive data was ever written. Then every
   leftover fragment anywhere on the disk is encrypted and useless.


-------------------------------------------------------------------------------
 2. WHAT TC WIPER DOES TO EACH FILE
-------------------------------------------------------------------------------

 When you press F6 (Move) into the plugin panel, nothing is erased at first.
 TC Wiper only collects the list, shows how many files and folders will be
 erased, and waits for you to confirm. Then, for every file:

  1. Safety checks. Files under the Windows and Program Files folders are
     refused. Files that have other hard links are refused (overwriting would
     destroy the data seen under the other names too). For a symbolic link
     only the link is removed; the file it points to is left alone. Files in
     use by another program are reported and left alone.

  2. Read-only, hidden and system attributes are cleared.

  3. The file is opened for exclusive access, so no other program can read
     or write it during the process.

  4. OVERWRITE. The whole file content is overwritten once with random data
     from the Windows cryptographic random number generator (BCryptGenRandom).
     Random data is used rather than zeros so that a drive with built-in
     compression cannot shrink the write, and so the result reveals nothing.

  5. SLACK SPACE. Disks store files in clusters (usually 4 KB). The unused
     tail of the file's last cluster can still hold bytes from an older,
     longer version of the file. TC Wiper overwrites that tail as well.

  6. FLUSH. Windows is told to push the written data out of its cache and
     the drive's cache onto the storage medium (FlushFileBuffers).

  7. ALTERNATE DATA STREAMS. NTFS files can carry hidden extra content
     ("streams", e.g. file.txt:secret). Every stream is overwritten the same
     way.

  8. TRUNCATE. The file is cut to zero length.

  9. NAME SCRAMBLING. The file is renamed 3 times to random names of the
     same length, so the original name is overwritten in the file system's
     own records, then it is deleted.

 10. FOLDERS. Folders that you moved and that are now empty are renamed to
     random names and removed the same way.

 If you press Cancel, the file being processed is finished (it is already
 damaged), and the remaining files are left completely untouched.

 What TC Wiper does NOT do: multiple overwrite passes (not needed, see
 section 3), reading back to verify, wiping the free space of the whole disk,
 or deleting copies stored elsewhere (section 6).


-------------------------------------------------------------------------------
 3. WHY ONE PASS IS ENOUGH (AND THE "35 PASSES" MYTH)
-------------------------------------------------------------------------------

 Many wipe tools offer 3, 7 or 35 overwrite passes. These come from:

  * The Gutmann method (1996), designed for old disk encodings (MFM/RLL)
    that are no longer used. Its author himself says the full 35 passes make
    no sense for modern drives.
  * US DoD 5220.22-M (3 passes), an old standard that no longer prescribes
    overwriting at all.

 The current reference, NIST Special Publication 800-88 "Guidelines for
 Media Sanitization", states that for modern magnetic hard disks a single
 overwrite pass is enough to prevent recovery even with state-of-the-art
 laboratory techniques. There is no published, reproducible recovery of
 data that was overwritten once on a modern hard disk.

 Extra passes therefore add time and wear, not security. On SSDs extra passes
 do not help at all, for the reasons in section 5.


-------------------------------------------------------------------------------
 4. CLASSIC HARD DISKS (HDD)
-------------------------------------------------------------------------------

 How it works: a hard disk stores each block at a fixed physical place.
 When Windows (NTFS) overwrites an existing, normal file, the new data is
 written exactly on top of the old data. That is why overwriting is
 effective on HDDs.

 Remaining risks on an HDD:

  * REALLOCATED SECTORS. When the drive detects a weak sector, its firmware
    silently moves the data to a spare sector. The old, weak sector is no
    longer reachable by Windows or by any wipe tool and may still contain a
    copy. This affects only a few sectors and only on aging drives, but it
    cannot be fixed at file level. (Drive-wide "Secure Erase" can handle it.)

  * SMR DRIVES. Some modern high-capacity HDDs use "shingled" recording
    (SMR). Drive-managed SMR disks first write changes to a cache area and
    move them later, a little like an SSD. On such drives an old copy can
    remain in the cache area for a while. Manufacturers do not always state
    whether a drive is SMR.

  * COMPRESSED, SPARSE OR EFS-ENCRYPTED FILES. For files compressed by NTFS
    (blue file names in Explorer), sparse files, and files encrypted with
    Windows EFS (green names), Windows may write the new data to different
    clusters, leaving the old clusters unwiped in free space. TC Wiper still
    erases these files, but the summary warns you when this happened.

  * SMALL FILES INSIDE THE MASTER FILE TABLE. Files smaller than roughly
    700 bytes are stored directly inside the NTFS Master File Table (MFT).
    TC Wiper overwrites them there in place. However, the NTFS transaction
    log ($LogFile) may briefly keep a copy of recent changes to such records.
    It is overwritten by normal disk activity within a short time.

  * FILE NAMES IN JOURNALS. Name scrambling removes the original name from
    the file's own record. But NTFS also keeps a change journal ($UsnJrnl)
    that records file names and operations (create, rename, delete). The
    original name, and the fact that the file was renamed and deleted, can
    stay in that journal until it is overwritten by newer entries (often
    days or weeks). The CONTENT of the file is not stored in that journal.
    The same applies to folder names: traces can remain in folder index
    structures until they are reused.

 Verdict for HDD: for normal files, the content is gone for good. Traces of
 the NAME may remain for some time in system journals.


-------------------------------------------------------------------------------
 5. SSD, NVMe, USB STICKS, MEMORY CARDS (FLASH MEMORY)
-------------------------------------------------------------------------------

 Flash memory works fundamentally differently, and this matters a lot.

  * NO OVERWRITE IN PLACE. Flash cells cannot be overwritten directly; they
    must be erased in large blocks first. So the drive's controller (the
    "flash translation layer") writes every change to a FRESH, empty location
    and simply updates its internal map. The old data stays physically in
    the chips, marked as "stale", until the controller's garbage collection
    erases that block at some later time.

    Consequence: when TC Wiper overwrites a file on an SSD, Windows sees the
    random data, but the original data may still exist in the flash chips.

  * WEAR LEVELING spreads writes across all cells to make the drive last
    longer. Data moves around internally; old copies can exist in several
    places.

  * OVER-PROVISIONING. SSDs contain hidden spare capacity (often 7-28 %)
    that Windows cannot address at all. No software running on Windows can
    overwrite it, including "wipe free space" functions.

  * TRIM. When a file is deleted, Windows tells the SSD which blocks are no
    longer used (the TRIM command). Most modern SSDs then return only zeros
    when those blocks are read, and erase them physically during the next
    garbage collection. This is why undelete and recovery software almost
    never recovers deleted files from an SSD with TRIM enabled. But WHEN the
    physical erase happens is up to the drive and is not guaranteed.

    Check that TRIM is on (in a Command Prompt):
        fsutil behavior query DisableDeleteNotify
    "DisableDeleteNotify = 0" means TRIM is enabled.

  * USB STICKS, SD CARDS AND MANY USB ENCLOSURES do NOT support TRIM. On them
    stale data can remain readable inside the flash for a long time.

  * RESEARCH. A study by the University of California, San Diego (Wei et al.,
    "Reliably Erasing Data From Flash-Based Solid State Drives", USENIX FAST
    2011) read flash chips directly and found that overwriting individual
    files left a significant part of their contents recoverable. Whole-drive
    sanitizing commands worked far better (when correctly implemented).

 Verdict for SSD / flash: TC Wiper reliably stops ordinary recovery
 software. It cannot guarantee protection against a laboratory that reads the
 flash chips directly. If that level of threat matters to you, use full-disk
 encryption (from the start), or erase or destroy the entire drive (section 7).


-------------------------------------------------------------------------------
 6. COPIES OF YOUR DATA THAT TC WIPER CANNOT REACH
-------------------------------------------------------------------------------

 Erasing a file only erases THAT file. Very often other copies exist.
 Think about each of these:

  * OLDER VERSIONS SAVED BY PROGRAMS. Word, Excel, many editors and image
    programs save by writing a NEW file and deleting the old one. Every
    earlier version you ever saved may therefore still exist in the free
    space of the disk. Autosave/AutoRecover files and backup copies
    (*.bak, ~$*.docx, *.tmp) may exist too.

  * TEMPORARY FILES in %TEMP%, browser caches and download folders.

  * THE RECYCLE BIN. If the file was previously deleted normally and then
    restored, or if other copies were deleted normally, they may still be in
    the Recycle Bin or in free space.

  * SHADOW COPIES / SYSTEM RESTORE / "PREVIOUS VERSIONS". Windows may keep
    snapshots of the disk that contain old versions of your files.
    Show them (administrator Command Prompt):  vssadmin list shadows
    They are managed in: System Properties > System Protection.

  * FILE HISTORY AND BACKUPS (Windows Backup, external backup drives, NAS
    backups, backup software). Erase the file there too, or accept that the
    backup keeps it.

  * CLOUD SYNC FOLDERS (OneDrive, Dropbox, Google Drive, iCloud). Deleting
    the local file moves the cloud copy into the service's online recycle
    bin, and the service keeps its own version history. Empty the online
    recycle bin and version history as well.

  * NETWORK DRIVES AND NAS. The server decides how files are stored. Many NAS
    systems use snapshots or copy-on-write file systems (Btrfs, ZFS), where
    an overwrite never touches the old data.

  * OTHER FILE SYSTEMS. On FAT32/exFAT (typical for USB sticks and cards)
    the overwrite is in place, but see section 5 about flash. On ReFS, data
    may be written copy-on-write, so the old data can remain.

  * VIRTUAL MACHINES. A virtual disk (VHDX, VMDK) may be dynamically
    expanding or have snapshots/checkpoints that keep old blocks.

  * THE PAGE FILE AND HIBERNATION FILE (pagefile.sys, hiberfil.sys). Parts
    of a document that was open in memory can be written there by Windows.
    Crash dumps (MEMORY.DMP) can also contain memory contents.

  * WINDOWS SEARCH INDEX, THUMBNAIL CACHE AND "RECENT FILES" LISTS may keep
    words from the document, small preview images, or the file name.

  * EMAIL ATTACHMENTS, messengers, and anywhere else the file was sent.


-------------------------------------------------------------------------------
 7. HOW TO GET STRONGER PROTECTION
-------------------------------------------------------------------------------

  * Full-disk encryption: turn on BitLocker (Windows Pro) or Device
    Encryption, or use VeraCrypt. Check status: manage-bde -status
    Data written AFTER encryption was enabled is protected even in
    leftovers. Data written before may remain in unencrypted form in free
    space (on SSDs, in stale flash blocks).

  * HDD: wipe the free space of a drive after erasing files, to remove old
    versions saved by programs:
        cipher /w:C:\
    (This takes a long time. It overwrites all FREE space, not your files.
    Do NOT use it routinely on SSDs: it does not reach the hidden areas and
    only wears the drive.)

  * Remove old restore points if they contain the data (System Properties >
    System Protection > Configure > Delete). This removes ALL restore points.

  * Turn off hibernation if you do not need it:  powercfg /h off
    (administrator Command Prompt; this deletes hiberfil.sys).

  * Empty cloud recycle bins and version histories.

  * Before selling, returning or discarding a whole drive: use the drive
    manufacturer's "Secure Erase" / "Sanitize" tool (for SSDs this resets
    all flash cells, including hidden areas), or, for an encrypted drive,
    destroy the encryption key. For the highest security, physically destroy
    the drive.


-------------------------------------------------------------------------------
 8. QUICK GUIDE: HOW SECURE WAS MY ERASE?
-------------------------------------------------------------------------------

 Normal file on internal HDD (NTFS)
     Content unrecoverable. Old versions saved by programs, shadow copies and
     backups are separate - see section 6.

 Compressed / encrypted (EFS) / sparse file on HDD
     Current content unrecoverable, but older clusters may remain in free
     space. The summary warns you. Wipe free space with cipher /w.

 Internal SSD / NVMe with TRIM enabled
     Unrecoverable for software and recovery tools. A chip-level lab attack
     may find fragments for an uncertain time.

 USB stick, memory card, SSD in USB enclosure (usually no TRIM)
     Software recovery of the erased file is defeated, but old copies may
     stay inside the flash for a long time.

 Any drive with BitLocker/VeraCrypt enabled before the data was written
     Excellent. Leftovers anywhere are encrypted.

 Network drive / NAS / cloud-synced folder
     The local or visible copy is erased; server snapshots, recycle bins and
     version histories are not. Check them.


-------------------------------------------------------------------------------
 9. WHAT THE MESSAGES MEAN
-------------------------------------------------------------------------------

 "Securely erased: N files and M folders"  - done as described in section 2.

 "Not erased: ... protected"     - the file is under Windows or Program Files.
 "Not erased: ... in use"        - another program has it open; close it and
                                   try again.
 "Not erased: ... hard links"    - the same data has another name elsewhere;
                                   delete or erase the other names too.
 "... compressed, encrypted or sparse" - see section 4.
 "Cancelled: N files were left untouched" - those files were not changed.
 "... NOT securely wiped"        - Total Commander removed the files before
                                   TC Wiper could erase them. Please report
                                   this; it should not happen.

 F5 (Copy) never erases anything; only F6 (Move) does.


-------------------------------------------------------------------------------
 10. TECHNICAL DETAILS
-------------------------------------------------------------------------------

  * Random data: BCryptGenRandom with the system-preferred RNG, 1 MB blocks.
  * File access: CreateFileW with exclusive access (no sharing),
    WriteFile, FlushFileBuffers, SetEndOfFile.
  * Size including slack: GetFileInformationByHandleEx(FileStandardInfo),
    AllocationSize vs. EndOfFile.
  * Alternate data streams: FindFirstStreamW / FindNextStreamW.
  * Renaming: MoveFileExW (3 times, random names), then DeleteFileW /
    RemoveDirectoryW.
  * Long paths over 260 characters are supported (\\?\ prefix).
  * Protected folders: the Windows folder and all Program Files folders.
    This also protects the 32-bit plugin from Windows' folder redirection.


-------------------------------------------------------------------------------
 DISCLAIMER
-------------------------------------------------------------------------------

 Erased files cannot be restored. TC Wiper is provided as is, without any
 warranty. It reduces the risk of data recovery as described above, but no
 file-level tool can guarantee complete removal of all traces on every type
 of storage. For highly sensitive data, combine it with full-disk encryption
 and the measures in section 7.

===============================================================================
