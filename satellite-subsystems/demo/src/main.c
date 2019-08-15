/*
 * main.c
 *      Author: Akhil
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <satellite-subsystems/version/version.h>

#include <at91/utility/exithandler.h>
#include <at91/commons.h>
#include <at91/utility/trace.h>
#include <at91/peripherals/cp15/cp15.h>


#include <hal/Utility/util.h>
#include <hal/Timing/WatchDogTimer.h>
#include <hal/Timing/Time.h>
#include <hal/Drivers/LED.h>
#include <hal/Drivers/I2C.h>
#include <hal/Drivers/SPI.h>
#include <hal/boolean.h>
#include <hal/version/version.h>

#include <hal/Storage/FRAM.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sub-systemCode/COMM/GSC.h"
#include "sub-systemCode/Main/commands.h"
#include "sub-systemCode/Main/Main_Init.h"
#include "sub-systemCode/Global/Global.h"
#include "sub-systemCode/EPS.h"

#include "sub-systemCode/Main/CMD/ADCS_CMD.h"
#include "sub-systemCode/ADCS/AdcsMain.h"

#include "sub-systemCode/ADCS/AdcsTest.h"
#define DEBUGMODE

#ifndef DEBUGMODE
	#define DEBUGMODE
#endif


#define HK_DELAY_SEC 10
#define MAX_SIZE_COMMAND_Q 20

void save_time()
{
	int err;
	// get current time from time module
	time_unix current_time;
	err = Time_getUnixEpoch(&current_time);
	check_int("set time, Time_getUnixEpoch", err);
	byte raw_time[TIME_SIZE];
	// converting the time_unix to raw (small endien)
	raw_time[0] = (byte)current_time;
	raw_time[1] = (byte)(current_time >> 8);
	raw_time[2] = (byte)(current_time >> 16);
	raw_time[3] = (byte)(current_time >> 24);
	// writing to FRAM the current time
	err = FRAM_write(raw_time, TIME_ADDR, TIME_SIZE);
	check_int("set time, FRAM_write", err);
}

void Command_logic()
{
	TC_spl command;
	int error = 0;
	do
	{
		error = get_command(&command);
		if (error == 0)
			act_upon_command(command);
	}
	while (error == 0);
}

/*
void TestGenericI2cTelemetry()
{
	int err = 0;
	cspace_adcs_powerdev_t device_ctrl;
	device_ctrl.fields.motor_cubecontrol= TRUE;
	device_ctrl.fields.pwr_cubesense = TRUE;
	device_ctrl.fields.signal_cubecontrol = TRUE;
	err = cspaceADCS_setPwrCtrlDevice(ADCS_ID, &device_ctrl);
	if(0!=err){
		printf("'cspaceADCS_setPwrCtrlDevice' = %d\n",err);
		return;
	}

	unsigned int tlm_id = 0;
	unsigned int tlm_length = 0;
	printf("choose from driver telemetry? Y=1/N=0\n");
	while(UTIL_DbguGetIntegerMinMax(&err,0,1)==0);

	printf("please choose TLM id(0 to cancel):\n");
	while(UTIL_DbguGetIntegerMinMax(&tlm_id,0,200)==0);
	if(tlm_id == 0) return;

	printf("please choose TLM length(0 to cancel):\n");
	while(UTIL_DbguGetIntegerMinMax(&tlm_length,0,300)==0);
	if(tlm_length == 0) return;

	byte buffer[300] ={0};
	adcs_i2c_cmd *i2c_cmd;

	AdcsGenericI2cCmd(tlm_id,buffer,tlm_length);

	if(err == 1){
		cspace_adcs_magfieldvec_t info_data;
		switch(tlm_id){
		case 151:
			err = cspaceADCS_getMagneticFieldVec(ADCS_ID, &info_data);
			err = err*1;
			memcpy(&info_data,buffer,sizeof(info_data));
			err = 0;
		break;
		}
	}
	else{
		for(int i=0;i<tlm_length;i++)
		{
			printf("%X\t",buffer[i]);
		}
	}

}*/

