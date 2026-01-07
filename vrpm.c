#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <time.h>
#include <zip.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/vfs.h>

#define VERSION "1.0.8"
#define BUILD_DATE __DATE__ " " __TIME__

#define MIN_RAM_GB 16
/* Set to 64KB to completely satisfy compiler worst-case truncation math */
#define CMD_MAX 65536
/* Path buffer plus filename buffer plus separator safety */
#define PATH_BUFFER_MAX (PATH_MAX + 512)
#define BAR_WIDTH 40

/* ANSI Color Codes */
#define RED    "\033[1;31m"
#define GREEN  "\033[1;32m"
#define YELLOW "\033[1;33m"
#define ORANGE "\033[38;5;208m"
#define RESET  "\033[0m"

/* Global Paths */
char PROFILE_SRC[PATH_MAX], PROFILE_RAM[] = "/dev/shm/vivaldi-profile";
char BACKUP_DIR[PATH_MAX], SYSTEMD_DIR[PATH_MAX], INSTALL_PATH[PATH_MAX];
char SERVICE_FILE[PATH_MAX + 128];

/* --------------------------------------------------
 * UI & Progress Helpers
 * -------------------------------------------------- */

void print_progress(const char* label, double percentage) {
    int progress = (int)(percentage * BAR_WIDTH);
    printf("\r%s: [", label);
    for (int i = 0; i < BAR_WIDTH; ++i) {
        if (i < progress) printf("=");
        else if (i == progress) printf(">");
        else printf(" ");
    }
    printf("] %.1f%%", percentage * 100);
    fflush(stdout);
}

/* --------------------------------------------------
 * Helper Functions
 * -------------------------------------------------- */

void init_paths() {
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, RED "Error: $HOME not set.\n" RESET); exit(1); }
    snprintf(PROFILE_SRC, PATH_MAX, "%s/.config/vivaldi", home);
    snprintf(BACKUP_DIR, PATH_MAX, "%s/Backups/vivaldi-profile-ram", home);
    snprintf(SYSTEMD_DIR, PATH_MAX, "%s/.config/systemd/user", home);
    snprintf(INSTALL_PATH, PATH_MAX, "%s/.local/bin/vivaldi-ram-profile", home);
    snprintf(SERVICE_FILE, sizeof(SERVICE_FILE), "%s/vivaldi-ram-profile.service", SYSTEMD_DIR);
}

int is_rsync_installed() {
    return (system("command -v rsync >/dev/null 2>&1") == 0);
}

int is_vivaldi_running() {
    return (system("pgrep -x vivaldi-bin >/dev/null 2>&1") == 0);
}

int is_mounted() {
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof(cmd), "mountpoint -q \"%s\"", PROFILE_SRC);
    return (system(cmd) == 0);
}

int confirm(const char *msg) {
    printf("%s [y/N]: ", msg);
    char buf[10];
    if (fgets(buf, sizeof(buf), stdin) && (buf[0] == 'y' || buf[0] == 'Y')) return 1;
    return 0;
}

unsigned long get_dir_size(const char *path) {
    char cmd[CMD_MAX];
    snprintf(cmd, sizeof(cmd), "du -sb \"%s\" 2>/dev/null", path);
    FILE *fp = popen(cmd, "r");
    unsigned long size = 0;
    if (fp) { if (fscanf(fp, "%lu", &size) != 1) size = 0; pclose(fp); }
    return size;
}

void handle_check_ram() {
    unsigned long profile_size = get_dir_size(PROFILE_SRC);
    struct statfs s;
    if (statfs("/dev/shm", &s) != 0) {
        printf(RED "Error: Could not check RAM disk status.\n" RESET);
        return;
    }
    unsigned long free_ram = s.f_bsize * s.f_bavail;

    printf("Profile size   : " ORANGE "%.2f MB" RESET "\n", (double)profile_size / (1024 * 1024));
    printf("Available RAM  : %.2f MB\n", (double)free_ram / (1024 * 1024));

    if (profile_size > free_ram) {
        printf(RED "Insufficient RAM to load profile!\n" RESET);
    } else {
        printf(GREEN "\nProfile fits in RAM.\n" RESET);
    }
}

