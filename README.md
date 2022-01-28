# Simple-File-System
perform operations on a simple file system, FAT12, used by MS-DOS.

use make to compile all the 4 programs
diskinfo.c, disklist.c, diskget.c, diskput.c

## usage:

diskinfo.c：print out basic information of the image file
`./diskinfo imagefile`

disklist.c：listing all the files and directories of the image file
`./disklist imagefile`

diskget.c：copy a file from the image file
`./diskget imagefile filename`

diskput.c：copy a file into the specific path of the image file
`./diskput imagefile path+filename`
e.g. `./diskput disk.ima /sub1/sub2/foo.txt`
