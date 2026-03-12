################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (11.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/App/app_main.c \
../Core/Src/App/stepper_tmc2209.c \
../Core/Src/App/uart_sequence.c 

OBJS += \
./Core/Src/App/app_main.o \
./Core/Src/App/stepper_tmc2209.o \
./Core/Src/App/uart_sequence.o 

C_DEPS += \
./Core/Src/App/app_main.d \
./Core/Src/App/stepper_tmc2209.d \
./Core/Src/App/uart_sequence.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/App/%.o Core/Src/App/%.su Core/Src/App/%.cyclo: ../Core/Src/App/%.c Core/Src/App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -IC:/Users/a2105/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3/Drivers/STM32F4xx_HAL_Driver/Inc -IC:/Users/a2105/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -IC:/Users/a2105/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3/Drivers/CMSIS/Device/ST/STM32F4xx/Include -IC:/Users/a2105/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-App

clean-Core-2f-Src-2f-App:
	-$(RM) ./Core/Src/App/app_main.cyclo ./Core/Src/App/app_main.d ./Core/Src/App/app_main.o ./Core/Src/App/app_main.su ./Core/Src/App/stepper_tmc2209.cyclo ./Core/Src/App/stepper_tmc2209.d ./Core/Src/App/stepper_tmc2209.o ./Core/Src/App/stepper_tmc2209.su ./Core/Src/App/uart_sequence.cyclo ./Core/Src/App/uart_sequence.d ./Core/Src/App/uart_sequence.o ./Core/Src/App/uart_sequence.su

.PHONY: clean-Core-2f-Src-2f-App

