/******************************************************************************
 * Copyright (C) 2013-21014 Marco Giammarini <http://www.warcomeb.it>
 * 
 * Author(s):
 *  Marco Giammarini <m.giammarini@warcomeb.it>
 *  
 * Project: gps-nmea
 * Package: -
 * Version: 0.0
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>
 ******************************************************************************/

/**
 * @file gps-nmea.c
 * @author Marco Giammarini <m.giammarini@warcomeb.it>
 * @brief GPS NMEA protocol functions implementation.
 */
#include "gps-nmea.h"

#define GPSNMEA_ACTIVE                     1
#define GPSNMEA_NO_ACTIVE                  0
#define GPSNMEA_BAUDRATE                   9600

#define GPSNMEA_START                      '$'
#define GPSNMEA_STOP                       '*'
#define GPSNMEA_SEPARATOR                  ','
#define GPSNMEA_DECIMAL                    '.'
#define GPSNMEA_END                        "\r\n"
#define GPSNMEA_END1                       '\r'
#define GPSNMEA_END2                       '\n'

#define GPSNMEA_STRING_GGA                 "GPGGA"
#define GPSNMEA_STRING_GLL                 "GPGLL"
#define GPSNMEA_STRING_RMC                 "GPRMC"
#define GPSNMEA_STRING_GSV                 "GPGSV"
#define GPSNMEA_STRING_GSA                 "GPGSA"
#define GPSNMEA_STRING_VTG                 "GPVTG"
#define GPSNMEA_STRING_ZDA                 "GPZDA"
#define GPSNMEA_STRING_PMTK001             "PMTK001"

#define GPSNMEA_MSG_MAX_LENGTH             80
//
//#define GPSNMEA_RX_BUFFER_MASK             0x7F
//#define GPSNMEA_TX_BUFFER_MASK             0x7F
//
//static uint8_t GpsNmea_rxBufferIndex;
//static uint8_t GpsNmea_txBufferIndex;

//static uint8_t GpsNmea_rxBuffer[GPSNMEA_RX_BUFFER_MASK + 1];

static Uart_DeviceHandle GpsNmea_device;
static uint8_t GpsNmea_active = GPSNMEA_NO_ACTIVE;

#define GPSENMEA_POS_STOP(n)               (n-5)
#define GPSENMEA_POS_END1(n)               (n-2)
#define GPSENMEA_POS_END2(n)               (n-1)

static union GpsNmea_ReadStatusType
{
    uint8_t status;
    
    struct {
        uint8_t reading             :1;
        uint8_t computeChecksum     :1;
        uint8_t receivingChecksum   :1;
        uint8_t nParameter          :4;
        uint8_t notUsed             :1;
    } flags;
} GpsNmea_readStatus;

static uint8_t GpsNmea_readWordIndex;
static uint8_t GpsNmea_readCharIndex;
static uint8_t GpsNmea_readChecksum;

static union GpsNmea_RxBufferType
{
    char message[20][15];
    
    struct
    {
        char command[15];
        /** UTC time of fix in hhmmss.ddd where hh is hour, mm is minutes, ss is seconds and ddd is decimal part. */
        char utcTime[15];
        /** Status indicator: A is valid, V is invalid */
        char status[15];
        char latitudeCoordinate[15];
        char latitudeDirection[15];
        char longitudeCoordinate[15];
        char longitudeDirection[15];
        /** Speed over ground measured in knots that is one nautical mile (1.852 km) per hour. */
        char speed[15];
        /** This indicates the direction that the device is currently moving in, in azimuth. */
        char heading[15];
        /** UTC date of fix in ddmmyy where dd is day, mm is months and yy is year. */
        char utcDate[15];
        char magneticVariation[15];   /* Not supported */
        char magneticDirection[15];   /* Not supported */
        /** Mode indicator: A is autonomous, N is data not valid */
        char mode[15];
    } rmc;

    struct
    {
        char command[15];
        char utcTime[15];
        char utcDay[15];
        char utcMonth[15];
        char utcYear[15];
        char localHour[15];
        char localMinute[15];
    } zda;

    struct
    {
        char command[15];
        char responseCommand[15];
        char responseFlag[15];
    } ptmk001;
} GpsNmea_rxBuffer;

static char GpsNmea_rxChecksumBuffer[4];
static uint8_t GpsNmea_readChecksumIndex;

union GpsNmea_StatusType GpsNmea_status;

