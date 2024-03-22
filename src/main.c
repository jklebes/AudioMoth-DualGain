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

typedef enum {RECORDING_OKAY, FILE_SIZE_LIMITED, SUPPLY_VOLTAGE_LOW, SWITCH_CHANGED, MICROPHONE_CHANGED, MAGNETIC_SWITCH, SDCARD_WRITE_ERROR} AM_recordingState_t;

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
    AM_gainSetting_t gain;
    uint8_t clockDivider;
    uint8_t acquisitionCycles;
    uint8_t oversampleRate;
    uint32_t sampleRate;
    uint8_t sampleRateDivider;
    uint16_t sleepDuration;
    uint16_t recordDuration;
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
    uint16_t lowerFilterFreq;
    uint16_t higherFilterFreq;
    union {
        uint16_t amplitudeThreshold;
        uint16_t frequencyTriggerCentreFrequency;
    };
    uint8_t requireAcousticConfiguration : 1;
    AM_batteryLevelDisplayType_t batteryLevelDisplayType : 1;
    uint8_t minimumTriggerDuration : 6;
    union {
        struct {
            uint8_t frequencyTriggerWindowLengthShift : 4;
            uint8_t frequencyTriggerThresholdPercentageMantissa : 4;
            int8_t frequencyTriggerThresholdPercentageExponent : 3;
        };
        struct {
            uint8_t enableAmplitudeThresholdDecibelScale : 1;
            uint8_t amplitudeThresholdDecibels : 7;
            uint8_t enableAmplitudeThresholdPercentageScale : 1;
            uint8_t amplitudeThresholdPercentageMantissa : 4;
            int8_t amplitudeThresholdPercentageExponent : 3;
        };
    };
    uint8_t enableEnergySaverMode : 1;
    uint8_t disable48HzDCBlockingFilter : 1;
    uint8_t enableTimeSettingFromGPS : 1;
    uint8_t enableMagneticSwitch : 1;
    uint8_t enableLowGainRange : 1;
    uint8_t enableFrequencyTrigger : 1;
    uint8_t enableDailyFolders : 1;
} configSettings_t;

#pragma pack(pop)

