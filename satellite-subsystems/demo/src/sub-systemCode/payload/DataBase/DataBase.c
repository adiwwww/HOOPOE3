/*
 * ImageDataBase.c
 *
 *  Created on: 7 ׳‘׳�׳�׳™ 2019
 *      Author: I7COMPUTER
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>		//TODO: remove before flight. including printfs
#include <stdint.h>
#include <math.h>
#include <hcc/api_fat.h>
#include <hal/Boolean.h>
#include <hal/Timing/Time.h>
#include <hal/Utility/util.h>
#include <hal/Storage/FRAM.h>

#include <satellite-subsystems/GomEPS.h>

#include "../../Global/GlobalParam.h"
#include "../../Global/logger.h"

#include "../Misc/Boolean_bit.h"
#include "../Misc/Macros.h"
#include "../Misc/FileSystem.h"

#include "../Compression/ImageConversion.h"
#include "DataBase.h"

typedef struct __attribute__ ((__packed__))
{
	uint32_t frameAmount;
	uint32_t frameRate;
	uint8_t adcGain;
	uint8_t pgaGain;
	uint32_t exposure;
} CameraPhotographyValues;

struct __attribute__ ((__packed__)) ImageDataBase_t
{
	// size = 21 bytes
	unsigned int numberOfPictures;		///< current number of pictures saved on the satellite
	imageid nextId;						///< the next id we will use for a picture, (camera id)
	CameraPhotographyValues cameraParameters;
	Boolean8bit AutoThumbnailCreation;
};
#define SIZEOF_IMAGE_DATABASE sizeof(struct ImageDataBase_t)

#define DATABASE_FRAM_START (DATABASEFRAMADDRESS + SIZEOF_IMAGE_DATABASE)	///< The ImageDataBase's starting address in the FRAM, not including the general fields at the start of the database
#define DATABASE_FRAM_END (DATABASE_FRAM_START + MAX_NUMBER_OF_PICTURES * sizeof(ImageMetadata))	///< The ImageDataBases ending address in the FRAM

#define DATABASE_FRAM_SIZE (DATABASE_FRAM_END - DATABASEFRAMADDRESS)

//---------------------------------------------------------------

/*
 * Will put in the string the supposed filename of an picture based on its id
 * @param soon2Bstring the number you want to convert into string form,
 * string will contain the number in string form
*/
static void getFileName(imageid id, fileType type, char string[FILE_NAME_SIZE])
{
	char baseString[FILE_NAME_SIZE];

	switch (type)
	{
		case raw:
			sprintf(baseString, "i%u.raw", id);
			break;
		case jpg:
			sprintf(baseString, "i%u.jpg", id);
			break;
		case bmp:
			sprintf(baseString, "i%u.bmp", id);
			break;
		case t02:
			sprintf(baseString, "i%u.t02", id);
			break;
		case t04:
			sprintf(baseString, "i%u.t04", id);
			break;
		case t08:
			sprintf(baseString, "i%u.t08", id);
			break;
		case t16:
			sprintf(baseString, "i%u.t16", id);
			break;
		case t32:
			sprintf(baseString, "i%u.t32", id);
			break;
		case t64:
			sprintf(baseString, "i%u.t64", id);
			break;
		default:
			break;
	}

	strcpy(string, baseString);

	printf("file name = %s\n", string);
}

/*
 * Will restart the database (only FRAM), deleting all of its contents
*/
ImageDataBaseResult zeroImageDataBase()
{
	uint32_t database_fram_size = DATABASE_FRAM_END - DATABASEFRAMADDRESS;
	uint8_t zero[database_fram_size];

	for (uint32_t i = 0; i < database_fram_size; i++)
	{
		zero[i] = 0;
	}

	int result = FRAM_write((unsigned char*)zero, DATABASEFRAMADDRESS, database_fram_size);
	CMP_AND_RETURN(result, 0, DataBaseFramFail);

	return DataBaseSuccess;
}

ImageDataBaseResult updateGeneralDataBaseParameters(ImageDataBase database)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer)

	int FRAM_result = FRAM_write((unsigned char*)(database), DATABASEFRAMADDRESS, SIZEOF_IMAGE_DATABASE);
	if(FRAM_result != 0)	// checking if the read from theFRAM succeeded
	{
		return DataBaseFramFail;
	}

	return DataBaseSuccess;
}

//---------------------------------------------------------------

