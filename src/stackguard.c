/** @file stackguard.c
 *  @brief Provides the implementation of the stack protection using the MPU
 *
 *  @author JK
 *  @bug No known bugs.
 */

#include <internal/opt.h>

// don't include stackguard if not enabled via options
#if SHEAPERD_STACK_GUARD == 1
#if MEMORY_PROTECTION == 0
	#define MEMORY_PROTECTION 1
#endif
#include <stackguard.h>

#if SHEAPERD_CMSIS_2 == 1
static osMutexId_t gStackMutex_id;
static const osMutexAttr_t stackMutex_attr = {
	  "stackguard_mutex",
	  osMutexRecursive,
	  NULL,
	  0U
};
#endif

#if SHEAPERD_CMSIS_1 == 1
osMutexDef(stackguard_mutex);
osMutexId gStackMutex_id;
#endif

#define SCB 					(uint32_t*)0xE000ED00
#define SCB_CFSR				(uint32_t*)(((uint8_t*)SCB) + 0x028)
#define SCB_MMFAR				(uint32_t*)(((uint8_t*)SCB) + 0x034)
#define SCB_CFSR_MEMFAULTSR_Msk	0xFF
#define SCB_CFSR_DACCVIOL_Msk	1 << 1

typedef struct{
	int32_t taskId;
	mpu_region_t mpuRegion;
} stackguard_mpuRegion_t;

static uint8_t gNumberOfRegions = 0;
static uint8_t gNextUnusedRegion = 0;
static stackguard_mpuRegion_t gTasksRegions[STACKGUARD_NUMBER_OF_MPU_REGIONS] = { 0 };
static stackguarg_memFault_cb gMemFault_cb = NULL;

static bool isPowerOfTwo(uint32_t value);
static mpu_region_t createDefaultRegion(uint32_t number);
static stackguard_error_t removeRegion(uint32_t taskId);
static void fillRegionDefaults(mpu_region_t* region);

static bool stackguard_acquireMutex();
static bool stackguard_releaseMutex();
//static void handleMemFault(stackguard_stackFrame_t* stackFrame);
void MemManage_Handler(void);

#if STACKGUARD_HALT_ON_MEM_FAULT == 1
	#define HALT_IF_DEBUGGING()                              \
	  do {                                                   \
		if ((*(volatile uint32_t *)0xE000EDF0) & (1 << 0)) { \
		    __asm__("\tBKPT #0");                            \
		}                                                    \
	} while (0)
#elif
    #define HALT_IF_DEBUGGING()
#endif

#if STACKGUARD_USE_MEMFAULT_HANDLER == 1
void handleMemFault(stackguard_stackFrame_t* stackFrame) {
    if(stackFrame == NULL) {
        return;
    }
	if ((*SCB_CFSR & SCB_CFSR_MEMFAULTSR_Msk) != 0) {
		if ((*SCB_CFSR & SCB_CFSR_DACCVIOL_Msk) != 0) {
			if(gMemFault_cb != NULL){
				gMemFault_cb(*SCB_MMFAR, *stackFrame);
			}
		}
	}
	HALT_IF_DEBUGGING();
}

void MemManage_Handler(void) {
	__asm volatile(
		"\ttst lr, #4 				\n" // Check if msp or psp should be saved
		"\tite eq 					\n" // If condition
		"\tmrseq r0, msp 			\n" // If equal use msp
		"\tmrsne r0, psp 			\n" // If not equal use psp
		"\tb handleMemFault\n"
	);
}
#endif