void show_status() {
    printf("=== RAM status ===\n  RAM active : %s\n\n", is_mounted() ? "yes" : "no");
    printf("=== Vivaldi status ===\n  Running    : %s\n\n", is_vivaldi_running() ? "yes" : "no");
    
    DIR *d = opendir(BACKUP_DIR);
    int count = 0;
    char latest[PATH_MAX] = "none";
    time_t ltime = 0;
    off_t lsize = 0;

    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d))) {
            if (strstr(dir->d_name, ".zip")) {
                count++;
                char p[PATH_BUFFER_MAX];
                snprintf(p, sizeof(p), "%s/%s", BACKUP_DIR, dir->d_name);
                struct stat st;
                if (stat(p, &st) == 0 && st.st_mtime > ltime) {
                    ltime = st.st_mtime;
                    lsize = st.st_size;
                    strncpy(latest, dir->d_name, PATH_MAX);
                }
            }
        }
        closedir(d);
    }

    printf("=== Backup status ===\n");
    printf("  Path       : %s\n", BACKUP_DIR);
    printf("  Count      : %d\n", count);
    if (count > 0) {
        printf("  Latest     : %s " ORANGE "(%.2f MB)" RESET "\n", latest, (double)lsize / (1024 * 1024));
    } else {
        printf("  Latest     : %s\n", latest);
    }
}

void show_usage(const char *prog_path) {
    char *p_copy = strdup(prog_path);
    char *prog_name = basename(p_copy);
    printf("Vivaldi RAM Profile Manager v%s\n", VERSION);
    printf("Copyright (C) 2025 Ino Jacob. All rights reserved.\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("OPTIONS\n");
    printf("  -i, --install         Install and enable RAM profile service\n");
    printf("  -d, --disable         Disable the service (keep files)\n");
    printf("  -r, --remove          Disable service and remove all files\n");
    printf("  -l, --load            Load Vivaldi profile into RAM\n");
    printf("  -s, --save            Save RAM profile back to disk\n");
    printf("  -S, --status          Show RAM and backup status\n");
    printf("  -c, --check-ram       Check profile size vs available RAM\n");
    printf("  -b, --backup          Create ZIP backup (RAM must be active)\n");
    printf("  -R, --restore         Restore the latest backup\n");
    printf("  -e, --restore-select  Restore a selected backup (interactive)\n");
    printf("  -n, --clean-backup    Delete all backups except the latest\n");
    printf("  -p, --purge-backup    Delete ALL backup files\n");
    printf("  -h, --sudo-help       Show password-less sudo mount instructions\n\n");
    printf("NOTE: This software is provided \"AS IS\", without warranty of any kind. Use it at your own risk.\n");
    printf("      The author is not responsible for any damages resulting from its use.\n");

    free(p_copy);
}

void show_sudo_help() {
    printf("Version: %s\n", VERSION);
    printf("Build Date: %s\n", BUILD_DATE);
    printf("\n============================================\n");
    printf(" Password-less mount/umount configuration\n");
    printf("============================================\n\n");
    printf("1) Open sudoers:  sudo visudo\n");
    printf("2) Add this line to the end (replace %s with your user):\n\n", getenv("USER"));
    printf("   %s ALL=(root) NOPASSWD: \\\n", getenv("USER") ? getenv("USER") : "USERNAME");
    printf("     /usr/bin/mount --bind /dev/shm/vivaldi-profile %s, \\\n", PROFILE_SRC);
    printf("     /usr/bin/umount %s\n\n", PROFILE_SRC);
    printf("3) Save and exit. The script will now run silently.\n\n");
    printf("--=[ NOTICE ]=------------------------------------------------------------------------------------\n");
    printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,\nINCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR\nPURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE\nLIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR\nOTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS\nIN THE SOFTWARE.\n");
    printf("--------------------------------------------------------------------------------------------------\n");

}

/* --------------------------------------------------
 * Core Handlers
 * -------------------------------------------------- */

void handle_save() {
    if (!is_mounted()) { printf(YELLOW "Profile is not mounted in RAM.\n" RESET); return; }
    if (is_vivaldi_running()) { if (!confirm("Vivaldi is running. Save anyway?")) return; }

    char cmd[CMD_MAX];
    printf("Unmounting profile...\n");
    snprintf(cmd, sizeof(cmd), "sudo umount \"%s\"", PROFILE_SRC);
    if (system(cmd) != 0) { printf(RED "Error: Could not unmount.\n" RESET); return; }

    printf("Syncing RAM to Disk...\n");
    snprintf(cmd, sizeof(cmd), "rsync -a --delete --info=progress2 \"%s/\" \"%s/\"", PROFILE_RAM, PROFILE_SRC);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            int pct;
            if (sscanf(line, "%*s %d%%", &pct) == 1) {
                print_progress("Syncing", (double)pct / 100.0);
            }
        }
        pclose(fp);
    }
    
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", PROFILE_RAM);
    system(cmd);
    printf(GREEN "\nProfile saved successfully.\n" RESET);
}

