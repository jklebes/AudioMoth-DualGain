/****************************************************************************
 * main.c
 * openacousticdevices.info
 * June 2017
 *****************************************************************************/

#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "audioconfig.h"
#include "audiomoth.h"
#include "digitalfilter.h"

/* Useful time constants */

#define MILLISECONDS_IN_SECOND                  1000

#define SECONDS_IN_MINUTE                       60
#define SECONDS_IN_HOUR                         (60 * SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY                          (24 * SECONDS_IN_HOUR)

#define MINUTES_IN_DAY                          1440
#define YEAR_OFFSET                             1900
#define MONTH_OFFSET                            1

#define START_OF_CENTURY                        946684800
#define MIDPOINT_OF_CENTURY                     2524608000

/* Useful type constants */

#define BITS_PER_BYTE                           8
#define UINT32_SIZE_IN_BITS                     32
#define UINT32_SIZE_IN_BYTES                    4
#define UINT16_SIZE_IN_BYTES                    2

/* Sleep and LED constants */

#define LOW_BATTERY_LED_FLASHES                 10

#define SHORT_LED_FLASH_DURATION                100
#define LONG_LED_FLASH_DURATION                 500

#define WAITING_LED_FLASH_DURATION              10
#define WAITING_LED_FLASH_INTERVAL              2000

#define MINIMUM_LED_FLASH_INTERVAL              500

#define SHORT_WAIT_INTERVAL                     100
#define DEFAULT_WAIT_INTERVAL                   1000

/* SRAM buffer constants */

#define NUMBER_OF_BUFFERS                       8
#define NUMBER_OF_BYTES_IN_SAMPLE               2
#define EXTERNAL_SRAM_SIZE_IN_SAMPLES           (AM_EXTERNAL_SRAM_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE)
#define NUMBER_OF_SAMPLES_IN_BUFFER             (EXTERNAL_SRAM_SIZE_IN_SAMPLES / NUMBER_OF_BUFFERS)

/* DMA transfer constant */

#define MAXIMUM_SAMPLES_IN_DMA_TRANSFER         1024

/* Compression constants */

#define COMPRESSION_BUFFER_SIZE_IN_BYTES        512

/* File size constants */

#define MAXIMUM_FILE_NAME_LENGTH                32

#define MAXIMUM_WAV_FILE_SIZE                   UINT32_MAX

/* Configuration file constants */

#define CONFIG_BUFFER_LENGTH                    512
#define CONFIG_TIMEZONE_LENGTH                  8

/* WAV header constant */

#define PCM_FORMAT                              1
#define RIFF_ID_LENGTH                          4
#define LENGTH_OF_ARTIST                        32
#define LENGTH_OF_COMMENT                       384  // TODO

/* USB configuration constant */

#define MAX_RECORDING_PERIODS         5

/* Supply voltage constant */

#define MINIMUM_SUPPLY_VOLTAGE                  2800

/* Recording error constant */

#define MAXIMUM_NUMBER_OF_RECORDING_ERRORS      5

/* Deployment ID constant */

#define DEPLOYMENT_ID_LENGTH                    8

/* Audio configuration constants */

#define AUDIO_CONFIG_PULSE_INTERVAL             10
#define AUDIO_CONFIG_TIME_CORRECTION            134
#define AUDIO_CONFIG_TONE_TIMEOUT               250
#define AUDIO_CONFIG_PACKETS_TIMEOUT            30000

/* USB configuration constant */

#define USB_CONFIG_TIME_CORRECTION              26

/* Recording preparation constants */

#define PREPARATION_PERIOD_INCREMENT            250
#define MINIMUM_PREPARATION_PERIOD              750
#define INITIAL_PREPARATION_PERIOD              2000
#define MAXIMUM_PREPARATION_PERIOD              30000

/* Energy saver mode constant */

#define ENERGY_SAVER_SAMPLE_RATE_THRESHOLD      48000



/* Useful macros */

#define FLASH_LED(led, duration) { \
    AudioMoth_set ## led ## LED(true); \
    AudioMoth_delay(duration); \
    AudioMoth_set ## led ## LED(false); \
}

#define FLASH_REPEAT_LED(led, repeats, duration) { \
    for (uint32_t i = 0; i < repeats; i += 1) { \
        AudioMoth_set ## led ## LED(true); \
        AudioMoth_delay(duration); \
        AudioMoth_set ## led ## LED(false); \
        AudioMoth_delay(duration); \
    } \
}

#define FLASH_LED_AND_RETURN_ON_ERROR(fn) { \
    bool success = (fn); \
    if (success != true) { \
        AudioMoth_setBothLED(false); \
        AudioMoth_delay(LONG_LED_FLASH_DURATION); \
        FLASH_LED(Both, LONG_LED_FLASH_DURATION) \
        return SDCARD_WRITE_ERROR; \
    } \
}

#define RETURN_BOOL_ON_ERROR(fn) { \
    bool success = (fn); \
    if (success != true) { \
        return success; \
    } \
}

#define SAVE_SWITCH_POSITION_AND_POWER_DOWN(milliseconds) { \
    *previousSwitchPosition = switchPosition; \
    AudioMoth_powerDownAndWakeMilliseconds(milliseconds); \
}



#define SERIAL_NUMBER                           "%08X%08X"

#define FORMAT_SERIAL_NUMBER(src)               (unsigned int)*((uint32_t*)src + 1),  (unsigned int)*((uint32_t*)src)

#define ABS(a)                                  ((a) < (0) ? (-a) : (a))

#define MIN(a, b)                               ((a) < (b) ? (a) : (b))

#define MAX(a, b)                               ((a) > (b) ? (a) : (b))

#define ROUNDED_DIV(a, b)                       (((a) + (b/2)) / (b))

#define ROUNDED_UP_DIV(a, b)                    (((a) + (b) - 1) / (b))

#define ROUND_UP_TO_MULTIPLE(a, b)              (((a) + (b) - 1) & ~((b)-1))

/* Recording state enumeration */

typedef enum {RECORDING_OKAY, FILE_SIZE_LIMITED, SUPPLY_VOLTAGE_LOW, SWITCH_CHANGED, MICROPHONE_CHANGED, SDCARD_WRITE_ERROR} AM_recordingState_t;

/* Gain 1 or 2 step in recording cycle - just an alias of a 0,1 flag*/

typedef enum {GAIN_STEP_1, GAIN_STEP_2} AM_DualGainStep_t;

/* Battery level display type */

typedef enum {BATTERY_LEVEL, NIMH_LIPO_BATTERY_VOLTAGE} AM_batteryLevelDisplayType_t;

/* WAV header */

#pragma pack(push, 1)

typedef struct {
    char id[RIFF_ID_LENGTH];
    uint32_t size;
} chunk_t;

typedef struct {
    chunk_t icmt;
    char comment[LENGTH_OF_COMMENT];
} icmt_t;

typedef struct {
    chunk_t iart;
    char artist[LENGTH_OF_ARTIST];
} iart_t;

typedef struct {
    uint16_t format;
    uint16_t numberOfChannels;
    uint32_t samplesPerSecond;
    uint32_t bytesPerSecond;
    uint16_t bytesPerCapture;
    uint16_t bitsPerSample;
} wavFormat_t;

typedef struct {
    chunk_t riff;
    char format[RIFF_ID_LENGTH];
    chunk_t fmt;
    wavFormat_t wavFormat;
    chunk_t list;
    char info[RIFF_ID_LENGTH];
    icmt_t icmt;
    iart_t iart;
    chunk_t data;
} wavHeader_t;

#pragma pack(pop)

static wavHeader_t wavHeader = {
    .riff = {.id = "RIFF", .size = 0},
    .format = "WAVE",
    .fmt = {.id = "fmt ", .size = sizeof(wavFormat_t)},
    .wavFormat = {.format = PCM_FORMAT, .numberOfChannels = 1, .samplesPerSecond = 0, .bytesPerSecond = 0, .bytesPerCapture = 2, .bitsPerSample = 16},
    .list = {.id = "LIST", .size = RIFF_ID_LENGTH + sizeof(icmt_t) + sizeof(iart_t)},
    .info = "INFO",
    .icmt = {.icmt.id = "ICMT", .icmt.size = LENGTH_OF_COMMENT, .comment = ""},
    .iart = {.iart.id = "IART", .iart.size = LENGTH_OF_ARTIST, .artist = ""},
    .data = {.id = "data", .size = 0}
};

/* USB configuration data structure */

#pragma pack(push, 1)

typedef struct {
    uint16_t startMinutes;
    uint16_t endMinutes;
} recordingPeriod_t;

// TODO add extra fields to struct for dualGain mode
// TODO and remove filter, GPS, and other options
typedef struct {
    uint32_t time;
    AM_gainSetting_t gain1;
    AM_gainSetting_t gain2;
    uint8_t clockDivider;
    uint8_t acquisitionCycles;
    uint8_t oversampleRate;
    uint32_t sampleRate;
    uint8_t sampleRateDivider;
    uint16_t recordDurationGain1;
    uint16_t sleepDuration;
    uint16_t recordDurationGain2;
    uint8_t enableLED;
    uint8_t activeRecordingPeriods;
    recordingPeriod_t recordingPeriods[MAX_RECORDING_PERIODS];
    int8_t timezoneHours;
    uint8_t enableLowVoltageCutoff;
    uint8_t disableBatteryLevelDisplay;
    int8_t timezoneMinutes;
    uint8_t disableSleepRecordCycle;
    uint32_t earliestRecordingTime;
    uint32_t latestRecordingTime;
    uint8_t requireAcousticConfiguration : 1;
    AM_batteryLevelDisplayType_t batteryLevelDisplayType : 1;
    uint8_t enableEnergySaverMode : 1;
    uint8_t disable48HzDCBlockingFilter : 1;
    uint8_t enableLowGainRange : 1;
    uint8_t enableDailyFolders : 1;
} configSettings_t;

#pragma pack(pop)