ImageDataBaseResult SearchDataBase_byID(imageid id, ImageMetadata* image_metadata, uint32_t* image_address, uint32_t database_current_address)
{
	int result;

	while (database_current_address < DATABASE_FRAM_END)
	{
		result = FRAM_read((unsigned char *)image_metadata, database_current_address, sizeof(ImageMetadata));
		CMP_AND_RETURN(result, 0, DataBaseFramFail);

		// printing for tests:
		printf("cameraId: %d, timestamp: %u, ", image_metadata->cameraId, image_metadata->timestamp);
		bit fileTypes[8];
		char2bits(image_metadata->fileTypes, fileTypes);
		printf("files types:");
		for (int j = 0; j < 8; j++) {
			printf(" %u", fileTypes[j].value);
		}
		printf(", angles: %u %u %u, marked = %u\n",
				image_metadata->angles[0], image_metadata->angles[1], image_metadata->angles[2], image_metadata->markedFor_TumbnailCreation);

		if (image_metadata->cameraId == id)
		{
			memcpy(image_address, &database_current_address, sizeof(uint32_t));
			return DataBaseSuccess;
		}
		else
		{
			database_current_address += sizeof(ImageMetadata);
		}
	}

	if (id == 0)
		return DataBaseFull;
	else
		return DataBaseIdNotFound;
}

ImageDataBaseResult SearchDataBase_byMark(uint32_t database_current_address, ImageMetadata* image_metadata, uint32_t* image_address)
{
	int result;

	while (database_current_address < DATABASE_FRAM_END)
	{
		result = FRAM_read((unsigned char *)image_metadata, database_current_address, sizeof(ImageMetadata));
		CMP_AND_RETURN(result, 0, DataBaseFramFail);

		if (image_metadata->markedFor_TumbnailCreation)
		{
			memcpy(image_address, &database_current_address, sizeof(uint32_t));
			return DataBaseSuccess;
		}
		else
		{
			database_current_address += sizeof(ImageMetadata);
		}
	}

	return DataBaseIdNotFound;
}

//---------------------------------------------------------------

ImageDataBaseResult checkForFileType(ImageMetadata image_metadata, fileType reductionLevel)
{
	bit fileTypes[8];
	char2bits(image_metadata.fileTypes, fileTypes);

	if (fileTypes[reductionLevel].value)
		return DataBaseSuccess;
	else
		return DataBaseNotInSD;
}

void updateFileTypes(ImageMetadata* image_metadata, uint32_t image_address, fileType reductionLevel, Boolean value)
{
	bit fileTypes[8];
	char2bits(image_metadata->fileTypes, fileTypes);

	if (value)
		fileTypes[reductionLevel].value = TRUE_bit;
	else
		fileTypes[reductionLevel].value = FALSE_bit;

	image_metadata->fileTypes = bits2char(fileTypes);

	FRAM_write((unsigned char*)image_metadata, image_address, sizeof(ImageMetadata));
}

uint32_t GetImageFactor(fileType image_type)
{
	return pow(2, image_type);
}

//---------------------------------------------------------------

ImageDataBaseResult readImageFromBuffer(imageid id, fileType image_type)
{
	char fileName[FILE_NAME_SIZE];

	ImageDataBaseResult result = GetImageFileName(id, image_type, fileName);
	DB_RETURN_ERROR(result);

	F_FILE *file = NULL;
	int error = f_managed_enterFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_open(fileName, "r", &file);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	uint32_t factor = GetImageFactor(image_type);
	byte* buffer = imageBuffer;

	error = ReadFromFile(file, buffer, IMAGE_SIZE / (factor * factor), 1);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_close(&file);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_releaseFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	return DataBaseSuccess;
}

ImageDataBaseResult saveImageToBuffer(imageid id, fileType image_type)
{
	char fileName[FILE_NAME_SIZE];
	ImageDataBaseResult result = GetImageFileName(id, image_type, fileName);
	DB_RETURN_ERROR(result);

	int error = f_managed_enterFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	F_FILE *file = NULL;
	error = f_managed_open(fileName, "r", &file);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	uint32_t factor = GetImageFactor(image_type);
	byte* buffer = imageBuffer;

	error = WriteToFile(file, buffer, IMAGE_SIZE / (factor * factor), 1);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_close(&file);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_releaseFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	return DataBaseSuccess;
}

//---------------------------------------------------------------

