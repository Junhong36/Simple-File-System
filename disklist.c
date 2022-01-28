#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_DIRECTORY 224
#define MAX_SECTOR 2879
#define MAX_PATH 10*8
#define MAX_LENGTH 20
#define timeOffset 14 //offset of creation time in directory entry
#define dateOffset 16 //offset of creation date in directory entry

void disklist(char *p);
void loop_sub_file(char *p, unsigned int first_logical_cluster, unsigned int parent_cluster, char *path);
void display_content(char *p, unsigned int first_logical_cluster, char flag, char *directory_name);

static unsigned int bytes_per_sector;

int main(int argc, char *argv[])
{

    int fd;
    struct stat sb;

    if (argc != 2) {
        printf("\nUsage: ./disklist diskimage\n\n");
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

    disklist(p);

    // the modifed the memory data would be mapped to the disk image
    munmap(p, sb.st_size);
    close(fd);
    return 0;
}

void disklist(char *p){
    unsigned int attribute = 0, num_of_directory = 0, first_logical_cluster = 0;
    unsigned int max_root_entry = 0;
    unsigned int free = 0x00, unused = 0xE5;
    char flag = '\0', directory_list[MAX_DIRECTORY][MAX_LENGTH] = {{0}}, directory_name[9]={0};

    memcpy(&max_root_entry, (p + 17), 2);
    memcpy(&bytes_per_sector, (p + 11), 2);

    printf("\nRoot\n");
    printf("================\n");

    for (int i = 0; i < (max_root_entry * 32); i += 32) {
        if (memcmp(&free, (p + bytes_per_sector * 19 + i), 1) == 0) {
            break;
        } else if (memcmp(&unused, (p + bytes_per_sector * 19 + i), 1) == 0) {
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * 19 + i + 11), 1);

        if (0x00 == attribute) { // file
            flag = 'F';
            display_content(p, (bytes_per_sector * 19 + i), flag, directory_name);
        } 
        else if (0x08 == attribute) { // volume label of the image
            continue;
        } 
        else if (0x0F == (attribute & 0x0F)) { // long file name
            continue;
        } 
        else if (0x10 == (attribute & 0x10)) { // subdirectory
            char directory_name[9]={0};

            memcpy(&first_logical_cluster, (p + bytes_per_sector * 19 + i + 26), 2);

            flag = 'D';
            display_content(p, (bytes_per_sector * 19 + i), flag, directory_name);

            sprintf(directory_list[num_of_directory], "%s %u", directory_name, first_logical_cluster);
            num_of_directory++;
        }
    }

    for (int i = 0; i < num_of_directory; i++){
        char path[MAX_PATH] = {0};

        sscanf(directory_list[i], "%s %u", path, &first_logical_cluster);
        loop_sub_file(p, first_logical_cluster, first_logical_cluster, path);
    }

    printf("\n");
}

/* a recursive function to loop into each subdirectory and print out its detail */
void loop_sub_file(char *p, unsigned int first_logical_cluster, unsigned int parent_cluster, char *path){
    unsigned int attribute = 0, num_of_directory = 0, new_cluster = 0;
    unsigned int free = 0x00, unused = 0xE5;
    char flag = '\0',directory_list[MAX_DIRECTORY][MAX_LENGTH] = {{0}}, directory_name[9]={0};
    int depth = 0;

    printf("\n/%s\n", path);
    printf("================\n");

    for (int i = 0; i < ((MAX_SECTOR - first_logical_cluster - 31) * bytes_per_sector); i += 32) {

        if (memcmp(&free, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i), 1) == 0) {
            break;
        } 
        else if (memcmp(&unused, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i), 1) == 0){
            continue;
        }

        memcpy(&attribute, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i + 11), 1);

        if (0x00 == attribute){   // file
            flag = 'F';
            display_content(p, (bytes_per_sector * (33 + first_logical_cluster - 2) + i), flag, directory_name);
        } 
        else if (0x0F == (attribute & 0x0F)){  // long file name
            continue;
        } 
        else if (0x10 == (attribute & 0x10)){  // subdirectory
            memcpy(&new_cluster, (p + bytes_per_sector * (33 + first_logical_cluster - 2) + i + 26), 2);

            /* when read a subdirectory store its name and first logical sector into a list */
            if ((new_cluster != 0) && (new_cluster != 1)) {
                if ((new_cluster != first_logical_cluster) && (new_cluster != parent_cluster)) {
                    flag = 'D';
                    display_content(p, (bytes_per_sector * (33 + first_logical_cluster - 2) + i), flag, directory_name);

                    sprintf(directory_list[num_of_directory], "/%s %u", directory_name, new_cluster);
                    num_of_directory++;
                }
            }
        }
    }

    depth = strlen(path);
    
    /* dive in the subdirectory that read before one by one */
    for (int i = 0; i < num_of_directory; i++) {
        char tmp1[9]={0};

        sscanf(directory_list[i], "%s %u", tmp1, &new_cluster);
        memcpy((path + depth), tmp1, (strlen(tmp1) + 1));

        loop_sub_file(p, new_cluster, first_logical_cluster, path);
    }
}

/* print out the details of each directory */
void display_content(char *p, unsigned int first_logical_cluster, char flag, char *directory_name){
    char file_name[9] = {0}, extension[4] = {0};
    unsigned int size_of_file = 0;
    int time, date;
    int hours, minutes, day, month, year;
    char *str_month[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    if(flag=='F'){
        memcpy(file_name, (p + first_logical_cluster), 8);
        sscanf(file_name, "%s", file_name);
    }
    else{
        memcpy(directory_name, (p + first_logical_cluster), 8);
        sscanf(directory_name, "%s", directory_name);
    }


    memcpy(extension, (p + first_logical_cluster + 8), 3);
    sscanf(extension, "%s", extension);

    memcpy(&size_of_file, (p + first_logical_cluster + 28), 4);

    if(flag=='F'){
        memcpy(&time, (p + first_logical_cluster + timeOffset), 2);
        memcpy(&date, (p + first_logical_cluster + dateOffset), 2);
    }
    else{
        memcpy(&time, (p + first_logical_cluster + 22), 2);
        memcpy(&date, (p + first_logical_cluster + 24), 2);
    }

    //the year is stored as a value since 1980
    //the year is stored in the high seven bits
    year = ((date & 0xFE00) >> 9) + 1980;
    //the month is stored in the middle four bits
    month = (date & 0x1E0) >> 5;
    //the day is stored in the low five bits
    day = (date & 0x1F);

    //the hours are stored in the high five bits
    hours = (time & 0xF800) >> 11;
    //the minutes are stored in the middle 6 bits
    minutes = (time & 0x7E0) >> 5;

    if(flag=='F'){
        printf("%c %9u %10s.%-5s %10s %02d %d %02d:%02d\n",
               flag, size_of_file, file_name, extension, str_month[month - 1], day, year, hours, minutes);
    }
    else{
        printf("%c %9u %13s%-5s %8s %02d %d %02d:%02d\n",
               flag, size_of_file, directory_name, extension, str_month[month - 1], day, year, hours, minutes);
    }

    return;
}
