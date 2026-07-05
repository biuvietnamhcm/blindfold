STM32_SigningTool_CLI -bin FSBL/Debug/RedEye_FSBL.bin  -nk -of 0x80000000 -t fsbl -o RedEye_FSBL_trusted.bin  -hv 2.3 -dump RedEye_FSBL_trusted.bin -align
STM32_SigningTool_CLI -bin Appli/Debug/RedEye_Appli.bin -nk -of 0x80000000 -t fsbl -o RedEye_Appli_trusted.bin -hv 2.3 -dump RedEye_Appli_trusted.bin -align
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el ~/RedEye/tools/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr -w RedEye_FSBL_trusted.bin  0x70000000
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el ~/RedEye/tools/ExternalLoader/MX25UM51245G_STM32N6570-NUCLEO.stldr -w RedEye_Appli_trusted.bin 0x70100000
