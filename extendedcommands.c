#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <reboot/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "../../external/yaffs2/yaffs2/utils/mkyaffs2image.h"
#include "../../external/yaffs2/yaffs2/utils/unyaffs.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"

#define ABS_MT_POSITION_X 0x35  /* Center X ellipse position */

int vib = 1;
int osb = 1;
int tmp1=0,tmp2=0,tmp3=0,tmp4=0,tmp5=0;
int signature_check_enabled = 1;
int script_assert_enabled = 1;
static const char *SDCARD_UPDATE_FILE = "/sdcard/update.zip";

void
toggle_signature_check()
{
    signature_check_enabled = !signature_check_enabled;
    ui_print("Signature Check: %s\n", signature_check_enabled ? "Enabled" : "Disabled");
}

void toggle_script_asserts()
{
    script_assert_enabled = !script_assert_enabled;
    ui_print("Script Asserts: %s\n", script_assert_enabled ? "Enabled" : "Disabled");
}

int install_zip(const char* packagefilepath)
{
    ui_print("\n-- Installing:\n %s\n", packagefilepath);
    if (device_flash_type() == MTD) {
        set_sdcard_update_bootloader_message();
    }
    int status = install_package(packagefilepath);
    ui_reset_progress();
    if (status != INSTALL_SUCCESS) {
        ui_set_background(BACKGROUND_ICON_ERROR);
        ui_print("Installation aborted.\n");
        return 1;
    }
    ui_set_background(BACKGROUND_ICON_CLOCKWORK);
    ui_print("\nInstall from sdcard complete.\n");
    return 0;
}

char* INSTALL_MENU_ITEMS[] = {  "choose zip from sdcard",
                                "apply /sdcard/update.zip",
                                "toggle signature verification",
                                "toggle script asserts",
                                "choose zip from internal sdcard",
                                NULL };
#define ITEM_CHOOSE_ZIP       0
#define ITEM_APPLY_SDCARD     1
#define ITEM_SIG_CHECK        2
#define ITEM_ASSERTS          3
#define ITEM_CHOOSE_ZIP_INT   4

void show_install_update_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Apply update from .zip file on SD card",
                                "",
                                NULL
    };
    
    if (volume_for_path("/emmc") == NULL)
        INSTALL_MENU_ITEMS[ITEM_CHOOSE_ZIP_INT] = NULL;
    
    for (;;)
    {
        int chosen_item = get_menu_selection(headers, INSTALL_MENU_ITEMS, 0, 0);
        switch (chosen_item)
        {
            case ITEM_ASSERTS:
                toggle_script_asserts();
                break;
            case ITEM_SIG_CHECK:
                toggle_signature_check();
                break;
            case ITEM_APPLY_SDCARD:
            {
                if (confirm_selection("Confirm install?", "Yes - Install /sdcard/update.zip"))
                    install_zip(SDCARD_UPDATE_FILE);
                break;
            }
            case ITEM_CHOOSE_ZIP:
	    {
                show_choose_zip_menu("/sdcard/");
                break;
	    }
	    case ITEM_CHOOSE_ZIP_INT:
                show_choose_zip_menu("/emmc/");
                break;
            default:
                return;
        }

    }
}

void free_string_array(char** array)
{
    if (array == NULL)
        return;
    char* cursor = array[0];
    int i = 0;
    while (cursor != NULL)
    {
        free(cursor);
        cursor = array[++i];
    }
    free(array);
}

char** gather_files(const char* directory, const char* fileExtensionOrDirectory, int* numFiles)
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int i;
    char** files = NULL;
    int pass;
    *numFiles = 0;
    int dirLen = strlen(directory);

    dir = opendir(directory);
    if (dir == NULL) {
        ui_print("Couldn't open directory.\n");
        return NULL;
    }

    int extension_length = 0;
    if (fileExtensionOrDirectory != NULL)
        extension_length = strlen(fileExtensionOrDirectory);

    int isCounting = 1;
    i = 0;
    for (pass = 0; pass < 2; pass++) {
        while ((de=readdir(dir)) != NULL) {
            // skip hidden files
            if (de->d_name[0] == '.')
                continue;

            // NULL means that we are gathering directories, so skip this
            if (fileExtensionOrDirectory != NULL)
            {
                // make sure that we can have the desired extension (prevent seg fault)
                if (strlen(de->d_name) < extension_length)
                    continue;
                // compare the extension
                if (strcmp(de->d_name + strlen(de->d_name) - extension_length, fileExtensionOrDirectory) != 0)
                    continue;
            }
            else
            {
                struct stat info;
                char fullFileName[PATH_MAX];
                strcpy(fullFileName, directory);
                strcat(fullFileName, de->d_name);
                stat(fullFileName, &info);
                // make sure it is a directory
                if (!(S_ISDIR(info.st_mode)))
                    continue;
            }

            if (pass == 0)
            {
                total++;
                continue;
            }

            files[i] = (char*) malloc(dirLen + strlen(de->d_name) + 2);
            strcpy(files[i], directory);
            strcat(files[i], de->d_name);
            if (fileExtensionOrDirectory == NULL)
                strcat(files[i], "/");
            i++;
        }
        if (pass == 1)
            break;
        if (total == 0)
            break;
        rewinddir(dir);
        *numFiles = total;
        files = (char**) malloc((total+1)*sizeof(char*));
        files[total]=NULL;
    }

    if(closedir(dir) < 0) {
        LOGE("Failed to close directory.");
    }

    if (total==0) {
        return NULL;
    }

    // sort the result
    if (files != NULL) {
        for (i = 0; i < total; i++) {
            int curMax = -1;
            int j;
            for (j = 0; j < total - i; j++) {
                if (curMax == -1 || strcmp(files[curMax], files[j]) < 0)
                    curMax = j;
            }
            char* temp = files[curMax];
            files[curMax] = files[total - i - 1];
            files[total - i - 1] = temp;
        }
    }

    return files;
}

