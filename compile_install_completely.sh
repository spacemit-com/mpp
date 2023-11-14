#!/bin/bash

###
 # Copyright 2022-2023 SPACEMIT. All rights reserved.
 # Use of this source code is governed by a BSD-style license
 # that can be found in the LICENSE file.
 # 
 # @Author: David(qiang.fu@spacemit.com)
 # @Date: 2023-01-17 09:36:45
 # @LastEditTime: 2023-11-02 17:56:10
 # @Description: compile and install sh, ubuntu can work, need sudo password at the "1" position
### 

echo 1 | sudo -S rm /usr/local/include/venc.h
sudo rm /usr/local/include/g2d.h
sudo rm /usr/local/include/sys.h
sudo rm /usr/local/include/vdec.h
sudo rm /usr/local/include/base.h
sudo rm /usr/local/include/para.h
sudo rm /usr/local/include/log.h
sudo rm /usr/local/include/os_log.h
sudo rm /usr/local/include/module.h
sudo rm /usr/local/include/frame.h
sudo rm /usr/local/include/packet.h
sudo rm /usr/local/include/type.h
sudo rm /usr/local/include/data.h
sudo rm /usr/local/include/al_interface_dec.h
sudo rm /usr/local/include/al_interface_g2d.h
sudo rm /usr/local/include/al_interface_enc.h
sudo rm /usr/local/include/al_interface_base.h
sudo rm /usr/local/lib/pkgconfig/spacemit_mpp.pc
sudo rm /usr/local/lib/libspacemit_mpp.so
sudo rm /usr/local/lib/libsoft_openh264.so
sudo rm /usr/local/lib/libvc8000.so
sudo rm /usr/local/lib/liblinlonv5v7.so
sudo rm /usr/local/lib/libv4l2_codec.so
sudo rm /usr/local/lib/libffmpegcodec.so
sudo rm /usr/local/lib/libsfdec_plugin.so
sudo rm /usr/local/lib/libsfenc_plugin.so
sudo rm /usr/local/lib/libsfomxil_plugin.so

echo 1 | sudo -S rm -rf out
mkdir out
cd out
#cmake -DCMAKE_INSTALL_PREFIX=/home/fuqiang/workspace/chromium/src/build/linux/debian_bullseye_amd64-sysroot/usr ..
cmake ..
make
echo 1 | sudo -S make install
cd ..
