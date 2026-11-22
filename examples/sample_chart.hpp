#pragma once

#include "tinychart/chart.hpp"

#include <cmath>
#include <utility>

inline tinychart::Chart sampleChart() {
    std::vector<tinychart::Sample> model;
    std::vector<tinychart::Sample> reference;
    std::vector<tinychart::Sample> observations;
    for (int i = 0; i <= 120; ++i) {
        const double x = i / 10.0;
        const double y = 18 + 4 * std::sin(x) + 0.5 * std::sin(6 * x);
        model.push_back({x, y});
        reference.push_back({x, 18 + 3 * std::cos(x * 0.7)});
        if (i % 15 == 0 && i > 0 && i < 120) {
            observations.push_back({x, y + 0.2 * std::cos(3 * x)});
        }
    }
    auto chart = tinychart::lineChart(std::move(model));
    chart.addLine(std::move(reference))
        .addScatter(std::move(observations), tinychart::MarkerStyle{{0.09, 0.28, 0.62}, 3.5})
        .setXDomain({0, 12})
        .setYDomain({12, 24});
    return chart;
}