// TODO Add dual gain mode.  duplicate gain state, time fields.
// TODO potentially this struct gets bigger and needs more space
static const configSettings_t defaultConfigSettings = {
    .time = 0,
    .gain = AM_GAIN_MEDIUM,
    .clockDivider = 4,
    .acquisitionCycles = 16,
    .oversampleRate = 1,
    .sampleRate = 384000,
    .sampleRateDivider = 8,
    .sleepDuration = 5,
    .recordDuration = 55,
    .enableLED = 1,
    .activeRecordingPeriods = 1,
    .recordingPeriods = {
        {.startMinutes = 0, .endMinutes = 0},
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
    .lowerFilterFreq = 0,
    .higherFilterFreq = 0,
    .amplitudeThreshold = 0,
    .requireAcousticConfiguration = 0,
    .batteryLevelDisplayType = BATTERY_LEVEL,
    .minimumTriggerDuration = 0,
    .enableAmplitudeThresholdDecibelScale = 0,
    .amplitudeThresholdDecibels = 0,
    .enableAmplitudeThresholdPercentageScale = 0,
    .amplitudeThresholdPercentageMantissa = 0,
    .amplitudeThresholdPercentageExponent = 0,
    .enableEnergySaverMode = 0,
    .disable48HzDCBlockingFilter = 0,
    .enableTimeSettingFromGPS = 0,
    .enableMagneticSwitch = 0,
    .enableLowGainRange = 0,
    .enableFrequencyTrigger = 0,
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

static void setHeaderComment(wavHeader_t *wavHeader, configSettings_t *configSettings, uint32_t currentTime, uint8_t *serialNumber, uint8_t *deploymentID, uint8_t *defaultDeploymentID, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, bool externalMicrophone, AM_recordingState_t recordingState, AM_filterType_t filterType) {

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

    comment += sprintf(comment, "at %s gain while battery was ", gainSettings[configSettings->gain]);

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

    //TODO remove triggers
    bool frequencyTriggerEnabled = configSettings->enableFrequencyTrigger;

    bool amplitudeThresholdEnabled = frequencyTriggerEnabled ? false : configSettings->amplitudeThreshold > 0 || configSettings->enableAmplitudeThresholdDecibelScale || configSettings->enableAmplitudeThresholdPercentageScale;

    if (frequencyTriggerEnabled) {

        comment += sprintf(comment, " Frequency trigger (%u.%ukHz and window length of %u samples) threshold was ", configSettings->frequencyTriggerCentreFrequency / 10, configSettings->frequencyTriggerCentreFrequency % 10, (0x01 << configSettings->frequencyTriggerWindowLengthShift));

        comment += formatPercentage(comment, configSettings->frequencyTriggerThresholdPercentageMantissa, configSettings->frequencyTriggerThresholdPercentageExponent);

        comment += sprintf(comment, " with %us minimum trigger duration.", configSettings->minimumTriggerDuration);

    }
    // TODO remove filters
    uint16_t lowerFilterFreq = configSettings->lowerFilterFreq;

    uint16_t higherFilterFreq = configSettings->higherFilterFreq;

    if (filterType == LOW_PASS_FILTER) {

        comment += sprintf(comment, " Low-pass filter with frequency of %01u.%01ukHz applied.", higherFilterFreq / 10, higherFilterFreq % 10);

    } else if (filterType == BAND_PASS_FILTER) {

        comment += sprintf(comment, " Band-pass filter with frequencies of %01u.%01ukHz and %01u.%01ukHz applied.", lowerFilterFreq / 10, lowerFilterFreq % 10, higherFilterFreq / 10, higherFilterFreq % 10);

    } else if (filterType == HIGH_PASS_FILTER) {

        comment += sprintf(comment, " High-pass filter with frequency of %01u.%01ukHz applied.", lowerFilterFreq / 10, lowerFilterFreq % 10);

    }

    if (amplitudeThresholdEnabled) {

        comment += sprintf(comment, " Amplitude threshold was ");

        if (configSettings->enableAmplitudeThresholdDecibelScale && configSettings->enableAmplitudeThresholdPercentageScale == false) {

            comment += formatDecibels(comment, configSettings->amplitudeThresholdDecibels);

        } else if (configSettings->enableAmplitudeThresholdPercentageScale && configSettings->enableAmplitudeThresholdDecibelScale == false) {

            comment += formatPercentage(comment, configSettings->amplitudeThresholdPercentageMantissa, configSettings->amplitudeThresholdPercentageExponent);

        } else {

            comment += sprintf(comment, "%u", configSettings->amplitudeThreshold);

        }

        comment += sprintf(comment, " with %us minimum trigger duration.", configSettings->minimumTriggerDuration);

    }

    if (recordingState != RECORDING_OKAY) {

        comment += sprintf(comment, " Recording stopped");

        if (recordingState == MICROPHONE_CHANGED) {

            comment += sprintf(comment, " due to microphone change.");

        } else if (recordingState == SWITCH_CHANGED) {

            comment += sprintf(comment, " due to switch position change.");

        } else if (recordingState == MAGNETIC_SWITCH) {

            comment += sprintf(comment, " by magnetic switch.");

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

    length += sprintf(configBuffer + length, "Gain                            : %s\r\n\r\n", gainSettings[configSettings->gain]);

    length += sprintf(configBuffer + length, "Sleep duration (s)              : ");

    if (configSettings->disableSleepRecordCycle) {

        length += sprintf(configBuffer + length, "-");

    } else {

        length += sprintf(configBuffer + length, "%u", configSettings->sleepDuration);

    }

    length += sprintf(configBuffer + length, "\r\nRecording duration (s)          : ");

    if (configSettings->disableSleepRecordCycle) {

        length += sprintf(configBuffer + length, "-");

    } else {

        length += sprintf(configBuffer + length, "%u", configSettings->recordDuration);

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

    length = sprintf(configBuffer, "\r\n\r\nFilter                          : ");

    if (configSettings->lowerFilterFreq == 0 && configSettings->higherFilterFreq == 0) {

        length += sprintf(configBuffer + length, "-");

    } else if (configSettings->lowerFilterFreq == UINT16_MAX) {

        length += sprintf(configBuffer + length, "Low-pass (%u.%ukHz)", configSettings->higherFilterFreq / 10, configSettings->higherFilterFreq % 10);

    } else if (configSettings->higherFilterFreq == UINT16_MAX) {

        length += sprintf(configBuffer + length, "High-pass (%u.%ukHz)", configSettings->lowerFilterFreq / 10, configSettings->lowerFilterFreq % 10);

    } else {

        length += sprintf(configBuffer + length, "Band-pass (%u.%ukHz - %u.%ukHz)", configSettings->lowerFilterFreq / 10, configSettings->lowerFilterFreq % 10, configSettings->higherFilterFreq / 10, configSettings->higherFilterFreq % 10);

    }

    bool frequencyTriggerEnabled = configSettings->enableFrequencyTrigger;

    bool amplitudeThresholdEnabled = frequencyTriggerEnabled ? false : configSettings->amplitudeThreshold > 0 || configSettings->enableAmplitudeThresholdDecibelScale || configSettings->enableAmplitudeThresholdPercentageScale;

    length += sprintf(configBuffer + length, "\r\n\r\nTrigger type                    : ");

    if (frequencyTriggerEnabled) {

        length += sprintf(configBuffer + length, "Frequency (%u.%ukHz and window length of %u samples)", configSettings->frequencyTriggerCentreFrequency / 10, configSettings->frequencyTriggerCentreFrequency % 10, (0x01 << configSettings->frequencyTriggerWindowLengthShift));

        length += sprintf(configBuffer + length, "\r\nThreshold setting               : ");

        length += formatPercentage(configBuffer + length, configSettings->frequencyTriggerThresholdPercentageMantissa, configSettings->frequencyTriggerThresholdPercentageExponent);

    } else if (amplitudeThresholdEnabled) {

        length += sprintf(configBuffer + length, "Amplitude");

        length += sprintf(configBuffer + length, "\r\nThreshold setting               : ");

        if (configSettings->enableAmplitudeThresholdDecibelScale && configSettings->enableAmplitudeThresholdPercentageScale == false) {

            length += formatDecibels(configBuffer + length, configSettings->amplitudeThresholdDecibels);

        } else if (configSettings->enableAmplitudeThresholdPercentageScale && configSettings->enableAmplitudeThresholdDecibelScale == false) {

            length += formatPercentage(configBuffer + length, configSettings->amplitudeThresholdPercentageMantissa, configSettings->amplitudeThresholdPercentageExponent);

        } else {

            length += sprintf(configBuffer + length, "%u", configSettings->amplitudeThreshold);

        }

    } else {

        length += sprintf(configBuffer + length, "-");

        length += sprintf(configBuffer + length, "\r\nThreshold setting               : -");

    }

    length += sprintf(configBuffer + length, "\r\nMinimum trigger duration (s)    : ");

    if (frequencyTriggerEnabled || amplitudeThresholdEnabled) {

        length += sprintf(configBuffer + length, "%u", configSettings->minimumTriggerDuration);

    } else {

        length += sprintf(configBuffer + length, "-");

    }

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    length = sprintf(configBuffer, "\r\n\r\nEnable LED                      : %s\r\n", configSettings->enableLED ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable low-voltage cut-off      : %s\r\n", configSettings->enableLowVoltageCutoff ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable battery level indication : %s\r\n\r\n", configSettings->disableBatteryLevelDisplay ? "No" : configSettings->batteryLevelDisplayType == NIMH_LIPO_BATTERY_VOLTAGE ? "Yes (NiMH/LiPo voltage range)" : "Yes");

    length += sprintf(configBuffer + length, "Always require acoustic chime   : %s\r\n", configSettings->requireAcousticConfiguration ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Use daily folder for WAV files  : %s\r\n\r\n", configSettings->enableDailyFolders ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Disable 48Hz DC blocking filter : %s\r\n", configSettings->disable48HzDCBlockingFilter ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable energy saver mode        : %s\r\n", configSettings->enableEnergySaverMode ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable low gain range           : %s\r\n\r\n", configSettings->enableLowGainRange ? "Yes" : "No");

    length += sprintf(configBuffer + length, "Enable magnetic switch          : %s\r\n", configSettings->enableMagneticSwitch ? "Yes" : "No");
    // TODO remove
    length += sprintf(configBuffer + length, "Enable GPS time setting         : %s\r\n", configSettings->enableTimeSettingFromGPS ? "Yes" : "No");

    RETURN_BOOL_ON_ERROR(AudioMoth_writeToFile(configBuffer, length));

    RETURN_BOOL_ON_ERROR(AudioMoth_closeFile());

    return true;

}

/* Backup domain variables */

static uint32_t *previousSwitchPosition = (uint32_t*)AM_BACKUP_DOMAIN_START_ADDRESS;

static uint32_t *timeOfNextRecording = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 4);

static uint32_t *durationOfNextRecording = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 8);

static uint32_t *timeOfNextGPSTimeSetting = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 12);

static uint32_t *writtenConfigurationToFile = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 16);

static uint8_t *deploymentID = (uint8_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 20);

static uint32_t *readyToMakeRecordings = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 28);

