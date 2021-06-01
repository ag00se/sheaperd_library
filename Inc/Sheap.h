/** @file sheap.h
 *  @brief Provides the api for the secure heap (sheap) implementation.
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
#define SHEAP_MALLOC(size, pVoid)                                                               \
do {                                                                                            \
	if(ASSERT_TYPE(size_t, size)){                                                              \
		register uint32_t r1 asm("r1");															\
		uint32_t r1Backup = r1;																	\
		__asm volatile("mov r1, pc\n");                /* Store the pc in r1                */  \
		uint32_t pc;																			\
		asm("mov %0, r1" : "=r" (pc));															\
		size_t s = size;                                                                        \
		pVoid = sheap_malloc(size, pc);                                                        	\
		r1 = r1Backup;																			\
	}                                                                                           \
} while(0)

#define SHEAP_FREE(pVoid)                                                                       \
do {                                                                                            \
	register uint32_t r1 asm("r1");																\
	uint32_t r1Backup = r1;																		\
	__asm volatile("mov r1, pc\n");                /* Store the pc in r1                */  	\
	uint32_t pc;																				\
	asm("mov %0, r1" : "=r" (pc));																\
	sheap_free(pVoid, pc);																		\
	r1 = r1Backup;																				\
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

/**
 * Allocates memory of the requested size and provides a pointer to the memory
 * to the caller. For best error detection support one should use the provided
 * SHEAP_MALLOC(size, pVoid) macro.
 * If this function is called directly the parameter pc can be set to 0. In
 * general it is intended to record the program counter of the caller to
 * obtain additional information.
 * If possible use the SHEAP_MALLOC(size, pVoid) macro.
 *
 * @param size the size of memory to be allocated
 * @param pc the current program counter of the caller
 */
void* sheap_malloc(size_t size, uint32_t pc);

/**
 * Deallocates memory associated with the provided pointer.
 * For best error detection support one should use the provided
 * SHEAP_FREE(pVoid) macro.
 * If this function is called directly the parameter pc can be set to 0. In
 * general it is intended to record the program counter of the caller to
 * obtain additional information.
 * If possible use the SHEAP_FREE(pVoid) macro.
 *
 * @param ptr the pointer associated with the memory to be freed
 */
void sheap_free(void* ptr, uint32_t pc);
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
