################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Rockblock/crossplatform.c \
../Core/Rockblock/imt_queue.c \
../Core/Rockblock/jspr.c \
../Core/Rockblock/jspr_command.c \
../Core/Rockblock/rockblock_9704.c \
../Core/Rockblock/serial.c 

OBJS += \
./Core/Rockblock/crossplatform.o \
./Core/Rockblock/imt_queue.o \
./Core/Rockblock/jspr.o \
./Core/Rockblock/jspr_command.o \
./Core/Rockblock/rockblock_9704.o \
./Core/Rockblock/serial.o 

C_DEPS += \
./Core/Rockblock/crossplatform.d \
./Core/Rockblock/imt_queue.d \
./Core/Rockblock/jspr.d \
./Core/Rockblock/jspr_command.d \
./Core/Rockblock/rockblock_9704.d \
./Core/Rockblock/serial.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Rockblock/%.o Core/Rockblock/%.su Core/Rockblock/%.cyclo: ../Core/Rockblock/%.c Core/Rockblock/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_STM32_HAL -DRB_GPIO -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../Core/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32G4xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock" -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock/platform" -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock/third_party" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Rockblock

clean-Core-2f-Rockblock:
	-$(RM) ./Core/Rockblock/crossplatform.cyclo ./Core/Rockblock/crossplatform.d ./Core/Rockblock/crossplatform.o ./Core/Rockblock/crossplatform.su ./Core/Rockblock/imt_queue.cyclo ./Core/Rockblock/imt_queue.d ./Core/Rockblock/imt_queue.o ./Core/Rockblock/imt_queue.su ./Core/Rockblock/jspr.cyclo ./Core/Rockblock/jspr.d ./Core/Rockblock/jspr.o ./Core/Rockblock/jspr.su ./Core/Rockblock/jspr_command.cyclo ./Core/Rockblock/jspr_command.d ./Core/Rockblock/jspr_command.o ./Core/Rockblock/jspr_command.su ./Core/Rockblock/rockblock_9704.cyclo ./Core/Rockblock/rockblock_9704.d ./Core/Rockblock/rockblock_9704.o ./Core/Rockblock/rockblock_9704.su ./Core/Rockblock/serial.cyclo ./Core/Rockblock/serial.d ./Core/Rockblock/serial.o ./Core/Rockblock/serial.su

.PHONY: clean-Core-2f-Rockblock

