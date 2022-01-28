#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_PATH 10 * 9
#define MAX_LIST 9
#define MAX_LENGTH 20
#define MAX_SECTOR 2879
#define MAX_FAT_ENTRY 2848 // Max free sector in FAT table

typedef struct fat12 fat12_t;
struct fat12 {
    char *file_list[MAX_LIST];
    int index;
    char lower_file_name[MAX_LENGTH];
    unsigned int next_sectors;
    unsigned int file_size;
    unsigned int total_sector;
    unsigned int date;
    unsigned int time;
    time_t mtime;
    int depth;
};

void diskput(char *p, char *file_path);
void get_file_time(fat12_t *input_file);
void loop_dir(char *p, unsigned int first_logical_cluster, fat12_t *input_file);
void write_to_image(char *p, int position, fat12_t *input_file);

static unsigned int bytes_per_sector;

int main(int argc, char *argv[])
{

    int fd;
    struct stat sb;

    if (argc != 3) {
        printf("\nUsage: ./diskput diskimage path\n\n");
        exit(1);
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        printf("\nFile cannot open!\n\n");
        exit(1);
    }

    fstat(fd, &sb);

    // p points to the starting pos of your mapped memory
    char *p = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("\nError: failed to map memory\n\n");
        exit(1);
    }

    diskput(p, argv[2]);

    // the modifed the memory data would be mapped to the disk image
    munmap(p, sb.st_size);
    close(fd);
    return 0;
}

void diskput(char *p, char *file_path)
{
    /* initialize the fat12_t struct */
    fat12_t *input_file = (fat12_t *)malloc(sizeof(fat12_t));
    for (int i = 0; i < MAX_LIST; i++) {
        input_file->file_list[i] = NULL;
        if (i < MAX_LENGTH) {
            input_file->lower_file_name[i] = 0;
        }
    }
    input_file->index = 0;
    input_file->next_sectors = 0;
    input_file->file_size = 0;
    input_file->total_sector = 0;
    input_file->date = 0;
    input_file->time = 0;
    input_file->mtime = 0;
    input_file->depth = 0;

    /* store the path to a list separated into subdirectories */
    char temp_file_path[MAX_PATH] = {0};
    char temp_lower[MAX_LENGTH] = "*******************";
    char *tmp = NULL;

    /* convert path to upper case letter */
    memcpy(temp_file_path, file_path, strlen(file_path));

    /* stat() function is case sensitive on the school computer */
    for (int i = MAX_LENGTH - 1, count = strlen(temp_file_path) - 1; count >= 0; count--, i--) {
        if (temp_file_path[count] != '/' && (i >= 0)) {
            temp_lower[i] = temp_file_path[count];
        } else {
            break;
        }
    }
    sscanf(temp_lower, "%*[*]%s", input_file->lower_file_name);

    tmp = temp_file_path;
    while (*tmp) {
        *tmp = toupper((unsigned int)*tmp);
        tmp++;
    }

    input_file->file_list[input_file->index] = strtok(temp_file_path, "/");
    while (input_file->file_list[input_file->index] != NULL) {
        input_file->index++;
        input_file->file_list[input_file->index] = strtok(NULL, "/");
    }

    /* mmap the input file to memory */
    struct stat file_info;

    if ((stat(input_file->lower_file_name, &file_info) == -1)) {
        printf("\nFile not found, %s\n\n", strerror(errno));
        exit(1);
    }

    input_file->mtime = file_info.st_mtim.tv_sec; // store input file epoch time
    input_file->file_size = file_info.st_size;    // store input file size
    get_file_time(input_file);                    // convert the time to the right format

    /* check if there is enough free space for the input file */
    unsigned int total_disk_size = 0, total_sector = 0;
    unsigned int sectors_per_FAT = 0;

    memcpy(&bytes_per_sector, (p + 11), 2);
    memcpy(&sectors_per_FAT, (p + 22), 1);
    memcpy(&total_sector, (p + 19), 2);
    total_disk_size = bytes_per_sector * total_sector;
    input_file->total_sector = total_sector;

    /* free size of the disk (little-endian) */
    unsigned int free_disk_size = 0, used_sectors = 0;
    unsigned int FAT = 0, mask_front = 0x000FFF, mask_end = 0xFFF000;

    /* check if the ima file large enough to store input file */
    for (int i = 0; i < (bytes_per_sector * sectors_per_FAT); i += 3) { // including the position 0 and 1
        memcpy(&FAT, (p + bytes_per_sector + i), 3);

        if ((FAT & mask_front) != 0) {
            used_sectors++;
        } else {
            break;
        }

        if (((FAT & mask_end) >> 12) != 0) {
            used_sectors++;
        } else {
            break;
        }
    }
    free_disk_size = total_disk_size - (used_sectors + 33 - 2) * bytes_per_sector;

    input_file->next_sectors = used_sectors;

    if (free_disk_size < file_info.st_size) {
        printf("\nNot enough free space in the disk image\n\n");
        exit(1);
    }

    loop_dir(p, 19, input_file);

    free(input_file);
}

