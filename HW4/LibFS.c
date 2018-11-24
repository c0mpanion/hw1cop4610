#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "LibDisk.h"
#include "LibFS.h"

// set to 1 to have detailed debug print-outs and 0 to have none
#define FSDEBUG 0
#define BYTE 8
#if FSDEBUG
#define dprintf printf
#else
#define dprintf noprintf

void noprintf(char *str, ...) {}

#endif

// the file system partitions the disk into five parts:

// 1. the superblock (one sector), which contains a magic number at
// its first four bytes (integer)
#define SUPERBLOCK_START_SECTOR 0

// the magic number chosen for our file system
#define OS_MAGIC 0xdeadbeef

// 2. the inode bitmap (one or more sectors), which indicates whether
// the particular entry in the inode table (#4) is currently in use
#define INODE_BITMAP_START_SECTOR 1

// the total number of bytes and sectors needed for the inode bitmap;
// we use one bit for each inode (whether it's a file or directory) to
// indicate whether the particular inode in the inode table is in use
#define INODE_BITMAP_SIZE ((MAX_FILES+7)/8)
#define INODE_BITMAP_SECTORS ((INODE_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)

// 3. the sector bitmap (one or more sectors), which indicates whether
// the particular sector in the disk is currently in use
#define SECTOR_BITMAP_START_SECTOR (INODE_BITMAP_START_SECTOR+INODE_BITMAP_SECTORS)

// the total number of bytes and sectors needed for the data block
// bitmap (we call it the sector bitmap); we use one bit for each
// sector of the disk to indicate whether the sector is in use or not
#define SECTOR_BITMAP_SIZE ((TOTAL_SECTORS+7)/8)
#define SECTOR_BITMAP_SECTORS ((SECTOR_BITMAP_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE)

// 4. the inode table (one or more sectors), which contains the inodes
// stored consecutively
#define INODE_TABLE_START_SECTOR (SECTOR_BITMAP_START_SECTOR+SECTOR_BITMAP_SECTORS)


// an inode is used to represent each file or directory; the data
// structure supposedly contains all necessary information about the
// corresponding file or directory
typedef struct _inode {
    int size; // the size of the file or number of directory entries
    int type; // 0 means regular file; 1 means directory
    int data[MAX_SECTORS_PER_FILE]; // indices to sectors containing data blocks
} inode_t;

// the inode structures are stored consecutively and yet they don't
// straddle accross the sector boundaries; that is, there may be
// fragmentation towards the end of each sector used by the inode
// table; each entry of the inode table is an inode structure; there
// are as many entries in the table as the number of files allowed in
// the system; the inode bitmap (#2) indicates whether the entries are
// current in use or not
#define INODES_PER_SECTOR (SECTOR_SIZE/sizeof(inode_t))
#define INODE_TABLE_SECTORS ((MAX_FILES+INODES_PER_SECTOR-1)/INODES_PER_SECTOR)

// 5. the data blocks; all the rest sectors are reserved for data
// blocks for the content of files and directories
#define DATABLOCK_START_SECTOR (INODE_TABLE_START_SECTOR+INODE_TABLE_SECTORS)

// other file related definitions

// max length of a path is 256 bytes (including the ending null)
#define MAX_PATH 256

// max length of a filename is 16 bytes (including the ending null)
#define MAX_NAME 16

// max number of open files is 256
#define MAX_OPEN_FILES 256

// each directory entry represents a file/directory in the parent
// directory, and consists of a file/directory name (less than 16
// bytes) and an integer inode number
typedef struct _dirent {
    char fname[MAX_NAME]; // name of the file
    int inode; // inode of the file
} dirent_t;

// the number of directory entries that can be contained in a sector
#define DIRENTS_PER_SECTOR (SECTOR_SIZE/sizeof(dirent_t))

// global errno value here
int osErrno;

// the name of the disk backstore file (with which the file system is booted)
static char bs_filename[1024];

/* the following functions are internal helper functions */
inode_t *loadInode(int inode_index);

// check magic number in the superblock; return 1 if OK, and 0 if not
static int check_magic() {
    char buf[SECTOR_SIZE];
    if (Disk_Read(SUPERBLOCK_START_SECTOR, buf) < 0)
        return 0;
    if (*(int *) buf == OS_MAGIC) return 1;
    else return 0;
}

