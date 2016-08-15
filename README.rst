SDI featued Intel® Curie BSP
################
This Curie BSP is Intel Curie BSP plus add-on features from Intel Smart Device Innovations team. 

Intel® Curie BSP is the SDK that will help you developing software on Curie based boards, for
example with the Arduino 101 board (AKA Genuino 101).

Curie BSP is the SDK dedicated to the wearable device. It is part of the Intel Software Platform for Curie which
allows you to develop an End to End Application, from the Cloud, through the
smartphone downto the Intel® Curie powered wearable device. For more
information, visit https://iqsdk.intel.com/.

For now Curie BSP is compatible with the Arduino 101 board (see http://www.intel.com/content/www/us/en/do-it-yourself/arduino-101.html).
Keep in mind that this configuration is unsupported by Arduino. After flashing a Curie BSP
application, you won't be able to run an Arduino sketch anymore unless you
restore the original firmware as indicated below.

The environment supports only the OS Ubuntu GNU/Linux as host.

Intel® Curie SoC documentation is available at http://www.intel.com/content/www/us/en/wearables/wearable-soc.html.


ONE TIME SETUP
**************

Curie BSP needs a set of external projects to be built. The repo tool is currently used to fetch
all the needed repositories.

Setup repo:

``mkdir ~/bin``
``wget http://commondatastorage.googleapis.com/git-repo-downloads/repo -O ~/bin/repo``
``chmod a+x ~/bin/repo``

In ~/.bashrc add:

``PATH=$PATH:~/bin``

Create your directory (eg. Curie_BSP):

``mkdir Curie_BSP && cd $_``

Initialize your repo:

``repo init -u https://github.com/SDICurie/manifest``

Download the sources files:

``repo sync -j 5 -d``

Initialize the environment:

``make -C wearable_device_sw/projects/curie_hello/ one_time_setup``

Backup your original Arduino 101 Software:

To be able to run back Arduino sketches and reset your Arduino 101 board to
factory settings, you need to prepare a backup of the original firmware.
The backup method and the hardware setup is the same as the Zephyr project one.
Refer to https://www.zephyrproject.org/doc/board/arduino_101.html.
The Curie BSP includes the Zephyr OS Source code in
'wearable_device_software/external/zephyr' so, no need to download it again.

BUILD AND FLASH A PROJECT
*************************

Curie BSP must be built out-of-tree, in a dedicated output directory whose location is chosen
by the end user.

The recommended procedure during development phase is the following:

Prepare your build environment:

``mkdir -p ./out``
``cd ./out``

Setup your project, for example based on the curie_hello project:

``make -f ../wearable_device_sw/projects/curie_hello/Makefile setup``

Compile:

``make image -j 32``

Flash:

You first need to flash the bootloader on the board using JTAG. This have to be done only once:

``make flash FLASH_CONFIG="jtag_x86_rom+bootloader"``

Then you can flash your board using the standard USB port:

``make flash``
then press the reset button on the board to start the flashing.

GENERATE DOCUMENTATION
**********************

To generate the documentation in the "doc" directory you can type:

``make doc``

Or you can generate and browse it directly using the default browser:

``make doc_view``