void perform_restore(const char *zip_path) {
    int err = 0;
    struct zip *za = zip_open(zip_path, 0, &err);
    if (!za) { printf(RED "Error: Failed to open ZIP: %s\n" RESET, zip_path); return; }

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    zip_uint64_t total_size = 0;
    for (zip_int64_t i = 0; i < num_entries; i++) {
        struct zip_stat st;
        zip_stat_index(za, i, 0, &st);
        total_size += st.size;
    }

    zip_uint64_t processed = 0;
    for (zip_int64_t i = 0; i < num_entries; i++) {
        struct zip_stat st;
        zip_stat_index(za, i, 0, &st);
        char out_path[PATH_BUFFER_MAX];
        snprintf(out_path, sizeof(out_path), "%s/%s", PROFILE_SRC, st.name);

        if (st.name[strlen(st.name) - 1] == '/') {
            mkdir(out_path, 0755);
        } else {
            struct zip_file *zf = zip_fopen_index(za, i, 0);
            FILE *out = fopen(out_path, "wb");
            if (zf && out) {
                char buffer[8192]; zip_int64_t n;
                while ((n = zip_fread(zf, buffer, sizeof(buffer))) > 0) {
                    fwrite(buffer, 1, n, out);
                    processed += n;
                    print_progress("Restoring", (double)processed / (total_size ? total_size : 1));
                }
                fclose(out); zip_fclose(zf);
            }
        }
    }
    zip_close(za);
    printf(GREEN "\nRestore complete.\n" RESET);
}

void handle_restore(int interactive) {
    if (!is_mounted()) { printf(RED "Error: RAM profile not active.\n" RESET); return; }
    DIR *d = opendir(BACKUP_DIR);
    if (!d) { printf(RED "Error: Backup directory not found.\n" RESET); return; }

    struct dirent *dir;
    char files[128][PATH_BUFFER_MAX]; time_t times[128]; int count = 0;
    while ((dir = readdir(d)) != NULL && count < 128) {
        if (strstr(dir->d_name, ".zip")) {
            snprintf(files[count], sizeof(files[count]), "%s/%s", BACKUP_DIR, dir->d_name);
            struct stat st; stat(files[count], &st);
            times[count] = st.st_mtime; count++;
        }
    }
    closedir(d);
    if (count == 0) { printf(RED "Error: No backups found.\n" RESET); return; }

    int pick = 0;
    if (interactive) {
        printf("\nAvailable Backups:\n");
        for (int i = 0; i < count; i++) {
            struct stat st; stat(files[i], &st);
            printf("[%d] %s " ORANGE "(%.2f MB)" RESET "\n", i + 1, basename(files[i]), (double)st.st_size / (1024 * 1024));
        }
        printf("Select (1-%d) or 'x' to cancel: ", count);
        char input[10];
        if (!fgets(input, sizeof(input), stdin)) return;
        if (input[0] == 'x' || input[0] == 'X') {
            printf("\nRestore cancelled.\n");
            return;
        }
        pick = atoi(input);
        if (pick < 1 || pick > count) {
            printf(RED "Invalid selection.\n" RESET);
            return;
        }
        pick--;
    } else {
        for (int i = 1; i < count; i++) if (times[i] > times[pick]) pick = i;
    }
    perform_restore(files[pick]);
}

void handle_clean_backups() {
    DIR *d = opendir(BACKUP_DIR);
    if (!d) return;
    struct dirent *dir;
    char latest[PATH_MAX] = ""; time_t ltime = 0;
    while ((dir = readdir(d))) {
        if (strstr(dir->d_name, ".zip")) {
            char p[PATH_BUFFER_MAX]; snprintf(p, sizeof(p), "%s/%s", BACKUP_DIR, dir->d_name);
            struct stat st; stat(p, &st);
            if (st.st_mtime > ltime) { ltime = st.st_mtime; strncpy(latest, dir->d_name, PATH_MAX); }
        }
    }
    rewinddir(d);
    while ((dir = readdir(d))) {
        if (strstr(dir->d_name, ".zip") && strcmp(dir->d_name, latest) != 0) {
            char p[PATH_BUFFER_MAX]; snprintf(p, sizeof(p), "%s/%s", BACKUP_DIR, dir->d_name);
            remove(p);
        }
    }
    closedir(d);
    printf(GREEN "\nOld backups cleaned. Kept: %s\n" RESET, latest);
}

void handle_purge_backups() {
    if (!confirm("Are you sure you want to delete ALL backup files?")) return;
    DIR *d = opendir(BACKUP_DIR);
    if (!d) { printf(YELLOW "Backup directory does not exist.\n" RESET); return; }
    struct dirent *dir;
    int deleted_count = 0;
    while ((dir = readdir(d))) {
        if (strstr(dir->d_name, ".zip")) {
            char p[PATH_BUFFER_MAX];
            snprintf(p, sizeof(p), "%s/%s", BACKUP_DIR, dir->d_name);
            if (remove(p) == 0) deleted_count++;
        }
    }
    closedir(d);
    printf(GREEN "\nPurged %d backup files.\n" RESET, deleted_count);
}