// initialize a bitmap with 'num' sectors starting from 'start'
// sector; all bits should be set to zero except that the first
// 'nbits' number of bits are set to one
static void bitmap_init(int start, int num, int nbits) {
    char *buffer = calloc(SECTOR_SIZE, sizeof(char));
    unsigned char allOnes = 255;
    int bitsInBuffer = SECTOR_SIZE * 8;

    for (int i = 0; i < num; i++) {
        // if bits to be set to one is greater than total bits in buffer
        if (nbits >= bitsInBuffer) {
            //set all bits to one
            memset(buffer, allOnes, SECTOR_SIZE);
            nbits = nbits - bitsInBuffer;
        } else if (nbits > 0) {
            // set only necessary bits
            free(buffer);
            buffer = calloc(SECTOR_SIZE, sizeof(char));
            int numOfCharsToSet = nbits / 8;

            memset(buffer, allOnes, (size_t) numOfCharsToSet);
            nbits = nbits - (numOfCharsToSet * 8);

            // set remaining bits to one
            for (int j = 0; j < 7; j++) {
                if (nbits) {
                    buffer[numOfCharsToSet] |= 1;
                    nbits--;
                }
                buffer[numOfCharsToSet] = buffer[numOfCharsToSet] << 1;
            }
        }
        Disk_Write(start + i, buffer);
    }
}

// set the first unused bit from a bitmap of 'nbits' bits (flip the
// first zero appeared in the bitmap to one) and return its location;
// return -1 if the bitmap is already full (no more zeros)
static int bitmap_first_unused(int start, int num, int nbits) {
    char buffer[SECTOR_SIZE];
    // read each sector until zero bit it's found
    for (int i = 0; i < num; i++) {
        Disk_Read(start + i, buffer);
        // iterate over each char
        for (int j = 0; j < SECTOR_SIZE; j++) {
            // iterate over each bit on the char and track
            // how many bits have been checked so far
            for (int k = 7; k >= 0 && nbits > 0; k--) {
                int zero = (buffer[j] >> k) & 1;
                nbits--;
                // found a zero
                if (!zero) {
                    int mask = 1;
                    mask = mask << k;
                    buffer[j] |= mask; // set bit to 1
                    Disk_Write(start + i, buffer);
                    // return position
                    return (i * SECTOR_SIZE * 8) + (j * 8) + (7 - k);
                }
            }
        }
    }
    return -1;
}

// reset the i-th bit of a bitmap with 'num' sectors starting from
// 'start' sector; return 0 if successful, -1 otherwise
static int bitmap_reset(int start, int num, int ibit) {
    char buffer[SECTOR_SIZE];

    int sector = ibit / (SECTOR_SIZE * BYTE); // get sector
    int byte = (ibit - (sector * (SECTOR_SIZE * BYTE))) / BYTE; // get byte/char position
    int bit = ibit - (byte * BYTE) - (sector * (SECTOR_SIZE * BYTE)); // get bit specific position

    // check if ibit is within boundaries
    if (sector >= num) {
        return -1;
    }

    Disk_Read(start + sector, buffer);
    // clear i-th bit
    buffer[byte] &= ~(1 << (7 - bit));
    // write changes to disk
    return Disk_Write(start + sector, buffer);
}

// return 1 if the file name is illegal; otherwise, return 0; legal
// characters for a file name include letters (case sensitive),
// numbers, dots, dashes, and underscores; and a legal file name
// should not be more than MAX_NAME-1 in length
static int illegal_filename(char *name) {
    // if name larger than allowed or empty string
    if ((strlen(name) > MAX_NAME - 1) || strlen(name) == 0) {
        return 1;
    }
    for (int i = 0; i < strlen(name); i++) {
        char c = name[i];
        // check if valid char
        if (!((c >= '0' && c <= '9') || (c == '/') ||
              (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c == '.' || c == '-' || c == '_'))) {
            return 1;
        }
    }
    return 0;
}

