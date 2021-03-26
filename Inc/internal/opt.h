/** @file opt.h
 *  @brief Provides the default options for the sheaperd library.
 *
 *  @author JK
 *  @bug No known bugs.
 */

#ifndef INTERNAL_OPT_H_
#define INTERNAL_OPT_H_

//included first so any user settings take precedence
#include "Sheaperdopts.h"

//TODO: Maybe add posix alternative?
#ifndef SHEAPERD_CMSIS_2
#define SHEAPERD_CMSIS_2
#include "cmsis_os2.h"
#endif

#define ASSERT_TYPE(TYPE, VALUE) ((TYPE){ 0 } = (VALUE))
#define SHEAPERD_ASSERT(msg, assert) 	\
do { 									\
	if (!(assert)) { 					\
		SHEAPERD_PORT_ASSERT(msg);		\
	}									\
} while(0)

/** \def SHEAPERD_PORT_ASSERT(msg)
*    \brief This macro is executed if an assert fails.
*
*    The msg \msg provides an error message. The user can provide a macro for SHEAPERD_PORT_ASSERT.
*    If none is provided the message is logged via printf.
*/
#ifndef SHEAPERD_PORT_ASSERT
#define SHEAPERD_PORT_ASSERT(msg) 														\
do {																					\
	printf("Assertion \"%s\" failed at line %d in %s\r\n", msg, __LINE__, __FILE__); 	\
} while(0)
#endif

#ifdef SHEAPERD_INERCEPT_SBRK
void* _sbrk(ptrdiff_t incr);
#endif

#define SHEAPERD_SHEAP_PC_LOG_SIZE 			20

#define SHEAPERD_SHEAP_OVERWRITE_ON_FREE	1
#define SHEAPERD_SHEAP_OVERWRITE_VALUE		0xFF

#define SHEAPERD_CRC32_POLY				0x04C11DB7
#define SHEAPERD_CRC32_XOR_OUT			0xFFFFFFFF

#endif /* INTERNAL_OPT_H_ */