static uint32_t *shouldSetTimeFromGPS = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 32);

static uint32_t *numberOfRecordingErrors = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 36);

static uint32_t *recordingPreparationPeriod = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 40);

static uint32_t *waitingForMagneticSwitch = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 44);

static uint32_t *poweredDownWithShortWaitInterval = (uint32_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 48);

static configSettings_t *configSettings = (configSettings_t*)(AM_BACKUP_DOMAIN_START_ADDRESS + 52);

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

static volatile bool magneticSwitch;

static volatile bool microphoneChanged;

static volatile bool switchPositionChanged;

/* DMA buffers */

static int16_t primaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

static int16_t secondaryBuffer[MAXIMUM_SAMPLES_IN_DMA_TRANSFER];

/* Firmware version and description */
// TODO name this version
static uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH] = {1, 0, 0};
// TODO name this verion
static uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH] = "DualGain-Firmware";

/* Function prototypes */

static void flashLedToIndicateBatteryLife(void);

static void scheduleRecording(uint32_t currentTime, uint32_t *timeOfNextRecording, uint32_t *durationOfNextRecording, uint32_t *startOfRecordingPeriod, uint32_t *endOfRecordingPeriod);

static AM_recordingState_t makeRecording(uint32_t timeOfNextRecording, uint32_t recordDuration, bool enableLED, AM_extendedBatteryState_t extendedBatteryState, int32_t temperature, uint32_t *fileOpenTime, uint32_t *fileOpenMilliseconds);

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