// return the child inode of the given file name 'fname' from the
// parent inode; the parent inode is currently stored in the segment
// of inode table in the cache (we cache only one disk sector for
// this); once found, both cached_inode_sector and cached_inode_buffer
// may be updated to point to the segment of inode table containing
// the child inode; the function returns -1 if no such file is found;
// it returns -2 is something else is wrong (such as parent is not
// directory, or there's read error, etc.)
static int find_child_inode(int parent_inode, char *fname,
                            int *cached_inode_sector, char *cached_inode_buffer) {
    int cached_start_entry = ((*cached_inode_sector) - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    int offset = parent_inode - cached_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *parent = (inode_t *) (cached_inode_buffer + offset * sizeof(inode_t));
    dprintf("... load parent inode: %d (size=%d, type=%d)\n",
            parent_inode, parent->size, parent->type);
    if (parent->type != 1) {
        dprintf("... parent not a directory\n");
        return -2;
    }

    int nentries = parent->size; // remaining number of directory entries
    int idx = 0;
    while (nentries > 0) {
        char buf[SECTOR_SIZE]; // cached content of directory entries
        if (Disk_Read(parent->data[idx], buf) < 0) return -2;
        for (int i = 0; i < DIRENTS_PER_SECTOR; i++) {
            if (i > nentries) break;
            if (!strcmp(((dirent_t *) buf)[i].fname, fname)) {
                // found the file/directory; update inode cache
                int child_inode = ((dirent_t *) buf)[i].inode;
                dprintf("... found child_inode=%d\n", child_inode);
                int sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
                if (sector != (*cached_inode_sector)) {
                    *cached_inode_sector = sector;
                    if (Disk_Read(sector, cached_inode_buffer) < 0) return -2;
                    dprintf("... load inode table for child\n");
                }
                return child_inode;
            }
        }
        idx++;
        nentries -= DIRENTS_PER_SECTOR;
    }
    dprintf("... could not find child inode\n");
    return -1; // not found
}

// follow the absolute path; if successful, return the inode of the
// parent directory immediately before the last file/directory in the
// path; for example, for '/a/b/c/d.txt', the parent is '/a/b/c' and
// the child is 'd.txt'; the child's inode is returned through the
// parameter 'last_inode' and its file name is returned through the
// parameter 'last_fname' (both are references); it's possible that
// the last file/directory is not in its parent directory, in which
// case, 'last_inode' points to -1; if the function returns -1, it
// means that we cannot follow the path
static int follow_path(char *path, int *last_inode, char *last_fname) {
    if (!path) {
        dprintf("... invalid path\n");
        return -1;
    }
    if (path[0] != '/') {
        dprintf("... '%s' not absolute path\n", path);
        return -1;
    }

    // make a copy of the path (skip leading '/'); this is necessary
    // since the path is going to be modified by strsep()
    char pathstore[MAX_PATH];
    strncpy(pathstore, path + 1, MAX_PATH - 1);
    pathstore[MAX_PATH - 1] = '\0'; // for safety
    char *lpath = pathstore;

    int parent_inode = -1, child_inode = 0; // start from root
    // cache the disk sector containing the root inode
    int cached_sector = INODE_TABLE_START_SECTOR;
    char cached_buffer[SECTOR_SIZE];
    if (Disk_Read(cached_sector, cached_buffer) < 0) return -1;
    dprintf("... load inode table for root from disk sector %d\n", cached_sector);

    // for each file/directory name separated by '/'
    char *token;
    while ((token = strsep(&lpath, "/")) != NULL) {
        dprintf("... process token: '%s'\n", token);
        if (*token == '\0') continue; // multiple '/' ignored
        if (illegal_filename(token)) {
            dprintf("... illegal file name: '%s'\n", token);
            return -1;
        }
        if (child_inode < 0) {
            // regardless whether child_inode was not found previously, or
            // there was issues related to the parent (say, not a
            // directory), or there was a read error, we abort
            dprintf("... parent inode can't be established\n");
            return -1;
        }
        parent_inode = child_inode;
        child_inode = find_child_inode(parent_inode, token,
                                       &cached_sector, cached_buffer);
        if (last_fname) strcpy(last_fname, token);
    }
    if (child_inode < -1) return -1; // if there was error, abort
    else {
        // there was no error, several possibilities:
        // 1) '/': parent = -1, child = 0
        // 2) '/valid-dirs.../last-valid-dir/not-found': parent=last-valid-dir, child=-1
        // 3) '/valid-dirs.../last-valid-dir/found: parent=last-valid-dir, child=found
        // in the first case, we set parent=child=0 as special case
        if (parent_inode == -1 && child_inode == 0) parent_inode = 0;
        dprintf("... found parent_inode=%d, child_inode=%d\n", parent_inode, child_inode);
        *last_inode = child_inode;
        return parent_inode;
    }
}

// add a new file or directory (determined by 'type') of given name
// 'file' under parent directory represented by 'parent_inode'
int add_inode(int type, int parent_inode, char *file) {
    // get a new inode for child
    int child_inode = bitmap_first_unused(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, INODE_BITMAP_SIZE);
    if (child_inode < 0) {
        dprintf("... error: inode table is full\n");
        return -1;
    }
    dprintf("... new child inode %d\n", child_inode);

    // load the disk sector containing the child inode
    int inode_sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if (Disk_Read(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);

    // get the child inode
    int inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    int offset = child_inode - inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *child = (inode_t *) (inode_buffer + offset * sizeof(inode_t));

    // update the new child inode and write to disk
    memset(child, 0, sizeof(inode_t));
    child->type = type;
    if (Disk_Write(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... update child inode %d (size=%d, type=%d), update disk sector %d\n",
            child_inode, child->size, child->type, inode_sector);

    // get the disk sector containing the parent inode
    inode_sector = INODE_TABLE_START_SECTOR + parent_inode / INODES_PER_SECTOR;
    if (Disk_Read(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... load inode table for parent inode %d from disk sector %d\n",
            parent_inode, inode_sector);

    // get the parent inode
    inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    offset = parent_inode - inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *parent = (inode_t *) (inode_buffer + offset * sizeof(inode_t));
    dprintf("... get parent inode %d (size=%d, type=%d)\n",
            parent_inode, parent->size, parent->type);

    // get the dirent sector
    if (parent->type != 1) {
        dprintf("... error: parent inode is not directory\n");
        return -2; // parent not directory
    }
    int group = parent->size / DIRENTS_PER_SECTOR;
    char dirent_buffer[SECTOR_SIZE];
    if (group * DIRENTS_PER_SECTOR == parent->size) {
        // new disk sector is needed
        int newsec = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);
        if (newsec < 0) {
            dprintf("... error: disk is full\n");
            return -1;
        }
        parent->data[group] = newsec;
        memset(dirent_buffer, 0, SECTOR_SIZE);
        dprintf("... new disk sector %d for dirent group %d\n", newsec, group);
    } else {
        if (Disk_Read(parent->data[group], dirent_buffer) < 0)
            return -1;
        dprintf("... load disk sector %d for dirent group %d\n", parent->data[group], group);
    }

    // add the dirent and write to disk
    int start_entry = group * DIRENTS_PER_SECTOR;
    offset = parent->size - start_entry;
    dirent_t *dirent = (dirent_t *) (dirent_buffer + offset * sizeof(dirent_t));
    strncpy(dirent->fname, file, MAX_NAME);
    dirent->inode = child_inode;
    if (Disk_Write(parent->data[group], dirent_buffer) < 0) return -1;
    dprintf("... append dirent %d (name='%s', inode=%d) to group %d, update disk sector %d\n",
            parent->size, dirent->fname, dirent->inode, group, parent->data[group]);

    // update parent inode and write to disk
    parent->size++;
    if (Disk_Write(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... update parent inode on disk sector %d\n", inode_sector);

    return 0;
}

// used by both File_Create() and Dir_Create(); type=0 is file, type=1
// is directory
int create_file_or_directory(int type, char *pathname) {
    int child_inode;
    char last_fname[MAX_NAME];
    int parent_inode = follow_path(pathname, &child_inode, last_fname);
    if (parent_inode >= 0) {
        if (child_inode >= 0) {
            dprintf("... file/directory '%s' already exists, failed to create\n", pathname);
            osErrno = E_CREATE;
            return -1;
        } else {
            if (add_inode(type, parent_inode, last_fname) >= 0) {
                dprintf("... successfully created file/directory: '%s'\n", pathname);
                return 0;
            } else {
                dprintf("... error: something wrong with adding child inode\n");
                osErrno = E_CREATE;
                return -1;
            }
        }
    } else {
        dprintf("... error: something wrong with the file/path: '%s'\n", pathname);
        osErrno = E_CREATE;
        return -1;
    }
}

// remove the child from parent; the function is called by both
// File_Unlink() and Dir_Unlink(); the function returns 0 if success,
// -1 if general error, -2 if directory not empty, -3 if wrong type
int remove_inode(int type, int parent_inode, int child_inode) {
    // TODO remove_inode
    // load the disk sector containing the child inode
    int inode_sector = INODE_TABLE_START_SECTOR + child_inode / INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if (Disk_Read(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... load inode table for child inode from disk sector %d\n", inode_sector);

    // get the child inode
    int inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    int offset = child_inode - inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *child = (inode_t *) (inode_buffer + offset * sizeof(inode_t));

    //check type and check for empty directory
    if (child->type != type) {
        return -3;
    } else if (child->type && child->size) { return -2; }

    // remove all data related to the file
    for (int i = 0; i < MAX_SECTORS_PER_FILE; ++i) {
        if (child->data[i]) {
            char clearingBuffer[SECTOR_SIZE];
            Disk_Read(child->data[i], clearingBuffer);
            memset(clearingBuffer, 0, SECTOR_SIZE);
            Disk_Write(child->data[i], clearingBuffer);
            bitmap_reset(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, child->data[i]);
        }
    }
    //remove the child inode
    memset(child, 0, sizeof(inode_t));
    if (Disk_Write(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... update child inode %d (size=%d, type=%d), update disk sector %d\n",
            child_inode, child->size, child->type, inode_sector);
    bitmap_reset(INODE_TABLE_START_SECTOR, INODE_BITMAP_SECTORS, child_inode);


    // get the disk sector containing the parent inode
    inode_sector = INODE_TABLE_START_SECTOR + parent_inode / INODES_PER_SECTOR;
    if (Disk_Read(inode_sector, inode_buffer) < 0) return -1;
    dprintf("... load inode table for parent inode %d from disk sector %d\n",
            parent_inode, inode_sector);

    // get the parent inode
    inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    offset = parent_inode - inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *parent = (inode_t *) (inode_buffer + offset * sizeof(inode_t));
    dprintf("... get parent inode %d (size=%d, type=%d)\n",
            parent_inode, parent->size, parent->type);

    if (parent->type != 1) {
        dprintf("... error: parent inode is not directory\n");
        return -2; // parent not directory
    }

    // get the dirent sectors and find child dirent
    char dirent_buffer[SECTOR_SIZE];
    for (int j = 0; j < MAX_SECTORS_PER_FILE; j++) {
        if (parent->data[j]) {
            if (Disk_Read(parent->data[j], dirent_buffer) < 0) { return -1; }
            dprintf("... load disk sector %d for dirent group %d\n", parent->data[j], j + 1);

            for (int k = 0; k < DIRENTS_PER_SECTOR; k++) {
                dirent_t *dirent = (dirent_t *) (dirent_buffer + (k * sizeof(dirent_t)));
                // found child?, remove child dirent
                if (dirent->inode == child_inode) {
                    dprintf("... found match: dirent inode %d, child inode %d\n", dirent->inode, child_inode);
                    memset(dirent, 0, sizeof(dirent_t));
                    if (Disk_Write(parent->data[j], dirent_buffer) < 0) { return -1; }
                    parent->size--;
                    if (Disk_Write(inode_sector, inode_buffer) < 0) return -1;
                    dprintf("... update parent inode on disk sector %d\n", inode_sector);
                    return 0;
                }
            }
        }
    }
    return -1;
}

// representing an open file
typedef struct _open_file {
    int inode; // pointing to the inode of the file (0 means entry not used)
    int size;  // file size cached here for convenience
    int pos;   // read/write position
} open_file_t;
static open_file_t open_files[MAX_OPEN_FILES];

// return true if the file pointed to by inode has already been open
int is_file_open(int inode) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].inode == inode)
            return 1;
    }
    return 0;
}

// return a new file descriptor not used; -1 if full
int new_file_fd() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].inode <= 0)
            return i;
    }
    return -1;
}