// pass in NULL for fileExtensionOrDirectory and you will get a directory chooser
char* choose_file_menu(const char* directory, const char* fileExtensionOrDirectory, const char* headers[])
{
    char path[PATH_MAX] = "";
    DIR *dir;
    struct dirent *de;
    int numFiles = 0;
    int numDirs = 0;
    int i;
    char* return_value = NULL;
    int dir_len = strlen(directory);

    char** files = gather_files(directory, fileExtensionOrDirectory, &numFiles);
    char** dirs = NULL;
    if (fileExtensionOrDirectory != NULL)
        dirs = gather_files(directory, NULL, &numDirs);
    int total = numDirs + numFiles;
    if (total == 0)
    {
        ui_print("No files found.\n");
    }
    else
    {
        char** list = (char**) malloc((total + 1) * sizeof(char*));
        list[total] = NULL;


        for (i = 0 ; i < numDirs; i++)
        {
            list[i] = strdup(dirs[i] + dir_len);
        }

        for (i = 0 ; i < numFiles; i++)
        {
            list[numDirs + i] = strdup(files[i] + dir_len);
        }

        for (;;)
        {
            int chosen_item = get_menu_selection(headers, list, 0, 0);
            if (chosen_item == GO_BACK)
                break;
            static char ret[PATH_MAX];
            if (chosen_item < numDirs)
            {
                char* subret = choose_file_menu(dirs[chosen_item], fileExtensionOrDirectory, headers);
                if (subret != NULL)
                {
                    strcpy(ret, subret);
                    return_value = ret;
                    break;
                }
                continue;
            }
            strcpy(ret, files[chosen_item - numDirs]);
            return_value = ret;
            break;
        }
        free_string_array(list);
    }

    free_string_array(files);
    free_string_array(dirs);
    return return_value;
}

void show_choose_zip_menu(const char *mount_point)
{
    if (ensure_path_mounted(mount_point) != 0) {
        LOGE ("Can't mount %s\n", mount_point);
        return;
    }

    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Choose a zip to apply",
                                "",
                                NULL
    };

    char* file = choose_file_menu(mount_point, ".zip", headers);
    if (file == NULL)
        return;
    static char* confirm_install  = "Confirm install?";
    static char confirm[PATH_MAX];
    sprintf(confirm, "Yes - Install %s", basename(file));
    if (confirm_selection(confirm_install, confirm))
        install_zip(file);
}

void show_nandroid_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return;
    }

    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Choose an image to restore",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, headers);
    if (file == NULL)
        return;

    if (confirm_selection("Confirm restore?", "Yes - Restore"))
        nandroid_restore(file, 1, 1, 1, 1, 1, 0);
}

#ifndef BOARD_UMS_LUNFILE
#define BOARD_UMS_LUNFILE	"/sys/devices/platform/usb_mass_storage/lun0/file"
#endif

void show_mount_usb_storage_menu()
{
    int fd;
    Volume *vol = volume_for_path("/sdcard");
    if ((fd = open(BOARD_UMS_LUNFILE, O_WRONLY)) < 0) {
        LOGE("Unable to open ums lunfile (%s)", strerror(errno));
        return -1;
    }

    if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
        (!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
        LOGE("Unable to write to ums lunfile (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"USB Mass Storage device",
                                "Leaving this menu unmount",
                                "your SD card from your PC.",
                                "",
                                NULL
    };

    static char* list[] = { "Unmount", NULL };

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK || chosen_item == 0)
            break;
    }

    if ((fd = open(BOARD_UMS_LUNFILE, O_WRONLY)) < 0) {
        LOGE("Unable to open ums lunfile (%s)", strerror(errno));
        return -1;
    }

    char ch = 0;
    if (write(fd, &ch, 1) < 0) {
        LOGE("Unable to write to ums lunfile (%s)", strerror(errno));
        close(fd);
        return -1;
    }
}

int confirm_selection(const char* title, const char* confirm)
{
    struct stat info;
    if (0 == stat("/sdcard/clockworkmod/.no_confirm", &info))
        return 1;

    char* confirm_headers[]  = {  title, "  THIS CAN NOT BE UNDONE.", "", NULL };
	if (0 == stat("/sdcard/clockworkmod/.one_confirm", &info)) {
		char* items[] = { "No",
						confirm, //" Yes -- wipe partition",   // [1]
						NULL };
		int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
		return chosen_item == 1;
	}
	else {
		char* items[] = { "No",
						confirm, //" Yes -- wipe partition",   // [1]
						NULL };
		int chosen_item = get_menu_selection(confirm_headers, items, 0, 0);
		return chosen_item == 1;
	}
	}

#define MKE2FS_BIN      "/sbin/mke2fs"
#define TUNE2FS_BIN     "/sbin/tune2fs"
#define E2FSCK_BIN      "/sbin/e2fsck"

int format_device(const char *device, const char *path, const char *fs_type) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        // no /sdcard? let's assume /data/media
        if (strstr(path, "/sdcard") == path && is_data_media()) {
            return format_unknown_device(NULL, path, NULL);
        }
        // silent failure for sd-ext
        if (strcmp(path, "/sd-ext") == 0)
            return -1;
        LOGE("unknown volume \"%s\"\n", path);
        return -1;
    }
    if (strcmp(fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", path);
        return -1;
    }

    if (strcmp(fs_type, "rfs") == 0) {
        if (ensure_path_unmounted(path) != 0) {
            LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
            return -1;
        }
        if (0 != format_rfs_device(device, path)) {
            LOGE("format_volume: format_rfs_device failed on %s\n", device);
            return -1;
        }
        return 0;
    }
 
    if (strcmp(v->mount_point, path) != 0) {
        return format_unknown_device(v->device, path, NULL);
    }

    if (ensure_path_unmounted(path) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(fs_type, "yaffs2") == 0 || strcmp(fs_type, "mtd") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n",device);
            return -1;
        }
        return 0;
    }

    if (strcmp(fs_type, "ext4") == 0) {
        reset_ext4fs_info();
        int result = make_ext4fs(device, NULL, NULL, 0, 0, 0);
        if (result != 0) {
            LOGE("format_volume: make_extf4fs failed on %s\n", device);
            return -1;
        }
        return 0;
    }

    return format_unknown_device(device, path, fs_type);
}

