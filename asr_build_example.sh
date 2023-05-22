#!/usr/bin/env bash

#
#    Copyright (c) 2023 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

#default
if [ -z "$ASR_BOARD" ]; then
    echo "ASR_BOARD not set! default asr582x"
    ASR_BOARD=asr582x
fi

if [ "$ASR_BOARD" != "asr582x" ] && [ "$ASR_BOARD" != "asr595x" ]; then
    echo "$ASR_BOARD not support!"
    exit 1
fi

if [ -z "$ASR_TOOLCHAIN_PATH" ]; then
    echo "ASR_TOOLCHAIN_PATH not set!"
    exit 1
fi

#arch
if [ "$ASR_BOARD" = "asr582x" ]; then
    ASR_ARCH=arm
elif [ "$ASR_BOARD" = "asr595x" ]; then
    ASR_ARCH=riscv
fi

#SDK root
ASR_SDK_ROOT=//third_party/connectedhomeip/third_party/asr/$ASR_BOARD

# Build steps
EXAMPLE_DIR=$1
OUTPUT_DIR=$2

USAGE="./scripts/examples/gn_asr_example.sh <AppRootFolder> <outputFolder> [<Build options>]"

#default args
optArgs="treat_warnings_as_errors=false "
optArgs+="chip_use_transitional_commissionable_data_provider=false "
optArgs+="custom_toolchain=\"//../../../config/asr/toolchain:asrtoolchain\" "
optArgs+="asr_ic_family=\"$ASR_BOARD\" "
optArgs+="target_cpu=\"$ASR_ARCH\" "
optArgs+="asr_sdk_build_root=\"$ASR_SDK_ROOT\"  "
optArgs+="mbedtls_target=\"$ASR_SDK_ROOT:asr_build\" "
optArgs+="asr_toolchain_root=\"$ASR_TOOLCHAIN_PATH\" "

if [ "$ASR_BOARD" == "asr582x" ] || [ "$ASR_BOARD" == "asr595x" ]; then
    optArgs+="chip_config_network_layer_ble=true "
fi

if [ "$#" == "0" ]; then
    echo "Build script for ASR Matter apps
    Format:
    $USAGE

    <AppRootFolder>
        Root Location of the app e.g: examples/lighting-app/asr/

    <outputFolder>
        Desired location for the output files

    <Build options> - optional noteworthy build options for ASR
        chip_build_libshell
            Enable libshell support. (Default false)
        setupDiscriminator
            Discriminatoor value used for BLE connexion. (Default 3840)
        chip_logging
            Enable Matter log. (Default true)
        chip_enable_factory_data
            Use factory data from Flash. (Default false)
        chip_enable_ota_requestor
            Enable OTA requestor. (Default false)
    "
elif [ "$#" -lt "2" ]; then
    echo "Invalid number of arguments
    Format:
    $USAGE"
else
    shift
    shift

    while [ $# -gt 0 ]; do
        optArgs+=$1" "
        shift
    done

    ./scripts/examples/gn_build_example.sh $EXAMPLE_DIR $OUTPUT_DIR $optArgs

    #print stats
    if [ "$ASR_BOARD" = "asr582x" ]; then
        $ASR_TOOLCHAIN_PATH/arm-none-eabi-size -A "$OUTPUT_DIR"/*.out
    elif [ "$ASR_BOARD" = "asr595x" ]; then
        $ASR_TOOLCHAIN_PATH/riscv-asr-elf-size -A "$OUTPUT_DIR"/*.out
    fi
fi