GpsNmea_Errors GpsNmea_init (Uart_DeviceHandle device)
{
    if (GpsNmea_active == GPSNMEA_NO_ACTIVE)
    {
        GpsNmea_device = device;
        Uart_setBaudRate(GpsNmea_device,GPSNMEA_BAUDRATE);
        Uart_init(GpsNmea_device);
        return GPSNMEA_ERROR_OK;
    }
    else
    {
        return GPSNMEA_ERROR_JUST_ACTIVE;        
    }
	
    GpsNmea_readStatus.status = 0;
}

/**
 * Enable communication port for GPS device.
 * 
 * @return Return the status of the operation by an element of GpsNmea_Errors
 */
GpsNmea_Errors GpsNmea_enable (void)
{
    if (GpsNmea_active == GPSNMEA_NO_ACTIVE)
    {
        Uart_enable(GpsNmea_device);
        GpsNmea_active = GPSNMEA_ACTIVE;
        return GPSNMEA_ERROR_OK;
    }
    else
    {
        return GPSNMEA_ERROR_JUST_ACTIVE;
    }
}

/**
 * Disable communication port for GPS device.
 * 
 * @return Return the status of the operation by an element of GpsNmea_Errors
 */
GpsNmea_Errors GpsNmea_disable (void)
{
    if (GpsNmea_active == GPSNMEA_ACTIVE)
    {
        Uart_disable(GpsNmea_device);
        GpsNmea_active = GPSNMEA_NO_ACTIVE;
        return GPSNMEA_ERROR_OK;
    }
    else
    {
        return GPSNMEA_ERROR_NO_ACTIVE;
    }
}

static uint8_t GpsNmea_computeChecksum (const uint8_t* data, uint8_t start, uint8_t length)
{
    uint8_t checksum = 0;
    uint8_t i;

     for (i = start; i < length; ++i)
     {
         checksum = checksum ^ (*data);
         data++;
     }

     return checksum;
}

GpsNmea_RxMessageType GpsNmea_getReceiveMessageType (void)
{
    if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_RMC) == 0)
        return GPSNMEA_RXMSG_RMC;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_GGA) == 0)
        return GPSNMEA_RXMSG_GGA;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_GLL) == 0)
        return GPSNMEA_RXMSG_GLL;        
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_GSV) == 0)
        return GPSNMEA_RXMSG_GSV;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_GSA) == 0)
        return GPSNMEA_RXMSG_GSA;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_VTG) == 0)
        return GPSNMEA_RXMSG_VTG;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_ZDA) == 0)
        return GPSNMEA_RXMSG_ZDA;
    else if (stringCompare(GpsNmea_rxBuffer.message[0],GPSNMEA_STRING_PMTK001) == 0)
        return GPSNEMA_RXMSG_PMTK001;
    else
        return GPSNMEA_RXMSG_ERROR;
}

/**
 * 
 * @return Return the status of the operation by an element of GpsNmea_Errors
 */