int format_unknown_device(const char *device, const char* path, const char *fs_type)
{
    LOGI("Formatting unknown device.\n");

    if (fs_type != NULL && get_flash_type(fs_type) != UNSUPPORTED)
        return erase_raw_partition(fs_type, device);

    // if this is SDEXT:, don't worry about it if it does not exist.
    if (0 == strcmp(path, "/sd-ext"))
    {
        struct stat st;
        Volume *vol = volume_for_path("/sd-ext");
        if (vol == NULL || 0 != stat(vol->device, &st))
        {
            ui_print("No app2sd partition found. Skipping format of /sd-ext.\n");
            return 0;
        }
    }

    if (NULL != fs_type) {
        if (strcmp("ext3", fs_type) == 0) {
            LOGI("Formatting ext3 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext3_device(device);
        }

        if (strcmp("ext2", fs_type) == 0) {
            LOGI("Formatting ext2 device.\n");
            if (0 != ensure_path_unmounted(path)) {
                LOGE("Error while unmounting %s.\n", path);
                return -12;
            }
            return format_ext2_device(device);
        }
    }

    if (0 != ensure_path_mounted(path))
    {
        ui_print("Error mounting %s!\n", path);
        ui_print("Skipping format...\n");
        return 0;
    }

    static char tmp[PATH_MAX];
    if (strcmp(path, "/data") == 0) {
        sprintf(tmp, "cd /data ; for f in $(ls -a | grep -v ^media$); do rm -rf $f; done");
        __system(tmp);
    }
    else {
        sprintf(tmp, "rm -rf %s/*", path);
        __system(tmp);
        sprintf(tmp, "rm -rf %s/.*", path);
        __system(tmp);
    }

    ensure_path_unmounted(path);
    return 0;
}

//#define MOUNTABLE_COUNT 5
//#define DEVICE_COUNT 4
//#define MMC_COUNT 2

typedef struct {
    char mount[255];
    char unmount[255];
    Volume* v;
} MountMenuEntry;

typedef struct {
    char txt[255];
    Volume* v;
} FormatMenuEntry;

int is_safe_to_format(char* name)
{
    char str[255];
    char* partition;
    property_get("ro.cwm.forbid_format", str, "/misc,/radio,/bootloader,/recovery,/efs");

    partition = strtok(str, ", ");
    while (partition != NULL) {
        if (strcmp(name, partition) == 0) {
            return 0;
        }
        partition = strtok(NULL, ", ");
    }

    return 1;
}

void show_mount_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Mount Partition Menu",
                                "",
                                NULL
    };

    static MountMenuEntry* mount_menue = NULL;

    typedef char* string;

    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    Volume* device_volumes;

    num_volumes = get_num_volumes();
    device_volumes = get_device_volumes();

    string options[255];

    if(!device_volumes)
		return;

		mountable_volumes = 0;

		mount_menue = malloc(num_volumes * sizeof(MountMenuEntry));

		for (i = 0; i < num_volumes; ++i) {
			Volume* v = &device_volumes[i];
			if(strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) != 0 && strcmp("emmc", v->fs_type) != 0 && strcmp("bml", v->fs_type) != 0)
			{
				sprintf(&mount_menue[mountable_volumes].mount, "mount %s", v->mount_point);
				sprintf(&mount_menue[mountable_volumes].unmount, "unmount %s", v->mount_point);
				mount_menue[mountable_volumes].v = &device_volumes[i];
				++mountable_volumes;
		    }
		}

    char confirm_string[255];

    for (;;)
    {

		for (i = 0; i < mountable_volumes; i++)
		{
			MountMenuEntry* e = &mount_menue[i];
			Volume* v = e->v;
			if(is_path_mounted(v->mount_point))
				options[i] = e->unmount;
			else
				options[i] = e->mount;
		}

        options[mountable_volumes+formatable_volumes] = "mount USB storage";
        options[mountable_volumes+formatable_volumes + 1] = NULL;

        int chosen_item = get_menu_selection(headers, &options, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        if (chosen_item == (mountable_volumes+formatable_volumes))
        {
            show_mount_usb_storage_menu();
        }
        else if (chosen_item < mountable_volumes)
        {
			MountMenuEntry* e = &mount_menue[chosen_item];
            Volume* v = e->v;

            if (is_path_mounted(v->mount_point))
            {
                if (0 != ensure_path_unmounted(v->mount_point))
                    ui_print("Error unmounting %s!\n", v->mount_point);
            }
            else
            {
                if (0 != ensure_path_mounted(v->mount_point))
                    ui_print("Error mounting %s!\n",  v->mount_point);
            }
        }
    }

    free(mount_menue);
}

void show_format_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Format Partition Menu",
                                "",
                                NULL
    };

    static FormatMenuEntry* format_menue = NULL;

    typedef char* string;

    int i, mountable_volumes, formatable_volumes;
    int num_volumes;
    Volume* device_volumes;

    num_volumes = get_num_volumes();
    device_volumes = get_device_volumes();

    string options[255];

    if(!device_volumes)
		return;

		formatable_volumes = 0;

		format_menue = malloc(num_volumes * sizeof(FormatMenuEntry));

		for (i = 0; i < num_volumes; ++i) {
			Volume* v = &device_volumes[i];
			if(strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) != 0 && strcmp("emmc", v->fs_type) != 0 && strcmp("bml", v->fs_type) != 0)
			{
				if (is_safe_to_format(v->mount_point)) {
					sprintf(&format_menue[formatable_volumes].txt, "format %s", v->mount_point);
					format_menue[formatable_volumes].v = &device_volumes[i];
					++formatable_volumes;
				}
		    }
		    else if (strcmp("ramdisk", v->fs_type) != 0 && strcmp("mtd", v->fs_type) == 0 && is_safe_to_format(v->mount_point))
		    {
				sprintf(&format_menue[formatable_volumes].txt, "format %s", v->mount_point);
				format_menue[formatable_volumes].v = &device_volumes[i];
				++formatable_volumes;
			}
		}


    static char* confirm_format  = "Confirm format?";
    static char* confirm = "Yes - Format";
    char confirm_string[255];

    for (;;)
    {
		for (i = 0; i < formatable_volumes; i++)
		{
			FormatMenuEntry* e = &format_menue[i];

			options[mountable_volumes+i] = e->txt;
		}

        options[mountable_volumes+formatable_volumes] = NULL;
        options[mountable_volumes+formatable_volumes + 1] = NULL;

        int chosen_item = get_menu_selection(headers, &options, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        
        if (chosen_item < (mountable_volumes + formatable_volumes))
        {
            chosen_item = chosen_item - mountable_volumes;
            FormatMenuEntry* e = &format_menue[chosen_item];
            Volume* v = e->v;

            sprintf(confirm_string, "%s - %s", v->mount_point, confirm_format);

            if (!confirm_selection(confirm_string, confirm))
                continue;
            ui_print("Formatting %s...\n", v->mount_point);
            if (0 != format_volume(v->mount_point))
                ui_print("Error formatting %s!\n", v->mount_point);
            else
                ui_print("Done.\n");
        }
    }

    free(format_menue);

}