/* a recursive function to loop through the subdirectory */
void loop_dir(char *p, unsigned int first_logical_cluster, fat12_t *input_file)
{
    /* start to find the directories to store the input file */
    unsigned int max_root_entry = 0;
    unsigned int free = 0x00, unused = 0xE5;
    unsigned int attribute = 0;
    char file_name[9] = {0}, extension[4] = {0}, full_name[13] = {0};
    unsigned int limit = 0;

    /* base on if in the root or subdirectory to design the right limit */
    memcpy(&max_root_entry, (p + 17), 2);
    if (input_file->depth == 0) {
        limit = max_root_entry * 32;
    } else {
        limit = (MAX_SECTOR - first_logical_cluster - 31) * bytes_per_sector;
    }

    
    for (int i = 0; i <= limit; i += 32) {

        if ((i > max_root_entry * 32) && (input_file->index == 1)) {
            printf("\nThe root directory entry is full\n\n");
            exit(1);
        }

        if ((memcmp(&free, (p + bytes_per_sector * first_logical_cluster + i), 1) == 0)) {

            if ((input_file->depth == (input_file->index - 1))) {
                write_to_image(p, bytes_per_sector * first_logical_cluster + i, input_file);
                return;
            } else {
                printf("\nDirectory not found\n\n");
                exit(1);
            }

        } else if (memcmp(&unused, (p + bytes_per_sector * first_logical_cluster + i), 1) == 0) {
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * first_logical_cluster + i + 11), 1);

        if ((0x00 == attribute)) { // file
            memcpy(file_name, (p + bytes_per_sector * first_logical_cluster + i), 8);
            sscanf(file_name, "%s", file_name);

            memcpy(extension, (p + bytes_per_sector * first_logical_cluster + i + 8), 3);
            sscanf(extension, "%s", extension);

            sprintf(full_name, "%s.%s", file_name, extension);

            /* check if the input file already exists */
            if (memcmp(full_name, input_file->file_list[input_file->index - 1],
                       strlen(input_file->file_list[input_file->index - 1])) == 0) {

                if (strlen(full_name) == strlen(input_file->file_list[input_file->index - 1])) {

                    if (input_file->depth == input_file->index - 1) {
                        printf("\nInput file name already exists\n\n");
                        exit(1);
                    }
                }
                continue;
            }

        } else if (0x0F == (attribute & 0x0F)) { // long file name
            continue;
        } else if (0x10 == (attribute & 0x10)) { // directory
            memcpy(file_name, (p + bytes_per_sector * first_logical_cluster + i), 8);
            sscanf(file_name, "%s", file_name);

            if (memcmp(file_name, input_file->file_list[input_file->depth],
                       strlen(input_file->file_list[input_file->depth])) == 0) {

                if (strlen(file_name) == strlen(input_file->file_list[input_file->depth])){
                    memcpy(&first_logical_cluster, (p + bytes_per_sector * first_logical_cluster + i + 26), 2);
                    input_file->depth++;

                    loop_dir(p, (33 + first_logical_cluster - 2), input_file);

                    return;
                }
            }
        }
    }
}

/* convert the time to the right format */
void get_file_time(fat12_t *input_file)
{
    struct tm ts;
    unsigned int date = 0, time = 0;
    unsigned int year = 0, month = 0, day = 0;
    unsigned int hours = 0, minutes = 0;
    char buf[80];

    ts = *localtime(&input_file->mtime);

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &ts);
    sscanf(buf, "%u-%u-%u %u:%u", &year, &month, &day, &hours, &minutes);

    date = ((year - 1980) << 9) | date;
    date = (month << 5) | date;
    date = day | date;

    time = (hours << 11) | time;
    time = (minutes << 5) | time;

    input_file->date = date;
    input_file->time = time;
}

