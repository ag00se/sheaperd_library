/** @file sheap.h
 *  @brief Provides the default options for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INC_SHEAP_H_
#define INC_SHEAP_H_

#include "sheaperd.h"

#define SHEAP_MINIMUM_MALLOC_SIZE 		4

/** \def SHEAP_MALLOC(size, pVoid)
*    \brief Allocates the size \size on the heap and returns the pointer in pVoid \pVoid.
*
*    Size \size specifies the amount of data to allocate and pVoid \pVoid is a void* to assign
*    the result to.
*    This macro records the pc of the caller for debugging purpose.
*/
#define SHEAP_MALLOC(size, pVoid)																\
do {																							\
	if(ASSERT_TYPE(size_t, size)){																\
		size_t s = size;																		\
		__asm volatile("mov r0, %0\n\t" : "=r" (s));	/* Store the size in r0				*/	\
		__asm volatile(																			\
				"mov r1, pc				\n" 			/* Store the pc in r1				*/	\
				"bl sheap_malloc_impl"					/* Branch to malloc implementation	*/	\
		);																						\
		register int* pAlloc asm ("r0");														\
		pVoid = pAlloc;																			\
	}																							\
} while(0)


#define SHEAP_FREE(pVoid)																			\
do {																								\
	register long r0 asm ("r0") = (long)pVoid;			/* Store the pointer to free in r0		*/	\
	__asm volatile(																					\
				"mov r1, pc				\n" 			/* Store the pc in r1					*/	\
				"bl sheap_free_impl"					/* Branch to malloc implementation		*/	\
		);																							\
} while(0)

typedef enum {
	SHEAP_ERROR_DOUBLE_FREE,
	SHEAP_ERROR_NULL_FREE,
	SHEAP_ERROR_OUT_OF_BOUND_WRITE,
	SHEAP_ERROR_FREE_INVALID_POINTER,
	SHEAP_ERROR_FREE_BLOCK_ALTERED_CRC_INVALID,
	SHEAP_ERROR_COALESCING_NEXT_BLOCK_ALTERED_INVALID_CRC,
	SHEAP_ERROR_COALESCING_PREV_BLOCK_ALTERED_INVALID_CRC
} sheap_error_t;


typedef void (*sheap_errorCallback_t)(sheap_error_t error);

void sheap_init(uint32_t* heapStart, size_t size);
void sheap_registerErrorCallback(sheap_errorCallback_t callback);
void* sheap_malloc_impl();
void sheap_free_impl();
void* malloc(size_t size);
void free(void* ptr);
size_t sheap_getHeapSize();
size_t sheap_getAllocatedBytes();

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