/* end of internal helper functions, start of API functions */

int FS_Boot(char *backstore_fname) {
    dprintf("FS_Boot('%s'):\n", backstore_fname);
    // initialize a new disk (this is a simulated disk)
    if (Disk_Init() < 0) {
        dprintf("... disk init failed\n");
        osErrno = E_GENERAL;
        return -1;
    }
    dprintf("... disk initialized\n");

    // we should copy the filename down; if not, the user may change the
    // content pointed to by 'backstore_fname' after calling this function
    strncpy(bs_filename, backstore_fname, 1024);
    bs_filename[1023] = '\0'; // for safety

    // we first try to load disk from this file
    if (Disk_Load(bs_filename) < 0) {
        dprintf("... load disk from file '%s' failed\n", bs_filename);

        // if we can't open the file; it means the file does not exist, we
        // need to create a new file system on disk
        if (diskErrno == E_OPENING_FILE) {
            dprintf("... couldn't open file, create new file system\n");

            // format superblock
            char buf[SECTOR_SIZE];
            memset(buf, 0, SECTOR_SIZE);
            *(int *) buf = OS_MAGIC;
            if (Disk_Write(SUPERBLOCK_START_SECTOR, buf) < 0) {
                dprintf("... failed to format superblock\n");
                osErrno = E_GENERAL;
                return -1;
            }
            dprintf("... formatted superblock (sector %d)\n", SUPERBLOCK_START_SECTOR);

            // format inode bitmap (reserve the first inode to root)
            bitmap_init(INODE_BITMAP_START_SECTOR, INODE_BITMAP_SECTORS, 1);
            dprintf("... formatted inode bitmap (start=%d, num=%d)\n",
                    (int) INODE_BITMAP_START_SECTOR, (int) INODE_BITMAP_SECTORS);

            // format sector bitmap (reserve the first few sectors to
            // superblock, inode bitmap, sector bitmap, and inode table)
            bitmap_init(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS,
                        DATABLOCK_START_SECTOR);
            dprintf("... formatted sector bitmap (start=%d, num=%d)\n",
                    (int) SECTOR_BITMAP_START_SECTOR, (int) SECTOR_BITMAP_SECTORS);

            // format inode tables
            for (int i = 0; i < INODE_TABLE_SECTORS; i++) {
                memset(buf, 0, SECTOR_SIZE);
                if (i == 0) {
                    // the first inode table entry is the root directory
                    ((inode_t *) buf)->size = 0;
                    ((inode_t *) buf)->type = 1;
                }
                if (Disk_Write(INODE_TABLE_START_SECTOR + i, buf) < 0) {
                    dprintf("... failed to format inode table\n");
                    osErrno = E_GENERAL;
                    return -1;
                }
            }
            dprintf("... formatted inode table (start=%d, num=%d)\n",
                    (int) INODE_TABLE_START_SECTOR, (int) INODE_TABLE_SECTORS);

            // we need to synchronize the disk to the backstore file (so
            // that we don't lose the formatted disk)
            if (Disk_Save(bs_filename) < 0) {
                // if can't write to file, something's wrong with the backstore
                dprintf("... failed to save disk to file '%s'\n", bs_filename);
                osErrno = E_GENERAL;
                return -1;
            } else {
                // everything's good now, boot is successful
                dprintf("... successfully formatted disk, boot successful\n");
                memset(open_files, 0, MAX_OPEN_FILES * sizeof(open_file_t));
                return 0;
            }
        } else {
            // something wrong loading the file: invalid param or error reading
            dprintf("... couldn't read file '%s', boot failed\n", bs_filename);
            osErrno = E_GENERAL;
            return -1;
        }
    } else {
        dprintf("... load disk from file '%s' successful\n", bs_filename);

        // we successfully loaded the disk, we need to do two more checks,
        // first the file size must be exactly the size as expected (thiis
        // supposedly should be folded in Disk_Load(); and it's not)
        int sz = 0;
        FILE *f = fopen(bs_filename, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            sz = ftell(f);
            fclose(f);
        }
        if (sz != SECTOR_SIZE * TOTAL_SECTORS) {
            dprintf("... check size of file '%s' failed\n", bs_filename);
            osErrno = E_GENERAL;
            return -1;
        }
        dprintf("... check size of file '%s' successful\n", bs_filename);

        // check magic
        if (check_magic()) {
            // everything's good by now, boot is successful
            dprintf("... check magic successful\n");
            memset(open_files, 0, MAX_OPEN_FILES * sizeof(open_file_t));
            return 0;
        } else {
            // mismatched magic number
            dprintf("... check magic failed, boot failed\n");
            osErrno = E_GENERAL;
            return -1;
        }
    }
}