static const configSettings_t defaultConfigSettings = {
    .time = 0,
    .gain1 = AM_GAIN_MEDIUM,
    .gain2 = AM_GAIN_LOW,
    .clockDivider = 4,
    .acquisitionCycles = 16,
    .oversampleRate = 1,
    .sampleRate = 384000,
    .sampleRateDivider = 8,
    .recordDurationGain1 = 5,
    .sleepDuration = 50,
    .recordDurationGain2 = 5,
    .enableLED = 1,
    .activeRecordingPeriods = 1,
    .recordingPeriods = {
        {.startMinutes = 0, .endMinutes = 1440},
        {.startMinutes = 0, .endMinutes = 0},
        {.startMinutes = 0, .endMinutes = 0},
        {.startMinutes = 0, .endMinutes = 0},
        {.startMinutes = 0, .endMinutes = 0}
    },
    .timezoneHours = 0,
    .enableLowVoltageCutoff = 1,
    .disableBatteryLevelDisplay = 0,
    .timezoneMinutes = 0,
    .disableSleepRecordCycle = 0,
    .earliestRecordingTime = 0,
    .latestRecordingTime = 0,
    .requireAcousticConfiguration = 0,
    .batteryLevelDisplayType = BATTERY_LEVEL,
    .enableEnergySaverMode = 0,
    .disable48HzDCBlockingFilter = 0,
    .enableLowGainRange = 0,
    .enableDailyFolders = 0
};

/* Persistent configuration data structure */

#pragma pack(push, 1)

typedef struct {
    uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH];
    uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH];
    configSettings_t configSettings;
} persistentConfigSettings_t;

#pragma pack(pop)

/* Functions to format header and configuration components */

static uint32_t formatDecibels(char *dest, uint32_t value) {

    if (value) return sprintf(dest, "-%lu dB", value);

    memcpy(dest, "0 dB", 4);

    return 4;

}

static uint32_t formatPercentage(char *dest, uint32_t mantissa, int32_t exponent) {

    uint32_t length = exponent < 0 ? 1 - exponent : 0;

    memcpy(dest, "0.0000", length);

    length += sprintf(dest + length, "%lu", mantissa);

    while (exponent-- > 0) dest[length++] = '0';

    dest[length++] = '%';

    return length;

}

// TODO add dualgain mode, details to header somewhere
/* Functions to set WAV header details and comment */

static void setHeaderDetails(wavHeader_t *wavHeader, uint32_t sampleRate, uint32_t numberOfSamples) {

    wavHeader->wavFormat.samplesPerSecond = sampleRate;
    wavHeader->wavFormat.bytesPerSecond = NUMBER_OF_BYTES_IN_SAMPLE * sampleRate;
    wavHeader->data.size = NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamples;
    wavHeader->riff.size = NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamples + sizeof(wavHeader_t) - sizeof(chunk_t);

}

// TODO
static void setHeaderComment(wavHeader_t *wavHeader, configSettings_t *configSettings, uint32_t currentTime, uint8_t *serialNumber, uint8_t *deploymentID, uint8_t *defaultDeploymentID, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, bool externalMicrophone, AM_recordingState_t recordingState) {

    struct tm time;

    time_t rawTime = currentTime + configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    gmtime_r(&rawTime, &time);

    /* Format artist field */

    char *artist = wavHeader->iart.artist;

    sprintf(artist, "AudioMoth " SERIAL_NUMBER, FORMAT_SERIAL_NUMBER(serialNumber));

    /* Format comment field */

    char *comment = wavHeader->icmt.comment;

    comment += sprintf(comment, "Recorded at %02d:%02d:%02d %02d/%02d/%04d (UTC", time.tm_hour, time.tm_min, time.tm_sec, time.tm_mday, MONTH_OFFSET + time.tm_mon, YEAR_OFFSET + time.tm_year);

    int8_t timezoneHours = configSettings->timezoneHours;

    int8_t timezoneMinutes = configSettings->timezoneMinutes;

    if (timezoneHours < 0) {

        comment += sprintf(comment, "%d", timezoneHours);

    } else if (timezoneHours > 0) {

        comment += sprintf(comment, "+%d", timezoneHours);

    } else {

        if (timezoneMinutes < 0) comment += sprintf(comment, "-%d", timezoneHours);

        if (timezoneMinutes > 0) comment += sprintf(comment, "+%d", timezoneHours);

    }

    if (timezoneMinutes < 0) comment += sprintf(comment, ":%02d", -timezoneMinutes);

    if (timezoneMinutes > 0) comment += sprintf(comment, ":%02d", timezoneMinutes);

    if (memcmp(deploymentID, defaultDeploymentID, DEPLOYMENT_ID_LENGTH)) {

        comment += sprintf(comment, ") during deployment " SERIAL_NUMBER " ", FORMAT_SERIAL_NUMBER(deploymentID));

    } else {

        comment += sprintf(comment, ") by %s ", artist);

    }

    if (externalMicrophone) {

        comment += sprintf(comment, "using external microphone ");

    }

    static char *gainSettings[5] = {"low", "low-medium", "medium", "medium-high", "high"};

    comment += sprintf(comment, "at %s gain while battery was ", gainSettings[configSettings->gain1]); //TODO

    if (extendedBatteryState == AM_EXT_BAT_LOW) {

        comment += sprintf(comment, "less than 2.5V");

    } else if (extendedBatteryState >= AM_EXT_BAT_FULL) {

        comment += sprintf(comment, "greater than 4.9V");

    } else {

        uint32_t batteryVoltage =  extendedBatteryState + AM_EXT_BAT_STATE_OFFSET / AM_BATTERY_STATE_INCREMENT;

        comment += sprintf(comment, "%01lu.%01luV", batteryVoltage / 10, batteryVoltage % 10);

    }

    char *sign = temperature < 0 ? "-" : "";

    uint32_t temperatureInDecidegrees = ROUNDED_DIV(ABS(temperature), 100);

    comment += sprintf(comment, " and temperature was %s%lu.%luC.", sign, temperatureInDecidegrees / 10, temperatureInDecidegrees % 10);

    if (recordingState != RECORDING_OKAY) {

        comment += sprintf(comment, " Recording stopped");

        if (recordingState == MICROPHONE_CHANGED) {

            comment += sprintf(comment, " due to microphone change.");

        } else if (recordingState == SWITCH_CHANGED) {

            comment += sprintf(comment, " due to switch position change.");

        } else if (recordingState == SUPPLY_VOLTAGE_LOW) {

            comment += sprintf(comment, " due to low voltage.");

        } else if (recordingState == FILE_SIZE_LIMITED) {

            comment += sprintf(comment, " due to file size limit.");

        }

    }

}