imageid getLatestID(ImageDataBase database)
{
	return database->nextId - 1;
}
unsigned int getNumberOfFrames(ImageDataBase database)
{
	return database->cameraParameters.frameAmount;
}

uint32_t getDataBaseStart()
{
	return DATABASE_FRAM_START;
}
uint32_t getDataBaseEnd()
{
	return DATABASE_FRAM_END;
}

//---------------------------------------------------------------

ImageDataBaseResult setDataBaseValues(ImageDataBase database)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer);

	database->numberOfPictures = 0;
	database->nextId = 1;
	database->AutoThumbnailCreation = TRUE_8BIT;
	setCameraPhotographyValues(database, DEFALT_FRAME_RATE, DEFALT_ADC_GAIN, DEFALT_PGA_GAIN, DEFALT_EXPOSURE, DEFALT_FRAME_AMOUNT);

	ImageDataBaseResult result = updateGeneralDataBaseParameters(database);
	DB_RETURN_ERROR(result);

	printf("\nRe: database->numberOfPictures = %u, database->nextId = %u; CameraParameters: frameAmount = %lu,"
			" frameRate = %lu, adcGain = %u, pgaGain = %u, exposure = %lu\n", database->numberOfPictures,
			database->nextId, database->cameraParameters.frameAmount, database->cameraParameters.frameRate,
			database->cameraParameters.adcGain, database->cameraParameters.pgaGain,
			database->cameraParameters.exposure);

	return DataBaseSuccess;
}

ImageDataBase initImageDataBase(Boolean first_activation)
{
	ImageDataBase database = malloc(SIZEOF_IMAGE_DATABASE);	// allocate the memory for the database's variables

	int FRAM_result = FRAM_read((unsigned char*)(database),  DATABASEFRAMADDRESS, SIZEOF_IMAGE_DATABASE);
	if(FRAM_result != 0)	// checking if the read from theFRAM succeeded
	{
		free(database);
		return NULL;
	}

	printf("numberOfPictures = %u, nextId = %u; CameraParameters: frameAmount = %lu, frameRate = %lu, adcGain = %u, pgaGain = %u, exposure = %lu\n", database->numberOfPictures, database->nextId, database->cameraParameters.frameAmount, database->cameraParameters.frameRate, database->cameraParameters.adcGain, database->cameraParameters.pgaGain, database->cameraParameters.exposure);

	if (database->nextId == 0 || first_activation)	// The FRAM is empty and the ImageDataBase wasn't initialized beforehand
	{
		zeroImageDataBase();
		setDataBaseValues(database);
	}

	return database;
}

ImageDataBaseResult resetImageDataBase(ImageDataBase database)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer);

	ImageDataBaseResult result = clearImageDataBase();
	CMP_AND_RETURN(result, DataBaseSuccess, result);

	result = zeroImageDataBase();
	CMP_AND_RETURN(result, DataBaseSuccess, result);

	result = setDataBaseValues(database);
	DB_RETURN_ERROR(result);

	return DataBaseSuccess;
}

//---------------------------------------------------------------

void setCameraPhotographyValues(ImageDataBase database, uint32_t frameRate, uint8_t adcGain, uint8_t pgaGain, uint32_t exposure, uint32_t frameAmount)
{
	database->cameraParameters.frameAmount = frameAmount;
	database->cameraParameters.frameRate = frameRate;
	database->cameraParameters.adcGain = adcGain;
	database->cameraParameters.pgaGain = pgaGain;
	database->cameraParameters.exposure = exposure;
}

//---------------------------------------------------------------

ImageDataBaseResult transferImageToSD_withoutSearch(imageid cameraId, uint32_t image_address, ImageMetadata image_metadata)
{
	FRAM_read((unsigned char*)&image_metadata, image_address, sizeof(ImageMetadata));

	int error = checkForFileType(image_metadata, raw);
	CMP_AND_RETURN(error, DataBaseNotInSD, DataBasealreadyInSD);

	// Reading the image to the buffer:

	vTaskDelay(CAMERA_DELAY);

	error = GECKO_ReadImage((uint32_t)cameraId, (uint32_t*)imageBuffer);
	if( error )
	{
		printf("\ntransferImageToSD Error = (%d) reading image!\n\r", error);
		return (GECKO_Read_Success - error);
	}

	vTaskDelay(DELAY);

	// Creating a file for the picture at iOBC SD:

	updateFileTypes(&image_metadata, image_address, raw, TRUE);

	error = saveImageToBuffer(cameraId, raw);
	CMP_AND_RETURN(error, DataBaseSuccess, error);

	// Updating the DataBase:

	error = checkForFileType(image_metadata, raw);
	CMP_AND_RETURN(error, DataBaseSuccess, DataBaseFail);

	WritePayloadLog(PAYLOAD_TRANSFERRED_IMAGE, image_metadata.cameraId);

	return DataBaseSuccess;
}

