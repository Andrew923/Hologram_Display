#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

bool Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Config: cannot open " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and blank lines
        if (line.empty() || line[0] == '#')
            continue;

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back()  == ' ') s.pop_back();
        };
        trim(key);
        trim(val);

        if      (key == "udp_port")             udp_port           = std::stoi(val);
        else if (key == "hall_gpio_pin")         hall_gpio_pin      = std::stoi(val);
        else if (key == "led_rows")              led_rows           = std::stoi(val);
        else if (key == "led_cols")              led_cols           = std::stoi(val);
        else if (key == "led_parallel")          led_parallel       = std::stoi(val);
        else if (key == "led_chain_length")      led_chain_length   = std::stoi(val);
        else if (key == "led_brightness")        led_brightness     = std::stoi(val);
        else if (key == "led_gpio_slowdown")     led_gpio_slowdown  = std::stoi(val);
        else if (key == "led_hardware_mapping")  led_hardware_mapping = val;
        else if (key == "slice_count")           slice_count        = std::stoi(val);
        else
            std::cerr << "Config: unknown key '" << key << "'\n";
    }

    return true;
}
