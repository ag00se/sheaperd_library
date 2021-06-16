/** @file sheaperd.h
 *  @brief Provides main includes and typedefs for the sheaperd library
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef SHEAPERD_H_
#define SHEAPERD_H_

#include "internal/def.h"
#include "internal/opt.h"
#include "internal/util.h"
#include "sheap.h"

typedef enum {
	SHEAPERD_MPU_NOT_AVAILABLE,
	SHEAPERD_MPU_INTIALIZATION_FAILED,
	SHEAPERD_MPU_NOT_SUPPORTED_ATM,
	SHEAPERD_MPU_INITIALIZED
} sheaperd_MPUState_t;

#include "stackguard.h"

typedef enum {
	SHEAPERD_GENERAL_ASSERT,
	SHEAPERD_ARRAY_BOUND_CHECK,
	SHEAPERD_ERROR_MUTEX_CREATION_FAILED,
	SHEAPERD_ERROR_MUTEX_DELETION_FAILED,
	SHEAPERD_ERROR_MUTEX_IS_NULL,
	SHEAPERD_ERROR_MUTEX_ACQUIRE_FAILED,
	SHEAPERD_ERROR_MUTEX_RELEASE_FAILED,
	SHEAP_INIT_INVALID_SIZE,
	SHEAP_NOT_INITIALIZED,
	SHEAP_OUT_OF_MEMORY,
	SHEAP_SIZE_ZERO_ALLOC,
	SHEAP_ERROR_INVALID_BLOCK,
	SHEAP_ERROR_DOUBLE_FREE,
	SHEAP_ERROR_NULL_FREE,
	SHEAP_ERROR_OUT_OF_BOUND_WRITE,
	SHEAP_ERROR_FREE_PTR_NOT_IN_HEAP,
	SHEAP_ERROR_FREE_INVALID_BOUNDARY,
	SHEAP_ERROR_FREE_INVALID_HEADER,
	SHEAP_ERROR_FREE_BLOCK_ALTERED_CRC_INVALID,
	SHEAP_ERROR_COALESCING_NEXT_BLOCK_ALTERED_INVALID_CRC,
	SHEAP_ERROR_COALESCING_PREV_BLOCK_ALTERED_INVALID_CRC,
	SHEAP_CONFIG_ERROR_INVALID_ALLOCATION_STRATEGY,
} sheaperd_assertion_t;

#if SHEAPERD_USE_SNPRINTF_ASSERT != 0
	extern char gAssertionBuffer[SHEAPERD_ASSERT_BUFFER_SIZE];
	#define SHEAPERD_PORT_ASSERT(msg, assertionType)																				 	 	\
	do {																																 	\
		snprintf(gAssertionBuffer, SHEAPERD_ASSERT_BUFFER_SIZE, "Assertion \"%s\" failed at line %d in %s", msg, __LINE__, __FILE__); 		\
		sheaperd_assert(gAssertionBuffer, assertionType);																		 	 	 	\
		/* The buffer is not cleared, as it is solely intended for strings and functions checking for '\0'. No problem should arise when */	\
		/* writing over older asserts */																									\
	} while(0)
#elif SHEAPERD_USE_SNPRINTF_ASSERT == 0
	#define SHEAPERD_PORT_ASSERT(msg, assertionType) sheaperd_assert(msg, assertionType)
#endif

typedef void (*sheaperd_assertion_cb) (sheaperd_assertion_t assert, char msg[]);

void sheaperd_assert(char msg[], sheaperd_assertion_t assertion);
void sheaperd_init(sheaperd_assertion_cb assertionCallback);

#endif /* SHEAPERD_H_ */
