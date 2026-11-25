#ifndef WEATHER_ROUTING_OFFSHORE_HAZARDS_H
#define WEATHER_ROUTING_OFFSHORE_HAZARDS_H

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace weather_routing {

// A read-only snapshot of supplemental exclusions for one calculation.
class OffshoreHazards {
 public:
  struct Hazard {
    std::string id;
    std::string source;
    double latitude;
    double longitude;
    double radiusNm;
    double uncertaintyNm;
  };

  struct Check {
    const Hazard* hazard{nullptr};
    bool indeterminate{false};
    explicit operator bool() const { return hazard || indeterminate; }
  };

  static constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
  static constexpr const char* kFilename = "offshore-hazards.json";

  // An invalid document is rejected as a unit.
  static std::shared_ptr<const OffshoreHazards> Parse(std::string_view json);

  Check Intersect(double lat1, double lon1, double lat2, double lon2,
                  double clearanceNm) const;
  const std::string& Name() const { return name_; }
  const std::string& Revision() const { return revision_; }
  std::size_t Size() const { return hazards_.size(); }

 private:
  struct IndexedHazard {
    Hazard hazard;
    std::array<double, 3> centre;
  };

  std::string name_;
  std::string revision_;
  std::vector<IndexedHazard> hazards_;
  double maximumRadiusNm_{0.0};
};

}  // namespace weather_routing

#endif
