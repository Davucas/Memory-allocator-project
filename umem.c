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

void split(node_t *curr, size_t size, size_t remaining_size) {
	curr->size = size;
	// using (char*)curr because the char type is 1 byte in size we can perform byte-level arithmetic
	node_t *new_block = (node_t *)((char *)curr + sizeof(node_t) + size);
	new_block->size = remaining_size - sizeof(node_t);
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
		node_t *best_fit = NULL;
		// We are looking for the node that has the least remaining size
		size_t best_size = -1;
		//curr->size - size;
		while (curr) {
			// Check if current node has enough space
			if (curr->size >= size) {
				// Fit found
				size_t remaining_size = curr->size - size;

				// Found a perfect fit
				if (remaining_size == 0) {
					//curr->size = size;
					if (prev) {
						prev->next = curr->next;
					} else {
						head = curr->next;
					}
					return (void*)(curr + 1);
				}
				
				// Found a better fit
				if (remaining_size < best_size || best_size == -1) {
					best_fit = curr;
					best_size = remaining_size;
					prev_best = prev;
				}
			
			}

			prev = curr;
			curr = curr->next;
		}

		if (best_fit == NULL) { break; }

		// Check if the node is big enough to split it
		if (best_size > sizeof(node_t)) {
			split(best_fit, size, best_size);
		}

		if (prev_best) {
			prev_best->next = best_fit->next;
		} else {
			head = best_fit->next;
		}
		
		return (void*)(best_fit + 1);
		break;
	
	case FIRST_FIT:
		while (curr) {
			// Check if current node has enough space
			if (curr->size >= size) {
				// Fit found, need to split the block
				size_t remaining_size = curr->size - size;
				curr->size = size;

				// Setup the new node if there is space left
				if (remaining_size > sizeof(node_t)) {
					split(curr, size, remaining_size);
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


void umemdump() {
	node_t *node = head;
	int i=0;

	printf("Head points at: %p\n", (void *)head);
	while (node) {
		size_t size = node->size;
		void *start = (void *)(node+1);
		void *end = (void *)((char *)start + size);
		printf("Node %d\n", i);
		printf("\t Header address: %p\n", (void *)node);
		printf("\t Start address: %p\n", start);
		printf("\t End address: %p\n", end);
		printf("\t Next: %p\n", (void *)node->next);
		printf("\t Size (without header): %ld\n", size);
		i++;
		node= node->next;
	}	

}


int ufree(void *ptr) {
	if (ptr == NULL) {
		return 0;
	}
	node_t *node = (node_t *)((char *)ptr - sizeof(node_t));
	if (node < head || head == NULL) {
		// If node < head then the only possible neighbour free node would be the head
		if (node->next == head) {
			//Coalescing node with the head
			node->size = node->size + head->size + sizeof(node_t);
			node->next = head->next;
		}
		else { node->next = head;}
		head = node;
		return 0;
	}
	
	node_t *curr = head;
	node_t *prev = NULL;
	while (curr) {
		if (curr > node) {
			node_t *prev_neighbour = (node_t *)((char *)prev + sizeof(node_t) + prev->size);
			// Check if the previous and the next free nodes of node are node's neighbours
			if (node->next == curr) {
				//Coalescing node with the next one
				node->size = node->size + curr->size + sizeof(node_t);
				node->next = curr->next;
			}

			if (prev_neighbour == node) {
				prev->size = prev->size + node->size + sizeof(node_t);
				prev->next = node->next;
			}

			if ( (node->next != curr) && (prev_neighbour != node)) {
				node->next = prev->next;
				prev->next = node;
			}
		}
		prev = curr;
		curr = curr->next;
	}
	return 0;
}


// This function would be called by the user's program
int main() {
	umeminit(4096, BEST_FIT);

	int *array1 = (int *)umalloc(20*sizeof(int));

	double *array2 = (double *)umalloc(3*sizeof(double));

	char *array3 = (char *)umalloc(3*sizeof(char));

	char **array4 = (char **)umalloc(5*sizeof(char)*15);

	umemdump();
	ufree(array1);
	umemdump();
	ufree(array3);
	umemdump();

	char *array5 = (char *)umalloc(9*sizeof(char));
	printf("size of array5 %ld\n", sizeof(array5));
	umemdump();


	array5= "Hey";
	printf("array5: %s\n", array5);


	return 0;
}