ImageDataBaseResult transferImageToSD(ImageDataBase database, imageid cameraId)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer);

	// Searching for the image on the database:

	ImageMetadata image_metadata;
	uint32_t image_address;

	int result;

	result = SearchDataBase_byID(cameraId, &image_metadata, &image_address, DATABASE_FRAM_START);
	DB_RETURN_ERROR(result);

	return transferImageToSD_withoutSearch(cameraId, image_address, image_metadata);
}

//---------------------------------------------------------------

ImageDataBaseResult DeleteImageFromOBC_withoutSearch(imageid cameraId, fileType type, uint32_t image_address, ImageMetadata image_metadata)
{
	FRAM_read((unsigned char*)&image_metadata, image_address, sizeof(ImageMetadata));

	char fileName[FILE_NAME_SIZE];
    getFileName(cameraId, type, fileName);

	int result = checkForFileType(image_metadata, type);
	DB_RETURN_ERROR(result);

	int error = f_managed_enterFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_delete(fileName);
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	error = f_managed_releaseFS();
	CMP_AND_RETURN(error, 0, DataBaseFileSystemError);

	updateFileTypes(&image_metadata, image_address, type, FALSE);

	return DataBaseSuccess;
}

ImageDataBaseResult DeleteImageFromOBC(ImageDataBase database, imageid cameraId, fileType type)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer);

	ImageMetadata image_metadata;
	uint32_t image_address;

	ImageDataBaseResult result = SearchDataBase_byID(cameraId, &image_metadata, &image_address, DATABASE_FRAM_START);
	DB_RETURN_ERROR(result);

    return DeleteImageFromOBC_withoutSearch(cameraId, type, image_address, image_metadata);
}

ImageDataBaseResult DeleteImageFromPayload(ImageDataBase database, imageid id)
{
	CHECK_FOR_NULL(database, DataBaseNullPointer);

	GomEpsResetWDT(0);

	int err = GECKO_EraseBlock(id);
	if( err )
	{
		printf("Error (%d) erasing image!\n\r",err);
		return (GECKO_Erase_Success - err);
	}

	vTaskDelay(DELAY);

	ImageMetadata image_metadata;
	uint32_t image_address;

	ImageDataBaseResult result = SearchDataBase_byID(id, &image_metadata, &image_address, DATABASE_FRAM_START);
	DB_RETURN_ERROR(result);

	for (fileType i = 0; i < NumberOfFileTypes; i++)
	{
		if(checkForFileType(image_metadata, i) == DataBaseSuccess)
		{
			DeleteImageFromOBC_withoutSearch(id, i, image_address, image_metadata);
			updateFileTypes(&image_metadata, image_address, i, FALSE);
		}
	}

	uint8_t zero = 0;
	for(unsigned int i = 0; i < sizeof(ImageMetadata); i += sizeof(uint8_t))
	{
		int FRAM_result = FRAM_write(&zero, image_address + i, sizeof(uint8_t));
		CMP_AND_RETURN(FRAM_result, 0, DataBaseFramFail);
	}

	database->numberOfPictures--;

	updateGeneralDataBaseParameters(database);

	WritePayloadLog(PAYLOAD_ERASED_IMAGE, image_metadata.cameraId);

	return DataBaseSuccess;
}

ImageDataBaseResult clearImageDataBase(void)
{
	int result;
	ImageMetadata image_metadata;
	uint32_t image_address = DATABASE_FRAM_START;

	while(image_address < DATABASE_FRAM_END)
	{
		vTaskDelay(DELAY);

		result = FRAM_read((unsigned char*)&image_metadata, image_address, sizeof(ImageMetadata));
		CMP_AND_RETURN(result, 0, DataBaseFramFail);

		image_address += sizeof(ImageMetadata);

		if (image_metadata.cameraId != 0)
		{
			for (fileType i = 0; i < NumberOfFileTypes; i++)
			{
				if(checkForFileType(image_metadata, i) == DataBaseSuccess)
				{
					result = DeleteImageFromOBC_withoutSearch(image_metadata.cameraId, i, image_address, image_metadata);												return result;
					if (result != DataBaseSuccess && result != DataBaseNotInSD)
					{
						return result;
					}
					updateFileTypes(&image_metadata, image_address, i, FALSE);
				}
			}
		}
	}

	return DataBaseSuccess;
}

