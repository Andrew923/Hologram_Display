#pragma once

#include <string>
#include <unordered_map>

struct Config {
    // Network
    int udp_port = 4210;

    // Hall effect sensor
    int hall_gpio_pin = 17;

    // LED matrix
    int led_rows           = 64;
    int led_cols           = 128;
    int led_parallel       = 2;
    int led_chain_length   = 1;
    int led_brightness     = 50;
    int led_gpio_slowdown  = 2;
    std::string led_hardware_mapping = "adafruit-hat";

    // Display
    int slice_count = 120;

    // Load from a key=value config file. Missing keys keep defaults.
    bool loadFromFile(const std::string& path);
};
