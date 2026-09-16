// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
namespace weather_routing {
struct ShorelineSpec {
  const char* quality;
  const char* code;
  const char* id;
  const char* hash;
  std::size_t bytes;
  const char* archive_hash;
  std::size_t archive_bytes;
};
inline constexpr std::array<ShorelineSpec, 5> kShorelineSpecs{{
    {"Crude", "c", "crude", "786c578f3c21db696ab43e89381cfb16fc43823101b31f8097abea4d89744d14", 3466480, "933d111ff352c8b505181e6a2d2888c093222fcccd5f3b455bfbb6f1df217891", 564857},
    {"Low", "l", "low", "9aa0a67e6fdf9d1bca60eaf1befbb4b970f42b72b8503cae459757c8164ec00e", 4911168, "51a1ae4874df513f25447d26740221d4b2a984fabbce9d3f84b29b2e25ff4ab9", 1263484},
    {"Intermediate", "i", "intermediate", "058c7324267dd64dfa61d097fbfb13e778e0979f9b4e1dfc4eb54562ad86cf26", 10660760, "9de2a76bed06711443466c7832798784b45e5a54c752a7c260ee50de00bf0a5e", 3866959},
    {"High", "h", "high", "f833f23da2de4d2083b9a82f4fe9d2563c7ed1575a5a382c190150690d4d5a84", 33435856, "065a10f49c72426cb295ddb076e1d54a1f985d75a759c552a0a93c3d4e1cf71a", 12837261},
    {"Full", "f", "full", "8d4d73897c82dd0e8df63f33e4dab9dd3aea7a26459b923bf404cb9299f1cf04", 171582632, "a36cb8c4fda7d56cfd851d92ed70a315d0a2ff81e10e99e8a0141ce9dc4e6d60", 57754863},
}};
inline const ShorelineSpec& ShorelineSpecFor(int resolution) {
  if (resolution < 0 || resolution > 4) throw std::invalid_argument("Shoreline resolution must be between 0 and 4");
  return kShorelineSpecs[resolution];
}
}  // namespace weather_routing
