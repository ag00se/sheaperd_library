/*
 * Sheaperdopts.h
 *
 *  Created on: 03.03.2021
 *      Author: JK
 */

//Lets see if this is necessary...
#ifndef SHEAPERDOPTS_H_
#define SHEAPERDOPTS_H_

#define SHEAPERD_PORT_ASSERT(msg) 					\
do {												\
	handleFailedAssert(msg, __LINE__, __FILE__); 	\
} while(0)

#endif /* SHEAPERDOPTS_H_ */
