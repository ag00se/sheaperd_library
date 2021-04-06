/*
 * Sheaperdopts.h
 *
 *  Created on: 03.03.2021
 *      Author: JK
 */

#ifndef SHEAPERDOPTS_H_
#define SHEAPERDOPTS_H_

#define SHEAPERD_SHEAP 				1
#define SHEAPERD_STACK_GUARD 		1
#define SHEAPERD_MPU_M3_M4_M7		1
#define SHEAPERD_MPU_MAX_REGIONS	16

#define SHEAPERD_PORT_ASSERT(msg) printf("Assertion \"%s\" failed at line %d in %s\r\n", msg, __LINE__, __FILE__);

#endif /* SHEAPERDOPTS_H_ */
