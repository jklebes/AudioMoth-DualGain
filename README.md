# AudioMoth-DualGain #
Custom AudioMoth firmware duplicating the recording step to make two recordings in sequence with two different gain settings each cycle.  This fork was composed by combining https://github.com/OpenAcousticDevices/AudioMoth-Project and https://github.com/OpenAcousticDevices/AudioMoth-Firmware-Basic/ .

There are now two gain variables, gain1 and gain2, and four timinig variables, recordingDurationGain1, sleepDuration, recordingDurationGain2, and sleepDurationsBetweenGains for the user to set.  The main sleep is sleep in the standard audiomoth power down - start up cycle in Energy Mode 4.  The sleep between gains is intended to be set to a short delay of 1-5 seconds to allow for the first gain recording's file writing to finish and close before the next is scheduled to start; it is spend in light sleep Energy Mode 1.  If this is set ot zero, 1-5 seconds will be missing from the start of the second gain recording.

Functionalities not needed in this deployment are removed:  GPS time setting, magnetic switch, filters, triggered recordings.

### Building ###

Download an arm-none-eabi compiler from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads and install as descibed in the release notes.  Edit ``build/Makefile`` to reflect the compiler install path and directory name.  Run ``make`` in build.  

### Use ###

Flash the custom firmware binary to the device using the standard AudioMoth flash app.

The time can be set via the acoustic chime app or via custom configuration app https://github.com/jklebes/AudioMoth-Configuration-App .

### Documentation ###

See the [Wiki](https://github.com/OpenAcousticDevices/AudioMoth-Project/wiki) for details of how to compile this example project and how to use the AudioMoth library.

### License ###

Copyright 2017 [Open Acoustic Devices](http://www.openacousticdevices.info/).

[MIT license](http://www.openacousticdevices.info/license).
