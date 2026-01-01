#!/usr/bin/env bash
set -e

# ==================================================
# Vivaldi RAM Profile Manager
# ==================================================

PROFILE_SRC="$HOME/.config/vivaldi"
PROFILE_RAM="/dev/shm/vivaldi-profile"
BACKUP_DIR="$HOME/Backups/vivaldi-profile-ram"
MIN_RAM_GB=16

SYSTEMD_DIR="$HOME/.config/systemd/user"
INSTALL_PATH="$HOME/.local/bin/vivaldi-ram-profile.sh"
SERVICE_FILE="$SYSTEMD_DIR/vivaldi-ram-profile.service"

ACTION="$1"

# --------------------------------------------------
# Helper functions
# --------------------------------------------------

is_vivaldi_running() {
  pgrep -x vivaldi-bin >/dev/null 2>&1
}

confirm() {
  read -r -p "$1 [y/N]: " CONFIRM
  [[ "$CONFIRM" =~ ^[Yy]$ ]]
}

check_min_ram() {
  TOTAL_RAM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
  TOTAL_RAM_GB=$(( TOTAL_RAM_KB / 1024 / 1024 ))
  (( TOTAL_RAM_GB >= MIN_RAM_GB ))
}

check_profile_vs_ram() {
  PROFILE_SIZE_BYTES=$(du -sb "$PROFILE_SRC" | awk '{print $1}')
  PROFILE_SIZE_MB=$(( PROFILE_SIZE_BYTES / 1024 / 1024 ))

  AVAILABLE_RAM_KB=$(awk '/MemAvailable/ {print $2}' /proc/meminfo)
  AVAILABLE_RAM_MB=$(( AVAILABLE_RAM_KB / 1024 ))

  REQUIRED_MB=$(( PROFILE_SIZE_MB * 2 ))

  echo "Profile size     : ${PROFILE_SIZE_MB} MB"
  echo "Available RAM    : ${AVAILABLE_RAM_MB} MB"
  echo "Required RAM     : ${REQUIRED_MB} MB (2× rule)"

  (( AVAILABLE_RAM_MB >= REQUIRED_MB ))
}

show_sudo_nopasswd_instructions() {
  echo
  echo "=================================================="
  echo " OPTIONAL: Password-less mount/umount configuration"
  echo "=================================================="
  echo
  echo "This step is OPTIONAL."
  echo
  echo "If you want to avoid entering your sudo password"
  echo "when loading or saving the Vivaldi RAM profile:"
  echo
  echo "1) Open sudoers safely:"
  echo
  echo "   sudo visudo"
  echo
  echo "2) Add this line at the end (replace USERNAME):"
  echo
  echo "   USERNAME ALL=(root) NOPASSWD: \\"
  echo "     /bin/mount --bind /dev/shm/vivaldi-profile $HOME/.config/vivaldi, \\"
  echo "     /bin/umount $HOME/.config/vivaldi"
  echo
  echo "3) Save and exit."
  echo
  echo "✔ After this, --load and --save will not ask for a password."
  echo
}

show_usage() {
  echo "Vivaldi RAM Profile Manager"
  echo
  echo "Usage: $(basename "$0") [OPTIONS]"
  echo
  echo "OPTIONS"
  echo "  --install         Install and enable RAM profile service"
  echo "  --disable         Disable the service (keep files)"
  echo "  --remove          Disable service and remove all files"
  echo "  --load            Load Vivaldi profile into RAM"
  echo "  --save            Save RAM profile back to disk"
  echo "  --status          Show RAM and backup status"
  echo "  --check-ram       Check profile size vs available RAM"
  echo "  --backup          Create ZIP backup (RAM must be active)"
  echo "  --restore         Restore the latest backup"
  echo "  --restore-select  Restore a selected backup (interactive)"
  echo "  --clean-backup    Delete all backups except the latest"
  echo "  --purge-backup    Delete ALL backup files"
  echo "  --sudo-help       Show optional password-less sudo mount instructions"
  echo
  echo "Note: Install 'pv' to show progressbar when creating backup."
  echo
}

[[ -z "$ACTION" ]] && { show_usage; exit 0; }

# --------------------------------------------------
# INSTALL SERVICE
# --------------------------------------------------

if [[ "$ACTION" == "--install" ]]; then
  mkdir -p "$SYSTEMD_DIR" "$(dirname "$INSTALL_PATH")"

  cp "$0" "$INSTALL_PATH"
  chmod +x "$INSTALL_PATH"

  cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Vivaldi RAM Profile Manager
