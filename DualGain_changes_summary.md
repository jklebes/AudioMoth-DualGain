- I assembled a working version of Audiomoth_Firmware_Basic by putting together files from Audiomoth_Firmware_Basic and Audiomoth_Project
	- Several	file names had to be corrected to all lowercase
	- It compiles with arm_eabi cross compiler.
- I removed files and functions related to filters; amplitude triggers; GPS time setting and magnetic switch.
- Implementing Dual Gain mode:  
	- Objective: to have two recordings made in quick succession in place of one in the scheduled recording cycle, for example two recordings of three minutes each taken every hour.  Each recording will have a different GAIN setting.  
	- Implementation:  The scheduling of the sleep-record cycle in recording periods will continue to run as in existing code.  The minimal intervention to implement dual gain mode is to alternate bewteen long ans short sleep periods.  A two-value toggle AM_DualGainStep keeps track of whether we are at recording gain 1 or 2 in the dual gain cycle.


## Configuration App
I forked a custom configuration app to work with the DualGain version, as it is not possible to set configuration variables directly from a configuration file.  Without the app a new firmware binary would have to be compiled and flashed to device to change variables such as timings.
- A newer npm/nodejs version than available in debian stable repositories is needed to build the app.  Install from alternative sources.
- There is currently a bug/warning message on running the app with ``npm run start``.  """Uncaught (in promise) TypeError: Failed to fetch", source: devtools://devtools/bundled/panels/elements/elements.js , "Devtools failed to load source map ... ".  This is an ongoing issue in electron.  The app works despite this message.
- I removed all references to filters, triggers (amplitude/frequency), GPS, magnetic switch.  The UI tab "filters" is removed. 
- I duplicated buttons and functions for setting gain, recording time, and sleep time to set gain1 and gain2, recordingDurationGain1 and recordingDurationGain2, sleep and sleepBetweenGains.  The second sleep refers to a short pause in lighter delay/EM1 sleep mode which should be set to 1-5 seconds to allow the first recording to finish between two adjected recordings.
	- To make space for the additional gain and timing buttons and inputs, other setting checkboxes - LED, battery level, voltage range - are moved from the bottom of the "Recording" tab to the "Advanced" tab.
- I changed checks on firmware version; the custom app works only with firmware with description "AudioMoth-DualGain" and not with other names including the official release.
		- There is no possibility of accidentally using the wrong configuration app, the original with DualGain devices or the custom configuration app with mainstream devices, because of initial checks on firmware version before allowing the configure button to be activated.
- The list of fields in configuration settings is changed to match the firmware, removing the unwanted variables and adding second gains and recording/sleep durations.
- Crucially the size of packet transferred and the arrangement of these variables into bits of the data packet is changed.