//---------------------------------------------------------------

ImageDataBaseResult handleMarkedPictures(uint32_t nuberOfPicturesToBeHandled)
{
	ImageMetadata image_metadata;
	uint32_t image_address;

	uint32_t database_current_address = DATABASE_FRAM_START;

	ImageDataBaseResult DB_result;

	for (uint32_t i = 0; i < nuberOfPicturesToBeHandled; i++)
	{
		DB_result = SearchDataBase_byMark(database_current_address, &image_metadata, &image_address);
		database_current_address = image_address + sizeof(ImageMetadata);

		if ( DB_result == 0 )
		{
			TurnOnGecko();

			DB_result = transferImageToSD_withoutSearch(image_metadata.cameraId, image_address, image_metadata);
			if (DB_result != DataBaseSuccess && DB_result != DataBasealreadyInSD)
				return DB_result;

			TurnOffGecko();

			vTaskDelay(DELAY);

			DB_result = CreateImageThumbnail_withoutSearch(image_metadata.cameraId, 4, TRUE, image_address, image_metadata);
			if (DB_result != DataBaseSuccess && DB_result != DataBasealreadyInSD)
				return DB_result;

			vTaskDelay(DELAY);

			DB_result = DeleteImageFromOBC_withoutSearch(image_metadata.cameraId, raw, image_address, image_metadata);
			DB_RETURN_ERROR(DB_result);

			// making sure i wont lose the data written in the functions above to the FRAM:
			FRAM_read( (unsigned char*)&image_metadata, image_address, (unsigned int)sizeof(ImageMetadata)); // reading the id from the ImageDescriptor file

			image_metadata.markedFor_TumbnailCreation = FALSE_8BIT;
			FRAM_write( (unsigned char*)&image_metadata, image_address, (unsigned int)sizeof(ImageMetadata)); // reading the id from the ImageDescriptor file
		}
	}

	return DataBaseSuccess;
}

//---------------------------------------------------------------

ImageDataBaseResult writeNewImageMetaDataToFRAM(ImageDataBase database, time_unix time_image_taken, short Attitude[3])
{
	ImageMetadata image_metadata;
	uint32_t image_address;

	imageid zero = 0;
	ImageDataBaseResult result = SearchDataBase_byID(zero, &image_metadata, &image_address, DATABASE_FRAM_START);	// Finding space at the database(FRAM) for the image's ImageMetadata:
	CMP_AND_RETURN(result, DataBaseSuccess, DataBaseFull);

	image_metadata.cameraId = database->nextId;
	image_metadata.timestamp = time_image_taken;
	memcpy(&image_metadata.angles, Attitude, sizeof(short) * 3);

	if (database->AutoThumbnailCreation)
		image_metadata.markedFor_TumbnailCreation = TRUE_8BIT;
	else
		image_metadata.markedFor_TumbnailCreation = FALSE_8BIT;

	for (fileType i = 0; i < NumberOfFileTypes; i++) {
		updateFileTypes(&image_metadata, image_address, i, FALSE_bit);
	}

	result = FRAM_write((unsigned char*)&image_metadata, image_address, sizeof(ImageMetadata));
	CMP_AND_RETURN(result, 0, DataBaseFramFail);

	database->nextId++;
	database->numberOfPictures++;

	return DataBaseSuccess;
}

