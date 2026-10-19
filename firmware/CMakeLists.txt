cmake_minimum_required(VERSION 3.16)
# secrets.defaults holds the Wi-Fi credentials and is not committed,
# copy secrets.defaults.example to create it
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;secrets.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(edge_cam)
