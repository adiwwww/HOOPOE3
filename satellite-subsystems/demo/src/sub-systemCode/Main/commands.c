/*
 * commands.c
 *
 *  Created on: Dec 5, 2018
 *      Author: DBTn
 */
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include "../Global/freertosExtended.h"
#include <freertos/task.h>

#include <at91/utility/exithandler.h>
#include <at91/commons.h>
#include <at91/utility/trace.h>
#include <at91/peripherals/cp15/cp15.h>

#include <hal/Utility/util.h>
#include <hal/Timing/WatchDogTimer.h>
#include <hal/Timing/Time.h>
#include <hal/Drivers/I2C.h>
#include <hal/Drivers/LED.h>
#include <hal/boolean.h>
#include <hal/errors.h>

#include <hal/Storage/FRAM.h>

#include <string.h>

#include "commands.h"

#include "CMD/EPS_CMD.h"
#include "CMD/General_CMD.h"
#include "CMD/COMM_CMD.h"
#include "CMD/SW_CMD.h"
#include "CMD/payload_CMD.h"
#include "CMD/ADCS_CMD.h"

#include "../COMM/splTypes.h"
#include "../COMM/DelayedCommand_list.h"
#include "../Global/Global.h"
#include "../TRXVU.h"
#include "../Ants.h"
#include "HouseKeeping.h"
#include "../EPS.h"
#include "../payload/Request Management/CameraManeger.h"
#include "../payload/DataBase/DataBase.h"
#include "../CUF/uploadCodeTelemetry.h"

#include "hcc/api_fat.h"

#define create_task(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask) xTaskCreate( (pvTaskCode) , (pcName) , (usStackDepth) , (pvParameters), (uxPriority), (pxCreatedTask) ); vTaskDelay(10);

/*TODO:
 * 1. finish all commands function
 * 2. remove comments from AUC to execute commands
 * 3. change the way commands pass throw tasks
 */

xSemaphoreHandle xCTE = NULL;

TC_spl command_to_execute[COMMAND_LIST_SIZE];
int place_in_list = 0;

void save_ACK_s(Ack_type type, ERR_type err, command_id ACKcommandId)
{
	int i_error = f_managed_enterFS();
	if (i_error == 0)
	{
		check_int("f_managed_enterFS in AUC", i_error);
		save_ACK(type, err, ACKcommandId);
		f_managed_releaseFS();
	}
}

void copy_command(TC_spl source, TC_spl* to)
{
	// copy command from source to to
	to->id = source.id;
	to->type = source.type;
	to->subType = source.subType;
	to->length = source.length;
	to->time = source.time;
	memcpy(to->data, source.data, (int)source.length);
}
void reset_command(TC_spl *command)
{
	// set to 0 all parameters of command
	memset((void*)command, 0, SIZE_OF_COMMAND);
}

//todo: change name to more sugnifficent
int init_command()
{
	if (xCTE != NULL)
		return 1;
	// 1. create semaphore
	vSemaphoreCreateBinary(xCTE);
	// 2. set place in list to 0
	place_in_list = 0;
	// 3. allocate memory for data in command
	int i;
	for (i = 0; i < COMMAND_LIST_SIZE; i++)
	{
		reset_command(&command_to_execute[i]);
	}
	return 0;
}
int add_command(TC_spl command)
{
	portBASE_TYPE error;
	// 1. try to take semaphore
	if (xSemaphoreTake_extended(xCTE, MAX_DELAY) == pdTRUE)
	{
		// 2. if queue full
		if (place_in_list == COMMAND_LIST_SIZE)
		{
			error = xSemaphoreGive_extended(xCTE);
			check_portBASE_TYPE("could not return xCTE in add_command", error);
			return 1;
		}
		// 3. copy command to list
		copy_command(command, &command_to_execute[place_in_list]);
		// 4. moving forward current place in command queue
		place_in_list++;
		// 5. return semaphore
		error = xSemaphoreGive_extended(xCTE);
		check_portBASE_TYPE("cold not return xCTE in add_command", error);
	}

	return 0;
}
int get_command(TC_spl* command)
{
	portBASE_TYPE error;
	// 1. try to take semaphore
	if (xSemaphoreTake_extended(xCTE, MAX_DELAY) == pdTRUE)
	{
		// 2. check if there's commands in list
		if (place_in_list == 0)
		{
			error = xSemaphoreGive_extended(xCTE);
			check_portBASE_TYPE("get_command, xSemaphoreGive_extended(xCTE)", error);
			return 1;
		}
		// 3. copy first in command to TC_spl* command
		copy_command(command_to_execute[0], command);
		int i;
		// 4. move every command forward in queue
		for (i = 0; i < place_in_list - 1; i++)
		{
			reset_command(&command_to_execute[i]);
			copy_command(command_to_execute[i + 1], &command_to_execute[i]);
		}
		// 5. reset the last place for command
		reset_command(&command_to_execute[i]);
		place_in_list--;
		// 6. return semaphore
		error = xSemaphoreGive_extended(xCTE);
		check_portBASE_TYPE("get_command, xSemaphoreGive_extended(xCTE)", error);
	}
	return 0;
}
int check_number_commands()
{
	return place_in_list;
}

