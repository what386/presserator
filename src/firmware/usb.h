#pragma once

#include "esp_err.h"

esp_err_t usb_init();
bool usb_is_mounted();
bool usb_is_suspended();