GpsNmea_Errors GpsNmea_addReceiveChar (void)
{
    char c;
    Uart_getChar(GpsNmea_device,&c);

    if (c == GPSNMEA_START)
    {
        GpsNmea_readStatus.status = 0;

        GpsNmea_readStatus.flags.reading = 1;
        GpsNmea_readStatus.flags.computeChecksum = 1;
        GpsNmea_readStatus.flags.receivingChecksum = 0;

        GpsNmea_readWordIndex = 0;
        GpsNmea_readCharIndex = 0;
        GpsNmea_readChecksum = 0;
        
        GpsNmea_readChecksumIndex = 0;
    }
    
    if (GpsNmea_readStatus.flags.reading == 1)
    {
        if ((c == GPSNMEA_END1) || (c == GPSNMEA_END2))
        {
            /* Delete last char to detect end of string. */
            GpsNmea_rxBuffer.message[GpsNmea_readWordIndex][GpsNmea_readCharIndex] = 0;
            GpsNmea_readWordIndex++;
            GpsNmea_readStatus.flags.reading = 0;
            
            GpsNmea_rxChecksumBuffer[GpsNmea_readChecksumIndex] = 0;
            GpsNmea_readChecksumIndex = 0;
            
            GpsNmea_readStatus.status = 0;
            /* Save number of parameter! */
            GpsNmea_readStatus.flags.nParameter = GpsNmea_readWordIndex;
            
            /* Notify to main application that we received a new message */
            GpsNmea_status.flags.commandReady = 1;
        }
        else if (c != GPSNMEA_START)
        {
            if (GpsNmea_readStatus.flags.receivingChecksum == 1)
            {
                GpsNmea_rxChecksumBuffer[GpsNmea_readChecksumIndex] = c;
                GpsNmea_readChecksumIndex++;
            }

            if ((GpsNmea_readStatus.flags.computeChecksum == 1) && (c == GPSNMEA_STOP))
            {
                GpsNmea_readStatus.flags.computeChecksum = 0;
                GpsNmea_readStatus.flags.receivingChecksum = 1;
            }
            
            if (GpsNmea_readStatus.flags.computeChecksum == 1)
                GpsNmea_readChecksum ^= c;
            
            GpsNmea_rxBuffer.message[GpsNmea_readWordIndex][GpsNmea_readCharIndex] = c;
            if ((c == GPSNMEA_SEPARATOR) || (c == GPSNMEA_STOP))
            {
                GpsNmea_rxBuffer.message[GpsNmea_readWordIndex][GpsNmea_readCharIndex] = 0;
                GpsNmea_readWordIndex++;
                GpsNmea_readCharIndex = 0;
            }
            else
            {
                GpsNmea_readCharIndex++;
            }
        }
    }
    else
    {
        GpsNmea_status.flags.commandReady = 0;
        return GPSNMEA_ERROR_WRONG_CHAR;
    }
    
    
#if 0
    if (IS_DIGIT(c) || IS_UPPERLETTER(c) || IS_UPPERLETTER(c) ||
        (c == GPSNMEA_START) || (c == GPSNMEA_SEPARATOR) ||
        (c == GPSNMEA_DECIMAL) || (c == GPSNMEA_STOP) || (c == GPSNMEA_END1))
    {
    	GpsNmea_rxBuffer[GpsNmea_rxBufferIndex] = c;
    	GpsNmea_rxBufferIndex++;
    }
    else if (c == GPSNMEA_END2)
    {
    	GpsNmea_rxBuffer[GpsNmea_rxBufferIndex] = c;
        GpsNmea_rxBufferIndex++;
    	GpsNmea_status.flags.commandReady = 1;
    }
    else
    {
    	GpsNmea_rxBufferIndex = 0;
    	return GPSNMEA_ERROR_WRONG_CHAR;
    }
    
    if (!GpsNmea_status.flags.commandReady && GpsNmea_rxBufferIndex > GPSNMEA_MSG_MAX_LENGTH)
    {
        GpsNmea_rxBufferIndex = 0;
        return GPSNMEA_ERROR_MSG_TOO_LONG;
    }
#endif
    
    return GPSNMEA_ERROR_OK;
}

/**
 * The time message are in the format hhmmss.dd where
 * - hh is hours
 * - mm is minutes
 * - ss is seconds
 * - dd is the decimal part of seconds
 * .
 * 
 * @param char* message The message that we need to convert.
 * @param Time_TimeType* result The result of the conversion.
 * @return Return the status of the operation by an element of @ref GpsNmea_Errors
 */
static GpsNmea_Errors GpsNmea_parseTime (const char* message, Time_TimeType* result)
{
    if (dtu8((uint8_t*)message,&(result->hours),2) != ERRORS_UTILITY_CONVERSION_OK)
        return GPSNMEA_ERROR_TIME_CONVERSION;
    
    message += 2;
    
    if (dtu8((uint8_t*)message,&(result->minutes),2) != ERRORS_UTILITY_CONVERSION_OK)
        return GPSNMEA_ERROR_TIME_CONVERSION;
    
    message += 2;

    if (dtu8((uint8_t*)message,&(result->seconds),2) != ERRORS_UTILITY_CONVERSION_OK)
        return GPSNMEA_ERROR_TIME_CONVERSION;
    
    return GPSNMEA_ERROR_OK;
}

/**
 * The coordinate message are in two format xxmm.dddd or xxxmm.dddd where
 * - xx or xxx is degrees
 * - mm is minutes
 * - dddd is the decimal part of minutes
 * .
 * 
 * @param char* message The message that we need to parse.
 * @param GpsNmea_CoordinateType* result This variable is used to store the conversion result.
 * @return Return the status of the operation by an element of @ref GpsNmea_Errors
 */
static GpsNmea_Errors GpsNmea_parseCoordinate (char* message, GpsNmea_CoordinateType* result)
{
    uint8_t digit;
    char* dotOnMessage = message+2;

    float minutePart = 0.0;
    uint8_t isDecimal = 0;
    float decimalPart = 0.0, decimalDivisor = 1.0;
    
    *result = 0;
    while (*message)
    {
        if (*message == GPSNMEA_DECIMAL)
        {
            isDecimal = 1;
            continue;
        }

        if (ddigit(*message,&digit) != ERRORS_UTILITY_CONVERSION_OK)
            return GPSNMEA_ERROR_COORD_CONVERSION;
        
        if (!isDecimal && (*dotOnMessage != GPSNMEA_DECIMAL))
        {
            *result = (10 * (*result)) + digit;
            dotOnMessage++;
        }
        else if (!isDecimal && (*dotOnMessage == GPSNMEA_DECIMAL))
        {
            minutePart = (10.0 * minutePart) + digit;
        }
        else
        {
            decimalPart = (10.0 * decimalPart) + digit;
            decimalDivisor *= 10.0; 
        }
        message++;
    }
    
    minutePart += (decimalPart/decimalDivisor);
    *result += (minutePart / 60.0);
    
    return GPSNMEA_ERROR_OK;
}