After=graphical-session.target

[Service]
Type=oneshot
ExecStart=$INSTALL_PATH --load
ExecStop=$INSTALL_PATH --save
RemainAfterExit=yes

[Install]
WantedBy=default.target
EOF

  systemctl --user daemon-reexec
  systemctl --user enable vivaldi-ram-profile.service

  echo "✅ Service installed and enabled."

  if confirm "Show OPTIONAL password-less sudo instructions?"; then
    show_sudo_nopasswd_instructions
  fi

  exit 0
fi

# --------------------------------------------------
# DISABLE SERVICE
# --------------------------------------------------

if [[ "$ACTION" == "--disable" ]]; then
  systemctl --user disable vivaldi-ram-profile.service 2>/dev/null || true
  echo "✅ Service disabled."
  exit 0
fi

# --------------------------------------------------
# REMOVE SERVICE
# --------------------------------------------------

if [[ "$ACTION" == "--remove" ]]; then
  systemctl --user disable vivaldi-ram-profile.service 2>/dev/null || true
  rm -f "$SERVICE_FILE"

  confirm "Delete installed script?" && rm -f "$INSTALL_PATH"
  confirm "Delete backup directory?" && rm -rf "$BACKUP_DIR"

  echo "✅ Service and files removed."
  exit 0
fi

# --------------------------------------------------
# LOAD
# --------------------------------------------------

if [[ "$ACTION" == "--load" ]]; then
  echo "=== Vivaldi Profile RAM Loader ==="

  # Check profile exists
  if [[ ! -d "$PROFILE_SRC" ]]; then
    echo "❌ Vivaldi profile not found at:"
    echo "  $PROFILE_SRC"
    exit 1
  fi

  # Create RAM directory
  echo "Preparing RAM directory..."
  mkdir -p "$PROFILE_RAM"

  # If already mounted, skip
  if mountpoint -q "$PROFILE_SRC"; then
    echo "ℹ️ Vivaldi profile is already mounted in RAM."
    exit 0
  fi

  # Warn if Vivaldi is running
  if is_vivaldi_running; then
    echo "⚠️ Vivaldi is currently running."
    confirm "Continue anyway?" || exit 1
  fi

  # Copy profile to RAM
  echo "Copying Vivaldi profile to RAM..."
  rsync -a --delete "$PROFILE_SRC/" "$PROFILE_RAM/"

  # Bind-mount RAM profile
  echo "Mounting RAM profile..."
  sudo mount --bind "$PROFILE_RAM" "$PROFILE_SRC"

  echo
  echo "✅ Vivaldi profile is now running from RAM."
  echo
  echo "IMPORTANT:"
  echo "• Do NOT delete $PROFILE_RAM while mounted"
  echo "• Run --save before shutdown to persist changes"
  exit 0
fi

# --------------------------------------------------
# SAVE
# --------------------------------------------------

if [[ "$ACTION" == "--save" ]]; then
  mountpoint -q "$PROFILE_SRC" || exit 0

  if is_vivaldi_running; then
    confirm "Vivaldi running. Continue?" || exit 1
  fi

  sudo umount "$PROFILE_SRC"
  rsync -a --delete "$PROFILE_RAM/" "$PROFILE_SRC/"
  rm -rf "$PROFILE_RAM"

  echo "✅ Profile saved."
  exit 0
fi

# --------------------------------------------------
# CHECK RAM
# --------------------------------------------------

if [[ "$ACTION" == "--check-ram" ]]; then
  check_profile_vs_ram && echo "✅ RAM OK" || echo "⚠️ RAM insufficient"
  exit 0
fi

# --------------------------------------------------
# BACKUP (with progress bar)
# --------------------------------------------------

if [[ "$ACTION" == "--backup" ]]; then
  echo "=== Creating Backup ==="

  mountpoint -q "$PROFILE_SRC" || { echo "❌ RAM profile not active."; exit 1; }

  mkdir -p "$BACKUP_DIR"
  TS=$(date +"%Y-%m-%d_%H-%M-%S")
  BACKUP_FILE="$BACKUP_DIR/vivaldi-profile-$TS.zip"

  echo "Calculating profile size..."
  TOTAL_SIZE=$(du -sb "$PROFILE_SRC" | awk '{print $1}')

  echo "Backing up profile to ZIP:"
  echo "  $BACKUP_FILE"
  echo

  (
    cd "$PROFILE_SRC" || exit 1
    tar -cf - . \
      | pv -s "$TOTAL_SIZE" \
      | zip -q -9 "$BACKUP_FILE" -
  )

  echo
  echo "✅ Backup completed successfully."
  exit 0