/* Function to write configuration to file */
// TODO match modified config struct
static bool writeConfigurationToFile(configSettings_t *configSettings, uint8_t *firmwareDescription, uint8_t *firmwareVersion, uint8_t *serialNumber, uint8_t *deploymentID, uint8_t *defaultDeploymentID) {

    struct tm time;

    static char configBuffer[CONFIG_BUFFER_LENGTH];

    static char timezoneBuffer[CONFIG_TIMEZONE_LENGTH];

    int32_t timezoneOffset = configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    RETURN_BOOL_ON_ERROR(AudioMoth_openFile("CONFIG.TXT"));

    uint32_t length = sprintf(configBuffer, "Device ID                       : " SERIAL_NUMBER "\r\n", FORMAT_SERIAL_NUMBER(serialNumber));

    length += sprintf(configBuffer + length, "Firmware                        : %s (%u.%u.%u)\r\n\r\n", firmwareDescription, firmwareVersion[0], firmwareVersion[1], firmwareVersion[2]);

    if (memcmp(deploymentID, defaultDeploymentID, DEPLOYMENT_ID_LENGTH)) {

        length += sprintf(configBuffer + length, "Deployment ID                   : " SERIAL_NUMBER "\r\n\r\n", FORMAT_SERIAL_NUMBER(deploymentID));

    }

    uint32_t timezoneLength = sprintf(timezoneBuffer, "UTC");

    if (configSettings->timezoneHours < 0) {

        timezoneLength += sprintf(timezoneBuffer + timezoneLength, "%d", configSettings->timezoneHours);

    } else if (configSettings->timezoneHours > 0) {

        timezoneLength += sprintf(timezoneBuffer + timezoneLength, "+%d", configSettings->timezoneHours);

    } else {

        if (configSettings->timezoneMinutes < 0) timezoneLength += sprintf(timezoneBuffer + timezoneLength, "-%d", configSettings->timezoneHours);

        if (configSettings->timezoneMinutes > 0) timezoneLength += sprintf(timezoneBuffer + timezoneLength, "+%d", configSettings->timezoneHours);

    }

    if (configSettings->timezoneMinutes < 0) timezoneLength += sprintf(timezoneBuffer + timezoneLength, ":%02d", -configSettings->timezoneMinutes);

    if (configSettings->timezoneMinutes > 0) timezoneLength += sprintf(timezoneBuffer + timezoneLength, ":%02d", configSettings->timezoneMinutes);

    length += sprintf(configBuffer + length, "Time zone                       : %s", timezoneBuffer);

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    length = sprintf(configBuffer, "\r\n\r\nSample rate (Hz)                : %lu\r\n", configSettings->sampleRate / configSettings->sampleRateDivider);

    static char *gainSettings[5] = {"Low", "Low-Medium", "Medium", "Medium-High", "High"};

    length += sprintf(configBuffer + length, "Gain1                            : %s\r\n\r\n", gainSettings[configSettings->gain1]);

    length += sprintf(configBuffer + length, "Gain2                            : %s\r\n\r\n", gainSettings[configSettings->gain2]);

    length += sprintf(configBuffer + length, "Sleep duration (s)            : ");

    if (configSettings->disableSleepRecordCycle) {

        length += sprintf(configBuffer + length, "-");

    } else {

        length += sprintf(configBuffer + length, "%u", configSettings->sleepDuration);

    }


    length += sprintf(configBuffer + length, "\r\nRecording duration gain 1 (s)          : ");

    if (configSettings->disableSleepRecordCycle) {

        length += sprintf(configBuffer + length, "-");

    } else {

        length += sprintf(configBuffer + length, "%u", configSettings->recordDurationGain1);

    }

    length += sprintf(configBuffer + length, "\r\nRecording duration gain 2 (s)          : ");

    if (configSettings->disableSleepRecordCycle) {

        length += sprintf(configBuffer + length, "-");

    } else {

        length += sprintf(configBuffer + length, "%u", configSettings->recordDurationGain2);

    }

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    length = sprintf(configBuffer, "\r\n\r\nActive recording periods        : %u\r\n", configSettings->activeRecordingPeriods);

    /* Find the first recording period */

    uint32_t minimumIndex = 0;

    uint32_t minimumStartMinutes = UINT32_MAX;

    for (uint32_t i = 0; i < configSettings->activeRecordingPeriods; i += 1) {

        uint32_t startMinutes = (MINUTES_IN_DAY + configSettings->recordingPeriods[i].startMinutes + timezoneOffset / SECONDS_IN_MINUTE) % MINUTES_IN_DAY;

        if (startMinutes < minimumStartMinutes) {

            minimumStartMinutes = startMinutes;

            minimumIndex = i;

        }

    }

 /* Display the recording periods */

    for (uint32_t i = 0; i < configSettings->activeRecordingPeriods; i += 1) {

        uint32_t index = (minimumIndex + i) % configSettings->activeRecordingPeriods;

        uint32_t startMinutes = (MINUTES_IN_DAY + configSettings->recordingPeriods[index].startMinutes + timezoneOffset / SECONDS_IN_MINUTE) % MINUTES_IN_DAY;

        uint32_t endMinutes = (MINUTES_IN_DAY + configSettings->recordingPeriods[index].endMinutes + timezoneOffset / SECONDS_IN_MINUTE) % MINUTES_IN_DAY;

        if (i == 0) length += sprintf(configBuffer + length, "\r\n");

        length += sprintf(configBuffer + length, "Recording period %lu              : %02lu:%02lu - %02lu:%02lu (%s)\r\n", i + 1, startMinutes / 60, startMinutes % 60, endMinutes / 60, endMinutes % 60, timezoneBuffer);

    }


    if (configSettings->earliestRecordingTime == 0) {

        length += sprintf(configBuffer + length, "\r\nFirst recording date            : ----------");

    } else {

        time_t rawTime = configSettings->earliestRecordingTime + timezoneOffset;

        gmtime_r(&rawTime, &time);

        if (time.tm_hour == 0 && time.tm_min == 0 && time.tm_sec == 0) {

            length += sprintf(configBuffer + length, "\r\nFirst recording date            : ");

            length += sprintf(configBuffer + length, "%04d-%02d-%02d (%s)", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday, timezoneBuffer);

        } else {

            length += sprintf(configBuffer + length, "\r\nFirst recording time            : ");

            length += sprintf(configBuffer + length, "%04d-%02d-%02d %02d:%02d:%02d (%s)", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, timezoneBuffer);

        }

    }

    if (configSettings->latestRecordingTime == 0) {

        length += sprintf(configBuffer + length, "\r\nLast recording date             : ----------");

    } else {

        time_t rawTime = configSettings->latestRecordingTime + timezoneOffset;

        gmtime_r(&rawTime, &time);

        if (time.tm_hour == 0 && time.tm_min == 0 && time.tm_sec == 0) {

            rawTime -= SECONDS_IN_DAY;

            gmtime_r(&rawTime, &time);

            length += sprintf(configBuffer + length, "\r\nLast recording date             : ");

            length += sprintf(configBuffer + length, "%04d-%02d-%02d (%s)", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday, timezoneBuffer);

        } else {

            length += sprintf(configBuffer + length, "\r\nLast recording time             : ");

            length += sprintf(configBuffer + length, "%04d-%02d-%02d %02d:%02d:%02d (%s)", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, timezoneBuffer);

        }

    }

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    length = sprintf(configBuffer, "\r\n\r\nEnable LED                      : %s\r\n", configSettings->enableLED ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable low-voltage cut-off      : %s\r\n", configSettings->enableLowVoltageCutoff ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable battery level indication : %s\r\n\r\n", configSettings->disableBatteryLevelDisplay ? "No" : configSettings->batteryLevelDisplayType == NIMH_LIPO_BATTERY_VOLTAGE ? "Yes (NiMH/LiPo voltage range)" : "Yes");

    length += sprintf(configBuffer + length, "Always require acoustic chime   : %s\r\n", configSettings->requireAcousticConfiguration ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Use daily folder for WAV files  : %s\r\n\r\n", configSettings->enableDailyFolders ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Disable 48Hz DC blocking filter : %s\r\n", configSettings->disable48HzDCBlockingFilter ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable energy saver mode        : %s\r\n", configSettings->enableEnergySaverMode ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable low gain range           : %s\r\n\r\n", configSettings->enableLowGainRange ? "Yes" : "No");

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    RETURN_BOOL_ON_ERROR(AudioMoth_closeFile());

    return true;

}

/* Backup domain variables */
// TODO put in AM_DualGainStep_t

static uint32_t *previousSwitchPosition = (uint32_t*)AM_BACKUP_DOMAIN_START_ADDRESS;

static uint32_t *timeOfNextRecordingGain1 = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 4);

static uint32_t *durationOfNextRecordingGain1 = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 8);

static uint32_t *timeOfNextRecordingGain2 = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 12);

static uint32_t *durationOfNextRecordingGain2 = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 16);

static AM_gainSetting_t *gainOfNextRecording = (AM_gainSetting_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 20);

static uint32_t *writtenConfigurationToFile = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 24);

static uint8_t *deploymentID = (uint8_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 28);

static uint32_t *readyToMakeRecordings = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 32);

static uint32_t *numberOfRecordingErrors = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 36);

static uint32_t *recordingPreparationPeriod = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 40);

static uint32_t *poweredDownWithShortWaitInterval = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 44);

static configSettings_t *configSettings = (configSettings_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 48);

/* DMA transfer variable */

static uint32_t numberOfRawSamplesInDMATransfer;

/* SRAM buffer variables */

static volatile uint32_t writeBuffer;

static volatile uint32_t writeBufferIndex;

static int16_t* buffers[NUMBER_OF_BUFFERS];

/* Flag to start processing DMA transfers */

static volatile uint32_t numberOfDMATransfers;

static volatile uint32_t numberOfDMATransfersToWait;

/* Compression buffers */

static bool writeIndicator[NUMBER_OF_BUFFERS];

static int16_t compressionBuffer[COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE];

/* Audio configuration variables */

static bool audioConfigStateLED;

static bool audioConfigToggleLED;

static uint32_t audioConfigPulseCounter;

static bool acousticConfigurationPerformed;

static uint32_t secondsOfAcousticSignalStart;

static uint32_t millisecondsOfAcousticSignalStart;

/* Deployment ID variable */

static uint8_t defaultDeploymentID[DEPLOYMENT_ID_LENGTH];

/* Recording state */

static volatile bool microphoneChanged;

static volatile bool switchPositionChanged;

/* DMA buffers */

static int16_t primaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

static int16_t secondaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

/* Firmware version and description */
// TODO name this version
static uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH] = {1, 0, 1};
// TODO name this verion
static uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH] = "DualGain-Firmware";

/* Function prototypes */

static void flashLedToIndicateBatteryLife(void);

static void scheduleRecording(uint32_t currentTime, uint32_t *timeOfNextRecordingGain1, uint32_t *durationOfNextRecordingGain1,  uint32_t *timeOfNextRecordingGain2, uint32_t *durationOfNextRecordingGain2, uint32_t *startOfRecordingPeriod, uint32_t *endOfRecordingPeriod);

static AM_recordingState_t makeRecording(uint32_t timeOfNextRecordingGain1, uint32_t recordDurationGain1, AM_gainSetting_t gainOfNextRecording, bool enableLED, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, uint32_t *fileOpenTime, uint32_t *fileOpenMilliseconds);

/* Functions of copy to and from the backup domain */

static void copyFromBackupDomain(uint8_t *dst, uint32_t *src, uint32_t length) {

    for (uint32_t i = 0; i < length; i += 1) {
        *(dst + i) = *((uint8_t*)src + i);
    }

}

static void copyToBackupDomain(uint32_t *dst, uint8_t *src, uint32_t length) {

    uint32_t value = 0;

    for (uint32_t i = 0; i < length / UINT32_SIZE_IN_BYTES; i += 1) {
        *(dst + i) = *((uint32_t*)src + i);
    }

    for (uint32_t i = 0; i < length % UINT32_SIZE_IN_BYTES; i += 1) {
        value = (value << BITS_PER_BYTE) + *(src + length - 1 - i);
    }

    if (length % UINT32_SIZE_IN_BYTES) *(dst + length / UINT32_SIZE_IN_BYTES) = value;

}

/* Function to select energy saver mode */

static bool isEnergySaverMode(configSettings_t *configSettings) {

    return configSettings->enableEnergySaverMode && configSettings->sampleRate / configSettings->sampleRateDivider <= ENERGY_SAVER_SAMPLE_RATE_THRESHOLD;

}


/* Function to calculate the time to the next event */
static void calculateTimeToNextEvent(uint32_t currentTime, uint32_t currentMilliseconds, int64_t *timeUntilPreparationStart) {

    *timeUntilPreparationStart = (int64_t)*timeOfNextRecordingGain1 * MILLISECONDS_IN_SECOND - (int64_t)*recordingPreparationPeriod - (int64_t)currentTime * MILLISECONDS_IN_SECOND - (int64_t)currentMilliseconds;
}


/* Main function */