stackguard_error_t stackguard_init(stackguarg_memFault_cb memFaultCallback){
	gMemFault_cb = memFaultCallback;
	memory_protection_disableMPU();
	gNumberOfRegions = 0;
	for(uint32_t i = 0; i < STACKGUARD_NUMBER_OF_MPU_REGIONS; i++){
		gTasksRegions[i].taskId = -1;
		gTasksRegions[i].mpuRegion = createDefaultRegion(i);
	}
	gNumberOfRegions = memory_protection_getNumberOfMPURegions();

#if SHEAPERD_NO_OS == 1
	util_error_t error = ERROR_NO_ERROR;
#elif SHEAPERD_CMSIS_1 == 1
	util_error_t error = util_initMutex(osMutex(stackguard_mutex), &gStackMutex_id);
#elif SHEAPERD_CMSIS_2 == 1
	util_error_t error = util_initMutex(&gStackMutex_id, &stackMutex_attr);
#endif

	SHEAPERD_ASSERT("Mutex creation failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_CREATION_FAILED);
	return gNumberOfRegions == 0 ? STACKGUARD_NO_MPU_AVAILABLE : STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_addTaskByteSize(uint32_t taskId, uint32_t* sp, uint32_t stackSize, mpu_access_permission_t initialAP, bool xn) {
    if(!isPowerOfTwo(stackSize)) {
        return STACKGUARD_MPU_INVALID_REGION_SIZE;
    }
    uint32_t exp = 0;
    size_t size = stackSize;
    while (size >>= 1) {
        exp++;
    }
    return stackguard_addTask(taskId, sp, (mpu_regionSize_t) (exp - 1), initialAP, xn);
}

stackguard_error_t stackguard_addTask(uint32_t taskId, uint32_t* sp, mpu_regionSize_t stackSize, mpu_access_permission_t initialAP, bool xn){
	if(gNextUnusedRegion >= gNumberOfRegions){
		return STACKGUARD_NO_MPU_REGION_LEFT;
	}
	if(!stackguard_acquireMutex()){
		return STACKGUARD_MUTEX_ACQUIRE_FAILED;
	}
	stackguard_mpuRegion_t region;
	region.taskId = taskId;
	region.mpuRegion.address = (uint32_t)sp;
	region.mpuRegion.number = gNextUnusedRegion;
	region.mpuRegion.size = stackSize;
	region.mpuRegion.ap = initialAP;
	fillRegionDefaults(&region.mpuRegion);
	region.mpuRegion.xn = xn;

	mpu_error_t error = memory_protection_configureRegion(&region.mpuRegion, false);
	switch (error){
		case INVALID_REGION_ADDRESS:
			stackguard_releaseMutex();
			return STACKGUARD_INVALID_MPU_ADDRESS;
		case INVALID_REGION_ADDRESS_ALIGNMENT:
			stackguard_releaseMutex();
			return STACKGUARD_INVALID_STACK_ALIGNMENT;
		case INVALID_REGION_NUMBER:
			stackguard_releaseMutex();
			return STACKGUARD_INVALID_REGION_NUMBER;
		default:
			break;
	}
	gTasksRegions[gNextUnusedRegion] = region;
	while (gNextUnusedRegion < gNumberOfRegions && gTasksRegions[gNextUnusedRegion].taskId != -1) {
		gNextUnusedRegion++;
	}
	stackguard_releaseMutex();
	return STACKGUARD_NO_ERROR;
}

stackguard_error_t stackguard_removeTask(uint32_t taskId){
	if(!stackguard_acquireMutex()){
		return STACKGUARD_MUTEX_ACQUIRE_FAILED;
	}
	stackguard_error_t error = removeRegion(taskId);
	stackguard_releaseMutex();
	return error;
}

void stackguard_taskSwitchIn(uint32_t taskId, bool enableMPU){
	if(!memory_protection_isMPUEnabled()){
		SHEAPERD_ASSERT("Stackguard task switch in: MPU is not enabled.", false, STACKGUARD_MPU_NOT_ENABLED);
	}
	memory_protection_disableMPU();
	for(int i = 0; i < gNumberOfRegions; i++){
		stackguard_mpuRegion_t region = gTasksRegions[i];
		if(region.taskId != -1){
			region.mpuRegion.ap = region.taskId == taskId ? MPU_REGION_ALL_ACCESS_ALLOWED : STACKGUARD_DEFAULT_TASK_SWITCH_OUT_PERMISSION;
			region.mpuRegion.number = i;
			memory_protection_configureRegion(&region.mpuRegion, false);
		}
	}
	if(enableMPU) {
	    memory_protection_enableMPU();
	}
}

stackguard_error_t stackguard_guard(){
	return memory_protection_enableMPU() == NO_MPU_AVAILABLE ? STACKGUARD_NO_MPU_AVAILABLE : STACKGUARD_NO_ERROR;
}

static bool isPowerOfTwo(uint32_t value) {
    return (value != 0) && ((value & (value - 1)) == 0);
}

static stackguard_error_t removeRegion(uint32_t taskId){
	for(uint32_t i = 0; i < gNumberOfRegions; i++){
		if (gTasksRegions[i].taskId == taskId) {
			gTasksRegions[i].taskId = -1;
			gTasksRegions[i].mpuRegion = createDefaultRegion(0);
			if(i < gNextUnusedRegion){
				gNextUnusedRegion = i;
			}
			return STACKGUARD_NO_ERROR;
		}
	}
	return STACKGUARD_TASK_NOT_FOUND;
}

static void fillRegionDefaults(mpu_region_t* region){
	region->enabled = true;
	region->cachable = true;
	region->bufferable = false;
	region->shareable = true;
	region->tex = MPU_DEFAULT_TEX;
	region->xn = false;
	region->srd = 0;
}

static mpu_region_t createDefaultRegion(uint32_t number){
	mpu_region_t region = {
			.address = 0,
			.number = number,
			.size = REGIONSIZE_32B,
			.ap = MPU_REGION_ALL_ACCESS_DENIED
	};
	fillRegionDefaults(&region);
	return region;
}

static bool stackguard_acquireMutex(){
	#if SHEAPERD_NO_OS == 1
		return true;
	#else
        util_error_t error = util_acquireMutex(gStackMutex_id, SHEAPERD_DEFAULT_MUTEX_WAIT_TICKS);
        SHEAPERD_ASSERT("Mutex acquire failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_ACQUIRE_FAILED);
        return error == ERROR_NO_ERROR;
	#endif
}

static bool stackguard_releaseMutex(){
	#if SHEAPERD_NO_OS == 1
		return true;
    #else
        util_error_t error = util_releaseMutex(gStackMutex_id);
        SHEAPERD_ASSERT("Mutex release failed.", error == ERROR_NO_ERROR, SHEAPERD_ERROR_MUTEX_RELEASE_FAILED);
        return error == ERROR_NO_ERROR;
	#endif
}

#endif
