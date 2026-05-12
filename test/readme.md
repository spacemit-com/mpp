cmake -S . -B build -DBUILD_ROS2_EXAMPLES=AUTO/ON/OFF
cmake --build build --target build_ros2_examples

* `publish_mode:="copy"/"mpp_zero_copy"`
* `./test_rtsp_pull rtsp://<ip>:<port>/<path>`: verify RTSP pull with DEMUX
* `bash scripts/run_rtsp_pull_on_board.sh --host <board_ip> --rtsp-url rtsp://<ip>:<port>/<path>`: build, upload, and run board-side verification