void show_nandroid_advanced_restore_menu(const char* path)
{
    if (ensure_path_mounted(path) != 0) {
        LOGE ("Can't mount sdcard\n");
        return;
    }

    static char* advancedheaders[] = {  "Choose an image to restore",
                                "",
                                "Choose an image to restore",
                                "first. The next menu will",
                                "you more options.",
                                "",
                                NULL
    };

    char tmp[PATH_MAX];
    sprintf(tmp, "%s/clockworkmod/backup/", path);
    char* file = choose_file_menu(tmp, NULL, advancedheaders);
    if (file == NULL)
        return;

    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Nandroid Advanced Restore",
                                "",
                                NULL
    };

    static char* list[] = { "Restore boot",
                            "Restore system",
                            "Restore data",
                            "Restore cache",
                            "Restore sd-ext",
                            "Restore wimax",
                            NULL
    };
    
    if (0 != get_partition_device("wimax", tmp)) {
        // disable wimax restore option
        list[5] = NULL;
    }

    static char* confirm_restore  = "Confirm restore?";

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
            if (confirm_selection(confirm_restore, "Yes - Restore boot"))
                nandroid_restore(file, 1, 0, 0, 0, 0, 0);
            break;
        case 1:
            if (confirm_selection(confirm_restore, "Yes - Restore system"))
                nandroid_restore(file, 0, 1, 0, 0, 0, 0);
            break;
        case 2:
            if (confirm_selection(confirm_restore, "Yes - Restore data"))
                nandroid_restore(file, 0, 0, 1, 0, 0, 0);
            break;
        case 3:
            if (confirm_selection(confirm_restore, "Yes - Restore cache"))
                nandroid_restore(file, 0, 0, 0, 1, 0, 0);
            break;
        case 4:
            if (confirm_selection(confirm_restore, "Yes - Restore sd-ext"))
                nandroid_restore(file, 0, 0, 0, 0, 1, 0);
            break;
        case 5:
            if (confirm_selection(confirm_restore, "Yes - Restore wimax"))
                nandroid_restore(file, 0, 0, 0, 0, 0, 1);
            break;
    }
}

void show_nandroid_advanced_backup_menu(){
    static char* backup_stat[] = {  "[ ] Backup Boot",
				    "[ ] Backup System",
				    "[ ] Backup Data",
				    "[ ] Backup Cache",
				    "[ ] Backup Sd-Ext\n",
				    "Backup NOW!!",
				    NULL };
					     
    static char* backup_headers[] = { "Select partition", "", NULL };
		
    int backup_item = get_menu_selection(backup_headers, backup_stat, 0, 0);
    //if (backup_item == GO_BACK)
    //break;
    switch (backup_item){
    case 0:
    {
	if (tmp1==0){
	    backup_stat[0]="[*] Backup Boot";
	    tmp1=1;
	}
	else{
	    backup_stat[0]="[ ] Backup Boot";
	    tmp1=0;
	}
	show_nandroid_advanced_backup_menu();
	break;
      }
      case 1:
    {
	if (tmp2==0){
	    backup_stat[1]="[*] Backup System";
	    tmp2=1;
	}
	else{
	    backup_stat[1]="[ ] Backup System";
	    tmp2=0;
	}
	show_nandroid_advanced_backup_menu();
	break;
      }
      case 2:
    {
	if (tmp3==0){
	    backup_stat[2]="[*] Backup Data";
	    tmp3=1;
	}
	else{
	    backup_stat[2]="[ ] Backup Data";
	    tmp3=0;
	}
	show_nandroid_advanced_backup_menu();
	break;
      }
      case 3:
    {
	if (tmp4==0){
	    backup_stat[3]="[*] Backup Cache";
	    tmp4=1;
	}
	else{
	    backup_stat[3]="[ ] Backup Cache";
	    tmp4=0;
	}
	show_nandroid_advanced_backup_menu();
	break;
      }
      case 4:
    {
	if (tmp5==0){
	    backup_stat[4]="[*] Backup Sd-Ext";
	    tmp5=1;
	}
	else{
	    backup_stat[4]="[ ] Backup Sd-Ext";
	    tmp5=0;
	}
	show_nandroid_advanced_backup_menu();
	break;
      }
      case 5:
      {
	  if (confirm_selection("Confirm BACKUP?", "Yes - Start Backup NOW!"))
               {
                char backup_path[PATH_MAX];
                time_t t = time(NULL);
                struct tm *tmp = localtime(&t);
		tmp->tm_hour=tmp->tm_hour+glo_timezone;
		
		if (tmp->tm_hour>24)
		  tmp->tm_hour=tmp->tm_hour-24;
		else if (tmp->tm_hour<0)
		  tmp->tm_hour=tmp->tm_hour+24;
		else
		  tmp->tm_hour=tmp->tm_hour;
		
                if (tmp == NULL)
                {
                    struct timeval tp;
                    gettimeofday(&tp, NULL);
                    sprintf(backup_path, "/sdcard/clockworkmod/backup/%d", tp.tv_sec);
                }
                else
                {
                    strftime(backup_path, sizeof(backup_path), "/sdcard/clockworkmod/backup/%F.%H.%M.%S", tmp);
                }
                nandroid_backup_advanced(backup_path);
               }
           break;
      }
      break;break;
    }
}

