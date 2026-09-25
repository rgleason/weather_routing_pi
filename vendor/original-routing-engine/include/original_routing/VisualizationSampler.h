// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace original_routing {

// Online, bounded selection of contour layers across an unknown voyage length.
// Search geometry and accepted route legs never depend on this display data.
class VisualizationSampler {
public:
  explicit VisualizationSampler(std::size_t maximum_layers = 128)
      : maximum_layers_(maximum_layers) {}

  template <typename Layer>
  bool shouldCapture(std::vector<Layer>& layers) {
    const std::size_t front_number = front_count_++;
    if (maximum_layers_ == 0 || front_number % stride_ != 0) return false;
    if (layers.size() >= maximum_layers_) {
      std::size_t retained = 0;
      for (std::size_t source = 0; source < layers.size(); source += 2)
        layers[retained++] = std::move(layers[source]);
      layers.resize(retained);
      stride_ *= 2;
      if (front_number % stride_ != 0) return false;
    }
    return true;
  }

private:
  std::size_t maximum_layers_;
  std::size_t front_count_{};
  std::size_t stride_{1};
};

}  // namespace original_routing
