- I assembled a working version of Audiomoth_Firmware_Basic by putting together files from Audiomoth_Firmware_Basic and Audiomoth_Project
	- Several	file names had to be corrected to all lowercase
	- It compiles with arm_eabi cross compiler.
- I removed files and functions related to filters; amplitude triggers; GPS time setting and magnetic switch.
- Implementing Dual Gain mode:  
	- Objective: to have two recordings made in quick succession in place of one in the scheduled recording cycle, for example two recordings of three minutes each taken every hour.  Each recording will have a different GAIN setting.  
	- Implementation:  The scheduling of the sleep-record cycle in recording periods will continue to run as in existing code.  The minimal intervention to implement dual gain mode is to alternate bewteen long ans short sleep periods.  A two-value toggle AM_DualGainStep keeps track of whether we are at recording gain 1 or 2 in the dual gain cycle.