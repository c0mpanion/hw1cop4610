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

    size_t total;
    void *block;
    struct node *curr;
    struct node *next;

    if (head != NULL) {
        curr = head;
        next = head->next;

        if (policySet == FIRST_FIT) {

            while (curr->next != NULL) {
                // if there is enough space between nodes, insert here

                if (((void *) (next - (curr + sizeof(struct node) + curr->size))) >
                    (void *) (sizeof(struct node) + size)) {
                    printf("\nIF CONDITION %p", (void *) (next - (curr + sizeof(struct node) + curr->size)));

                    struct node *temp = (curr->address + curr->size);
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
            printf("\nCURR > %p", ((void *) curr + curr->size + sizeof(struct node)));
            curr->next = ((void *) curr + curr->size + sizeof(struct node));
            curr = curr->next;
            curr->address = (sizeof(struct node) + (void *) curr);
            curr->size = size;
            return curr->address;


        } else if (policySet == BEST_FIT) {

        } else if (policySet == WORST_FIT) {

        }
    } else {
        printf("\nFirst is null");
        head = (struct node *) base;
        // as far as the user is concern, this is the address of his object.
        head->address = sizeof(struct node) + base;
        head->size = size;
        return head->address;
    }
    return 0;
}

int main(int argc, char *argv[]) {

    if (!Mem_Init(1, 0)) {
        printf("\nBASE >> %p\n", base);
    } else {
        printf("Failed");
    }
    printf("\nSIZE >> %d", (int) sizeof(struct node));

    void *p = Mem_Alloc(1);
    printf("\nP > %p\n", p);
    void *x = Mem_Alloc(1);
    printf("\nX > %p\n", x);
    void *c = Mem_Alloc(1);
    printf("\nC > %p\n", c);
    void *v = Mem_Alloc(1);
    printf("\nV > %p\n", v);

}