int FS_Sync() {
    if (Disk_Save(bs_filename) < 0) {
        // if can't write to file, something's wrong with the backstore
        dprintf("FS_Sync():\n... failed to save disk to file '%s'\n", bs_filename);
        osErrno = E_GENERAL;
        return -1;
    } else {
        // everything's good now, sync is successful
        dprintf("FS_Sync():\n... successfully saved disk to file '%s'\n", bs_filename);
        return 0;
    }
}

int File_Create(char *file) {
    dprintf("File_Create('%s'):\n", file);
    return create_file_or_directory(0, file);
}

int delete_file_or_dir(int type, char *pathname) {
    int child_inode;
    char last_fname[MAX_NAME];
    int parent_inode = follow_path(pathname, &child_inode, last_fname);

    // if it is a file and is open
    if (!type && is_file_open(child_inode)) {
        dprintf("... file '%s' is currently open\n", last_fname);
        osErrno = E_FILE_IN_USE;
        return -1;
    }

    if (parent_inode >= 0) {
        if (child_inode >= 0) {
            int operation = remove_inode(type, parent_inode, child_inode);
            if (!operation) {
                dprintf("... file/directory '%s' successfully Unlinked\n", pathname);
                // successful removal
                return 0;
            } else {
                if (operation == -2) {
                    dprintf("... directory '%s' is not empty.\n", pathname);
                    osErrno = E_DIR_NOT_EMPTY;
                } else if (operation == -3) {
                    dprintf("... wrong type '%s'.\n", pathname);
                    osErrno = E_GENERAL;
                } else {
                    dprintf("... file/directory '%s' unable to Unlink\n", pathname);
                    osErrno = E_GENERAL;
                }
                return -1;
            }
        } else {
            dprintf("... file/directory '%s' does not exists.\n", pathname);
            if (type) { osErrno = E_NO_SUCH_DIR; }
            else { osErrno = E_NO_SUCH_FILE; }
            return -1;
        }
    } else {
        dprintf("... error: something wrong with the file/path: '%s'\n", pathname);
        osErrno = E_GENERAL;
        return -1;
    }
}

