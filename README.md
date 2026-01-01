# Vivaldi RAM Profile Manager (vrpm)

vrpm is a high-performance utility written in C designed to relocate your Vivaldi browser profile to a RAM disk (`/dev/shm`). This enhances browser responsiveness, eliminates disk-bound latency, and reduces wear on physical SSDs.

## Features

* **Memory-Speed Browsing:** Near-instant tab switching and UI responsiveness.
* **Automated Persistence:** Systemd integration handles loading on boot and saving on shutdown.
* **Integrated Backups:** Compressed ZIP snapshots with orange-coded size reporting.
* **Health Checks:** Validation tools to ensure your profile fits within available RAM.
* **Clean UI:** Simple, emoji-free, color-coded terminal output.

## Installation

### Dependencies

Ensure the following are installed on your system:
* **libzip**: For backup and restore operations.
* **rsync**: For efficient profile synchronization.
* **pv**: For real-time progress bars during backups.

### Compilation

Compile the source using `gcc`:

```bash
gcc -o vrpm vrpm.c -lzip
```

### Service Setup

Install the binary and enable the systemd user service:

```bash
./vrpm --install
```
### Command Reference

| Option | Description |
| :--- | :--- |
| `-S, --status` | Display RAM activity, Vivaldi status, and backup history. |
| `-c, --check-ram` | Compare profile size against available RAM disk space. |
| `-l, --load` | Manually sync profile to RAM and mount. |
| `-s, --save` | Sync RAM changes back to disk and unmount. |
| `-b, --backup` | Create a high-compression ZIP backup. |
| `-R, --restore` | Restore the most recent backup. |
| `-e, --restore-select` | Interactively select a backup from a list. |
| `-n, --clean-backup` | Remove all backups except for the latest one. |
| `-p, --purge-backup` | Delete all backup files in the backup directory. |
| `-h, --sudo-help` | View version info and password-less sudo instructions. |

## Automation Logic

The application utilizes bind mounts to transparently redirect Vivaldi's configuration path to the RAM disk.

* **Load:** `vrpm` copies `~/.config/vivaldi` to `/dev/shm/vivaldi-profile`.
* **Mount:** It performs a `mount --bind` to overlay the RAM data onto the original path.
* **Save:** Upon exit, it unmounts and uses `rsync` to update the physical disk with changes made during the session.

## Sudo Configuration

To enable seamless background operation (especially for the systemd service), `vrpm` requires permission to mount/umount without a password prompt.

Run the following for specific instructions:

```bash
./vrpm --sudo-help
```

## License

This project is licensed under the **GPL-3** license.

This project is provided as-is. Users are responsible for ensuring they have adequate RAM to accommodate their profile size.
