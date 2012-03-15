extern int signature_check_enabled;
extern int script_assert_enabled;

void
toggle_signature_check();

void
toggle_script_asserts();

void
show_choose_zip_menu();

int
do_nandroid_backup(const char* backup_name);

int
do_nandroid_restore();

void
show_nandroid_restore_menu(const char* path);

void
show_nandroid_advanced_restore_menu(const char* path);

void
show_nandroid_menu();

void
show_mount_menu();

void
show_format_menu();

void
show_choose_zip_menu();

int
install_zip(const char* packagefilepath);

int
__system(const char *command);

void
show_advanced_menu();

void show_reboot_menu();

void show_wipe_menu();

void show_setting_menu();

int format_unknown_device(const char *device, const char* path, const char *fs_type);

void
wipe_battery_stats();

void create_fstab();

int has_datadata();

void handle_failure(int ret);

void process_volumes();

int extendedcommand_file_exists();

void show_install_update_menu();

int confirm_selection(const char* title, const char* confirm);

int run_and_remove_extendedcommand();

//vibrate
int vib;

//onscreen button
int osb;

//timezone
int glo_timezone;

//advanced backup
int tmp1,tmp2,tmp3,tmp4,tmp5;

//THEME
int MTC[4],NTC[4], HTC[4], LC[4], SC[4], SR[4];