/**
 * This function is the opposite of File_Create(). This function should delete the file
 * referenced by file, including removing its name from the directory it is in, and
 * freeing up any data blocks and inodes that the file has been using. If the file does
 * not currently exist, return -1 and set osErrno to E_NO_SUCH_FILE. If the file is
 * currently open, return -1 and set osErrno to E_FILE_IN_USE (and do NOT
 * delete the file). Upon success, return 0.
 * @param file
 * @return
 */

int File_Unlink(char *file) {
    // TODO File_Unlink
    dprintf("File_Unlink ('%s'):\n", file);
    return delete_file_or_dir(0, file);
}

int File_Open(char *file) {
    dprintf("File_Open('%s'):\n", file);
    int fd = new_file_fd();
    if (fd < 0) {
        dprintf("... max open files reached\n");
        osErrno = E_TOO_MANY_OPEN_FILES;
        return -1;
    }

    int child_inode;
    follow_path(file, &child_inode, NULL);
    if (child_inode >= 0) {
        // get inode for child
        inode_t *child = loadInode(child_inode);
        if (child->type != 0) {
            dprintf("... error: '%s' is not a file\n", file);
            osErrno = E_GENERAL;
            return -1;
        }
        // initialize open file entry and return its index
        open_files[fd].inode = child_inode;
        open_files[fd].size = child->size;
        open_files[fd].pos = 0;
        return fd;
    } else {
        dprintf("... file '%s' is not found\n", file);
        osErrno = E_NO_SUCH_FILE;
        return -1;
    }
}

