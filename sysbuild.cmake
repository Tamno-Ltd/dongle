# Dongle sysbuild: cpuapp (USB HID, root app) + cpurad (ESB PRX, remote).
#
# Build: west build -p -b nrf54h20dk/nrf54h20/cpuapp --sysbuild -d build .
#
# The root application builds for cpuapp (the only core with the USBHS
# controller). The radio core image below builds for cpurad (the only core
# with the RADIO peripheral / ESB). cpuapp boots cpurad at runtime via
# CONFIG_SOC_NRF54H20_CPURAD_ENABLE=y, and the two exchange mouse reports over
# the cpuapp_cpurad_ipc ICBMSG link. Pattern mirrors the NCS spis_wakeup sample
# and the tempo firmware.

if(SB_CONFIG_SOC_NRF54H20)
  ExternalZephyrProject_Add(
    APPLICATION remote_rad
    SOURCE_DIR  ${APP_DIR}/radio
    BOARD ${SB_CONFIG_BOARD}/${SB_CONFIG_SOC}/cpurad
    BOARD_REVISION ${BOARD_REVISION}
  )
endif()
