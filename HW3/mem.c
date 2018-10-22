#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>
#include <sys/fcntl.h>

#define FIRST_FIT 0
#define BEST_FIT 1
#define WORST_FIT 2

int policySet = FIRST_FIT; // default policy
uint8_t initFlag = 0;
void *base = NULL;
void *limit = NULL;
unsigned long totalAllocated = 0;
struct list *memoryList = NULL;

struct node {
    void *address;
    int size;
    struct node *next;
};

struct list {
    struct node *head;
    unsigned long remainingMemory;
};

int Mem_Init(int size, int policy) {
    if (initFlag) {
        return -1;
    }
    initFlag = 1;
    policySet = policy;

    // round up to units of page size
    if (size < getpagesize() && size > 0) {
        size = getpagesize();
    } else {
        size = (int) ceil((double) size / (double) getpagesize()) * getpagesize();
    }

    // open the /dev/zero device
    int fd = open("/dev/zero", O_RDWR);
    // size (in bytes) must be divisible by page size
    base = mmap(NULL, (size_t) size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    limit = base + size;
    totalAllocated = (unsigned long) size;
    // close the device (don't worry, mapping should be unaffected)
    close(fd);
    return 0;
}


void *Mem_Alloc(int size) {

    struct node *curr;
    struct node *next;

    if (memoryList != NULL) {
        //if requested is greater than remaining
        if (memoryList->remainingMemory < size) {
            return NULL;
        }

        curr = memoryList->head;
        next = curr->next;

        if (policySet == FIRST_FIT) {
            //if fit at head of list
            if ((curr->address - base) > size) {
                int fd = open("/dev/zero", O_RDWR);
                struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE, fd, 0);
                temp->address = base;
                temp->size = size;
                temp->next = curr;
                memoryList->head = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }

            //find space in between nodes
            while (curr->next != NULL) {
                if (((next->address) - (curr->address + curr->size)) > size) {
                    int fd = open("/dev/zero", O_RDWR);
                    struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                             MAP_PRIVATE, fd, 0);
                    temp->address = curr->address + curr->size;
                    temp->size = size;
                    temp->next = next;
                    curr->next = temp;
                    memoryList->remainingMemory = memoryList->remainingMemory - size;
                    return temp->address;
                }
                curr = curr->next;
                next = next->next;
            }
            // space found at end
            int fd = open("/dev/zero", O_RDWR);
            struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE, fd, 0);
            temp->address = curr->address + curr->size;
            temp->size = size;
            temp->next = next;
            curr->next = temp;
            memoryList->remainingMemory = memoryList->remainingMemory - size;
            return temp->address;

        } else if (policySet == BEST_FIT) {

        } else if (policySet == WORST_FIT) {

        }
    }
        //memoryList is empty and requested fits
    else if (size < (limit - base)) {
        int fd = open("/dev/zero", O_RDWR);
        int fd2 = open("/dev/zero", O_RDWR);

        memoryList = mmap(NULL, sizeof(struct list), PROT_READ | PROT_WRITE,
                          MAP_PRIVATE, fd, 0);
        memoryList->head = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                MAP_PRIVATE, fd2, 0);
        memoryList->remainingMemory = totalAllocated - size;
        memoryList->head->next = NULL;
        memoryList->head->address = base;
        memoryList->head->size = size;
        return memoryList->head->address;
    }
    return NULL;
}

//
int Mem_Free(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    struct node *curr = memoryList->head;
    struct node *prev = memoryList->head;
    // if pointer is at head of memoryList
    if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
        memoryList->remainingMemory = memoryList->remainingMemory + curr->size;
        struct node *temp = memoryList->head->next;
        munmap(curr, sizeof(struct node));
        memoryList->head = temp;
        return 0;
    }
    // if pointer is within a node, remove node form memoryList
    curr = curr->next;
    while (curr != NULL) {
        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
            memoryList->remainingMemory = memoryList->remainingMemory + curr->size;
            prev->next = curr->next;
            munmap(curr, sizeof(struct node));
            return 0;
        }
        prev = prev->next;
        curr = curr->next;
    }
    return -1;
}
//
//int Mem_IsValid(void *ptr) {
//    if (ptr == NULL) {
//        return 0;
//    }
//    struct node *curr = head;
//    while (curr != NULL) {
//        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
//            return 1;
//        }
//        curr = curr->next;
//    }
//    return 0;
//}

int main(int argc, char *argv[]) {

    if (!Mem_Init(1, 0)) {
        printf("\nBASE >> %p, LIMIT %p\n", base, limit);
    } else {
        printf("Failed");
    }
    if (!Mem_Init(66000, 0)) {
        printf("\nBASE >> %p, LIMIT %p\n", base, limit);
    } else {
        printf("Failed");
    }
    printf("\nSIZE >> %zu\n\n", sizeof(struct node));




//**********************************************************************

    void *p = Mem_Alloc(20);
    printf("\nP > %lu\n", (unsigned long) p);
    printf("\nPAGE SIZE > %d\n", getpagesize());

    void *a = Mem_Alloc(1);
    printf("\nA > %lu\n", (unsigned long) a);

    void *z = Mem_Alloc(2);
    printf("\nZ > %lu\n", (unsigned long) z);

    printf("FREE P %lu\n", Mem_Free(p));


    void *l = Mem_Alloc(10);
    printf("\nL > %lu\n", (unsigned long) l);


    void *N = Mem_Alloc(50);
    printf("\nN > %lu\n", (unsigned long) N);

////**********************************************************************


    munmap(base, limit - base);

}