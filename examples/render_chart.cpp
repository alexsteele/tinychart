#include "sample_chart.hpp"
#include "tinychart/macos.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: render_chart OUTPUT.png [SCALE]\n";
        return 2;
    }
    try {
        tinychart::PngOptions options;
        if (argc == 3) {
            const std::string argument(argv[2]);
            std::size_t consumed = 0;
            options.scale = std::stod(argument, &consumed);
            if (consumed != argument.size()) {
                std::cerr << "Invalid scale: " << argument << '\n';
                return 2;
            }
        }
        tinychart::writePng(sampleChart(), argv[1], options);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
