/** @file sheaperd.c
 *  @brief Provides some general initialization for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include "sheaperd.h"

#if SHEAPERD_DISABLE_CORTEXM3_M4_WRITE_BUFFERING
	typedef struct {
		uint32_t DISMCYCINT : 1;
		uint32_t DISDEFWBUF : 1;
		uint32_t DISFOLD 	: 1;
		uint32_t UNUSED		: 5;
		uint32_t DISFPCA	: 1;
		uint32_t DISOOFP	: 1;
		uint32_t RESERVED	: 22;
	} ACTLR_t;
	#define ACTLR ((ACTLR_t*)0xE000E008)
#endif

/**
* This buffer is used for the creation of assert messages using 'snprintf'
* This buffer should not be accessed outside of the sheaperd library
*/
char gAssertionBuffer[SHEAPERD_ASSERT_BUFFER_SIZE] = { 0 };
static sheaperd_assertion_cb gAssertionCallback = NULL;

void sheaperd_init(sheaperd_assertion_cb assertionCallback){
	gAssertionCallback = assertionCallback;
#if SHEAPERD_DISABLE_CORTEXM3_M4_WRITE_BUFFERING
	//Turn off write buffering (can result in imprecise fault becoming a precise fault)
	ACTLR->DISDEFWBUF = 1;
#endif
}

void sheaperd_assert(char msg[], sheaperd_assertion_t assertion){
	if(gAssertionCallback != NULL){
		gAssertionCallback(assertion, msg);
	}
}
