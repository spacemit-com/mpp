# MPP zero-copy video frame descriptor for ROS2.
#
# The publisher keeps image payload in MPP SYS/VB dma-buf memory and publishes
# only this descriptor. A subscriber imports token with VB_Import(), maps it with
# VB_GetVirAddr() or VB_GetDmaBufFd(), consumes it, then calls VB_ReleaseBuffer().

std_msgs/Header header

uint32 version
uint64 token
uint64 pool_id
uint64 buffer_id
uint32 frame_index

uint32 width
uint32 height
uint32 align
uint32 pixel_format
uint32 compress_mode
uint32 color_space

uint64 pts
uint32 total_size
uint32 plane_num
uint32[6] plane_stride
uint32[6] plane_size
uint32[6] plane_size_valid
uint64[6] plane_phy_addr