int main() {

    /* Initialise device */

    AudioMoth_initialise();

    /* Check the switch position */

    AM_switchPosition_t switchPosition = AudioMoth_getSwitchPosition();

    if (AudioMoth_isInitialPowerUp()) {

        /* Initialise recording schedule variables */  //TODO add AM_DualGainStep_t - as ptr?

        *timeOfNextRecordingGain1 = 0;

        *timeOfNextRecordingGain2 = 0;

        *durationOfNextRecordingGain1 = UINT32_MAX;

        *durationOfNextRecordingGain2 = UINT32_MAX;

        /* Initialise configuration writing variable */

        *writtenConfigurationToFile = false;

        /* Initialise recording state variables */

        *previousSwitchPosition = AM_SWITCH_NONE;

        *readyToMakeRecordings = false;

        *numberOfRecordingErrors = 0;

        *recordingPreparationPeriod = INITIAL_PREPARATION_PERIOD;

        /* Initialise the power down interval flag */

        *poweredDownWithShortWaitInterval = false;

        /* Copy default deployment ID */

        copyToBackupDomain((uint32_t*)deploymentID, (uint8_t*)defaultDeploymentID, DEPLOYMENT_ID_LENGTH);

        /* Check the persistent configuration */

        persistentConfigSettings_t *persistentConfigSettings = (persistentConfigSettings_t*)AM_FLASH_USER_DATA_ADDRESS;

        if (memcmp(persistentConfigSettings->firmwareVersion, firmwareVersion, AM_FIRMWARE_VERSION_LENGTH) == 0 && memcmp(persistentConfigSettings->firmwareDescription, firmwareDescription, AM_FIRMWARE_DESCRIPTION_LENGTH) == 0) {

            copyToBackupDomain((uint32_t*)configSettings, (uint8_t*)&persistentConfigSettings->configSettings, sizeof(configSettings_t));

        } else {

            copyToBackupDomain((uint32_t*)configSettings, (uint8_t*)&defaultConfigSettings, sizeof(configSettings_t));

        }

    }

    /* Handle the case that the switch is in USB position  */

    if (switchPosition == AM_SWITCH_USB) {

        if (configSettings->disableBatteryLevelDisplay == false && (*previousSwitchPosition == AM_SWITCH_DEFAULT || *previousSwitchPosition == AM_SWITCH_CUSTOM)) {

            flashLedToIndicateBatteryLife();

        }

        AudioMoth_handleUSB();

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

        /* Read the time */

    uint32_t currentTime;

    uint32_t currentMilliseconds;

    AudioMoth_getTime(&currentTime, &currentMilliseconds);

    /* Check if switch has just been moved to CUSTOM or DEFAULT */

    bool fileSystemEnabled = false;

    bool writtenConfigurationToFileInThisSession = false;

    if (switchPosition != *previousSwitchPosition) {

        /* Reset the power down interval flag */

        *poweredDownWithShortWaitInterval = false;

        /* Check there are active recording periods if the switch is in CUSTOM position */

        *readyToMakeRecordings = switchPosition == AM_SWITCH_DEFAULT || (switchPosition == AM_SWITCH_CUSTOM && configSettings->activeRecordingPeriods > 0);

        /* Check if acoustic configuration is required */

        if (*readyToMakeRecordings) {

            /* Determine if acoustic configuration is required */

            bool shouldPerformAcousticConfiguration = switchPosition == AM_SWITCH_CUSTOM && (AudioMoth_hasTimeBeenSet() == false || configSettings->requireAcousticConfiguration);

            /* Determine whether to listen for the acoustic tone */

            bool listenForAcousticTone = shouldPerformAcousticConfiguration == false && switchPosition == AM_SWITCH_CUSTOM;

            if (listenForAcousticTone) {

                AudioConfig_enableAudioConfiguration();

                shouldPerformAcousticConfiguration = AudioConfig_listenForAudioConfigurationTone(AUDIO_CONFIG_TONE_TIMEOUT);

            }

            if (shouldPerformAcousticConfiguration) {

                AudioMoth_setRedLED(true);

                AudioMoth_setGreenLED(false);

                audioConfigPulseCounter = 0;

                audioConfigStateLED = false;

                audioConfigToggleLED = false;

                acousticConfigurationPerformed = false;

                if (listenForAcousticTone == false) {

                    AudioConfig_enableAudioConfiguration();

                }

                bool timedOut = AudioConfig_listenForAudioConfigurationPackets(listenForAcousticTone, AUDIO_CONFIG_PACKETS_TIMEOUT);

                AudioConfig_disableAudioConfiguration();

                if (acousticConfigurationPerformed) {

                    /* Indicate success with LED flashes */

                    AudioMoth_setRedLED(false);

                    AudioMoth_setGreenLED(true);

                    AudioMoth_delay(1000);

                    AudioMoth_delay(1000);

                    AudioMoth_setGreenLED(false);

                    AudioMoth_delay(500);

                } else if (listenForAcousticTone && timedOut) {

                    /* Turn off LED */

                    AudioMoth_setBothLED(false);

                } else {

                    /* Not ready to make a recording - no time set, no acoustic signal */

                    *readyToMakeRecordings = false;

                    /* Turn off LED */

                    AudioMoth_setBothLED(false);

                    /* Power down */

                    SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

                }

            } else if (listenForAcousticTone) {

                AudioConfig_disableAudioConfiguration();

            } //end if shouldPerformAcousticConfiguration

        } //end if readyToMakeRecordings

        /* Calculate time of next recording if ready to make a recording
        - in DEFAULT, or in CUSTOM with some schedule set and time has been set */

        if (*readyToMakeRecordings) {

            /* Enable energy saver mode */

            if (isEnergySaverMode(configSettings)) AudioMoth_setClockDivider(AM_HF_CLK_DIV2);

            /* Reset the recording error counter */

            *numberOfRecordingErrors = 0;

            /* Reset the recording preparation period to default */

            *recordingPreparationPeriod = INITIAL_PREPARATION_PERIOD;

            /* Reset persistent configuration write flag */

            *writtenConfigurationToFile = false;

            /* Try to write configuration to file */

            fileSystemEnabled = AudioMoth_enableFileSystem(configSettings->sampleRateDivider == 1 ? AM_SD_CARD_HIGH_SPEED : AM_SD_CARD_NORMAL_SPEED);

            if (fileSystemEnabled) writtenConfigurationToFileInThisSession = writeConfigurationToFile(configSettings, firmwareDescription, firmwareVersion, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, deploymentID, defaultDeploymentID);

            /* Update the time and calculate earliest schedule start time */

            AudioMoth_getTime(&currentTime, &currentMilliseconds);

            uint32_t scheduleTime = currentTime + ROUNDED_UP_DIV(currentMilliseconds + *recordingPreparationPeriod, MILLISECONDS_IN_SECOND);

            /* Schedule the next recording */

            if (switchPosition == AM_SWITCH_CUSTOM) {

                //sets next times, durations
                uint32_t timeOfNextEvent = UINT32_MAX;
                scheduleRecording(scheduleTime, timeOfNextRecordingGain1, durationOfNextRecordingGain1, timeOfNextRecordingGain2, durationOfNextRecordingGain2, &timeOfNextEvent, NULL);

            }


            /* Set parameters to start recording now */

            if (switchPosition == AM_SWITCH_DEFAULT) {

                // on DEFAULT mode record in Gain 1 from now

                *timeOfNextRecordingGain1 = scheduleTime;

                *durationOfNextRecordingGain1 = UINT32_MAX;

                *timeOfNextRecordingGain2 = UINT32_MAX;

                *durationOfNextRecordingGain2 = UINT32_MAX;

                *gainOfNextRecording = configSettings->gain1;

            }

        } // end if *readyToMakeRecordings

    } // end if (switchPosition != *previousSwitchPosition)

    /* If not ready to make a recording then flash LED and power down */

    if (*readyToMakeRecordings == false) {

        FLASH_LED(Both, SHORT_LED_FLASH_DURATION)

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

    /* Reset LED flags */

    bool enableLED = (switchPosition == AM_SWITCH_DEFAULT) || configSettings->enableLED;

    bool shouldSuppressLED = *poweredDownWithShortWaitInterval;

    *poweredDownWithShortWaitInterval = false;

    /* Calculate time until next activity */

    int64_t timeUntilPreparationStart;

    calculateTimeToNextEvent(currentTime, currentMilliseconds, &timeUntilPreparationStart);

    /* Make a recording */

    if (timeUntilPreparationStart <= 0) {

        /* Enable energy saver mode */

        if (isEnergySaverMode(configSettings)) AudioMoth_setClockDivider(AM_HF_CLK_DIV2);

        /* Write configuration if not already done so */

        if (writtenConfigurationToFileInThisSession == false && *writtenConfigurationToFile == false) {

            if (!fileSystemEnabled) fileSystemEnabled = AudioMoth_enableFileSystem(configSettings->sampleRateDivider == 1 ? AM_SD_CARD_HIGH_SPEED : AM_SD_CARD_NORMAL_SPEED);

            if (fileSystemEnabled) *writtenConfigurationToFile = writeConfigurationToFile(configSettings, firmwareDescription, firmwareVersion, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, deploymentID, defaultDeploymentID);

        }

        /* Make the recording */

        uint32_t fileOpenTime;

        uint32_t fileOpenMilliseconds;

        AM_recordingState_t recordingState = RECORDING_OKAY;

        /* Measure battery voltage */

        uint32_t supplyVoltage = AudioMoth_getSupplyVoltage();

        AM_extendedBatteryState_t extendedBatteryState = AudioMoth_getExtendedBatteryState(supplyVoltage);

        /* Check if low voltage check is enabled and that the voltage is okay */

        bool okayToMakeRecording = true;

        if (configSettings->enableLowVoltageCutoff) {

            AudioMoth_enableSupplyMonitor();

            AudioMoth_setSupplyMonitorThreshold(MINIMUM_SUPPLY_VOLTAGE);

            okayToMakeRecording = AudioMoth_isSupplyAboveThreshold();

        }

        /* Make recording if okay */

        if (okayToMakeRecording) {

            AudioMoth_enableTemperature();

            int32_t temperature = AudioMoth_getTemperature();

            AudioMoth_disableTemperature();

            if (!fileSystemEnabled) fileSystemEnabled = AudioMoth_enableFileSystem(configSettings->sampleRateDivider == 1 ? AM_SD_CARD_HIGH_SPEED : AM_SD_CARD_NORMAL_SPEED);

            if (fileSystemEnabled)  {
                // TODO main event !
                *gainOfNextRecording = configSettings->gain1;
                recordingState = makeRecording(*timeOfNextRecordingGain1, *durationOfNextRecordingGain1, *gainOfNextRecording, enableLED, extendedBatteryState, temperature, &fileOpenTime, &fileOpenMilliseconds);
                *gainOfNextRecording = configSettings->gain2;
                recordingState = makeRecording(*timeOfNextRecordingGain2, *durationOfNextRecordingGain2, *gainOfNextRecording,  enableLED, extendedBatteryState, temperature, &fileOpenTime, &fileOpenMilliseconds);

            } else {

                FLASH_LED(Both, LONG_LED_FLASH_DURATION);

                recordingState = SDCARD_WRITE_ERROR;

            }

        } else {

            recordingState = SUPPLY_VOLTAGE_LOW;

        }

        /* Disable low voltage monitor if it was used */

        if (configSettings->enableLowVoltageCutoff) AudioMoth_disableSupplyMonitor();

        /* Enable the error warning flashes */

        if (recordingState == SUPPLY_VOLTAGE_LOW) {

            AudioMoth_delay(LONG_LED_FLASH_DURATION);

            FLASH_LED(Both, LONG_LED_FLASH_DURATION);

            *numberOfRecordingErrors += 1;

        }

        if (recordingState == SDCARD_WRITE_ERROR) {

            *numberOfRecordingErrors += 1;

        }

        /* Update the preparation period */

        if (recordingState != SDCARD_WRITE_ERROR) {

            int64_t measuredPreparationPeriod = (int64_t)fileOpenTime * MILLISECONDS_IN_SECOND + (int64_t)fileOpenMilliseconds - (int64_t)currentTime * MILLISECONDS_IN_SECOND - (int64_t)currentMilliseconds;

            *recordingPreparationPeriod = MIN(MAXIMUM_PREPARATION_PERIOD, MAX(MINIMUM_PREPARATION_PERIOD, measuredPreparationPeriod + PREPARATION_PERIOD_INCREMENT));

        }

        /* Update the time and calculate earliest schedule start time */

        AudioMoth_getTime(&currentTime, &currentMilliseconds);

        uint32_t scheduleTime = currentTime + ROUNDED_UP_DIV(currentMilliseconds + *recordingPreparationPeriod, MILLISECONDS_IN_SECOND);

        /* Schedule the next recording */

        if (*numberOfRecordingErrors >= MAXIMUM_NUMBER_OF_RECORDING_ERRORS) {

            /* Cancel the schedule */

            *timeOfNextRecordingGain1 = UINT32_MAX;

            *durationOfNextRecordingGain1 = 0;

            *timeOfNextRecordingGain2 = UINT32_MAX;

            *durationOfNextRecordingGain2 = 0;

        } else if (switchPosition == AM_SWITCH_CUSTOM) {

            /* Update schedule time as if the recording (both gain steps) have ended correctly */

            if (recordingState == RECORDING_OKAY || recordingState == SUPPLY_VOLTAGE_LOW || recordingState == SDCARD_WRITE_ERROR) {

                scheduleTime = MAX(scheduleTime, *timeOfNextRecordingGain2 + *durationOfNextRecordingGain2);

            }

            /* Calculate the next recording schedule */

            uint32_t timeOfNextEvent = UINT32_MAX;

            scheduleRecording(scheduleTime, timeOfNextRecordingGain1, durationOfNextRecordingGain1, timeOfNextRecordingGain2, durationOfNextRecordingGain2, &timeOfNextEvent, NULL);


        } else {

            /* Set parameters to start recording now */

            *timeOfNextRecordingGain1 = scheduleTime;

            *durationOfNextRecordingGain1 = UINT32_MAX;

            *timeOfNextRecordingGain2 = UINT32_MAX;

            *durationOfNextRecordingGain2 = UINT32_MAX;

        }

         /* Power down with short interval if the next recording is due */

        if (switchPosition == AM_SWITCH_CUSTOM) {

            calculateTimeToNextEvent(currentTime, currentMilliseconds, &timeUntilPreparationStart);

            if (timeUntilPreparationStart < DEFAULT_WAIT_INTERVAL) {

                *poweredDownWithShortWaitInterval = true;

                SAVE_SWITCH_POSITION_AND_POWER_DOWN(SHORT_WAIT_INTERVAL);

            }

        }

        /* Power down */

        SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    }

    /* Power down if switch position has changed */

    if (switchPosition != *previousSwitchPosition) SAVE_SWITCH_POSITION_AND_POWER_DOWN(DEFAULT_WAIT_INTERVAL);

    /* Calculate the wait intervals */

    int64_t waitIntervalMilliseconds = WAITING_LED_FLASH_INTERVAL;

    uint32_t waitIntervalSeconds = WAITING_LED_FLASH_INTERVAL / MILLISECONDS_IN_SECOND;

    /* Wait for the next event whilst flashing the LED */

    bool startedRealTimeClock = false;

    while (true) {

        /* Update the time */

        AudioMoth_getTime(&currentTime, &currentMilliseconds);

        /* Calculate the time to the next event */

        calculateTimeToNextEvent(currentTime, currentMilliseconds, &timeUntilPreparationStart);

        int64_t timeToEarliestEvent = timeUntilPreparationStart;

        /* Flash LED */

        bool shouldFlashLED = enableLED && shouldSuppressLED == false && timeToEarliestEvent > MINIMUM_LED_FLASH_INTERVAL;

        if (shouldFlashLED) {

            if (*numberOfRecordingErrors > 0) {

                FLASH_LED(Both, WAITING_LED_FLASH_DURATION);

            } else {

                FLASH_LED(Green, WAITING_LED_FLASH_DURATION);

            }

        }

        /* Check there is time to sleep */

        if (timeToEarliestEvent < waitIntervalMilliseconds) {

            /* Calculate the remaining time to power down */

            uint32_t timeToWait = timeToEarliestEvent < 0 ? 0 : timeToEarliestEvent;

            SAVE_SWITCH_POSITION_AND_POWER_DOWN(timeToWait);

        }

        /* Start the real time clock if it isn't running */

        if (startedRealTimeClock == false) {

            AudioMoth_startRealTimeClock(waitIntervalSeconds);

            startedRealTimeClock = true;

        }

        /* Enter deep sleep */

        AudioMoth_deepSleep();

        /* Handle time overflow on awakening */

        AudioMoth_checkAndHandleTimeOverflow();

    }

} // end main()

/* Time zone handler */

inline void AudioMoth_timezoneRequested(int8_t *timezoneHours, int8_t *timezoneMinutes) {

    *timezoneHours = configSettings->timezoneHours;

    *timezoneMinutes = configSettings->timezoneMinutes;

}



/* AudioMoth interrupt handlers */

inline void AudioMoth_handleMicrophoneChangeInterrupt() {

    microphoneChanged = true;

}


inline void AudioMoth_handleSwitchInterrupt() {

    switchPositionChanged = true;

    AudioConfig_cancelAudioConfiguration();

}

inline void AudioMoth_handleDirectMemoryAccessInterrupt(bool isPrimaryBuffer, int16_t **nextBuffer) {

    int16_t *source = secondaryBuffer;

    if (isPrimaryBuffer) source = primaryBuffer;

    /* Apply filter to samples */

    bool thresholdExceeded = DigitalFilter_applyFilter(source, buffers[writeBuffer] + writeBufferIndex, configSettings->sampleRateDivider, numberOfRawSamplesInDMATransfer);

    numberOfDMATransfers += 1;

    /* Update the current buffer index and write buffer if wait period is over */

    if (numberOfDMATransfers > numberOfDMATransfersToWait) {

        writeIndicator[writeBuffer] |= thresholdExceeded;

        writeBufferIndex += numberOfRawSamplesInDMATransfer / configSettings->sampleRateDivider;

        if (writeBufferIndex == NUMBER_OF_SAMPLES_IN_BUFFER) {

            writeBufferIndex = 0;

            writeBuffer = (writeBuffer + 1) & (NUMBER_OF_BUFFERS - 1);

            writeIndicator[writeBuffer] = false;

        }

    }

}


/* AudioMoth USB message handlers */

inline void AudioMoth_usbFirmwareVersionRequested(uint8_t **firmwareVersionPtr) {

    *firmwareVersionPtr = firmwareVersion;

}

inline void AudioMoth_usbFirmwareDescriptionRequested(uint8_t **firmwareDescriptionPtr) {

    *firmwareDescriptionPtr = firmwareDescription;

}

inline void AudioMoth_usbApplicationPacketRequested(uint32_t messageType, uint8_t *transmitBuffer, uint32_t size) {

    /* Copy the current time to the USB packet */

    uint32_t currentTime;

    AudioMoth_getTime(&currentTime, NULL);

    memcpy(transmitBuffer + 1, &currentTime, UINT32_SIZE_IN_BYTES);

    /* Copy the unique ID to the USB packet */

    memcpy(transmitBuffer + 5, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, AM_UNIQUE_ID_SIZE_IN_BYTES);

    /* Copy the battery state to the USB packet */

    uint32_t supplyVoltage = AudioMoth_getSupplyVoltage();

    AM_batteryState_t batteryState = AudioMoth_getBatteryState(supplyVoltage);

    memcpy(transmitBuffer + 5 + AM_UNIQUE_ID_SIZE_IN_BYTES, &batteryState, 1);

    /* Copy the firmware version to the USB packet */

    memcpy(transmitBuffer + 6 + AM_UNIQUE_ID_SIZE_IN_BYTES, firmwareVersion, AM_FIRMWARE_VERSION_LENGTH);

    /* Copy the firmware description to the USB packet */

    memcpy(transmitBuffer + 6 + AM_UNIQUE_ID_SIZE_IN_BYTES + AM_FIRMWARE_VERSION_LENGTH, firmwareDescription, AM_FIRMWARE_DESCRIPTION_LENGTH);

}

inline void AudioMoth_usbApplicationPacketReceived(uint32_t messageType, uint8_t* receiveBuffer, uint8_t *transmitBuffer, uint32_t size) {

    /* Make persistent configuration settings data structure */

    static persistentConfigSettings_t persistentConfigSettings __attribute__ ((aligned(UINT32_SIZE_IN_BYTES)));

    memcpy(&persistentConfigSettings.firmwareVersion, &firmwareVersion, AM_FIRMWARE_VERSION_LENGTH);

    memcpy(&persistentConfigSettings.firmwareDescription, &firmwareDescription, AM_FIRMWARE_DESCRIPTION_LENGTH);

    memcpy(&persistentConfigSettings.configSettings, receiveBuffer + 1,  sizeof(configSettings_t));

    /* Implement energy saver mode changes */

    if (isEnergySaverMode(&persistentConfigSettings.configSettings)) {

        persistentConfigSettings.configSettings.sampleRate /= 2;
        persistentConfigSettings.configSettings.clockDivider /= 2;
        persistentConfigSettings.configSettings.sampleRateDivider /= 2;

    }

    /* Copy persistent configuration settings to flash */

    uint32_t numberOfBytes = ROUND_UP_TO_MULTIPLE(sizeof(persistentConfigSettings_t), UINT32_SIZE_IN_BYTES);

    bool success = AudioMoth_writeToFlashUserDataPage((uint8_t*)&persistentConfigSettings, numberOfBytes);

    if (success) {

        /* Copy the USB packet contents to the back-up register data structure location */

        copyToBackupDomain((uint32_t*)configSettings, (uint8_t*)&persistentConfigSettings.configSettings, sizeof(configSettings_t));

        /* Copy the back-up register data structure to the USB packet */

        copyFromBackupDomain(transmitBuffer + 1, (uint32_t*)configSettings, sizeof(configSettings_t));

        /* Revert energy saver mode changes */

        configSettings_t *tempConfigSettings = (configSettings_t*)(transmitBuffer + 1);

        if (isEnergySaverMode(tempConfigSettings)) {

            tempConfigSettings->sampleRate *= 2;
            tempConfigSettings->clockDivider *= 2;
            tempConfigSettings->sampleRateDivider *= 2;

        }

        /* Set the time */

        AudioMoth_setTime(configSettings->time, USB_CONFIG_TIME_CORRECTION);

    } else {

        /* Return blank configuration as error indicator */

        memset(transmitBuffer + 1, 0, sizeof(configSettings_t));

    }

}

/* Audio configuration handlers */

inline void AudioConfig_handleAudioConfigurationEvent(AC_audioConfigurationEvent_t event) {

    if (event == AC_EVENT_PULSE) {

        audioConfigPulseCounter = (audioConfigPulseCounter + 1) % AUDIO_CONFIG_PULSE_INTERVAL;

    } else if (event == AC_EVENT_START) {

        audioConfigStateLED = true;

        audioConfigToggleLED = true;

        AudioMoth_getTime(&secondsOfAcousticSignalStart, &millisecondsOfAcousticSignalStart);

    } else if (event == AC_EVENT_BYTE) {

        audioConfigToggleLED = !audioConfigToggleLED;

    } else if (event == AC_EVENT_BIT_ERROR || event == AC_EVENT_CRC_ERROR) {

        audioConfigStateLED = false;

    }

    AudioMoth_setGreenLED((audioConfigStateLED && audioConfigToggleLED) || (!audioConfigStateLED && !audioConfigPulseCounter));

}

inline void AudioConfig_handleAudioConfigurationPacket(uint8_t *receiveBuffer, uint32_t size) {

    bool isTimePacket = size == (UINT32_SIZE_IN_BYTES + UINT16_SIZE_IN_BYTES);

    bool isDeploymentPacket = size == (UINT32_SIZE_IN_BYTES + UINT16_SIZE_IN_BYTES + DEPLOYMENT_ID_LENGTH);

    if (isTimePacket || isDeploymentPacket) {

        /* Copy time from the packet */

        uint32_t time;

        memcpy(&time, receiveBuffer, UINT32_SIZE_IN_BYTES);

        /* Calculate the time correction */

        uint32_t secondsOfAcousticSignalEnd;

        uint32_t millisecondsOfAcousticSignalEnd;

        AudioMoth_getTime(&secondsOfAcousticSignalEnd, &millisecondsOfAcousticSignalEnd);

        uint32_t millisecondTimeOffset = (secondsOfAcousticSignalEnd - secondsOfAcousticSignalStart) * MILLISECONDS_IN_SECOND + millisecondsOfAcousticSignalEnd - millisecondsOfAcousticSignalStart + AUDIO_CONFIG_TIME_CORRECTION;

        /* Set the time */

        AudioMoth_setTime(time + millisecondTimeOffset / MILLISECONDS_IN_SECOND, millisecondTimeOffset % MILLISECONDS_IN_SECOND);

        /* Set deployment */

        if (isDeploymentPacket) {

            copyToBackupDomain((uint32_t*)deploymentID, receiveBuffer + UINT32_SIZE_IN_BYTES + UINT16_SIZE_IN_BYTES, DEPLOYMENT_ID_LENGTH);

        }

        /* Indicate success */

        AudioConfig_cancelAudioConfiguration();

        acousticConfigurationPerformed = true;

    }

    /* Reset receive state */

    audioConfigStateLED = false;

}

/* Clear and encode the compression buffer */

static void clearCompressionBuffer() {

    for (uint32_t i = 0; i < COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE; i += 1) {

        compressionBuffer[i] = 0;

    }

}

static void encodeCompressionBuffer(uint32_t numberOfCompressedBuffers) {

    for (uint32_t i = 0; i < UINT32_SIZE_IN_BITS; i += 1) {

        compressionBuffer[i] = numberOfCompressedBuffers & 0x01 ? 1 : -1;

        numberOfCompressedBuffers >>= 1;

    }

    for (uint32_t i = UINT32_SIZE_IN_BITS; i < COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE; i += 1) {

        compressionBuffer[i] = 0;

    }

}

/* Generate foldername and filename from time */ //TODO
//TODO remove debug duration again
static void generateFolderAndFilename(char *foldername, char *filename, uint32_t timestamp, uint32_t duration_, bool prefixFoldername) {

    struct tm time;

    struct tm duration;

    time_t rawTime = timestamp + configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    time_t rawDuration = duration_ + configSettings->timezoneHours * SECONDS_IN_HOUR + configSettings->timezoneMinutes * SECONDS_IN_MINUTE;

    gmtime_r(&rawTime, &time);

    gmtime_r(&rawDuration, &duration);

    sprintf(foldername, "%04d%02d%02d", YEAR_OFFSET + time.tm_year, MONTH_OFFSET + time.tm_mon, time.tm_mday);

    uint32_t length = prefixFoldername ? sprintf(filename, "%s/", foldername) : 0;

    length += sprintf(filename + length, "%s_%02d%02d%02d_%02d%02d%02d", foldername, time.tm_hour, time.tm_min, time.tm_sec,duration.tm_hour, duration.tm_min, duration.tm_sec);

    char *extension = ".WAV";

    strcpy(filename + length, extension);

}


/* Save recording to SD card */

static AM_recordingState_t makeRecording(uint32_t timeOfNextRecording, uint32_t recordDuration, AM_gainSetting_t gainOfNextRecording, bool enableLED, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, uint32_t *fileOpenTime, uint32_t *fileOpenMilliseconds) {


    /* Calculate effective sample rate */

    uint32_t effectiveSampleRate = configSettings->sampleRate / configSettings->sampleRateDivider;

    /* Calculate the sample multiplier */

    float sampleMultiplier = 16.0f / (float)(configSettings->oversampleRate * configSettings->sampleRateDivider);

    if (AudioMoth_hasInvertedOutput()) sampleMultiplier = -sampleMultiplier;

    /* Calculate the number of samples in each DMA transfer (while ensuring that number of samples written to the SRAM buffer on each DMA transfer is a power of two so each SRAM buffer is filled after an integer number of DMA transfers) */

    numberOfRawSamplesInDMATransfer = MAXIMUM_SAMPLES_IN_DMA_TRANSFER / configSettings->sampleRateDivider;

    while (numberOfRawSamplesInDMATransfer & (numberOfRawSamplesInDMATransfer - 1)) {

        numberOfRawSamplesInDMATransfer = numberOfRawSamplesInDMATransfer & (numberOfRawSamplesInDMATransfer - 1);

    }

    numberOfRawSamplesInDMATransfer *= configSettings->sampleRateDivider;

    /* Initialise buffers */

    writeBuffer = 0;

    writeBufferIndex = 0;

    buffers[0] = (int16_t*)AM_EXTERNAL_SRAM_START_ADDRESS;

    for (uint32_t i = 1; i < NUMBER_OF_BUFFERS; i += 1) {
        buffers[i] = buffers[i - 1] + NUMBER_OF_SAMPLES_IN_BUFFER;
    }

    /* Initialise termination conditions */

    microphoneChanged = false;

    bool supplyVoltageLow = false;

    /* Initialise microphone for recording */

    AudioMoth_enableExternalSRAM();

    AM_gainRange_t gainRange = configSettings->enableLowGainRange ? AM_LOW_GAIN_RANGE : AM_NORMAL_GAIN_RANGE;

    bool externalMicrophone = AudioMoth_enableMicrophone(gainRange, gainOfNextRecording, configSettings->clockDivider, configSettings->acquisitionCycles, configSettings->oversampleRate);

    AudioMoth_initialiseDirectMemoryAccess(primaryBuffer, secondaryBuffer, numberOfRawSamplesInDMATransfer);

    /* Show LED for SD card activity */

    if (enableLED) AudioMoth_setRedLED(true);

    /* Open a file with the current local time as the name */

    static char filename[MAXIMUM_FILE_NAME_LENGTH];

    static char foldername[MAXIMUM_FILE_NAME_LENGTH];
// TODO
    generateFolderAndFilename(foldername, filename, timeOfNextRecording, recordDuration, configSettings->enableDailyFolders);

    if (configSettings->enableDailyFolders) {

        bool directoryExists = AudioMoth_doesDirectoryExist(foldername);

        if (directoryExists == false) FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_makeDirectory(foldername));

    }

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_openFile(filename));

    AudioMoth_setRedLED(false);

    /* Measure the time difference from the start time */

    AudioMoth_getTime(fileOpenTime, fileOpenMilliseconds);

    /* Calculate time correction for sample rate due to file header */

    uint32_t numberOfSamplesInHeader = sizeof(wavHeader_t) / NUMBER_OF_BYTES_IN_SAMPLE;

    int32_t sampleRateTimeOffset = ROUNDED_DIV(numberOfSamplesInHeader * MILLISECONDS_IN_SECOND, effectiveSampleRate);

    /* Calculate time until the recording should start */

    int64_t millisecondsUntilRecordingShouldStart = (int64_t)timeOfNextRecording * MILLISECONDS_IN_SECOND - (int64_t)*fileOpenTime * MILLISECONDS_IN_SECOND - (int64_t)*fileOpenMilliseconds - (int64_t)sampleRateTimeOffset;

    /* Calculate the actual recording start time if the intended start has been missed */

    uint32_t timeOffset = millisecondsUntilRecordingShouldStart < 0 ? 1 - millisecondsUntilRecordingShouldStart / MILLISECONDS_IN_SECOND : 0;

    recordDuration = timeOffset >= recordDuration ? 0 : recordDuration - timeOffset;

    millisecondsUntilRecordingShouldStart += timeOffset * MILLISECONDS_IN_SECOND;

    /* Calculate the period to wait before starting the DMA transfers */

    uint32_t numberOfRawSamplesPerMillisecond = configSettings->sampleRate / MILLISECONDS_IN_SECOND;

    uint32_t numberOfRawSamplesToWait = millisecondsUntilRecordingShouldStart * numberOfRawSamplesPerMillisecond;

    numberOfDMATransfersToWait = numberOfRawSamplesToWait / numberOfRawSamplesInDMATransfer;

    uint32_t remainingNumberOfRawSamples = numberOfRawSamplesToWait % numberOfRawSamplesInDMATransfer;

    uint32_t remainingMillisecondsToWait = ROUNDED_DIV(remainingNumberOfRawSamples, numberOfRawSamplesPerMillisecond);

    /* Calculate updated recording parameters */

    uint32_t maximumNumberOfSeconds = (MAXIMUM_WAV_FILE_SIZE - sizeof(wavHeader_t)) / NUMBER_OF_BYTES_IN_SAMPLE / effectiveSampleRate;

    bool fileSizeLimited = (recordDuration > maximumNumberOfSeconds);

    uint32_t numberOfSamples = effectiveSampleRate * (fileSizeLimited ? maximumNumberOfSeconds : recordDuration);

    /* Initialise main loop variables */

    uint32_t readBuffer = 0;

    uint32_t samplesWritten = 0;

    uint32_t buffersProcessed = 0;

    uint32_t numberOfCompressedBuffers = 0;

    uint32_t totalNumberOfCompressedSamples = 0;

    /* Start processing DMA transfers */

    numberOfDMATransfers = 0;

    AudioMoth_delay(remainingMillisecondsToWait);

    AudioMoth_startMicrophoneSamples(configSettings->sampleRate);

    /* Main recording loop */

    while (samplesWritten < numberOfSamples + numberOfSamplesInHeader && !microphoneChanged && !switchPositionChanged  && !supplyVoltageLow) {

        while (readBuffer != writeBuffer && samplesWritten < numberOfSamples + numberOfSamplesInHeader && !microphoneChanged && !switchPositionChanged && !supplyVoltageLow) {

            /* Determine the appropriate number of bytes to the SD card */

            uint32_t numberOfSamplesToWrite = MIN(numberOfSamples + numberOfSamplesInHeader - samplesWritten, NUMBER_OF_SAMPLES_IN_BUFFER);

            /* Check if this buffer should actually be written to the SD card */

            bool writeIndicated =  writeIndicator[readBuffer];

            /* Ensure the minimum number of buffers will be written */

            bool shouldWriteThisSector = writeIndicated ;

            /* Compress the buffer or write the buffer to SD card */

            if (shouldWriteThisSector == false && buffersProcessed > 0 && numberOfSamplesToWrite == NUMBER_OF_SAMPLES_IN_BUFFER) {

                numberOfCompressedBuffers += NUMBER_OF_BYTES_IN_SAMPLE * NUMBER_OF_SAMPLES_IN_BUFFER / COMPRESSION_BUFFER_SIZE_IN_BYTES;

            } else {

                /* Light LED during SD card write if appropriate */

                if (enableLED) AudioMoth_setRedLED(true);

                /* Encode and write compression buffer */

                if (numberOfCompressedBuffers > 0) {

                    encodeCompressionBuffer(numberOfCompressedBuffers);

                    totalNumberOfCompressedSamples += (numberOfCompressedBuffers - 1) * COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE;

                    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, COMPRESSION_BUFFER_SIZE_IN_BYTES));

                    numberOfCompressedBuffers = 0;

                }

                /* Either write the buffer or write a blank buffer */

                if (shouldWriteThisSector) {

                    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(buffers[readBuffer], NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamplesToWrite));

                } else {

                    clearCompressionBuffer();

                    uint32_t numberOfBlankSamplesToWrite = numberOfSamplesToWrite;

                    while (numberOfBlankSamplesToWrite > 0) {

                        uint32_t numberOfSamples = MIN(numberOfBlankSamplesToWrite, COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE);

                        FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, NUMBER_OF_BYTES_IN_SAMPLE * numberOfSamples));

                        numberOfBlankSamplesToWrite -= numberOfSamples;

                    }

                }

                /* Clear LED */

                AudioMoth_setRedLED(false);

            }

            /* Increment buffer counters */

            readBuffer = (readBuffer + 1) & (NUMBER_OF_BUFFERS - 1);

            samplesWritten += numberOfSamplesToWrite;

            buffersProcessed += 1;

        }

        /* Check the voltage level */

        if (configSettings->enableLowVoltageCutoff && AudioMoth_isSupplyAboveThreshold() == false) {

            supplyVoltageLow = true;

        }

        /* Sleep until next DMA transfer is complete */

        AudioMoth_sleep();

    }

    /* Write the compression buffer files at the end */

    if (samplesWritten < numberOfSamples + numberOfSamplesInHeader && numberOfCompressedBuffers > 0) {

        /* Light LED during SD card write if appropriate */

        if (enableLED) AudioMoth_setRedLED(true);

        /* Encode and write compression buffer */

        encodeCompressionBuffer(numberOfCompressedBuffers);

        totalNumberOfCompressedSamples += (numberOfCompressedBuffers - 1) * COMPRESSION_BUFFER_SIZE_IN_BYTES / NUMBER_OF_BYTES_IN_SAMPLE;

        FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(compressionBuffer, COMPRESSION_BUFFER_SIZE_IN_BYTES));

        /* Clear LED */

        AudioMoth_setRedLED(false);

    }

    /* Determine recording state */

    AM_recordingState_t recordingState = microphoneChanged ? MICROPHONE_CHANGED :
                                         switchPositionChanged ? SWITCH_CHANGED :
                                         supplyVoltageLow ? SUPPLY_VOLTAGE_LOW :
                                         fileSizeLimited ? FILE_SIZE_LIMITED :
                                         RECORDING_OKAY;

    /* Initialise the WAV header */

    samplesWritten = MAX(numberOfSamplesInHeader, samplesWritten);

    setHeaderDetails(&wavHeader, effectiveSampleRate, samplesWritten - numberOfSamplesInHeader - totalNumberOfCompressedSamples);

    setHeaderComment(&wavHeader, configSettings, timeOfNextRecording + timeOffset, (uint8_t*)AM_UNIQUE_ID_START_ADDRESS, deploymentID, defaultDeploymentID, extendedBatteryState, temperature, externalMicrophone, recordingState);

    /* Write the header */

    if (enableLED) AudioMoth_setRedLED(true);

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_seekInFile(0));

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_writeToFile(&wavHeader, sizeof(wavHeader_t)));

    /* Close the file */

    FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_closeFile());

    AudioMoth_setRedLED(false);

    /* Rename the file if necessary */

    static char newFilename[MAXIMUM_FILE_NAME_LENGTH];

    if (timeOffset > 0) {
        //TODO
        generateFolderAndFilename(foldername, newFilename, timeOfNextRecording + timeOffset, recordDuration, configSettings->enableDailyFolders);

        if (enableLED) AudioMoth_setRedLED(true);

        FLASH_LED_AND_RETURN_ON_ERROR(AudioMoth_renameFile(filename, newFilename));

        AudioMoth_setRedLED(false);

    }

    /* Return recording state */

    return recordingState;

}