void show_nandroid_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Nandroid",
                                "",
                                NULL
    };

    static char* list[] = { "backup",
                            "restore",
                            "advanced restore",
			    "advanced backup",
                            "backup to internal sdcard",
                            "restore from internal sdcard",
                            "advanced restore from internal sdcard",
                            NULL
    };

    if (volume_for_path("/emmc") == NULL)
        list[4] = NULL;

    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item)
    {
        case 0:
            {
              if (confirm_selection("Confirm BACKUP?", "Yes - Start Backup NOW!"))
               {
                char backup_path[PATH_MAX];
                time_t t = time(NULL);
                struct tm *tmp = localtime(&t);
		tmp->tm_hour=tmp->tm_hour+glo_timezone;
		
		if (tmp->tm_hour>24)
		  tmp->tm_hour=tmp->tm_hour-24;
		else if (tmp->tm_hour<0)
		  tmp->tm_hour=tmp->tm_hour+24;
		else
		  tmp->tm_hour=tmp->tm_hour;
		
                if (tmp == NULL)
                {
                    struct timeval tp;
                    gettimeofday(&tp, NULL);
                    sprintf(backup_path, "/sdcard/clockworkmod/backup/%d", tp.tv_sec);
                }
                else
                {
                    strftime(backup_path, sizeof(backup_path), "/sdcard/clockworkmod/backup/%F.%H.%M.%S", tmp);
                }
                nandroid_backup(backup_path);
               }
            }
            break;
        case 1:
            show_nandroid_restore_menu("/sdcard");
            break;
        case 2:
            show_nandroid_advanced_restore_menu("/sdcard");
            break;
	case 3:
	{
	    tmp1=tmp2=tmp3=tmp4=tmp5=0;
	    show_nandroid_advanced_backup_menu();
	    break;
	}
        case 4:
            {
                char backup_path[PATH_MAX];
                time_t t = time(NULL);
                struct tm *tmp = localtime(&t);
                if (tmp == NULL)
                {
                    struct timeval tp;
                    gettimeofday(&tp, NULL);
                    sprintf(backup_path, "/emmc/clockworkmod/backup/%d", tp.tv_sec);
                }
                else
                {
                    strftime(backup_path, sizeof(backup_path), "/emmc/clockworkmod/backup/%F.%H.%M.%S", tmp);
                }
                nandroid_backup(backup_path);
            }
            break;
        case 5:
            show_nandroid_restore_menu("/emmc");
            break;
        case 6:
            show_nandroid_advanced_restore_menu("/emmc");
            break;
    }
}

void wipe_battery_stats()
{
    ensure_path_mounted("/data");
    remove("/data/system/batterystats.bin");
    ensure_path_unmounted("/data");
    ui_print("Battery Stats Wiped.\n");
}

static long tmplog_offset = 0;

static int
erase_volume(const char *volume) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    ui_print("Formatting %s...\n", volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }

    return format_volume(volume);
}

void show_wipe_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Wipe & Factory Reset menu",
                                "",
                                NULL
    };
    
    static char* list[] = { "Wipe Cache",
                            "Wipe Data (Factory Reset)",
                            "Wipe Dalvik Cache",
                            "Wipe Battery Stats",
                            NULL
    };
    
    for (;;)
    {
	int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
	{
	    case 0:
	    {
	      static char* headers[] = {  "asdf",
					  NULL
	      };
		if (confirm_selection("Confirm wipe?", "Yes - Wipe Cache"))
                {
                    ui_print("\n-- Wiping cache...\n");
                    erase_volume("/cache");
                    ui_print("Cache wipe complete.\n");
                    if (!ui_text_visible()) return;
                }
                break;
	    }
		
            case 1:
            {
		if (confirm_selection("Confirm Factory Reset?", "Yes - Factory Reset"))
                {
                    ui_print("\n-- Performing Factory Reset...\n");
                    device_wipe_data();
		    erase_volume("/data");
		    erase_volume("/cache");
		    if (has_datadata()) {
			erase_volume("/datadata");
		    }
		    ui_print("Wiping sd-ext...\n");
		    ensure_path_mounted("/sd-ext");
		    __system("rm -r /sd-ext");
		    ensure_path_unmounted("/sd-ext");
		    erase_volume("/sdcard/.android_secure");
		    ui_print("Factory Reset complete.\n");
                    if (!ui_text_visible()) return;
                }
		
                break;
            }
            case 2:
            {
                if (0 != ensure_path_mounted("/data"))
                    break;
                ensure_path_mounted("/sd-ext");
                ensure_path_mounted("/cache");
                if (confirm_selection( "Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
                    __system("rm -r /data/dalvik-cache");
                    __system("rm -r /cache/dalvik-cache");
                    __system("rm -r /sd-ext/dalvik-cache");
                    ui_print("Dalvik Cache wiped.\n");
                }
                ensure_path_unmounted("/data");
                break;
            }
            case 3:
            {
                if (confirm_selection( "Confirm wipe?", "Yes - Wipe Battery Stats"))
                    wipe_battery_stats();
                break;
            }
	}
    }
}

void show_reboot_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Menu for Reboot Phone",
                                "",
                                NULL
    };
    
    static char* list[] = { "Reboot to System",
                            "Reboot to Recovery",
                            "Reboot to Download Mode",
                            NULL
    };
    
    for (;;)
    {
	int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
	{
	    case 0:
            {
                reboot_wrapper("");
                break;
            }
            case 1:
            {
                reboot_wrapper("recovery");
                break;
            }
            case 2:
            {
                reboot_wrapper("download");
                break;
            }
	}
    }
}

