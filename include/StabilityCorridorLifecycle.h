/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN Weather Routing contributors            *
 ***************************************************************************/

#ifndef _STABILITY_CORRIDOR_LIFECYCLE_H_
#define _STABILITY_CORRIDOR_LIFECYCLE_H_

#include <vector>

/**
 * Controller-independent state for a transient stability-corridor overlay.
 *
 * Route tokens are opaque identities owned by the caller.  Keeping this
 * class free of wxWidgets and RouteMapOverlay makes the lifecycle rules easy
 * to exercise in unit tests.
 */
class StabilityCorridorLifecycle {
public:
  using RouteToken = const void*;

  StabilityCorridorLifecycle();

  void Show(int familyId, const std::vector<RouteToken>& displayedRoutes);
  void SetPinned(bool pinned);

  /** Hide unless the displayed corridor has been explicitly pinned. */
  bool ResultsClosed();

  /** Hide when the main Weather Routing selection no longer matches. */
  bool DisplayedRoutesChanged(const std::vector<RouteToken>& displayedRoutes);

  /** Hide and release the presentation identity. */
  bool Hide();

  bool IsVisible() const { return m_visible; }
  bool IsPinned() const { return m_pinned; }
  int FamilyId() const { return m_familyId; }
  bool Contains(RouteToken route) const;

private:
  static bool SameRoutes(const std::vector<RouteToken>& first,
                         const std::vector<RouteToken>& second);

  bool m_visible;
  bool m_pinned;
  int m_familyId;
  std::vector<RouteToken> m_displayedRoutes;
};

#endif
