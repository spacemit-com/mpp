#!/bin/bash
###
 # Copyright 2022-2023 SPACEMIT. All rights reserved.
 # Use of this source code is governed by a BSD-style license
 # that can be found in the LICENSE file.
 # 
 # @Author: David(qiang.fu@spacemit.com)
 # @Date: 2023-02-02 10:05:09
 # @LastEditTime: 2023-02-02 16:22:05
 # @Description: code style optimization, remove spaces at the end of line.
### 

find . -name '*.h'   | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's///g' {}
find . -name '*.c'   | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's///g' {}
find . -name '*.cpp' | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's///g' {}
find . -name '*.cc'  | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's///g' {}
find . -name '*.hh'  | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's///g' {}

find . -name '*.h'   | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's/ *$//g' {}
find . -name '*.c'   | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's/ *$//g' {}
find . -name '*.cpp' | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's/ *$//g' {}
find . -name '*.cc'  | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's/ *$//g' {}
find . -name '*.hh'  | cut -d : -f 1 | uniq | xargs -I{} -t sed -i -e 's/ *$//g' {}
