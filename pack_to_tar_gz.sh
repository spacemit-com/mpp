#!/bin/bash

###
 # Copyright 2022-2023 SPACEMIT. All rights reserved.
 # Use of this source code is governed by a BSD-style license
 # that can be found in the LICENSE file.
 # 
 # @Author: David(qiang.fu@spacemit.com)
 # @Date: 2022-12-21 17:05:26
 # @LastEditTime: 2023-02-02 10:14:48
 # @Description: util, tar the mpp code to a tar.gz, naming with commit and date info
### 

rm -rf out
rm -rf test_result

COMMITINFO=$(git log -1 --oneline --pretty=format:"%<|(8)%an%s" | sed s:\ :_:g)
echo $COMMITINFO
DATE=$(date "+%Y%m%d%H%M%S")
cd ..
tar -zcvf spacemit_mpp_$DATE\_$COMMITINFO.tar.gz spacemit_mpp/
