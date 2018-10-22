#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

#define FIRST_FIT 0
#define BEST_FIT 1
#define WORST_FIT 2

int policySet = FIRST_FIT; // default policy
void *base;
void *limit;
struct node *head, *tail;


struct node {
    void *address;
    int size;
    struct node *next;
};

int Mem_Init(int size, int policy) {

    policySet = policy;
    // round up to units of page size
    if (size < getpagesize() && size > 0) {
        size = getpagesize();
    } else {
        size = (int) ceil(size / getpagesize()) * getpagesize();
    }

    if ((base = mmap(NULL, (size_t) size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) ==
        (caddr_t) -1) {
        return -1;
    }
    limit = base + size;
    printf("\n%p", limit);
    return 0;
}


void *Mem_Alloc(int size) {

    struct node *curr;
    struct node *next;

    if (head != NULL) {
        curr = head;
        next = head->next;

        if (policySet == FIRST_FIT) {
            //check if there is space between base and head of list
            if ((void *) curr - base > size) {
                struct node *temp = (struct node *) base;
                temp->address = sizeof(struct node) + base;
                temp->size = size;
                head = temp;
                return temp->address;
            }

            while (curr->next != NULL) {
                // if there is enough space between nodes, insert here
                if (((void *) next - ((void *) curr + sizeof(struct node) + curr->size)) >
                    (void *) (sizeof(struct node) + size)) {
                    struct node *temp = (curr + sizeof(struct node) + curr->size);
                    temp->address = ((void *) temp + sizeof(struct node));
                    temp->size = size;
                    temp->next = next;
                    curr->next = temp;
                    return temp->address;
                } else {
                    curr = curr->next;
                    next = next->next;
                }
            }
            //if limit address has no been reached
            if ((((void *) curr + curr->size + sizeof(struct node)) + sizeof(struct node) + size) < limit) {
                curr->next = ((void *) curr + curr->size + sizeof(struct node));
                curr = curr->next;
                curr->address = (sizeof(struct node) + (void *) curr);
                curr->size = size;
                return curr->address;
            }
            return NULL;


        } else if (policySet == BEST_FIT) {

        } else if (policySet == WORST_FIT) {

        }
    } else {
        head = (struct node *) base;
        // as far as the user is concern, this is the address of his object.
        head->address = sizeof(struct node) + base;
        head->size = size;
        return head->address;
    }
    return NULL;
}


int Mem_Free(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }

    struct node *curr = head;
    struct node *prev = head;

    // if pointer is at head of list
    if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
        head = head->next;
        return 0;
    }

    // if pointer is within a node, remove node form list
    curr = curr->next;
    while (curr != NULL) {
        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
            prev->next = curr->next;
            return 0;
        }
        curr = curr->next;
    }
    return -1;
}

int Mem_IsValid(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    struct node *curr = head;
    while (curr != NULL) {
        if (curr->address <= ptr && ptr < (curr->address + curr->size)) {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

int main(int argc, char *argv[]) {

    if (!Mem_Init(1, 0)) {
        printf("\nBASE >> %p\n", base);
    } else {
        printf("Failed");
    }
    printf("\nSIZE >> %d\n\n", (int) sizeof(struct node));

    void *p = Mem_Alloc(50);
    printf("\nP > %p\n", p);
    void *x = Mem_Alloc(20);
    printf("\nX > %p\n", x);


    void *c = Mem_Alloc(1);
    printf("\nC > %p\n", c);
    void *v = Mem_Alloc(1);
    printf("\nV > %p\n", v);

    printf("FREE %d\n", Mem_Free(p));
    printf("IS VALID %d\n", Mem_IsValid(p));

    void *z = Mem_Alloc(65535);
    void *b = Mem_Alloc(10);

    printf("IS VALID %d\n", Mem_IsValid(z));
    printf("IS VALID %d\n", Mem_IsValid(b));


}