# ASR_matter_factory_tool

## 编译

1、将src/platform/BUILD.gn中的chip_use_transitional_commissionable_data_provider改为false

2、在third_party/asr/BUILD.gn中，定义CONFIG_ENABLE_ASR_FACTORY_DATA_PROVIDER=1

3、src/platform/ASR/CHIPDevicePlatformConfig.h 中找到CHIP_DEVICE_CONFIG_DEVICE_VENDOR_ID以及CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID，设置PID和VID

## 环境

1、请在编译docker下进行如下操作

2、python3 -m pip install -r requirements.txt

3、export PATH="$PATH:/workspace/connectedhomeip/asr_factory/tool"

## 使用参考：

./ASR_matter_factory_tool -d 3434 -p 99663300 --dac-cert  ../credentials/test/attestation/Chip-Test-DAC-FFF2-8001-0008-Cert.pem --dac-key  ../credentials/test/attestation/Chip-Test-DAC-FFF2-8001-0008-Key.pem --pai-cert  ../credentials/test/attestation/Chip-Test-PAI-FFF2-8001-Cert.der --cd  ../credentials/test/certification-declaration/Chip-Test-CD-FFF2-8001.der

参数如下：

VID: 0xFFF2
PID: 8001
discriminator: 3434
pincode: 99663300

可选参数：

--nokey: 将DAC private key独立为一个bin

--out: 指定生成文件的输出目录

--qrcode： 按输入参数生成QR code 和 manual code，并生成二维码文件

以下需要定义宏 CONFIG_ENABLE_ASR_FACTORY_DEVICE_INFO_PROVIDER=1

--vendor-id 0x133F
--vendor-name ASR
--product-id 0x5821
--product-name asr5821

## 烧录

执行如上命令后会生成ASR_matter_factory.bin，需要将其烧录至Flash

ASR582X地址：0x101C1000

ASR595X地址：0x801C1000