void act_upon_command(TC_spl decode)
{
	//later use in ACK
	switch (decode.type)
	{
	case (COMM_T):
		AUC_COMM(decode);
		break;
	case (GENERAL_T):
		AUC_general(decode);
		break;
	case (PAYLOAD_T):
		AUC_payload(decode);
		break;
	case (EPS_T):
		AUC_EPS(decode);
		break;
	case (TC_ADCS_T):
		AdcsCmdQueueAdd(&decode); // TODO: save ACK
		break;
	case (SOFTWARE_T):
		AUC_SW(decode);
		break;
	case (CUF_T):
		AUC_CUF(decode);
		break;
	case (TC_ONLINE_TM_T):
		AUC_onlineTM(decode);
		break;
	default:
		printf("wrong type: %d\n", decode.type);
		break;
	}
}


void cmd_error(Ack_type* type, ERR_type* err)
{
	*type = ACK_NOTHING;
	*err = ERR_FAIL;
}


void AUC_COMM(TC_spl decode)
{
	Ack_type type;
	ERR_type err;
	switch (decode.subType)
	{
	case (MUTE_ST):
		cmd_mute(&type, &err, decode);
		break;
	case (UNMUTE_ST):
		cmd_unmute(&type, &err);
		break;
	case (ACTIVATE_TRANS_ST):
		cmd_active_trans(&type, &err, decode);
		return;
		break;
	case (SHUT_TRANS_ST):
		cmd_shut_trans(&type, &err);
		break;
	case (CHANGE_TRANS_RSSI_ST):
		cmd_change_trans_rssi(&type, &err, decode);
		break;
	case (APRS_DUMP_ST):
		cmd_aprs_dump(&type, &err);
		break;
	case (STOP_DUMP_ST):
	   cmd_stop_dump(&type, &err);
		break;
	case (TIME_FREQUENCY_ST):
		cmd_time_frequency(&type, &err, decode);
		break;
	case (UPDATE_BIT_RATE_ST):
		cmd_change_def_bit_rate(&type, &err, decode);
		break;
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

void AUC_general(TC_spl decode)
{
	Ack_type type;
	ERR_type err;

	switch (decode.subType)
	{
	//todo: add generic ADCS I2C command as bypass to the ADCS logic = use the AdcsGenericI2C...

	case (GENERIC_I2C_ST):
		cmd_generic_I2C(&type, &err, decode);
		break;
	case (UPLOAD_TIME_ST):
		cmd_upload_time(&type, &err, decode);
		break;
	case (DUMP_ST):
		cmd_dump(decode);
		return;
		break;
	case (DELETE_PACKETS_ST):
		cmd_delete_TM(&type, &err, decode);
		break;
	case (RESET_FILE_ST):
		cmd_reset_file(&type, &err, decode);
		break;
	case (RESTSRT_FS_ST):
		cmd_reset_TLM_SD(&type, &err);
		break;
	case (REDEPLOY):
		break;
	case (ARM_DISARM):
		cmd_ARM_DIARM(&type, &err, decode);
		break;
	case (HARD_RESET_ST):
		cmd_hard_reset_cmponent(&type, &err, decode);
		break;
	case (RESET_SAT_ST):
		cmd_reset_satellite(&type, &err);
		break;
	case (SOFT_RESET_ST):
		cmd_soft_reset_cmponent(&type, &err, decode);
		break;
	case (GRACEFUL_RESET_ST):
		cmd_gracefull_reset_satellite(&type, &err);
		break;
	case (DUMMY_FUNC_ST):
		cmd_dummy_func(&type, &err);
		break;
	default:
		cmd_error(&type, &err);
		break;

	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

void AUC_payload(TC_spl decode)
{
	Ack_type type = ACK_CAMERA;
	ERR_type err = ERR_SUCCESS;

	Camera_Request request;
	request.cmd_id = decode.id;
	request.keepOnCamera = 24*60*60;	// currently one day, ToDo: just do something!
	memcpy(request.data, decode.data, SPL_TC_DATA_SIZE);

	switch (decode.subType)
	{
		case (SEND_PIC_CHUNCK_CHUNK_FIELD_ST):
			request.id = Image_Dump_chunkField;
			break;
		case (SEND_PIC_CHUNCK_BIT_FIELD_ST):
			request.id = Image_Dump_bitField;
			break;
		case (TAKE_IMG_ST):
			request.id = take_picture;
			break;
		case (TAKE_IMG_SPECIAL_VAL_ST):
			request.id = take_picture_with_special_values;
			break;
		case (TAKE_PICTURE_WITH_TIME_IN_BETWEEN):
			request.id = take_pictures_with_time_in_between;
			break;
		case (UPDATE_PHOTOGRAPHY_VALUES_ST):
			request.id = update_photography_values;
			break;
		case (DELETE_PIC_FILE_ST):
			request.id = delete_picture_file;
			break;
		case (DELETE_PIC_ST):
			request.id = delete_picture;
			break;
		case (MOV_IMG_CAM_OBS_ST):
			request.id = transfer_image_to_OBC;
			break;
		case (CREATE_THUMBNAIL_FROM_IMAGE_ST):
			request.id = create_thumbnail;
			break;
		case (CREATE_JPEG_FROM_IMAGE_ST):
			request.id = create_jpg;
			break;
		case (RESET_DATA_BASE_ST):
			request.id = reset_DataBase;
			break;
		case (SEND_IMAGE_DATA_BASE_ST):
			request.id = DataBase_Dump;
			break;
		case (UPDATE_DEF_DUR_ST):
			request.id = update_defult_duration;
			break;
		case (OFF_CAM_ST):
			request.id = Turn_Off_Camera;
			break;
		case (ON_CAM_ST):
			request.id = Turn_On_Camera;
			break;
		case (SET_CHUNK_SIZE):
			request.id = Set_Chunk_Size;
			break;
		default:
			cmd_error(&type, &err);
			break;
	}

	if (err == ERR_SUCCESS)
		addRequestToQueue(request);

}

void AUC_EPS(TC_spl decode)
{
	Ack_type type;
	ERR_type err;

	switch (decode.subType)
	{
	case (UPD_LOGIC_VOLT_ST):
		cmd_upload_volt_logic(&type, &err, decode);
		break;
	case (CHANGE_HEATER_TMP_ST):
		cmd_heater_temp(&type, &err, decode);
		break;
	case (UPD_COMM_VOLTAGE):
		cmd_upload_volt_COMM(&type, &err, decode);
		break;
	case (ALLOW_ADCS_ST):
		cmd_allow_ADCS(&type, &err, decode);
		break;
	case (SHUT_ADCS_ST):
		cmd_SHUT_ADCS(&type, &err, decode);
		break;
	case (ALLOW_CAM_ST):
		cmd_allow_CAM(&type, &err, decode);
		break;
	case (SHUT_CAM_ST):
		cmd_SHUT_CAM(&type, &err, decode);
		break;
	case (UPDATE_EPS_ALPHA_ST):
		cmd_update_alpha(&type, &err, decode);
		break;
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

void AUC_SW(TC_spl decode)
{
	Ack_type type;
	ERR_type err;

	switch (decode.subType)
	{
	case (RESET_APRS_LIST_ST):
		cmd_reset_APRS_list(&type, &err);
		break;
	case (RESET_DELAYED_CM_LIST_ST):
		cmd_reset_delayed_command_list(&type, &err);
		break;
	case (RESET_FRAM_ST):
		cmd_reset_FRAM(&type, &err, decode);
		break;
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

void AUC_onlineTM(TC_spl decode)
{
	Ack_type type;
	ERR_type err;
	switch (decode.subType)
	{
	case (RESET_APRS_LIST_ST):
		cmd_reset_APRS_list(&type, &err);
		break;
	case (GET_ONLINE_TM_INDEX_ST):
		cmd_get_onlineTM(&type, &err, decode);
		break;
	case (RESET_OFF_LINE_LIST_ST):
		cmd_reset_off_line(&type, &err, decode);
		break;
	case (ADD_ITEM_OFF_LINE_LIST_ST):
		cmd_add_item_off_line(&type, &err, decode);
		break;
	case (DELETE_ITEM_OFF_LINE_LIST_ST):
		cmd_delete_item_off_line(&type, &err, decode);
		break;
	case GET_OFFLINE_LIST_SETTING_ST:
		cmd_get_off_line_setting(&type, &err, decode);
		break;
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

void AUC_CUF(TC_spl decode)
{
	Ack_type type;
	ERR_type err;

	switch (decode.subType)
	{
	case 0:
		headerHandle(decode);
		break;
	case 1:
		addToArray(decode, (int) decode.data[0]);
		break;
	case 2:
		startCUFintegration();
		break;
	case 3:
		ExecuteCUF(decode.data);
		break;
	case 4:
		saveBackup();
		break;
	case 5:
		loadBackup();
		break;
	case 6:
		removeFiles();
		break;
	case 7:
		RemoveCUF(decode.data);
		break;
	case 8:
		DisableCUF(decode.data);
		break;
	case 9:
		EnableCUF(decode.data);
		break;
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK_s(type, err, decode.id);
#endif
}

#ifdef TESTING
void AUC_test(TC_spl decode)
{
	Ack_type type;
	ERR_type err;

	switch (decode.subType)
	{
	default:
		cmd_error(&type, &err);
		break;
	}
	//Builds ACK
#ifndef NOT_USE_ACK_HK
	save_ACK(type, err, decode.id);
#endif
}
#endif