fi

# --------------------------------------------------
# RESTORE LATEST
# --------------------------------------------------

if [[ "$ACTION" == "--restore" ]]; then
  mountpoint -q "$PROFILE_SRC" || { echo "❌ RAM not active."; exit 1; }

  mapfile -t BACKUPS < <(ls -t "$BACKUP_DIR"/vivaldi-profile-*.zip 2>/dev/null)
  [[ ${#BACKUPS[@]} -gt 0 ]] || { echo "❌ No backups found."; exit 1; }

  confirm "Restore latest backup?" || exit 0

  TMP=$(mktemp -d)
  unzip -q "${BACKUPS[0]}" -d "$TMP"
  rsync -a --delete "$TMP/" "$PROFILE_SRC/"
  rm -rf "$TMP"

  echo "✅ Latest backup restored."
  exit 0
fi

# --------------------------------------------------
# RESTORE SELECT
# --------------------------------------------------

if [[ "$ACTION" == "--restore-select" ]]; then
  mountpoint -q "$PROFILE_SRC" || { echo "❌ RAM not active."; exit 1; }

  mapfile -t BACKUPS < <(ls -t "$BACKUP_DIR"/vivaldi-profile-*.zip 2>/dev/null)
  [[ ${#BACKUPS[@]} -gt 0 ]] || { echo "❌ No backups found."; exit 1; }

  select B in "${BACKUPS[@]}" "Cancel"; do
    [[ "$B" == "Cancel" ]] && exit 0
    [[ -n "$B" ]] || continue

    confirm "Restore selected backup?" || exit 0

    TMP=$(mktemp -d)
    unzip -q "$B" -d "$TMP"
    rsync -a --delete "$TMP/" "$PROFILE_SRC/"
    rm -rf "$TMP"

    echo "✅ Backup restored."
    exit 0
  done
fi

# --------------------------------------------------
# CLEAN / PURGE BACKUPS
# --------------------------------------------------

if [[ "$ACTION" == "--clean-backup" ]]; then
  mapfile -t BACKUPS < <(ls -t "$BACKUP_DIR"/vivaldi-profile-*.zip 2>/dev/null)
  (( ${#BACKUPS[@]} > 1 )) || exit 0
  confirm "Delete all backups except latest?" && rm -f "${BACKUPS[@]:1}"
  exit 0
fi

if [[ "$ACTION" == "--purge-backup" ]]; then
  confirm "Delete ALL backups?" && rm -rf "$BACKUP_DIR"
  echo "✅ All backups deleted."
  exit 0
fi

# --------------------------------------------------
# STATUS
# --------------------------------------------------

if [[ "$ACTION" == "--status" ]]; then
  echo "=== RAM status ==="
  mountpoint -q "$PROFILE_SRC" && echo "  RAM active : yes" || echo "  RAM active : no"

  echo
  echo "=== Backup status ==="
  echo "  Backup path : $BACKUP_DIR"

  if [[ -d "$BACKUP_DIR" ]]; then
    mapfile -t BACKUPS < <(ls -t "$BACKUP_DIR"/vivaldi-profile-*.zip 2>/dev/null)
    if (( ${#BACKUPS[@]} > 0 )); then
      AGE_DAYS=$(( ( $(date +%s) - $(stat -c %Y "${BACKUPS[0]}") ) / 86400 ))
      echo "  Backups     : ${#BACKUPS[@]}"
      echo "  Latest age  : $AGE_DAYS days"
      (( AGE_DAYS > 7 )) && echo "  ⚠️ Backup older than 7 days"
    else
      echo "  Backups     : none"
    fi
  fi
  exit 0
fi

# --------------------------------------------------
# SUDO HELP
# --------------------------------------------------

if [[ "$ACTION" == "--sudo-help" ]]; then
  show_sudo_nopasswd_instructions
  exit 0
fi

# --------------------------------------------------
# UNKNOWN OPTION
# --------------------------------------------------

echo "Unknown option: $ACTION"
echo
show_usage
exit 1