#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_NAME 33
#define MAX_FILE 224
#define MAX_DIRECTORY 50
#define MAX_FAT_ENTRY 2848 // Max free sector in FAT table
#define MAX_SECTOR 2879

void diskinfo(char *p);
int count_sub_file(char *p, unsigned int first_logical_cluster, unsigned int parent_cluster, unsigned int bytes_per_sector);

int main(int argc, char *argv[])
{

    int fd;
    struct stat sb;

    if(argc != 2){
        printf("\nUsage: ./diskinfo diskimage\n\n");
        exit(1);
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1){
        printf("\nFile cannot open!\n");
        exit(1);
    }

    fstat(fd, &sb);

    // p points to the starting pos of your mapped memory
    char *p = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("\nError: failed to map memory\n\n");
        exit(1);
    }

    diskinfo(p);

    // the modifed the memory data would be mapped to the disk image
    munmap(p, sb.st_size);
    close(fd);
    return 0;
}

void diskinfo(char *p){

    /* OS name */
    char OS_name[MAX_NAME] = {0};
    
    memcpy(OS_name, (p + 3), 8);

    /* total size of the disk */
    unsigned int total_disk_size = 0;
    unsigned int bytes_per_sector = 0, total_sector = 0;

    memcpy(&bytes_per_sector, (p + 11), 2);
    memcpy(&total_sector, (p + 19), 2);
    total_disk_size = bytes_per_sector * total_sector;

    /* number of FAT copies and sectors per FAT */
    unsigned int num_FAT = 0, sectors_per_FAT = 0;

    memcpy(&num_FAT, (p + 16), 1);
    memcpy(&sectors_per_FAT, (p + 22), 1);

    /* free size of the disk (little-endian) */
    unsigned int free_disk_size = 0, used_sectors = 0;
    unsigned int FAT = 0, mask_front = 0x000FFF, mask_end = 0xFFF000;

    /* count remain free disk size */
    for (int i = 0; i < (bytes_per_sector * MAX_FAT_ENTRY); i += 3) { // including the position 0 and 1
        memcpy(&FAT, (p + bytes_per_sector + i), 3);

        if((FAT & mask_front) != 0){
            used_sectors++;
        }else{
            break;
        }

        if (((FAT & mask_end) >> 12) != 0) {
            used_sectors++;
        }else{
            break;
        }
    }
    free_disk_size = total_disk_size - (used_sectors + 33 - 2) * bytes_per_sector;

    /* the number of files in the disk & label */
    char label[MAX_NAME] = {0};
    unsigned int attribute = 0, num_of_file = 0, first_logical_cluster = 0;
    unsigned int max_root_entry = 0;
    unsigned int free = 0x00, unused = 0xE5;

    memcpy(&max_root_entry, (p + 17), 2);

    for (int i = 0; i < (max_root_entry * 32); i += 32) {
        if (memcmp(&free, (p + bytes_per_sector * 19 + i), 1) == 0) {
            break;
        } else if (memcmp(&unused, (p + bytes_per_sector * 19 + i), 1) == 0) {
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * 19 + i + 11), 1);

        if (0x00 == attribute) { // file
            num_of_file++;
        } 
        else if (0x08 == attribute) { // volume label of the image
            memcpy(label, (p + 43), 11);

            if ((memcmp(label, "NO NAME", strlen(label))) == 0 ||
                        (memcmp(label, "           ", strlen(label)) == 0)) {
                memcpy(label, (p + bytes_per_sector * 19 + i), 8);
                sscanf(label, "%s", label);
            }
        } 
        else if (0x0F == (attribute & 0x0F)) { // long file name
            continue;
        } 
        else if (0x10 == (attribute & 0x10)) { // subdirectory
            memcpy(&first_logical_cluster, (p + bytes_per_sector * 19 + i + 26),2);
            num_of_file += count_sub_file(p, first_logical_cluster, first_logical_cluster, bytes_per_sector);
        }
    }

    printf("\nOS Name: %s\n", OS_name);
    printf("Label of the disk: %s\n", label);
    printf("Total size of the disk: %u bytes\n", total_disk_size);
    printf("Free size of the disk: %u bytes\n", free_disk_size);
    printf("\n================\n");
    printf("The number of files in the image: %u\n", num_of_file);
    printf("\n================\n");
    printf("Number of FAT copies: %u\n", num_FAT);
    printf("Sectors per FAT: %u\n\n", sectors_per_FAT);
}

/* a recursive function dive in the subdirectory to count the number of file */
int count_sub_file(char *p, unsigned int first_logical_cluster, unsigned int parent_cluster, unsigned int bytes_per_sector){
    unsigned int attribute = 0, num_of_file = 0, new_cluster = 0;
    unsigned int free = 0x00, unused = 0xE5;

    for (int i = 0; i < ((MAX_SECTOR - first_logical_cluster - 31) * bytes_per_sector); i += 32) {

        if (memcmp(&free, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i), 1) == 0) {
            return num_of_file;
        } 
        else if (memcmp(&unused, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i), 1) == 0){
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i + 11), 1);

        if (0x00 == attribute){ // file
            num_of_file++;
        } 
        else if (0x0F == (attribute & 0x0F)){ // long file name
            continue;
        } 
        else if (0x10 == (attribute & 0x10)){ // subdirectory
            memcpy(&new_cluster, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i + 26), 2);

            if ((new_cluster != 0) && (new_cluster != 1)) {
                if ((new_cluster != first_logical_cluster) && (new_cluster != parent_cluster)) {
                    num_of_file += count_sub_file(p, new_cluster, first_logical_cluster, bytes_per_sector);
                }
            }
        }
    }
    return num_of_file;
}