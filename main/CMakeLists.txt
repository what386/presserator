idf_component_register(SRCS "../src/firmware/main.cpp"
                            "../src/firmware/usb.cpp"
                            "../src/firmware/bluetooth.cpp"
                            "../src/firmware/keyboard.cpp"
                            "../src/firmware/receiver.cpp"
                            "../src/protocol.cpp"
                       INCLUDE_DIRS "../src/firmware" "../src"
                       PRIV_REQUIRES driver esp_driver_gpio esp_tinyusb bt nvs_flash)
