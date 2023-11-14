#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2023-01-17 09:26:17
# @LastEditTime: 2023-02-03 15:49:21
# @Description: 
#------------------------------------------------------------

prefix=/usr
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include

Name: mpp
Description: Spacemit Media Process Platform
Requires.private:
Version: 0.1.0
Libs: -L${libdir} -lspacemit_mpp
Libs.private:
Cflags: -I${includedir}
