# A minimal ESP-IDF project whose only job is to prove the runtime runs on the
# real part.  Built for ESP32-S3 (Xtensa) and ESP32-C3 (RISC-V), and run under
# the ESP-IDF QEMU builds, which model both.
#
#   . ~/.espressif/activate_idf_v5.5.4.sh
#   cd tests/embedded/idf && idf.py set-target esp32s3 build
#   tools/run_qemu_firmware.sh --target xtensa
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nj2me_esp32)