/* Magnetic switch wait functions */

static void startWaitingForMagneticSwitch() {

    /* Flash LED to indicate start of waiting for magnetic switch */

    FLASH_REPEAT_LED(Red, MAGNETIC_SWITCH_CHANGE_FLASHES, SHORT_LED_FLASH_DURATION);

    /* Cancel any scheduled recording */

    *timeOfNextRecording = UINT32_MAX;

    *durationOfNextRecording = UINT32_MAX;

    *timeOfNextGPSTimeSetting = UINT32_MAX;

    *waitingForMagneticSwitch = true;

}

static void stopWaitingForMagneticSwitch(uint32_t *currentTime, uint32_t *currentMilliseconds) {

    /* Flash LED to indicate end of waiting for magnetic switch */

    FLASH_REPEAT_LED(Green, MAGNETIC_SWITCH_CHANGE_FLASHES, SHORT_LED_FLASH_DURATION);

    /* Schedule next recording */

    AudioMoth_getTime(currentTime, currentMilliseconds);

    uint32_t scheduleTime = *currentTime + ROUNDED_UP_DIV(*currentMilliseconds + *recordingPreparationPeriod, MILLISECONDS_IN_SECOND);

    scheduleRecording(scheduleTime, timeOfNextRecording, durationOfNextRecording, timeOfNextGPSTimeSetting, NULL);

    *timeOfNextGPSTimeSetting = configSettings->enableTimeSettingFromGPS ? *timeOfNextGPSTimeSetting - GPS_MAX_TIME_SETTING_PERIOD : UINT32_MAX;

    *waitingForMagneticSwitch = false;

}

/* Function to calculate the time to the next event */

static void calculateTimeToNextEvent(uint32_t currentTime, uint32_t currentMilliseconds, int64_t *timeUntilPreparationStart, int64_t *timeUntilNextGPSTimeSetting) {

    *timeUntilPreparationStart = (int64_t)*timeOfNextRecording * MILLISECONDS_IN_SECOND - (int64_t)*recordingPreparationPeriod - (int64_t)currentTime * MILLISECONDS_IN_SECOND - (int64_t)currentMilliseconds;

    *timeUntilNextGPSTimeSetting = (int64_t)*timeOfNextGPSTimeSetting * MILLISECONDS_IN_SECOND - (int64_t)currentTime * MILLISECONDS_IN_SECOND - (int64_t)currentMilliseconds;

}

/* Required time zone handler */

void AudioMoth_timezoneRequested(int8_t *timezoneHours, int8_t *timezoneMinutes) { }

/* Required interrupt handles */

void AudioMoth_handleSwitchInterrupt() { }
void AudioMoth_handleMicrophoneChangeInterrupt() { }
void AudioMoth_handleMicrophoneInterrupt(int16_t sample) { }
void AudioMoth_handleDirectMemoryAccessInterrupt(bool primaryChannel, int16_t **nextBuffer) { }

/* Required USB message handlers */

void AudioMoth_usbFirmwareVersionRequested(uint8_t **firmwareVersionPtr) {

    *firmwareVersionPtr = firmwareVersion;

}

void AudioMoth_usbFirmwareDescriptionRequested(uint8_t **firmwareDescriptionPtr) {

    *firmwareDescriptionPtr = firmwareDescription;

}

void AudioMoth_usbApplicationPacketRequested(uint32_t messageType, uint8_t *transmitBuffer, uint32_t size) { }
void AudioMoth_usbApplicationPacketReceived(uint32_t messageType, uint8_t *receiveBuffer, uint8_t *transmitBuffer, uint32_t size) { }

/* Main function */

int main() {

    /* Initialise device */

    AudioMoth_initialise();

    /* Check the switch position */

    AM_switchPosition_t switchPosition = AudioMoth_getSwitchPosition();

    if (switchPosition == AM_SWITCH_USB) {

        /* Handle the case that the switch is in USB position. Waits in low energy state until USB disconnected or switch moved  */

        AudioMoth_handleUSB();

    } else {

        /* Flash both LED */

        AudioMoth_setBothLED(true);

        AudioMoth_delay(100);

        AudioMoth_setBothLED(false);

    }

    /* Power down and wake up in one second */

    AudioMoth_powerDownAndWake(1, true);

}