/**
 * 
 * @return Return the status of the operation by an element of GpsNmea_Errors
 */
GpsNmea_Errors GpsNmea_parseMessage (GpsNmea_DataType* data)
{
    static uint8_t rxChecksum = 0;
    
//    static uint8_t messageLength = 0;

    static GpsNmea_RxMessageType messageType = GPSNMEA_RXMSG_ERROR;
    static GpsNmea_Errors error = GPSNMEA_ERROR_OK;

    /* Reset buffer index indicator */
//    messageLength = GpsNmea_rxBufferIndex;
//    GpsNmea_rxBufferIndex = 0;

    xtu8(GpsNmea_rxChecksumBuffer,&rxChecksum,2);
    if (GpsNmea_readChecksum != rxChecksum)
        return GPSNMEA_ERROR_CHECKSUM; /* Checksum mismatch */

    messageType = GpsNmea_getReceiveMessageType();
    switch (messageType)
    {
    case GPSNMEA_RXMSG_RMC:
        /* Check data status: A is ok, V is not valid */
        if (GpsNmea_rxBuffer.rmc.status[0] == 'V')
        {
            return GPSNMEA_ERROR_MSG_RMC_INVALID;
        }
        /* Parse time and save it */
        GpsNmea_parseTime(GpsNmea_rxBuffer.rmc.utcTime,&(data->rmc.utcTime));
        /* TODO: Parse date and save it */
        /* Parse latitude value and direction */
        error = GpsNmea_parseCoordinate(GpsNmea_rxBuffer.rmc.latitudeCoordinate,&(data->rmc.latitude));
        if (error != GPSNMEA_ERROR_OK) return error;
        if (GpsNmea_rxBuffer.rmc.latitudeDirection[0] == 'S') data->rmc.latitude *= -1.0;
        /* Parse longitude value and direction */
        error = GpsNmea_parseCoordinate(GpsNmea_rxBuffer.rmc.longitudeCoordinate,&(data->rmc.longitude));
        if (error != GPSNMEA_ERROR_OK) return error;
        if (GpsNmea_rxBuffer.rmc.longitudeDirection[0] == 'W') data->rmc.longitude *= -1.0;
        /* Parse speed and convert from knots to km/h */
        strtf(GpsNmea_rxBuffer.rmc.speed,&(data->rmc.speed));
        if (data->rmc.speed != 0.0) data->rmc.speed /= 1.852;
        /* Parse heading */
        strtf(GpsNmea_rxBuffer.rmc.heading,&(data->rmc.heading));
        break;
    case GPSNMEA_RXMSG_ERROR:
        return GPSNMEA_ERROR_MSG_TYPE;
    }
    
    
    return GPSNMEA_ERROR_OK;
    
#if 0
    /* Control start and end chars of message */
    if ((GpsNmea_rxBuffer[0] == GPSNMEA_START) && 
        (GpsNmea_rxBuffer[GPSENMEA_POS_STOP(messageLength)] == GPSNMEA_STOP) &&
        (GpsNmea_rxBuffer[GPSENMEA_POS_END1(messageLength)] == GPSNMEA_END1) &&
        (GpsNmea_rxBuffer[GPSENMEA_POS_END2(messageLength)] == GPSNMEA_END2))
    {
        /* Control checksum */
        xtu8(&GpsNmea_rxBuffer[messageLength-4],&rxChecksum,2);
        computeChecksum = GpsNmea_computeChecksum(&GpsNmea_rxBuffer[1],1,GPSENMEA_POS_STOP(messageLength));
        if (computeChecksum != rxChecksum)
            return GPSNMEA_ERROR_CHECKSUM; /* Checksum mismatch */

        messageType = GpsNmea_getReceiveMessageType();
        if (messageType == GPSNMEA_MSG_EMPTY)
            return GPSNMEA_ERROR_MSG_TYPE;

        return GPSNMEA_ERROR_OK;
    }
    else
    {
        return GPSNMEA_ERROR_NOT_COMPLIANT;
    }
#endif
}