void taskMain()
{
	WDT_startWatchdogKickTask(10 / portTICK_RATE_MS, FALSE);
	InitSubsystems();

	vTaskDelay(100);
	printf("init finished\n");
	SubSystemTaskStart();
	printf("Task Main start: ADCS test mode\n");

	//portTickType xLastWakeTime = xTaskGetTickCount();
	//const portTickType xFrequency = 1000;
	
	TC_spl adcsCmd;
	adcsCmd.id = TC_ADCS_T;
	//tests testId;
	int err;
	cspace_adcs_estmode_sel est_mode = 0;
	cspace_adcs_attctrl_mod_t ctrl_mode = {.raw = {0}};
	cspace_adcs_runmode_t runmode = 0;
	cspace_adcs_powerdev_t pwr_device = {.raw = {0}};
	unsigned int input;
	gom_eps_hk_t eps_tlm;
	while(1)
	{
		GomEpsGetHkData_general(0,&eps_tlm);
		printf("Use Driver? (1 = Y / 0 = N)\n");
		UTIL_DbguGetIntegerMinMax(&input, 0,1);
		if(1 == input){
			printf("\t0) exit\n\r");
			printf("\t1) Set run mode\n\r");
			printf("\t2) Set control mode\n\r");
			printf("\t3) Set estimation mode\n\r");
			printf("\t4) Set power mode\n\r");
			UTIL_DbguGetIntegerMinMax(&input, 0,4);
			switch(input){
			case 0: continue;
			case 1:
				printf("choose run mode(0 to 3):\n");
				while(UTIL_DbguGetIntegerMinMax(&input, 0,3)==0);
				err = cspaceADCS_setRunMode(ADCS_ID, runmode);
				if(0 != err) printf("error in execution = %d\n",err);
				break;
			case 2:
				printf("choose control mode with no override(0 to 6):\n");
				while(UTIL_DbguGetIntegerMinMax(&input, 0,13)==0);
				err = cspaceADCS_setAttCtrlMode(ADCS_ID,&ctrl_mode);
				if(0 != err) printf("error in execution = %d\n",err);
				break;
			case 3:
				printf("choose estimation(0 to 6):\n");
				while(UTIL_DbguGetIntegerMinMax(&input, 0,6)==0);
				err = cspaceADCS_setAttEstMode(ADCS_ID,est_mode);
				if(0 != err) printf("error in execution = %d\n",err);
				break;
			case 4:
				printf("\nChoose power mode using a 3 byte bit field, as described:\n");
				printf("signal_cubecontrol : 2,	 ///< Power CubeControl Signal");
				printf("motor_cubecontrol  : 2,	 ///< Power CubeControl Motor");
				printf("pwr_cubesense	   : 2,	 ///< Power CubeSense");
				printf("pwr_cubestar 	   : 2;	 ///< Power CubeStar");
				printf("pwr_cubewheel1     : 2,	 ///< Power CubeWheel 1");
				printf("pwr_cubewheel2     : 2,	 ///< Power CubeWheel 2");
				printf("pwr_cubewheel3     : 2,	 ///< Power CubeWheel 3");
				printf("pwr_motor 		   : 2;	 ///< Power Motor");
				printf("pwr_gps; 		   : 8;	 ///< Power GPS LNA");

				while(UTIL_DbguGetHexa32((unsigned int*)pwr_device.raw) == 0);
				err = cspaceADCS_getPwrCtrlDevice(ADCS_ID, &pwr_device);
				if(0 != err) printf("error in execution = %d\n",err);
				break;
			}
		}
		printf("Send new command\n");
		printf("Enter ADCS sub type\n");
		UTIL_DbguGetIntegerMinMax(&input, 0,1000);
		if (input != 0)
		{
			if (input < 200)
			{
				adcsCmd.subType = input;
				printf("Enter ADCS command data in hex(Max length 200 bytes)\n");
				while (UTIL_DbguGetHexa32((unsigned int*)adcsCmd.data) == 0);
				err = AdcsCmdQueueAdd(&adcsCmd);
				printf("ADCS command error = %d\n\n\n", err);

			}
		}

		vTaskDelay(1000);
		/* UTIL_DbguGetIntegerMinMax(testId,0,TEST_AMOUNT);
		switch(testId)
		{
			case SEND_CMD:
				AddCommendToQ();
				break;
			case MAG_TEST:
				Mag_Test();
			break;
			case ADCS_RUN_MODE:

				break;
			case ADCS_GENERIC_I2C:
				break;
			case MAG_CMD:
				break;
			case ERR_FLAG_TEST:
				ErrFlagTest();
				break;
			case PRINT_ERR_FLAG:
				printErrFlag();
				break;
			case TEST_AMOUNT:
				break;
		} */
	}
}

int main()
{
	TRACE_CONFIGURE_ISP(DBGU_STANDARD, 2000000, BOARD_MCK);
	// Enable the Instruction cache of the ARM9 core. Keep the MMU and Data Cache disabled.
	CP15_Enable_I_Cache();

	WDT_start();

	printf("Task Main 2121\n");
	xTaskGenericCreate(taskMain, (const signed char *)("taskMain"), 4000, NULL, configMAX_PRIORITIES - 2, NULL, NULL, NULL);
	printf("start sch\n");
	vTaskStartScheduler();
	while(1){
		printf("should not be here\n");
		vTaskDelay(2000);
	}

	return 0;
}
