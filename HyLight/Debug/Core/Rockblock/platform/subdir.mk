################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Rockblock/platform/gpio_stm32.c \
../Core/Rockblock/platform/serial_stm32.c 

OBJS += \
./Core/Rockblock/platform/gpio_stm32.o \
./Core/Rockblock/platform/serial_stm32.o 

C_DEPS += \
./Core/Rockblock/platform/gpio_stm32.d \
./Core/Rockblock/platform/serial_stm32.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Rockblock/platform/%.o Core/Rockblock/platform/%.su Core/Rockblock/platform/%.cyclo: ../Core/Rockblock/platform/%.c Core/Rockblock/platform/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_STM32_HAL -DRB_GPIO -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32G431xx -c -I../Core/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc -I../Drivers/STM32G4xx_HAL_Driver/Inc/Legacy -I../Drivers/BSP/STM32G4xx_Nucleo -I../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock" -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock/platform" -I"C:/Users/Utilisateur/Documents/0_IPSA/aero4/Hylight/stage/1_FTS/github/FTS/FTS_2.0_v1/Core/Rockblock/third_party" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Rockblock-2f-platform

clean-Core-2f-Rockblock-2f-platform:
	-$(RM) ./Core/Rockblock/platform/gpio_stm32.cyclo ./Core/Rockblock/platform/gpio_stm32.d ./Core/Rockblock/platform/gpio_stm32.o ./Core/Rockblock/platform/gpio_stm32.su ./Core/Rockblock/platform/serial_stm32.cyclo ./Core/Rockblock/platform/serial_stm32.d ./Core/Rockblock/platform/serial_stm32.o ./Core/Rockblock/platform/serial_stm32.su

.PHONY: clean-Core-2f-Rockblock-2f-platform

