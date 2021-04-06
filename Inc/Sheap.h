/** @file sheap.h
 *  @brief Provides the api and and some macros for the secure heap (sheap) implementation.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include "sheaperd.h"

#ifndef SHEAP_MINIMUM_MALLOC_SIZE
	#define SHEAP_MINIMUM_MALLOC_SIZE 		4
#endif
#if SHEAP_MINIMUM_MALLOC_SIZE < 4
	#define SHEAP_MINIMUM_MALLOC_SIZE 		4
#endif


/**
* Allocates the size \size on the heap and returns the pointer in pVoid \pVoid.
*
* @param size 	Specifies the amount of data to allocate
* @param pVoid  A void* to assign the result to.
*
*/
#define SHEAP_MALLOC(size, pVoid)                                                               \
do {                                                                                            \
	if(ASSERT_TYPE(size_t, size)){                                                              \
		size_t s = size;                                                                        \
		register uint32_t lr asm ("lr");                /* Backup lr                        */  \
		uint32_t lrBackup = lr;                         /* Backup lr                        */  \
		__asm volatile("mov r0, %0\n\t" : "=r" (s));    /* Store the size in r0             */  \
		__asm volatile(                                                                         \
				"mov r1, pc\n"                          /* Store the pc in r1               */  \
				"bl sheap_malloc_impl"                  /* Branch to malloc implementation  */  \
		);                                                                                      \
		register int* pAlloc asm ("r0");                                                        \
		lr = lrBackup;                                  /* Restore lr                       */  \
		pVoid = (void*)pAlloc;                                                                  \
	}                                                                                           \
} while(0)

#define SHEAP_FREE(pVoid)                                                                       \
do {                                                                                            \
	register long r0 asm ("r0") = (long)pVoid;          /* Store the pointer to free in r0  */  \
	register uint32_t lr asm ("lr");                    /* Backup lr                        */  \
	uint32_t lrBackup = lr;                             /* Backup lr                        */  \
	__asm volatile(                                                                             \
				"mov r1, pc\n"                          /* Store the pc in r1               */  \
				"bl sheap_free_impl"                    /* Branch to malloc implementation  */  \
		);                                                                                      \
	lr = lrBackup;                                      /* Restore lr                       */  \
} while(0)

typedef struct{
	uint8_t* 	heapMin;
	uint8_t* 	heapMax;
	uint32_t 	currentAllocations;
	uint32_t 	totalBytesAllocated;
	uint32_t	userDataAllocatedAlligned;
	uint32_t	userDataAllocated;
	size_t 		size;
} sheap_heap_t;

typedef enum {
	SHEAP_ERROR_INVALID_BLOCK,
	SHEAP_ERROR_DOUBLE_FREE,
	SHEAP_ERROR_NULL_FREE,
	SHEAP_ERROR_SIZE_ZERO_ALLOC,
	SHEAP_ERROR_OUT_OF_BOUND_WRITE,
	SHEAP_ERROR_FREE_PTR_NOT_IN_HEAP,
	SHEAP_ERROR_FREE_INVALID_BOUNDARY_POSSIBLE_OUT_OF_BOUND_WRITE,
	SHEAP_ERROR_FREE_INVALID_HEADER,
	SHEAP_ERROR_FREE_BLOCK_ALTERED_CRC_INVALID,
	SHEAP_ERROR_COALESCING_NEXT_BLOCK_ALTERED_INVALID_CRC,
	SHEAP_ERROR_COALESCING_PREV_BLOCK_ALTERED_INVALID_CRC,
	SHEAP_ERROR_OUT_OF_MEMORY,
	SHEAP_ERROR_MUTEX_CREATION_FAILED,
	SHEAP_ERROR_MUTEX_IS_NULL,
	SHEAP_ERROR_MUTEX_ACQUIRE_FAILED,
	SHEAP_ERROR_MUTEX_RELEASE_FAILED
} sheap_error_t;


typedef void (*sheap_errorCallback_t)(sheap_error_t error);

void sheap_registerErrorCallback(sheap_errorCallback_t callback);
void* sheap_malloc_impl();
void sheap_free_impl();
void* malloc(size_t size);
void free(void* ptr);
size_t sheap_getHeapSize();
size_t sheap_getAllocatedBytesAligned();
size_t sheap_getAllocatedBytes();
void sheap_getHeapStatistic(sheap_heap_t* heap);

/**
 * Initializes the sheap allocator
 * ATTENTION: this function must be called before the scheduler is started
 */
void sheap_init(uint32_t* heapStart, size_t size);

/**
 * Align the the size to a multiple of a predefined size
 *
 * The define 'SHEAP_MINIMUM_MALLOC_SIZE' can be used to specify the size to align to.
 * This function align the provided size to 'SHEAP_MINIMUM_MALLOC_SIZE'
 *
 * Examples: n = 7 --> return 8; n = 11 --> return 12
 *
 * @param n		The size to be aligned
 *
 * @return		n aligned to 'SHEAP_MINIMUM_MALLOC_SIZE'
 */
size_t sheap_align(size_t n);

/**
 * Gets the latest program counter addresses that used SHEAP_MALLOC() or SHEAP_FREE()
 *
 * Fills the provided destination array with the last recorded program counter
 * addresses that used SHEAP_MALLOC() or SHEAP_FREE()
 *
 * @param destination 	Filled with the recorded pc addresses
 * @param n			  	Size of the provided destination array
 *
 * @return			 	The numbers of pcs written to the destination array
 */
uint32_t sheap_getLatestAllocationPCs(uint32_t destination[], uint32_t n);

#endif /* INC_SHEAP_H_ */