void show_setting_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Setting & Interface Menu",
                                "",
                                NULL
    };
    
    static char* list[] = { "Enable/Disable Vibrate",
                            "Enable/Disable OnScreen Button",
                            "Set Brightness Level",
			    "Set Timezone",
			    "Set Themes",
                            NULL
    };
    
    for (;;)
    {
	int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
	{
	    case 0:
	    {
		static char* vibrate_stat[] = { "Enable",
						"Disable",
						 NULL };
					     
		static char* vibrate_headers[] = { "Enable/Disable Vibrate", "", NULL };
		
		int vibra = get_menu_selection(vibrate_headers, vibrate_stat, 0, 0);
                if (vibra == GO_BACK)
                    break;
		switch (vibra)
		{
		  case 0:
		  {
		      vib=1;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 1 > /sdcard/.px-recovery/settings/vibrate");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 1:
		  {
		      vib=0;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 0 > /sdcard/.px-recovery/settings/vibrate");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		}
	      break;
	    }
            case 1:
	    {
		static char* onscreen_stat[] = { "Enable",
						 "Disable",
						 NULL };
					     
		static char* onscreen_headers[] = { "Enable/Disable OnScreen Button", "", NULL };
		
		int osbutton = get_menu_selection(onscreen_headers, onscreen_stat, 0, 0);
                if (osbutton == GO_BACK)
                    break;
		switch (osbutton)
		{
		  case 0:
		  {
		      osb=1;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 1 > /sdcard/.px-recovery/settings/osbutton");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 1:
		  {
		      osb=0;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 0 > /sdcard/.px-recovery/settings/osbutton");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		}
	      break;
	    }
            case 2:
	    {
		static char* brightness_level[] = { "Low",
						    "Medium",
						    "High",
						     NULL };
					     
		static char* brightness_headers[] = { "Brightness Level", "", NULL };
		
		int brightness = get_menu_selection(brightness_headers, brightness_level, 0, 0);
                if (brightness == GO_BACK)
                    break;
		switch (brightness)
		{
		  case 0:
		  {
		      __system("echo 1 > /sys/class/leds/lcd-backlight/brightness");
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 1 > /sdcard/.px-recovery/settings/brightness");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 1:
		  {
		      __system("echo 75 > /sys/class/leds/lcd-backlight/brightness");
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 2 > /sdcard/.px-recovery/settings/brightness");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 2:
		  {
		      __system("echo 200 > /sys/class/leds/lcd-backlight/brightness");
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 3 > /sdcard/.px-recovery/settings/brightness");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		}
	      break;
	    }
	    case 3:
	    {
		static char* time_stat[] = { "GMT -11:00",
					     "GMT -10:00",
					     "GMT -09:00",
					     "GMT -08:00",
					     "GMT -07:00",
					     "GMT -06:00",
					     "GMT -05:00",
					     "GMT -04:00",
					     "GMT -03:00",
					     "GMT -02:00",
					     "GMT -01:00",
					     "GMT +00:00",
					     "GMT +01:00",
					     "GMT +02:00",
					     "GMT +03:00",
					     "GMT +04:00",
					     "GMT +05:00",
					     "GMT +06:00",
					     "GMT +07:00",
					     "GMT +08:00",
					     "GMT +09:00",
					     "GMT +10:00",
					     "GMT +11:00",
					     "GMT +12:00",
					     "GMT +13:00",
					      NULL };
					     
		static char* time_headers[] = { "Select your timezone!", "", NULL };
		
		int timem = get_menu_selection(time_headers, time_stat, 0, 0);
                if (timem == GO_BACK)
                    break;
		switch (timem)
		{
		  case 0:
		  {
		      glo_timezone=-11;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -11 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 1:
		  {
		      glo_timezone=-10;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -10 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 2:
		  {
		      glo_timezone=-9;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -9 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 3:
		  {
		    glo_timezone=-8;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -8 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 4:
		  {
		    glo_timezone=-7;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -7 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 5:
		  {
		    glo_timezone=-6;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -6 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 6:
		  {
		    glo_timezone=-5;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -5 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 7:
		  {
		    glo_timezone=-4;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -4 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 8:
		  {
		    glo_timezone=-3;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -3 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 9:
		  {
		    glo_timezone=-2;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -2 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 10:
		  {
		    glo_timezone=-1;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo -1 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 11:
		  {
		    glo_timezone=0;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 0 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 12:
		  {
		    glo_timezone=1;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 1 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 13:
		  {
		    glo_timezone=2;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 2 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 14:
		  {
		    glo_timezone=3;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 3 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 15:
		  {
		    glo_timezone=4;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 4 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 16:
		  {
		    glo_timezone=5;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 5 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 17:
		  {
		    glo_timezone=6;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 6 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 18:
		  {
		    glo_timezone=7;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 7 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 19:
		  {
		    glo_timezone=8;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 8 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 20:
		  {
		    glo_timezone=9;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 9 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 21:
		  {
		    glo_timezone=10;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 10 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 22:
		  {
		    glo_timezone=11;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 11 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 23:
		  {
		    glo_timezone=12;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 12 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		  case 24:
		  {
		    glo_timezone=13;
		      ensure_path_mounted("/sdcard");
		      __system("mkdir -p /sdcard/.px-recovery/settings");
		      __system("echo 13 > /sdcard/.px-recovery/settings/timezone");
		      ensure_path_unmounted("/sdcard");
		      break;
		  }
		}
	      break;
	    }
	    case 4:
	    {
		ensure_path_mounted("/sdcard");
		
		static char* themes_headers[] = { EXPAND(RECOVERY_VERSION),
						  "Themes Menu",
						  "",
						  "Choose your theme",
						  NULL 
		};
		
		char tmp[PATH_MAX];
		sprintf(tmp, "/sdcard/.px-recovery/themes/");
		char* file = choose_file_menu(tmp, NULL, themes_headers);
		if (file == NULL)
		    return;
		
		if (confirm_selection("Confirm theme?", "Yes - apply theme")){
		    FILE *thm;
		    thm = fopen ("/sdcard/.px-recovery/settings/theme", "w");
		    fprintf(thm,"%s",file);
		    fclose(thm);
		    ui_print("\nplease reboot to apply theme\n");
		}
	      ensure_path_unmounted("/sdcard");
	      break;
	    }
	}
    }
}

void show_advanced_menu()
{
    static char* headers[] = {  EXPAND(RECOVERY_VERSION),
				"",
				"Advanced and Debugging Menu",
                                "",
                                NULL
    };

    static char* list[] = { "Report Error",
                            "Key Test",
                            "Show log",
                            "Partition SD Card",
                            "Fix Permissions",
			    "Info",
                            NULL
    };

    for (;;)
    {
        int chosen_item = get_menu_selection(headers, list, 0, 0);
        if (chosen_item == GO_BACK)
            break;
        switch (chosen_item)
        {
            /*case 0:
            {
                reboot_wrapper("recovery");
                break;
            }
            case 1:
            {
                if (0 != ensure_path_mounted("/data"))
                    break;
                ensure_path_mounted("/sd-ext");
                ensure_path_mounted("/cache");
                if (confirm_selection( "Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
                    __system("rm -r /data/dalvik-cache");
                    __system("rm -r /cache/dalvik-cache");
                    __system("rm -r /sd-ext/dalvik-cache");
                    ui_print("Dalvik Cache wiped.\n");
                }
                ensure_path_unmounted("/data");
                break;
            }
            case 2:
            {
                if (confirm_selection( "Confirm wipe?", "Yes - Wipe Battery Stats"))
                    wipe_battery_stats();
                break;
            }
            */
            case 0:
                handle_failure(1);
                break;
            case 1:
            {
                ui_print("Outputting key codes.\n");
                ui_print("Go back to end debugging.\n");
                struct keyStruct{
          int code;
          int x;
          int y;
        }*key;
                int action;
                do
                {
                    key = ui_wait_key();
          if(key->code == ABS_MT_POSITION_X)
          {
                action = device_handle_mouse(key, 1);
            ui_print("Touch: X: %d\tY: %d\n", key->x, key->y);
          }
          else
          {
                action = device_handle_key(key->code, 1);
            ui_print("Key: %x\n", key->code);
          }
                }
                while (action != GO_BACK);
                break;
            }
            case 2:
            {
                ui_printlogtail(12);
                break;
            }
            case 3:
            {
               if (confirm_selection("Confirm: SDCARD will be wiped!!", "Yes - Continue with SDCARD Partitioning"))
                {

                static char* ext_sizes[] = { "128M",
                                             "256M",
                                             "512M",
                                             "1024M",
                                             "2048M",
                                             "4096M",
                                             NULL };

                static char* ext_fs[] = { "ext2",
                                          "ext3",
                                          "ext4",
                                          NULL };

                static char* swap_sizes[] = { "0M",
                                              "32M",
                                              "64M",
                                              "128M",
                                              "256M",
                                              NULL };

                static char* ext_headers[] = { "Ext Size", "", NULL };
                static char* ext_fs_headers[] = { "Ext File System", "", NULL };
                static char* swap_headers[] = { "Swap Size", "", NULL };

                int ext_size = get_menu_selection(ext_headers, ext_sizes, 0, 0);
                if (ext_size == GO_BACK)
                    continue;

                int ext_fs_selected = get_menu_selection(ext_fs_headers, ext_fs, 0, 0);
                if (ext_fs_selected == GO_BACK)
                    continue;

                int swap_size = get_menu_selection(swap_headers, swap_sizes, 0, 0);
                if (swap_size == GO_BACK)
                    continue;

                char sddevice[256];
                Volume *vol = volume_for_path("/sdcard");
                strcpy(sddevice, vol->device);
                // we only want the mmcblk, not the partition
                sddevice[strlen("/dev/block/mmcblkX")] = NULL;
                char cmd[PATH_MAX];
                setenv("SDPATH", sddevice, 1);
                sprintf(cmd, "sdparted -es %s -ss %s -efs %s -s", ext_sizes[ext_size], swap_sizes[swap_size], ext_fs[ext_fs_selected]);
                ui_print("Partitioning SD Card... please wait...\n");
                if (0 == __system(cmd))
                    ui_print("Done!\n");
                else
                    ui_print("An error occured while partitioning your SD Card. Please see /tmp/recovery.log for more details.\n");

               }

                break;
            }
            case 4:
            {
                ensure_path_mounted("/system");
                ensure_path_mounted("/data");
                ui_print("Fixing permissions...\n");
                __system("fix_permissions");
                ui_print("Done!\n");
                break;
            }
            case 5:
            {
		static char* info_item[] = { "Disk",
					     "Memory",
					      NULL };
					     
		static char* info_headers[] = { "Select info do you want", "", NULL };
		
		int info = get_menu_selection(info_headers, info_item, 0, 0);
                if (info == GO_BACK)
                    break;
		switch (info)
		{
		  case 0:
		  {
		    ensure_path_mounted("/system");
		    FILE *pfp;
		    char line[100];
		    void read_partition(){
		      for ( fgets(line, sizeof(line), pfp); fgets(line, sizeof(line), pfp); ){
			  
		      }
		      __system("mkdir -p /tmp/px-recovery");
		      
		      FILE *disko;
		      disko = fopen("/tmp/px-recovery/disk", "w");
		      fprintf(disko, "%s",line);
		      fclose(disko);
		      
		      FILE *disk;
		      disk = fopen("/tmp/px-recovery/disk", "r");
			
		      char fs[100],size[100],used[100],avail[100],usep[100],mount[100];
		      fscanf(disk,"\n\n\n\n%s\t%s\t%s\t%s\t%s\t%s",fs,size,used,avail,usep,mount);
		      ui_print("---------------------\n");
		      ui_print("size\t\t\t\t: %sB\n",size);
		      ui_print("used\t\t\t\t: %sB, %s\n",used,usep);
		      ui_print("avail\t\t\t: %sB\n",avail);
		      ui_print("location: %s\n\t",fs);
		      fclose(disk);
		      ensure_path_unmounted("/system");
		    }
		    static char* disk_item[] = { "System",
						 "Data",
						 "Cache",
						 "Sdcard",
						 "Sd-Ext",
						  NULL };
						
		    static char* disk_headers[] = { "Select partition do you want", "", NULL };
		    
		    int disk = get_menu_selection(disk_headers, disk_item, 0, 0);
		    if (disk == GO_BACK)
			break;
		    switch(disk)
		    {
		      case 0:
		      {
			ensure_path_mounted("/system");
			if( (pfp=popen("df -h /system", "r")) == NULL )
			{
			  fprintf(stderr, "POPEN Error\n");
			  exit(1);
			}
			ui_print("\n---------------------\n");
			ui_print("SYSTEM Partition info\n");
			read_partition();
			ensure_path_unmounted("/system");
			break;
		      }
		      
		      case 1:
		      {
			ensure_path_mounted("/data");
			if( (pfp=popen("df -h /data", "r")) == NULL )
			{
			  fprintf(stderr, "POPEN Error\n");
			  exit(1);
			}
			ui_print("\n---------------------\n");
			ui_print("DATA Partition info\n");
			read_partition();
			ensure_path_unmounted("/data");
			break;
		      }
		      
		      case 2:
		      {
			ensure_path_mounted("/cache");
			if( (pfp=popen("df -h /cache", "r")) == NULL )
			{
			  fprintf(stderr, "POPEN Error\n");
			  exit(1);
			}
			ui_print("\n---------------------\n");
			ui_print("CACHE Partition info\n");
			read_partition();
			break;
		      }
		      
		      case 3:
		      {
			ensure_path_mounted("/sdcard");
			if( (pfp=popen("df -h /sdcard", "r")) == NULL )
			{
			  fprintf(stderr, "POPEN Error\n");
			  exit(1);
			}
			ui_print("\n---------------------\n");
			ui_print("SDCARD Partition info\n");
			read_partition();
			ensure_path_unmounted("/sdcard");
			break;
		      }
		      
		      case 4:
		      {
			ensure_path_mounted("/sd-ext");
			if( (pfp=popen("df -h /sd-ext", "r")) == NULL )
			{
			  fprintf(stderr, "POPEN Error\n");
			  exit(1);
			}
			ui_print("\n---------------------\n");
			ui_print("SD-EXT Partition info\n");
			read_partition();
			ensure_path_unmounted("/sd-ext");
			break;
		      }		      
		    }
		    break;
		  }
		  case 1:
		  {
		      ensure_path_mounted("/system");
		      ui_print("\n-----------\n");
		      ui_print("MEMORY INFO\n");
		      ui_print("-----------\n");
		      FILE *pfp;
		      char line[100];
		      if( (pfp=popen("grep MemTotal /proc/meminfo", "r")) == NULL )
		      {
			fprintf(stderr, "POPEN Error\n");
			exit(1);
		      }
		      for ( fgets(line, sizeof(line), pfp); fgets(line, sizeof(line), pfp); ){
		      }

		      ui_print("%s",line);
		      
		      FILE *pfq;
		      if( (pfq=popen("grep MemFree /proc/meminfo", "r")) == NULL )
		      {
			fprintf(stderr, "POPEN Error\n");
			exit(1);
		      }
		      for ( fgets(line, sizeof(line), pfq); fgets(line, sizeof(line), pfq); ){
		      }

		      ui_print("%s",line);
		      ensure_path_unmounted("/system");
		  }
		}
                break;
            }
        }
    }
}

void write_fstab_root(char *path, FILE *file)
{
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGW("Unable to get recovery.fstab info for %s during fstab generation!\n", path);
        return;
    }

    char device[200];
    if (vol->device[0] != '/')
        get_partition_device(vol->device, device);
    else
        strcpy(device, vol->device);

    fprintf(file, "%s ", device);
    fprintf(file, "%s ", path);
    // special case rfs cause auto will mount it as vfat on samsung.
    fprintf(file, "%s rw\n", vol->fs_type2 != NULL && strcmp(vol->fs_type, "rfs") != 0 ? "auto" : vol->fs_type);
}

void create_fstab()
{
    struct stat info;
    __system("touch /etc/mtab");
    FILE *file = fopen("/etc/fstab", "w");
    if (file == NULL) {
        LOGW("Unable to create /etc/fstab!\n");
        return;
    }
    Volume *vol = volume_for_path("/boot");
    if (NULL != vol && strcmp(vol->fs_type, "mtd") != 0 && strcmp(vol->fs_type, "emmc") != 0 && strcmp(vol->fs_type, "bml") != 0)
         write_fstab_root("/boot", file);
    write_fstab_root("/cache", file);
    write_fstab_root("/data", file);
    write_fstab_root("/datadata", file);
    write_fstab_root("/emmc", file);
    write_fstab_root("/system", file);
    write_fstab_root("/sdcard", file);
    write_fstab_root("/sd-ext", file);
    fclose(file);
    LOGI("Completed outputting fstab.\n");
}

int bml_check_volume(const char *path) {
    ui_print("Checking %s...\n", path);
    ensure_path_unmounted(path);
    if (0 == ensure_path_mounted(path)) {
        ensure_path_unmounted(path);
        return 0;
    }
    
    Volume *vol = volume_for_path(path);
    if (vol == NULL) {
        LOGE("Unable process volume! Skipping...\n");
        return 0;
    }
    
    ui_print("%s may be rfs. Checking...\n", path);
    char tmp[PATH_MAX];
    sprintf(tmp, "mount -t rfs %s %s", vol->device, path);
    int ret = __system(tmp);
    printf("%d\n", ret);
    return ret == 0 ? 1 : 0;
}

void process_volumes() {
    create_fstab();

    if (is_data_media()) {
        setup_data_media();
    }

    return;

    // dead code.
    if (device_flash_type() != BML)
        return;

    ui_print("Checking for ext4 partitions...\n");
    int ret = 0;
    ret = bml_check_volume("/system");
    ret |= bml_check_volume("/data");
    if (has_datadata())
        ret |= bml_check_volume("/datadata");
    ret |= bml_check_volume("/cache");
    
    if (ret == 0) {
        ui_print("Done!\n");
        return;
    }
    
    char backup_path[PATH_MAX];
    time_t t = time(NULL);
    char backup_name[PATH_MAX];
    struct timeval tp;
    gettimeofday(&tp, NULL);
    sprintf(backup_name, "before-ext4-convert-%d", tp.tv_sec);
    sprintf(backup_path, "/sdcard/clockworkmod/backup/%s", backup_name);

    ui_set_show_text(1);
    ui_print("Filesystems need to be converted to ext4.\n");
    ui_print("A backup and restore will now take place.\n");
    ui_print("If anything goes wrong, your backup will be\n");
    ui_print("named %s. Try restoring it\n", backup_name);
    ui_print("in case of error.\n");

    nandroid_backup(backup_path);
    nandroid_restore(backup_path, 1, 1, 1, 1, 1, 0);
    ui_set_show_text(0);
}

void handle_failure(int ret)
{
    if (ret == 0)
        return;
    if (0 != ensure_path_mounted("/sdcard"))
        return;
    mkdir("/sdcard/clockworkmod", S_IRWXU);
    __system("cp /tmp/recovery.log /sdcard/clockworkmod/recovery.log");
    ui_print("/tmp/recovery.log was copied to /sdcard/clockworkmod/recovery.log. Please open ROM Manager to report the issue.\n");
}

int is_path_mounted(const char* path) {
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        return 0;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return 0;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv) {
        // volume is already mounted
        return 1;
    }
    return 0;
}

int has_datadata() {
    Volume *vol = volume_for_path("/datadata");
    return vol != NULL;
}

int volume_main(int argc, char **argv) {
    load_volume_table();
    return 0;
}
