cmake_minimum_required ( VERSION 3.13 )

include ( $ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake )

project ( ATARI_HDC_EMU )

pico_sdk_init ()


add_executable(
	${PROJECT_NAME} 
	sdcard/sd_card.c
	sdcard/sd_spi.c
	sdcard/spi.c
	emusdcard.c
	emu.c 
	mountRAW.c
	emurtc.c
	emuinit.c
	emuscsi.c
	emucheckcards.c
	shell/emushell.c
	shell/uptime.c
	shell/datetime.c
	shell/cpuclock.c
	shell/status.c
	shell/emumount.c
	shell/cpdisk.c
	shell/debug.c
	shell/sdtest.c
	)

target_link_libraries( 
	${PROJECT_NAME}
	pico_stdlib
	pico_multicore
	hardware_spi
	hardware_rtc
	hardware_clocks
	hardware_pll
	hardware_dma
	)

pico_enable_stdio_usb ( ${PROJECT_NAME} 1 )

#pico_add_extra_outputs ( pio_try )
pico_add_extra_outputs(${PROJECT_NAME})

# stop the "Out of Memory" PANIC
add_compile_definitions ( PICO_MALLOC_PANIC=0 )
#pico_set_binary_type ( ${PROJECT_NAME} no_flash )
add_compile_options ( PICO_COPY_TO_RAM )
