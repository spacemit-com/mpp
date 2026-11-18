# Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
# Use of this source code is governed by the MIT license
# that can be found in the LICENSE file.

prefix=@CMAKE_INSTALL_PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include

Name: mpp
Description: Spacemit Media Process Platform
Requires.private:
Version: @PROJECT_VERSION@
Libs: -L${libdir} -lspacemit_mpp
Libs.private:
Cflags: -I${includedir} -I${includedir}/sys -I${includedir}/vi -I${includedir}/vo -I${includedir}/venc -I${includedir}/vdec -I${includedir}/demux -I${includedir}/mux -I${includedir}/v2d -I${includedir}/cpp -I${includedir}/uvc