void write_to_image(char *p, int position, fat12_t *input_file)
{

    char file_name[9] = {0}, extension[4] = {0};
    unsigned int mask_front = 0x000FFF, mask_end = 0xFFF000;
    int sector_list[MAX_FAT_ENTRY] = {0}, location = 0;

    // create new directory entry for input file
    sscanf(input_file->file_list[input_file->index - 1], "%[^.].%s", file_name, extension);

    memcpy((p + position), file_name, 8);                        // name
    memcpy((p + position + 8), extension, 3);                    // extension
    //memcpy((p + position + 11), 0x00, 1);     // attribute
    //memcpy((p + position + 12), (unsigned int *)(0x18), 1);    // UBYTE Reserved

    memcpy((p + position + 14), &(input_file->time), 2);         // DOSTIME CreateTime
    memcpy((p + position + 16), &(input_file->date), 2);         // DOSDATE CreateDate
    memcpy((p + position + 22), &(input_file->time), 2);         // DOSTIME UpdateTime
    memcpy((p + position + 24), &(input_file->date), 2);         // DOSDATE UpdateDate
    memcpy((p + position + 26), &(input_file->next_sectors), 2); // USHORT Cluster
    memcpy((p + position + 28), &(input_file->file_size), 4);    // ULONG FileSizeInBytes

    sector_list[location] = input_file->next_sectors; // store the first free sector from FAT table
    location++;

    for (unsigned int pre = input_file->next_sectors++, tmp = 0; (input_file->next_sectors) < (input_file->total_sector);
         input_file->next_sectors++) { // even mask front, odd mask end

        if ((input_file->next_sectors % 2) == 0) {

            if (input_file->file_size < bytes_per_sector) {                  // when input file only need 1 sector
                memcpy(&tmp, (p + bytes_per_sector + (pre - 1) / 2 * 3), 3); // next_sector is even, so pre is odd
                tmp = tmp | mask_end;
                memcpy((p + bytes_per_sector + (pre - 1) / 2 * 3), &tmp, 3); // end of file, 0xFFF

                memcpy((p + bytes_per_sector + (pre - 1) / 2 * 3 + 4608), &tmp, 3); // make a copy to the second FAT

                break;
            }

            memcpy(&tmp, (p + bytes_per_sector + input_file->next_sectors / 2 * 3), 3);

            if ((tmp & mask_front) == 0) {
                memcpy(&tmp, (p + bytes_per_sector + (pre - 1) / 2 * 3), 3); // next_sector is even, so pre is odd
                tmp = (input_file->next_sectors) << 12 | tmp;
                memcpy((p + bytes_per_sector + (pre - 1) / 2 * 3), &tmp, 3); // copy the next sector value to pre entry

                memcpy((p + bytes_per_sector + (pre - 1) / 2 * 3 + 4608), &tmp, 3); // make a copy to the second FAT

                sector_list[location] = input_file->next_sectors;
                location++;
                input_file->file_size -= bytes_per_sector;
            }
        } else {

            if (input_file->file_size < bytes_per_sector) {            // when input file only need 1 sector
                memcpy(&tmp, (p + bytes_per_sector + pre / 2 * 3), 3); // next_sector is odd, so pre is even
                tmp = tmp | mask_front;
                memcpy((p + bytes_per_sector + pre / 2 * 3), &tmp, 3); // end of file, 0xFFF

                memcpy((p + bytes_per_sector + pre / 2 * 3 + 4608), &tmp, 3); // make a copy to the second FAT

                break;
            }

            memcpy(&tmp, (p + bytes_per_sector + (input_file->next_sectors - 1) / 2 * 3), 3);

            if (((tmp & mask_end) >> 12) == 0) {
                memcpy(&tmp, (p + bytes_per_sector + pre / 2 * 3), 3); // next_sector is odd, so pre is even
                tmp = input_file->next_sectors | tmp;
                memcpy((p + bytes_per_sector + pre / 2 * 3), &tmp, 3); // copy the next sector value to pre entry

                memcpy((p + bytes_per_sector + pre / 2 * 3 + 4608), &tmp, 3); // make a copy to the second FAT

                sector_list[location] = input_file->next_sectors;
                location++;
                input_file->file_size -= bytes_per_sector;
            }
        }

        pre = input_file->next_sectors;
    }

    /* mmap the input file to memory */
    int fd2;
    struct stat file_info_2;

    fd2 = open(input_file->lower_file_name, O_RDWR);
    if (fd2 == -1) {
        printf("\nFile not found!, %s\n\n", strerror(errno));
        exit(1);
    }

    if ((fstat(fd2, &file_info_2) == -1)) {
        printf("\nFile not found!, %s\n\n", strerror(errno));
    }

    /* p2 points to the starting pos of your mapped memory */
    char *p2 = mmap(NULL, file_info_2.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (p2 == MAP_FAILED) {
        printf("\nError: failed to map memory, %s\n\n", strerror(errno));
        exit(1);
    }

    for (int i = 0; i < location; i++) {
        memcpy((p + bytes_per_sector * (33 + sector_list[i] - 2)), (p2 + i * bytes_per_sector), bytes_per_sector);
        if ((i + 1) == location) {
            if ((input_file->file_size > 0) && (input_file->file_size) < bytes_per_sector) {
                memcpy((p + bytes_per_sector * (33 + sector_list[i] - 2)), (p2 + i * bytes_per_sector), input_file->file_size);
            }
        }
    }

    /* the modifed the memory data would be mapped to the disk image */
    munmap(p2, file_info_2.st_size);
    close(fd2);
}