/* Schedule recordings */

static void adjustRecordingDuration(uint32_t *duration, uint32_t recordDuration1, uint32_t recordDuration2, uint32_t sleepDuration) {
    //this cuts the recording period down to not include any final sleep phase

    uint32_t durationOfCycle = recordDuration1 + recordDuration2 + sleepDuration;

    uint32_t numberOfCycles = *duration / durationOfCycle;

    uint32_t partialCycle = *duration % durationOfCycle;

    if (partialCycle == 0) {

        *duration = *duration > sleepDuration ? *duration - sleepDuration : 0;

    } else {

        *duration = MIN(*duration, numberOfCycles * durationOfCycle + recordDuration1 + recordDuration2);

    }

}

static void calculateStartAndDuration(uint32_t currentTime, uint32_t currentSeconds, recordingPeriod_t *period, uint32_t *startTime, uint32_t *duration) {

    *startTime = currentTime - currentSeconds + SECONDS_IN_MINUTE * period->startMinutes;

    *duration = period->endMinutes <= period->startMinutes ? MINUTES_IN_DAY + period->endMinutes - period->startMinutes : period->endMinutes - period->startMinutes;

    *duration *= SECONDS_IN_MINUTE;

}

/* sets timeOfNextRecording, durationOfNextRecording */
static void scheduleRecording(uint32_t currentTime, uint32_t *timeOfNextRecordingGain1, uint32_t *durationOfNextRecordingGain1, uint32_t *timeOfNextRecordingGain2, uint32_t *durationOfNextRecordingGain2, uint32_t *startOfRecordingPeriod, uint32_t *endOfRecordingPeriod) {

    /* Enforce minumum schedule date */

    currentTime = MAX(currentTime, START_OF_CENTURY);

    /* Check if recording should be limited by earliest recording time */

    if (configSettings->earliestRecordingTime > 0) {

        currentTime = MAX(currentTime, configSettings->earliestRecordingTime);

    }

    /* No suitable recording periods */

    uint32_t activeRecordingPeriods = MIN(configSettings->activeRecordingPeriods, MAX_RECORDING_PERIODS);

    if (activeRecordingPeriods == 0) {

        *timeOfNextRecordingGain1 = UINT32_MAX;

        *timeOfNextRecordingGain2 = UINT32_MAX;

        if (startOfRecordingPeriod) *startOfRecordingPeriod = UINT32_MAX;

        if (endOfRecordingPeriod) *endOfRecordingPeriod = UINT32_MAX;

        *durationOfNextRecordingGain1 = 0;

        *durationOfNextRecordingGain2 = 0;

        return;

    }

    /* Calculate the number of seconds of this day */

    time_t rawTime = currentTime;

    struct tm *time = gmtime(&rawTime);

    uint32_t currentSeconds = SECONDS_IN_HOUR * time->tm_hour + SECONDS_IN_MINUTE * time->tm_min + time->tm_sec;

    /* Check the last active period on the previous day */

    recordingPeriod_t *lastPeriod = configSettings->recordingPeriods + configSettings->activeRecordingPeriods - 1;

    uint32_t startTime, duration;

    calculateStartAndDuration(currentTime - SECONDS_IN_DAY, currentSeconds, lastPeriod, &startTime, &duration);
    //of last period

    if (configSettings->disableSleepRecordCycle == false) {

        adjustRecordingDuration(&duration, configSettings->recordDurationGain1, configSettings->recordDurationGain2, configSettings->sleepDuration);

    }

    if (currentTime < startTime + duration && duration > 0) goto done;  //*lastperiod is the recording phase we're still in

    /* Check each active recording period on the same day*/
    // else
    // if not currently in the *lastperiod recording period, identify next one
    for (uint32_t i = 0; i < activeRecordingPeriods; i += 1) {

        recordingPeriod_t *currentPeriod = configSettings->recordingPeriods + i;

        calculateStartAndDuration(currentTime, currentSeconds, currentPeriod, &startTime, &duration);

        if (configSettings->disableSleepRecordCycle == false) {

            adjustRecordingDuration(&duration, configSettings->recordDurationGain1, configSettings->recordDurationGain2, configSettings->sleepDuration);

        }

        if (currentTime < startTime + duration && duration > 0) goto done;  //*pointer is at next or current period

    }

    /* Calculate time until first period tomorrow */

    recordingPeriod_t *firstPeriod = configSettings->recordingPeriods;

    calculateStartAndDuration(currentTime + SECONDS_IN_DAY, currentSeconds, firstPeriod, &startTime, &duration);

    if (configSettings->disableSleepRecordCycle == false) {

        adjustRecordingDuration(&duration, configSettings->recordDurationGain1, configSettings->recordDurationGain2, configSettings->sleepDuration);

    }

done:  //start and duration of current/next period have been identified at startTime, duration

    /* Set the time for start and end of the recording period */

    if (startOfRecordingPeriod) *startOfRecordingPeriod = startTime;

    if (endOfRecordingPeriod) *endOfRecordingPeriod = startTime + duration;

    /* Resolve sleep and record cycle */

    if (configSettings->disableSleepRecordCycle) {

        // if sleep record cycle disabled there will be one long recording at gain1
        // for the whole recording period

        *timeOfNextRecordingGain1 = startTime;

        *durationOfNextRecordingGain1 = duration;

        *timeOfNextRecordingGain2 = UINT32_MAX;

        *durationOfNextRecordingGain2 = UINT32_MAX;

    } else {

        if (currentTime <= startTime) {

            /* Recording should start at the start of the recording period */

            *timeOfNextRecordingGain1 = startTime;

            *durationOfNextRecordingGain1 = MIN(duration, configSettings->recordDurationGain1);

            if (duration >= startTime + configSettings->recordDurationGain1){ //at least some of Gain2 recording fits in period

                *timeOfNextRecordingGain2 = startTime + configSettings->recordDurationGain1; //start after recording 1

                *durationOfNextRecordingGain2 = MIN(duration - configSettings->recordDurationGain1, configSettings->recordDurationGain2); //run for full time or rest of period
            } else {

                *timeOfNextRecordingGain2 = UINT32_MAX; //never

                *durationOfNextRecordingGain2 = 0;

            }

        } else { //we are currently somewhere in a recording period

            /* Recording should start immediately or at the start of the next recording cycle */

            uint32_t secondsFromStartOfPeriod = currentTime - startTime;

            //figure out what recording/sleep phase were in by looking at current time

            uint32_t durationOfCycle = configSettings->recordDurationGain1 + configSettings->recordDurationGain2 + configSettings->sleepDuration;

            uint32_t partialCycle = secondsFromStartOfPeriod % durationOfCycle;  //where we are in the recordGain1 - recordGain2 - sleep cycle

            *timeOfNextRecordingGain1 = currentTime - partialCycle;

            *timeOfNextRecordingGain2 = currentTime - partialCycle + configSettings->recordDurationGain1;

            if (partialCycle >= configSettings->recordDurationGain1) { //we're past first gain recording

                /* Wait for next cycle to begin */

                *timeOfNextRecordingGain1 += durationOfCycle;

                if (partialCycle >= configSettings->recordDurationGain1 + configSettings->recordDurationGain2) { //we're also past second gain recording

                    *timeOfNextRecordingGain2 += durationOfCycle;

                }

            }

            uint32_t remainingDuration = startTime + duration - *timeOfNextRecordingGain1; //of period, for next recording

           *durationOfNextRecordingGain1 = MIN(remainingDuration, configSettings->recordDurationGain1);

            if (remainingDuration >= configSettings->recordDurationGain1){ //at least some of Gain2 recording fits in period

                *durationOfNextRecordingGain2 = MIN(remainingDuration - configSettings->recordDurationGain1, configSettings->recordDurationGain2);
            }
            else{

                *durationOfNextRecordingGain2 =0;
            }


        }

    }

    /* Check if recording should be limited by last recording time */ // TODO

    uint32_t latestRecordingTime = configSettings->latestRecordingTime > 0 ? configSettings->latestRecordingTime : MIDPOINT_OF_CENTURY;

    if (*timeOfNextRecordingGain1 >= latestRecordingTime) {

        *timeOfNextRecordingGain1 = UINT32_MAX;

        if (startOfRecordingPeriod) *startOfRecordingPeriod = UINT32_MAX;

        if (endOfRecordingPeriod) *endOfRecordingPeriod = UINT32_MAX;

        *durationOfNextRecordingGain1 = 0;

    } else {

        int64_t excessTime = (int64_t)*timeOfNextRecordingGain1 + (int64_t)*durationOfNextRecordingGain1 - (int64_t)latestRecordingTime;

        if (excessTime > 0) *durationOfNextRecordingGain1 -= excessTime;

        if (endOfRecordingPeriod) *endOfRecordingPeriod = *timeOfNextRecordingGain1 + *durationOfNextRecordingGain1;

    }


    if (*timeOfNextRecordingGain2 >= latestRecordingTime) {

        *timeOfNextRecordingGain2 = UINT32_MAX;

        if (startOfRecordingPeriod) *startOfRecordingPeriod = UINT32_MAX;

        if (endOfRecordingPeriod) *endOfRecordingPeriod = UINT32_MAX;

        *durationOfNextRecordingGain2 = 0;

    } else {

        int64_t excessTime = (int64_t)*timeOfNextRecordingGain2 + (int64_t)*durationOfNextRecordingGain2 - (int64_t)latestRecordingTime;

        if (excessTime > 0) *durationOfNextRecordingGain2 -= excessTime;

        if (endOfRecordingPeriod) *endOfRecordingPeriod = *timeOfNextRecordingGain2 + *durationOfNextRecordingGain2;

    }

}