/* --------------------------------------------------
 * Main
 * -------------------------------------------------- */

int main(int argc, char *argv[]) {
    init_paths();
    if (argc < 2) { show_usage(argv[0]); return 0; }
    char *action = argv[1];

    if (strcmp(action, "--install") == 0 || strcmp(action, "-i") == 0) {
        char cmd[CMD_MAX];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" && mkdir -p \"$(dirname %s)\"", SYSTEMD_DIR, INSTALL_PATH); system(cmd);
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" && chmod +x \"%s\"", argv[0], INSTALL_PATH, INSTALL_PATH); system(cmd);
        FILE *f = fopen(SERVICE_FILE, "w");
        if (f) {
            fprintf(f, "[Unit]\nDescription=Vivaldi RAM Profile\nAfter=graphical-session.target\n\n[Service]\nType=oneshot\nExecStart=%s --load\nExecStop=%s --save\nRemainAfterExit=yes\n\n[Install]\nWantedBy=default.target\n", INSTALL_PATH, INSTALL_PATH);
            fclose(f); system("systemctl --user daemon-reload && systemctl --user enable vivaldi-ram-profile.service");
            printf(GREEN "Service installed and enabled.\n" RESET);
        }
    } 
    else if (strcmp(action, "--load") == 0 || strcmp(action, "-l") == 0) {
        if (!is_rsync_installed()) {
            printf(RED "Error: 'rsync' is not installed. Please install it to continue.\n" RESET);
            return 1;
        }
        if (is_mounted()) { printf(YELLOW "Already in RAM.\n" RESET); return 0; }
        
        char cmd[CMD_MAX];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", PROFILE_RAM); system(cmd);
        
        printf("Copying profile to RAM...\n");
        snprintf(cmd, sizeof(cmd), "rsync -a --delete --info=progress2 \"%s/\" \"%s/\"", PROFILE_SRC, PROFILE_RAM);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                int pct;
                if (sscanf(line, "%*s %d%%", &pct) == 1) {
                    print_progress("Loading", (double)pct / 100.0);
                }
            }
            pclose(fp);
            printf("\n");
        }

        snprintf(cmd, sizeof(cmd), "sudo mount --bind \"%s\" \"%s\"", PROFILE_RAM, PROFILE_SRC); 
        if (system(cmd) == 0) {
            printf(GREEN "\nLoaded successfully.\n" RESET);
        } else {
            printf(RED "Error: Failed to mount profile.\n" RESET);
        }
    }
    else if (strcmp(action, "--save") == 0 || strcmp(action, "-s") == 0) handle_save();
    else if (strcmp(action, "--backup") == 0 || strcmp(action, "-b") == 0) {
        if (!is_mounted()) { printf(RED "Error: RAM profile not active.\n" RESET); return 1; }
        char cmd[CMD_MAX], ts[64], b_path[PATH_BUFFER_MAX];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", BACKUP_DIR); system(cmd);
        time_t now = time(NULL); strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", localtime(&now));
        snprintf(b_path, sizeof(b_path), "%s/vivaldi-profile-%s.zip", BACKUP_DIR, ts);
        unsigned long total_size = get_dir_size(PROFILE_SRC);
        printf("Backing up to: %s\n", b_path);
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && tar -cf - . | pv -s %lu | zip -q -9 \"%s\" -", PROFILE_SRC, total_size, b_path);
        system(cmd); printf(GREEN "\nBackup done.\n" RESET);
    }
    else if (strcmp(action, "--restore") == 0 || strcmp(action, "-R") == 0) handle_restore(0);
    else if (strcmp(action, "--restore-select") == 0 || strcmp(action, "-e") == 0) handle_restore(1);
    else if (strcmp(action, "--clean-backup") == 0 || strcmp(action, "-n") == 0) handle_clean_backups();
    else if (strcmp(action, "--purge-backup") == 0 || strcmp(action, "-p") == 0) handle_purge_backups();
    else if (strcmp(action, "--sudo-help") == 0 || strcmp(action, "-h") == 0) show_sudo_help();
    else if (strcmp(action, "--status") == 0 || strcmp(action, "-S") == 0) show_status();
    else if (strcmp(action, "--check-ram") == 0 || strcmp(action, "-c") == 0) handle_check_ram();
    else { show_usage(argv[0]); }

    return 0;
}