int File_Read(int fd, void *buffer, int size) {
    // TODO File_Read
    int bytesRead = 0;
    open_file_t openFileEntry = open_files[fd];
    // check to see if file is open
    if (openFileEntry.inode == 0) {
        dprintf("...file not open");
        osErrno = E_BAD_FD;
        return -1;
    }
    // load the disk sector containing the inode
    int inode_sector = INODE_TABLE_START_SECTOR + openFileEntry.inode / INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if (Disk_Read(inode_sector, inode_buffer) < 0) return -1;
    dprintf("\n... load inode from disk sector %d\n", inode_sector);

    // get the inode
    int inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    int offset = openFileEntry.inode - inode_start_entry;

    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *inode = (inode_t *) (inode_buffer + offset * sizeof(inode_t));

    // next sector to be read
    int nextSector = openFileEntry.pos / SECTOR_SIZE;
    // if position is at end, or sector is not valid
    if (openFileEntry.pos == MAX_FILE_SIZE || !inode->data[nextSector]) { return 0; }

    char sectorBuffer[SECTOR_SIZE];
    if (Disk_Read(inode->data[nextSector], sectorBuffer) < 0) { return -1; }
    dprintf("... load disk sector %d\n", inode->data[nextSector]);

    void *bufferWriter = buffer;
    void *readingPosition = sectorBuffer + (open_files[fd].pos - (openFileEntry.pos / SECTOR_SIZE) * SECTOR_SIZE);

    // read file data until EOF or until requested size bytes have been read
    while (open_files[fd].pos < MAX_FILE_SIZE && size > 0 && inode->data[nextSector]) {

        if (size >= SECTOR_SIZE) {
            dprintf("... Reading BYTE  %d\n", open_files[fd].pos);
            memcpy(bufferWriter, readingPosition, SECTOR_SIZE);
            size -= SECTOR_SIZE;
            bufferWriter += SECTOR_SIZE;
            bytesRead += SECTOR_SIZE;
            open_files[fd].pos += SECTOR_SIZE;
            nextSector++;
            // check if there is more data available
            if (open_files[fd].pos < MAX_FILE_SIZE) {
                if (Disk_Read(inode->data[nextSector], sectorBuffer) < 0) { return -1; }
                readingPosition = sectorBuffer;
            } else { return bytesRead; }

        } else {
            dprintf("... Reading BYTE  %d\n", open_files[fd].pos);
            memcpy(bufferWriter, readingPosition, (size_t) size);
            bytesRead += size;
            open_files[fd].pos += size;
            return bytesRead;
        }
    }
    return bytesRead;
}

int File_Write(int fd, void *buffer, int size) {
    // TODO File_Write
    int bytesWriten = size;
    void *bufferReader = buffer;
    open_file_t openFileEntry = open_files[fd];
    //check if file is not open
    if (openFileEntry.inode == 0) {
        dprintf("... file not open");
        osErrno = E_BAD_FD;
        return -1;
    }
    // check if there is enough space
    if ((openFileEntry.size + size) > MAX_FILE_SIZE) {
        dprintf("... file is too big.");
        osErrno = E_FILE_TOO_BIG;
        return -1;
    }
    char sectorBuffer[SECTOR_SIZE];
    inode_t *fileInode;
    // read sector containing inode
    Disk_Read(INODE_TABLE_START_SECTOR + (openFileEntry.inode / INODES_PER_SECTOR), sectorBuffer);
    // calculate offset within the sector
    int offset = openFileEntry.inode - (openFileEntry.inode / INODES_PER_SECTOR) * INODES_PER_SECTOR;

    fileInode = (inode_t *) sectorBuffer + offset;
    int sectorsNeeded = (size / SECTOR_SIZE) + 1;

    // request free sectors and write
    for (int i = 0; i < sectorsNeeded; i++) {
        char sector[SECTOR_SIZE];
        int sectorIndex = bitmap_first_unused(SECTOR_BITMAP_START_SECTOR, SECTOR_BITMAP_SECTORS, SECTOR_BITMAP_SIZE);

        // if no sectors left
        if (sectorIndex < 0) {
            osErrno = E_NO_SPACE;
            dprintf("... no space left.");
            Disk_Write(INODE_TABLE_START_SECTOR + (openFileEntry.inode / INODES_PER_SECTOR), sectorBuffer);
            return -1;
        }
        fileInode->data[i] = sectorIndex;
        Disk_Read(sectorIndex, sector);

        // write buffer to sector. Buffer might be bigger than sector
        // need to request more sectors
        if (size > SECTOR_SIZE) {
            size -= SECTOR_SIZE;
            memcpy(sector, bufferReader, SECTOR_SIZE);
            bufferReader += SECTOR_SIZE;
            fileInode->size += SECTOR_SIZE;
        } else {
            memcpy(sector, bufferReader, (size_t) size);
            bufferReader += size;
            fileInode->size += size;
            size = 0;
        }
        openFileEntry.size = fileInode->size;
        openFileEntry.pos = fileInode->size;
        Disk_Write(sectorIndex, sector);
    }
    Disk_Write(INODE_TABLE_START_SECTOR + (openFileEntry.inode / INODES_PER_SECTOR), sectorBuffer);
    return bytesWriten - size;
}