/* Flash LED according to battery life */

static void flashLedToIndicateBatteryLife(void) {

    uint32_t numberOfFlashes = LOW_BATTERY_LED_FLASHES;

    uint32_t supplyVoltage = AudioMoth_getSupplyVoltage();

    if (configSettings->batteryLevelDisplayType == NIMH_LIPO_BATTERY_VOLTAGE) {

        /* Set number of flashes according to battery voltage */

        AM_extendedBatteryState_t batteryState = AudioMoth_getExtendedBatteryState(supplyVoltage);

        if (batteryState > AM_EXT_BAT_4V3) {

            numberOfFlashes = 1;

        } else if (batteryState > AM_EXT_BAT_3V5) {

            numberOfFlashes = AM_EXT_BAT_4V4 - batteryState;

        }

    } else {

        /* Set number of flashes according to battery state */

        AM_batteryState_t batteryState = AudioMoth_getBatteryState(supplyVoltage);

        if (batteryState > AM_BATTERY_LOW) {

            numberOfFlashes = (batteryState >= AM_BATTERY_4V6) ? 4 : (batteryState >= AM_BATTERY_4V4) ? 3 : (batteryState >= AM_BATTERY_4V0) ? 2 : 1;

        }

    }

    /* Flash LED */

    for (uint32_t i = 0; i < numberOfFlashes; i += 1) {

        FLASH_LED(Red, SHORT_LED_FLASH_DURATION)

        if (numberOfFlashes == LOW_BATTERY_LED_FLASHES) {

            AudioMoth_delay(SHORT_LED_FLASH_DURATION);

        } else {

            AudioMoth_delay(LONG_LED_FLASH_DURATION);

        }

    }

}

