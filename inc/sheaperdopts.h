/*
 * sheaperdopts.h
 *
 *  Created on: 03.03.2021
 *      Author: JK
 */

#ifndef SHEAPERDOPTS_H_
#define SHEAPERDOPTS_H_

/* See the file 'opt.h' for available configurations */
#define SHEAPERD_SHEAP 						1
#define SHEAPERD_SHEAP_USE_EXTENDED_HEADER  1

#define SHEAPERD_STACK_GUARD 				1
#define MEMORY_PROTECTION					1
#define SHEAPERD_MPU_M3_M4_M7				1
#define STACKGUARD_HALT_ON_MEM_FAULT		1
#define STACKGUARD_NUMBER_OF_MPU_REGIONS	8
#define STACKGUARD_USE_MEMFAULT_HANDLER		1

/* Attention:
    In case that sheap is configured to disable irqs for allocation and deallocation, no mutex will be aquired regardless of the
    setting SHEAPERD_CMSIS_* configuration.
*/
#define SHEAPERD_SHEAP_DISABLE_IRQS         1

#endif /* SHEAPERDOPTS_H_ */