int File_Seek(int fd, int offset) {
    // TODO File_Seek
    open_file_t openFileEntry = open_files[fd];
    //check if file is not open
    if (openFileEntry.inode == 0) {
        dprintf("... file not open");
        osErrno = E_BAD_FD;
        return -1;
    }

    //If offset is larger than the size of
    //the file or negative
    if (offset > openFileEntry.size || offset < 0) {
        osErrno = E_SEEK_OUT_OF_BOUNDS;
        return -1;
    }

    open_files[fd].pos = offset;
    return open_files[fd].pos;
}

int File_Close(int fd) {
    dprintf("File_Close(%d):\n", fd);
    if (0 > fd || fd > MAX_OPEN_FILES) {
        dprintf("... fd=%d out of bound\n", fd);
        osErrno = E_BAD_FD;
        return -1;
    }
    if (open_files[fd].inode <= 0) {
        dprintf("... fd=%d not an open file\n", fd);
        osErrno = E_BAD_FD;
        return -1;
    }

    dprintf("... file closed successfully\n");
    open_files[fd].inode = 0;
    return 0;
}

int Dir_Create(char *path) {
    dprintf("Dir_Create('%s'):\n", path);
    return create_file_or_directory(1, path);
}

int Dir_Unlink(char *path) {
    // TODO Dir_Unlink
    dprintf("Dir_Unlink ('%s'):\n", path);
    return delete_file_or_dir(1, path);
}

int Dir_Size(char *path) {
    // TODO Dir_Size
    int inode_index;
    follow_path(path, &inode_index, NULL);

    if (inode_index >= 0) {
        inode_t *dir_inode = loadInode(inode_index);

        if (dir_inode->type != 1) {
            dprintf("... error: '%s' is not a directory\n", path);
            osErrno = E_GENERAL;
            return -1;
        }
        dprintf("... RETURNING SIZE: '%d' \n", (int) (dir_inode->size * sizeof(dirent_t)));

        return (int) (dir_inode->size * sizeof(dirent_t));
    } else {
        dprintf("... directory '%s' is not found\n", path);
        return 0;
    }
}

int Dir_Read(char *path, void *buffer, int size) {
    // TODO Dir_Read
    int dirSize = Dir_Size(path);
    int inode_index;
    follow_path(path, &inode_index, NULL);

    // check if buffer is big enough
    if (dirSize > size) {
        osErrno = E_BUFFER_TOO_SMALL;
        return -1;
    }

    if (inode_index >= 0) {
        inode_t *dir_inode = loadInode(inode_index);

        // copy dirent into buffer
        void *writer = buffer;
        int blocks = dir_inode->size / DIRENTS_PER_SECTOR;

        for (int i = 0; i <= blocks; i++) {
            char sector[SECTOR_SIZE];
            if (Disk_Read(dir_inode->data[i], sector) < 0) { return -1; }
            dprintf("... load sector %d\n", dir_inode->data[i]);
            // TODO need to check
            memcpy(writer, sector, dirSize);
            writer += dirSize;
        }
        dprintf(".. SIZE: '%d' \n", dir_inode->size);

        return dir_inode->size;
    } else {
        dprintf("... directory '%s' is not found\n", path);
        return -1;
    }
}

inode_t *loadInode(int inode_index) {
    // load the disk sector containing the inode
    int inode_sector = INODE_TABLE_START_SECTOR + inode_index / INODES_PER_SECTOR;
    char inode_buffer[SECTOR_SIZE];
    if (Disk_Read(inode_sector, inode_buffer) < 0) {
        osErrno = E_GENERAL;
        return NULL;
    }
    dprintf("... load inode table for inode from disk sector %d\n", inode_sector);

    // get the inode
    int inode_start_entry = (inode_sector - INODE_TABLE_START_SECTOR) * INODES_PER_SECTOR;
    int offset = inode_index - inode_start_entry;
    assert(0 <= offset && offset < INODES_PER_SECTOR);
    inode_t *inode = (inode_t *) (inode_buffer + offset * sizeof(inode_t));
    dprintf("... inode %d (size=%d, type=%d)\n",
            inode_index, inode->size, inode->type);
    return inode;
}