/** @file sheap.h
 *  @brief Provides the api and and some macros for the secure heap (sheap) implementation.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include "sheaperd.h"

/**
* Allocates the size \size on the heap and returns the pointer in pVoid \pVoid.
*
* @param size 	Specifies the amount of data to allocate
* @param pVoid  A void* to assign the result to.
*
*/


//TODO: Store pc and provide it as function parameter instead of asm
#define SHEAP_MALLOC(size, pVoid)                                                                \
do {                                                                                             \
	if(ASSERT_TYPE(size_t, size)){                                                               \
		size_t s = size;                                                                         \
		register uint32_t lr asm ("lr");                /* Backup lr                         */  \
		uint32_t lrBackup = lr;                         /* Backup lr                         */  \
		__asm volatile("mov r0, %0\n\t" : "=r" (s));    /* Store the size in r0              */  \
		__asm volatile(                                                                          \
				"mov r1, pc\n"                          /* Store the pc in r1                */  \
				"bl sheap_malloc_impl"                  /* Branch to malloc implementation   */  \
		);                                                                                       \
		register int* pAlloc asm ("r0");                                                         \
		lr = lrBackup;                                  /* Restore lr                        */  \
		pVoid = (void*)pAlloc;                                                                   \
	}                                                                                            \
} while(0)

#define SHEAP_FREE(pVoid)                                                                        \
do {                                                                                             \
	register uint32_t r0 asm ("r0") = (uint32_t)pVoid;  /* Store the pointer to free in r0   */  \
	uint32_t r0Backup = r0;                             /* Backup r0 - avoid unused warning  */  \
	register uint32_t lr asm ("lr");                    /* Backup lr                         */  \
	uint32_t lrBackup = lr;                             /* Backup lr                         */  \
	__asm volatile(                                                                              \
				"mov r1, pc\n"                          /* Store the pc in r1                */  \
				"bl sheap_free_impl"                    /* Branch to free implementation     */  \
		);                                                                                       \
	lr = lrBackup;                                      /* Restore lr                        */  \
	r0 = r0Backup;                                      /* Restore r0 - avoid unused warning */  \
} while(0)

typedef struct{
	uint8_t* 	heapMin;
	uint8_t* 	heapMax;
	uint32_t 	currentAllocations;
	uint32_t 	totalBytesAllocated;
	uint32_t	userDataAllocatedAlligned;
	uint32_t	userDataAllocated;
	size_t 		size;
} sheap_heapStat_t;

//void* sheap_malloc_impl();
//void sheap_free_impl();
void* malloc(size_t size);
void free(void* ptr);
size_t sheap_getHeapSize();
size_t sheap_getAllocatedBytesAligned();
size_t sheap_getAllocatedBytes();
void sheap_getHeapStatistic(sheap_heapStat_t* heapStat);

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
