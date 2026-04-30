cmake -S . -B build -DBUILD_ROS2_EXAMPLES=AUTO/ON/OFF
cmake --build build --target build_ros2_examples

* `publish_mode:="copy"/"mpp_zero_copy"`
