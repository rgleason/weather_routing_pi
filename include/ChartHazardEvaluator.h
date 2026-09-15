/***************************************************************************
 * Plugin-owned immutable chart-hazard evaluation for route workers.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_CHART_HAZARD_EVALUATOR_H
#define WEATHER_ROUTING_CHART_HAZARD_EVALUATOR_H

#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "ocpn_plugin.h"
#include "OptionalChartSafetyApi.h"

namespace weather_routing {

class ChartSafetyCache;

/**
 * Converts raw chart-authority tiles into immutable, route-parameter-specific
 * masks and evaluates segments without calling OpenCPN chart objects.
 */
class ChartHazardEvaluator {
public:
  explicit ChartHazardEvaluator(ChartSafetyCache& cache);

  /**
   * Return true when the plugin-owned tiles completely answer the query.
   * False means a raw tile is missing and the host extraction seam must be
   * invoked before retrying.
   */
  bool CheckSegment(double lat1, double lon1, double lat2, double lon2,
                    const PlugInSegmentSafetyOptions& options,
                    PlugInSegmentSafetyResult* result);

  void ClearDerivedMasks();
  std::size_t DerivedMaskCount() const;

private:
  struct DerivedMask {
    long lat_tile{0};
    long lon_tile{0};
    double resolution{0.0};
    int rows{0};
    int cols{0};
    int source{PI_SEGMENT_SAFETY_SOURCE_NONE};
    int chart_db_index{-1};
    int chart_scale{-1};
    std::string chart_path;
    std::vector<std::uint16_t> flags;
  };

  std::string MaskKey(long lat_tile, long lon_tile,
                      const PlugInSegmentSafetyOptions& options) const;
  std::shared_ptr<const DerivedMask> GetMask(
      long lat_tile, long lon_tile,
      const PlugInSegmentSafetyOptions& options);
  std::shared_ptr<const DerivedMask> BuildMask(
      long lat_tile, long lon_tile,
      const PlugInSegmentSafetyOptions& options) const;

  ChartSafetyCache& cache_;
  mutable std::shared_mutex mutex_;
  std::map<std::string, std::shared_ptr<const DerivedMask>> masks_;
};

}  // namespace weather_routing

#endif
