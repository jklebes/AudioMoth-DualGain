
## Dual Gain Firmware
- I assembled a working version of Audiomoth_Firmware_Basic by putting together files from Audiomoth_Firmware_Basic and Audiomoth_Project
	- Several	file names had to be corrected to all lowercase
	- It compiles with arm_eabi_none cross compiler from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads .  Install this compiler as described in its Release Notes, edit its install location and folder name in ``build/Makefile``, then run ``make`` in the ``build/`` directory.
- I removed files and functions related to filters; amplitude triggers; GPS time setting and magnetic switch.  Only 48HzDCBlockingFilter and NO_FILTER options are used.  
#### Implementing Dual Gain mode:  
- Objective: to have two recordings made in quick succession in place of one in the scheduled recording cycle, for example two recordings of three minutes each taken every hour.  Each recording will have a different gain setting.  
- Function ScheduleRecording() calculates the start and end times of both the next gain1 recording and the next gain2 recording.
- In the main loop, makeRecording() is called twice in succession, first near the scheduled gain1 start time, then, if a gain2 recording is scheduled to follow shortly, immediately after the gain1 recording finishes.  The scheduled start time of the next recording is passed in; while the function is called near / slightly before the scheduled start time, it waits in AudioMoth_delay (sleep mode EM1) to start the recording exactly at the scheduled time.  There is no power down/power up, no repeat of the main loop with all checks and calculations between a gain1 and a gain2 recording.  
			- It takes 1 second and may take up to 5 seconds according to the documentation for the first makeRecording function to close the file and exit.  The variable sleepBetweenGains is intended for the user to add 1-5 seconds between scheduled recordings to allow the gain1 recording to finish before the scheduled start of the gain2 recording.  If this variable is set to 0, the second recording will be scheduled for the end time of the first, and the second makeRecording() will be called immediate after the first one exits.  Consequently, with the first call taking some time to exit, the actual second function call will occur a few seconds after its scheduled start time.  The second recording will run from its delayed start time to its scheduled end time; its length will be 1-5 seconds less than the expected length.
#### Not fixed
- If a local time zone is selected in the configuration app, the timestamp in the file name is correct to local time of recording but the system "Last modified" timestamp is off by the same time difference.  This issue is present in the main Firmware.


## Configuration App
I forked a custom configuration app to work with the DualGain version.  Configuration settings can be set and the timer started via the configuration app, or in the firmware and via the chime mobile app.  Without the app a new firmware binary would have to be compiled and flashed to device to change variables such as timings.
- On debian a newer npm/nodejs version than available in debian stable repositories is needed to build the app.  Install from alternative sources.  To build on windows I had to install nodejs (admin), work in a local (not mounted) location, and enable developer mode.
- There is currently a bug/warning message on running the app with ``npm run start``.  """Uncaught (in promise) TypeError: Failed to fetch", source: devtools://devtools/bundled/panels/elements/elements.js , "Devtools failed to load source map ... ".  This is an ongoing issue in electron.  The app works despite this message.
- I removed all references to filters, triggers (amplitude/frequency), GPS, magnetic switch.  The UI tab "filters" is removed. 
- I duplicated buttons and functions for setting gain, recording time, and sleep time to set gain1 and gain2, recordingDurationGain1 and recordingDurationGain2, sleep and sleepBetweenGains.  The second sleep refers to a short pause in lighter delay/EM1 sleep mode which should be set to 1-5 seconds to allow the first recording to finish between two adjected recordings.
	- To make space for the additional gain and timing buttons and inputs, other setting checkboxes - LED, battery level, voltage range - are moved from the bottom of the "Recording" tab to the "Advanced" tab.
- I changed checks on firmware version; the custom app works only with firmware with description "AudioMoth-DualGain" and not with other names including the official release.
		- There is no possibility of accidentally using the wrong configuration app, the original with DualGain devices or the custom configuration app with mainstream devices, because of initial checks on firmware version before allowing the configure button to be activated.
- The list of fields in configuration settings is changed to match the firmware, removing the unwanted variables and adding second gains and recording/sleep durations.
- Crucially the size of packet transferred and the arrangement of variables into the bytes of the data packet is changed.
#### Not fixed
- The above warning/error message.
- The battery usage estimate shown in the configuration app has not been updated for Dual Gain use, is not accurate anymore.
