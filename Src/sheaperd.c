/*
 * Sheaperd.c
 *
 *  Created on: 03.03.2021
 *      Author: JK
 */

#include "sheaperd.h"

/*#define SHEAPERD_ASSERT(msg, assert) 				\
do { 												\
	if (!(assert) && gAssertFailed_cb != NULL) { 	\
		gAssertFailed_cb(msg)						\
	}												\
} while(0)

static sheaperd_onAssertFailedCallback_t gAssertFailed_cb = NULL;

void sheaperd_registerAssertFailedCallback(sheaperd_onAssertFailedCallback_t callback){
	gAssertFailed_cb = callback;
}
*/
