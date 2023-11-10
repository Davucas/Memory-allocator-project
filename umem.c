#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include "umem.h"

typedef struct  node_t{
	size_t size;
	struct node_t *next;
} node_t;

//Global variables
node_t *head = NULL;
int algo = -1;

// Aligns the given size to the page size
size_t align_to_page(size_t size) {
	int num_pags = size/getpagesize();
	if (size%getpagesize() > 0) { num_pags++; }
	
	printf("numero de paginas es: %d \n", num_pags);
	return (size*num_pags);
}

size_t align_size_to_eight(size_t size) {
	size_t offset = size % 8;
	if (offset == 0) { return size; }
	size += (8-offset);
	return size;
}


int umeminit(size_t size, int allocation_algorithm) {

	if (algo != -1 || size <= 0) {
		return -1;
	}
	
	size = align_to_page(size);
	// open the /dev/zero device
	int fd = open("/dev/zero", O_RDWR);
	if (fd == -1) {
		perror("open failed");
		exit(1);
	}

	head = (node_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (head == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}
	close(fd);

	algo = allocation_algorithm;

	// Initialize the head of the free list
	head->size = size - sizeof(node_t);
	head->next = NULL;

	return 0;
}

void split(node_t *curr, size_t size) {
	size_t remaining_size = curr->size - size - sizeof(node_t);

	// using (char*)curr because the char type is 1 byte in size we can perform byte-level arithmetic
	node_t *new_block = (node_t *)((char *)curr + sizeof(node_t) + size);
	new_block->size = remaining_size;
	new_block->next = curr->next;
	curr->next = new_block;
}


void *umalloc(size_t size) {
	// The size of the nodes (sizeof(node_t)) will always be aligned because it's a mutiple of 8. It's 16.
	size = align_size_to_eight(size);
	node_t *prev = NULL;
	node_t *curr = head;
	switch (algo)
	{
	case BEST_FIT:
		node_t *prev_best=NULL;
		node_t *best_fit = head;
		size_t best_size = curr->size - size - sizeof(node_t);
		while (curr) {
			// Check if current block has enough space
			if (curr->size >= size + sizeof(node_t)) {
				// Fit found
				size_t remaining_size = curr->size - size - sizeof(node_t);

				// Found a perfect fit
				if (remaining_size == 0) {
					curr->size = size;
					if (prev) {
						prev->next = curr->next;
					} else {
						head = curr->next;
					}
					return (void*)(curr + 1);
				}

				// Found a better fit
				if (remaining_size < best_size) {
					best_fit = curr;
					best_size = remaining_size;
					prev_best = prev;
				}
			
			}

			prev = curr;
			curr = curr->next;
		}

		split(best_fit, size);
		
		if (prev_best) {
			prev_best->next = best_fit->next;
		} else {
			head = best_fit->next;
		}
		
		return (void*)(best_fit + 1);
		break;
	
	case FIRST_FIT:
		while (curr) {
			// Check if current block has enough space
			if (curr->size >= size + sizeof(node_t)) {
				// Fit found, need to split the block
				size_t remaining_size = curr->size - size - sizeof(node_t);
				curr->size = size;

				// Setup the new block if there is space left
				if (remaining_size > sizeof(node_t)) {
					// using (char*)curr because the char type is 1 byte in size we can perform byte-level arithmetic
					node_t *new_block = (node_t *)((char *)curr + sizeof(node_t) + size);
					new_block->size = remaining_size;
					new_block->next = curr->next;
					curr->next = new_block;
				}

				if (prev) {
					prev->next = curr->next;
				} else {
					head = curr->next;
				}
				
				return (void*)(curr + 1);
			}

			prev = curr;
			curr = curr->next;
		}
		break;
	
	default:
		break;
	}

	// No fit found
	return NULL;
}


void memdump() {
	node_t *node = head;
	int i=0;

	while (node) {
		size_t size = node->size;
		void *start = (void *)(node+1);
		void *end = (void *)((char *)start + size);
		printf("Node %d\n", i);
		printf("\t Start address: %p\n", start);
		printf("\t End address: %p\n", end);
		i++;
		node= node->next;
	}	

}



// This function would be called by the user's program
int main() {
	umeminit(4096, BEST_FIT);
	memdump();
	int *array = (int *)umalloc(10 * sizeof(int));
	memdump();
	if (array == NULL) {
		printf("Memory allocation failed\n");
		return 1;
	}
	
	// The array can now be used
	for (int i = 0; i < 10; i++) {
		array[i] = i;
		printf("Element %d of array: %d, address: %p \n", i, array[i],(void *)&array[i]);
	}
	char *array2 = (char *) umalloc(3* sizeof(char));
	array2 ="HEY";
	memdump();
	
	double **array3 = (double**)umalloc(5*sizeof(double));
	memdump();

    return 0;
}
