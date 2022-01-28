#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FAT_ENTRY 2848 // Max FAT entry number

void diskget(char *p, char *file);

static unsigned int bytes_per_sector;

int main(int argc, char *argv[])
{

    int fd;
    struct stat sb;

    if (argc != 3) {
        printf("\nUsage: ./diskget diskimage\n\n");
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

    diskget(p, argv[2]);

    // the modifed the memory data would be mapped to the disk image
    munmap(p, sb.st_size);
    close(fd);
    return 0;
}

void diskget(char *p, char *file){
    unsigned int attribute = 0, first_logical_cluster = 0, size_of_file = 0;
    unsigned int max_root_entry = 0;
    unsigned int free = 0x00, unused = 0xE5;
    unsigned int mask_front = 0x000FFF, mask_end = 0xFFF000;
    char file_name[9] = {0}, extension[4] = {0}, full_name[13] = {0};
    char tmp_file[13] = {0};
    char *tmp=NULL;

    /* convert file name to upper case letter */
    sscanf(file, "%s", tmp_file);

    tmp = tmp_file;
    while(*tmp){
        *tmp = toupper((unsigned int)*tmp);
        tmp++;
    }
    file = tmp_file;

    memcpy(&max_root_entry, (p + 17), 2);
    memcpy(&bytes_per_sector, (p + 11), 2);

    /* loop through the root directory */
    for (int i = 0; i <= (max_root_entry * 32); i += 32) {

        if ((memcmp(&free, (p + bytes_per_sector * 19 + i), 1) == 0) || (i == (max_root_entry * 32))) {
            printf("\nFile not found.\n\n");
            exit(1);
        } else if (memcmp(&unused, (p + bytes_per_sector * 19 + i), 1) == 0) {
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * 19 + i + 11), 1);

        if (0x00 == attribute) { // file
           
            unsigned int next_logical_cluster = 0;
            FILE *fptr;

            memcpy(file_name, (p + bytes_per_sector * 19 + i), 8);
            sscanf(file_name, "%s", file_name);

            memcpy(extension, (p + bytes_per_sector * 19 + i + 8), 3);
            sscanf(extension, "%s", extension);

            sprintf(full_name, "%s.%s", file_name, extension);

            if ((memcmp(full_name, file, strlen(full_name)) != 0) || (strlen(full_name) != strlen(file))) {
                continue;
            }

            memcpy(&first_logical_cluster, (p + bytes_per_sector * 19 + i + 26), 2);
            memcpy(&size_of_file, (p + bytes_per_sector * 19 + i + 28), 4);

            next_logical_cluster = first_logical_cluster;

            fptr = fopen(file, "wb");
            if (fptr == NULL) {
                printf("\nFile cannot be created!\n\n");
                exit(1);
            }

            /* each time read next logical sector of the matching file from FAT table
               then write into the created file until remain file size is zero */
            for (unsigned int remain_size = size_of_file; remain_size > 0; remain_size -= bytes_per_sector) {

                unsigned int tmp = 0;
                
                if (next_logical_cluster > MAX_FAT_ENTRY) {
                    printf("\nExceed file size\n\n");
                    exit(1);
                }

                if (remain_size <= bytes_per_sector) {
                    fwrite((p + bytes_per_sector * (33 + next_logical_cluster - 2)), 1, remain_size, fptr);
                    break;
                }

                fwrite((p + bytes_per_sector * (33 + next_logical_cluster - 2)), 1, bytes_per_sector, fptr);

                if ((next_logical_cluster % 2) == 0) {
                    memcpy(&tmp, (p + bytes_per_sector + next_logical_cluster / 2 * 3), 3);
                    next_logical_cluster = tmp & mask_front;
                }
                else {
                    memcpy(&tmp, (p + bytes_per_sector + (next_logical_cluster - 1) / 2 * 3), 3);
                    next_logical_cluster = ((tmp & mask_end) >> 12);
                }
            }

            fclose(fptr);
            break;
        } 
    }
}