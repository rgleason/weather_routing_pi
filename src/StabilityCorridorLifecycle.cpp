/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN Weather Routing contributors            *
 ***************************************************************************/

#include "StabilityCorridorLifecycle.h"

#include <algorithm>
#include <cstdint>

StabilityCorridorLifecycle::StabilityCorridorLifecycle()
    : m_visible(false), m_pinned(false), m_familyId(-1) {}

void StabilityCorridorLifecycle::Show(
    int familyId, const std::vector<RouteToken>& displayedRoutes) {
  m_visible = true;
  m_pinned = false;
  m_familyId = familyId;
  m_displayedRoutes = displayedRoutes;
}

void StabilityCorridorLifecycle::SetPinned(bool pinned) {
  m_pinned = m_visible && pinned;
}

bool StabilityCorridorLifecycle::ResultsClosed() {
  if (m_pinned) return false;
  return Hide();
}

bool StabilityCorridorLifecycle::DisplayedRoutesChanged(
    const std::vector<RouteToken>& displayedRoutes) {
  if (!m_visible || SameRoutes(m_displayedRoutes, displayedRoutes))
    return false;
  return Hide();
}

bool StabilityCorridorLifecycle::Hide() {
  const bool changed = m_visible;
  m_visible = false;
  m_pinned = false;
  m_familyId = -1;
  m_displayedRoutes.clear();
  return changed;
}

bool StabilityCorridorLifecycle::Contains(RouteToken route) const {
  return std::find(m_displayedRoutes.begin(), m_displayedRoutes.end(), route) !=
         m_displayedRoutes.end();
}

bool StabilityCorridorLifecycle::SameRoutes(
    const std::vector<RouteToken>& first,
    const std::vector<RouteToken>& second) {
  if (first.size() != second.size()) return false;
  std::vector<std::uintptr_t> sortedFirst;
  std::vector<std::uintptr_t> sortedSecond;
  sortedFirst.reserve(first.size());
  sortedSecond.reserve(second.size());
  for (RouteToken route : first)
    sortedFirst.push_back(reinterpret_cast<std::uintptr_t>(route));
  for (RouteToken route : second)
    sortedSecond.push_back(reinterpret_cast<std::uintptr_t>(route));
  std::sort(sortedFirst.begin(), sortedFirst.end());
  std::sort(sortedSecond.begin(), sortedSecond.end());
  return sortedFirst == sortedSecond;
}