ImageDataBaseResult takePicture(ImageDataBase database, Boolean8bit testPattern)
{
	int err = 0;
	
	if (database->numberOfPictures == MAX_NUMBER_OF_PICTURES)
		return DataBaseFull;

	// Erasing previous image before taking one to clear this part of the SDs:

	for (unsigned int i = 0; i < database->cameraParameters.frameAmount; i++)
	{
		err = GECKO_EraseBlock(database->nextId + i);
		CMP_AND_RETURN(err, 0, GECKO_Erase_Success - err);
	}

	vTaskDelay(CAMERA_DELAY);

	unsigned int currentDate = 0;
	Time_getUnixEpoch(&currentDate);

	short Attitude[3];
	for (int i = 0; i < 3; i++) {
		Attitude[i] = get_Attitude(i);
	}

	err = GECKO_TakeImage( database->cameraParameters.adcGain, database->cameraParameters.pgaGain, database->cameraParameters.exposure, database->cameraParameters.frameAmount, database->cameraParameters.frameRate, database->nextId, testPattern);
	CMP_AND_RETURN(err, 0, GECKO_Take_Success - err);

	vTaskDelay(DELAY);

	// ImageDataBase handling:

	ImageDataBaseResult result;

	for (uint32_t numOfFramesTaken = 0; numOfFramesTaken < database->cameraParameters.frameAmount; numOfFramesTaken++)
	{
		vTaskDelay(DELAY);

		result = writeNewImageMetaDataToFRAM(database, currentDate, Attitude);
		DB_RETURN_ERROR(result);

		currentDate += database->cameraParameters.frameRate;
	}

	updateGeneralDataBaseParameters(database);

	WritePayloadLog(PAYLOAD_TOOK_IMAGE, getLatestID(database));

	return DataBaseSuccess;
}

ImageDataBaseResult takePicture_withSpecialParameters(ImageDataBase database, uint32_t frameAmount, uint32_t frameRate, uint8_t adcGain, uint8_t pgaGain, uint32_t exposure, Boolean8bit testPattern)
{
	CameraPhotographyValues regularParameters;
	memcpy(&regularParameters, &database->cameraParameters, sizeof(CameraPhotographyValues));

	setCameraPhotographyValues(database, frameRate, adcGain, pgaGain, exposure, frameAmount);

	ImageDataBaseResult DB_result = takePicture(database, testPattern);

	setCameraPhotographyValues(database, regularParameters.frameRate, regularParameters.adcGain, regularParameters.pgaGain, regularParameters.exposure, regularParameters.frameAmount);

	return DB_result;
}

//---------------------------------------------------------------

ImageDataBaseResult GetImageFileName(imageid id, fileType fileType, char fileName[FILE_NAME_SIZE])
{
	ImageMetadata image_metadata;
	uint32_t image_address;

	ImageDataBaseResult result = SearchDataBase_byID(id, &image_metadata, &image_address, DATABASE_FRAM_START);
	DB_RETURN_ERROR(result);

	if (fileType != bmp)
	{
		result = checkForFileType(image_metadata, fileType);
		DB_RETURN_ERROR(result);
	}

	getFileName(id, fileType, fileName);

	return DataBaseSuccess;
}

//---------------------------------------------------------------

uint32_t getDataBaseSize()
{
	return DATABASE_FRAM_SIZE;
}

ImageDataBaseResult getImageDataBaseBuffer(imageid start, imageid end, byte buffer[DATABASE_FRAM_SIZE], uint32_t* size)
{
	int result = FRAM_read((unsigned char*)(buffer),  DATABASEFRAMADDRESS, DATABASE_FRAM_SIZE);
	CMP_AND_RETURN(result, 0, DataBaseFramFail);

	uint32_t database_size = SIZEOF_IMAGE_DATABASE;
	Boolean previus_zero = FALSE;

	// Write MetaData:

	ImageMetadata image_metadata;
	for (uint32_t i = 0; i < MAX_NUMBER_OF_PICTURES; i++)
	{
		vTaskDelay(10);

		memcpy(&image_metadata, buffer + SIZEOF_IMAGE_DATABASE + i * sizeof(ImageMetadata), sizeof(ImageMetadata));

		if (image_metadata.cameraId != 0 && (image_metadata.cameraId >= start && image_metadata.cameraId <= end))
		{
			database_size += sizeof(ImageMetadata);

			// printing for tests:
			printf("cameraId: %d, timestamp: %u, inOBC:", image_metadata.cameraId, image_metadata.timestamp);
			bit fileTypes[8];
			char2bits(image_metadata.fileTypes, fileTypes);
			printf(", files:");
			for (int j = 0; j < 8; j++) {
				printf(" %u", fileTypes[j].value);
			}
			printf(", angles: %u %u %u, markedFor_4thTumbnailCreation = %u\n",
					image_metadata.angles[0], image_metadata.angles[1], image_metadata.angles[2], image_metadata.markedFor_TumbnailCreation);
		}
		else
		{
			if (previus_zero)
			{
				break;
			}
			else
			{
				previus_zero = TRUE;
			}
		}
	}

	memcpy(size, &database_size, sizeof(uint32_t));

	return DataBaseSuccess;
}