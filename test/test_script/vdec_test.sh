#!/bin/bash

###
 # Copyright 2022-2023 SPACEMIT. All rights reserved.
 # Use of this source code is governed by a BSD-style license
 # that can be found in the LICENSE file.
 # 
 # @Author: David(qiang.fu@spacemit.com)
 # @Date: 2022-12-21 14:05:48
 # @LastEditTime: 2023-02-02 10:19:39
 # @Description: video decode test sh, running the csv config case
###

rm -rf test_result
mkdir test_result

CURRENT_PATH=`pwd`
LOG_PATH=$CURRENT_PATH/test_result/test.log
CMD_PATH=$CURRENT_PATH/out/test/vdec_test
STREAM_PATH=$CURRENT_PATH/test/test_script/streams/h264
RESULT_PATH=$CURRENT_PATH/test_result

echo "CURRENT_PATH : $CURRENT_PATH" > $LOG_PATH
echo "LOG_PATH : $LOG_PATH" >> $LOG_PATH
echo "CMD_PATH : $CMD_PATH" >> $LOG_PATH
echo "STREAM_PATH : $STREAM_PATH" >> $LOG_PATH
echo "RESULT_PATH : $RESULT_PATH" >> $LOG_PATH

echo "============ vdec test start ==============" >> $LOG_PATH

test_num=0
pass_num=0
fail_num=0

IFS=$'\n'
for line in $(cat $1); do
  name=$(echo ${line} | cut -d: -f 1)
  codingtype=$(echo ${line} | cut -d : -f 2)
  width=$(echo ${line} | cut -d : -f 3)
  height=$(echo ${line} | cut -d : -f 4)
  codectype=$(echo ${line} | cut -d : -f 5)
  md5=$(echo ${line} | cut -d : -f 6)

  test_num=$((test_num+1))
  echo "test $test_num" >> $LOG_PATH
  echo ">>>>> $name test start" >> $LOG_PATH

  yuv_out=$RESULT_PATH/$name-$width-$height-$codectype.yuv

  $CMD_PATH -i $STREAM_PATH/$name -c $codingtype -o $yuv_out -ct $codectype

  md5_out=$(md5sum $yuv_out |cut -f 1 -d " ")

  if [ $md5 == $md5_out ]; then
    echo "$md5 == $md5_out" >> $LOG_PATH
    echo ">>>>> $name test pass" >> $LOG_PATH
    pass_num=$((pass_num+1))
  else
    echo "$md5 != $md5_out" >> $LOG_PATH
    echo ">>>>> $name test fail" >> $LOG_PATH
    fail_num=$((fail_num+1))
  fi

done

echo "============ vdec test finish ==============" >> $LOG_PATH

echo "Total:$test_num    Pass:$pass_num    Fail:$fail_num" >> $LOG_PATH
