#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>
#include <sys/fcntl.h>
#include "mem.h"


int policySet = MEM_POLICY_FIRSTFIT; // default policy
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

        if (policySet == MEM_POLICY_FIRSTFIT) {
            int fd = open("/dev/zero", O_RDWR);
            struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE, fd, 0);

            //if fit at head of list
            if ((curr->address - base) >= size) {
                temp->address = base;
                temp->size = size;
                temp->next = curr;
                memoryList->head = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }

            //find space in between nodes
            while (curr->next != NULL) {
                if (((next->address) - (curr->address + curr->size)) >= size) {
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
            //if space found at end
            if (((limit - ((curr->address) + curr->size)) >= size)) {
                curr->next = temp;
                temp->address = curr->address + curr->size;
                temp->size = size;
                temp->next = NULL;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }
            // if it gets here, then no space was found, free memory allocated for temp node
            // since it was not used.
            munmap(temp, sizeof(struct node));
            return NULL;
        }

            // **************************************************************** BEST FIT
        else if (policySet == MEM_POLICY_BESTFIT) {
            struct node *insertAfter = memoryList->head;
            uint8_t insertAtHead = 0;
            uint8_t insertInMiddle = 0;

            unsigned long smallestSpace = LONG_MAX;
            int fd = open("/dev/zero", O_RDWR);
            struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE, fd, 0);

            //check if there is space at beginning of block
            if ((curr->address - base) >= size) {
                insertAtHead = 1;
                smallestSpace = curr->address - base;
            }
            //check if there is a better fit
            while (curr->next != NULL) {
                unsigned long space = ((curr->next->address) - (curr->address + curr->size));
                if (space >= size && space < smallestSpace) {
                    insertAfter = curr;
                    smallestSpace = space;
                    insertAtHead = 0;
                    insertInMiddle = 1;
                }
                curr = curr->next;
            }

            // insert last if last is smallest space
            if (((limit - ((curr->address) + curr->size)) >= size) &&
                ((limit - ((curr->address) + curr->size)) < smallestSpace)) {
                curr->next = temp;
                temp->address = curr->address + curr->size;
                temp->size = size;
                temp->next = NULL;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }
            // if space is at beginning of block
            if (insertAtHead) {
                temp->address = base;
                temp->size = size;
                temp->next = memoryList->head;
                memoryList->head = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return memoryList->head->address;
            }
            // found space in between
            if (insertInMiddle) {
                temp->address = insertAfter->address + insertAfter->size;
                temp->size = size;
                temp->next = insertAfter->next;
                insertAfter->next = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }
            // if it gets here, then no space was found, free memory allocated for temp node
            // since it was not used.
            munmap(temp, sizeof(struct node));
            return NULL;
        }
            // **************************************************************** WORST FIT
        else if (policySet == MEM_POLICY_WORSTFIT) {
            struct node *insertAfter = memoryList->head;
            uint8_t insertAtHead = 0;
            uint8_t insertInMiddle = 0;

            unsigned long largestSpace = 0;
            int fd = open("/dev/zero", O_RDWR);
            struct node *temp = mmap(NULL, sizeof(struct node), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE, fd, 0);

            //check if there is space at beginning of block
            if ((curr->address - base) >= size) {
                insertAtHead = 1;
                largestSpace = curr->address - base;
            }
            //check if there is a bigger space fit
            while (curr->next != NULL) {
                unsigned long space = ((curr->next->address) - (curr->address + curr->size));
                if (space >= size && space > largestSpace) {
                    insertAfter = curr;
                    largestSpace = space;
                    insertAtHead = 0;
                    insertInMiddle = 1;
                }
                curr = curr->next;
            }
            // insert last if last is smallest space
            if (((limit - ((curr->address) + curr->size)) >= size) &&
                ((limit - ((curr->address) + curr->size)) > largestSpace)) {
                curr->next = temp;
                temp->address = curr->address + curr->size;
                temp->size = size;
                temp->next = NULL;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }
            // if space is at beginning of block
            if (insertAtHead) {
                temp->address = base;
                temp->size = size;
                temp->next = memoryList->head;
                memoryList->head = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return memoryList->head->address;
            }
            // found space in between
            if (insertInMiddle) {
                temp->address = insertAfter->address + insertAfter->size;
                temp->size = size;
                temp->next = insertAfter->next;
                insertAfter->next = temp;
                memoryList->remainingMemory = memoryList->remainingMemory - size;
                return temp->address;
            }
            // if it gets here, then no space was found, free memory allocated for temp node
            // since it was not used.
            munmap(temp, sizeof(struct node));
            return NULL;
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

int Mem_IsValid(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    struct node *curr = memoryList->head;
    while (curr != NULL) {
        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

int Mem_GetSize(void *ptr) {
    if (ptr == NULL) {
        return -1;
    }
    struct node *curr = memoryList->head;
    while (curr != NULL) {
        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
            return curr->size;
        }
        curr = curr->next;
    }
    return -1;
}

float Mem_GetFragmentation() {
    unsigned long largest = 0;
    struct node *curr = memoryList->head;

    // if no free space
    if (!(memoryList->remainingMemory)) {
        return 1;
    }

    if ((curr->address - base) > largest) {
        largest = (curr->address - base);
    }

    while (curr->next != NULL) {
        unsigned long space = (curr->next->address) - (curr->address + curr->size);
        if (space > largest) {
            largest = space;
        }
        curr = curr->next;
    }
    // check end of block
    if ((limit - (curr->address + curr->size)) > largest) {
        largest = (limit - (curr->address + curr->size));
    }
    return (float) largest / (float) memoryList->remainingMemory;
}


