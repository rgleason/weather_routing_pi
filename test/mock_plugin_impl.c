// #include "ocpn_plugin.h"

typedef struct wxMenuItem wxMenuItem;
typedef struct opencpn_plugin opencpn_plugin;

double DistGreatCircle_Plugin(double slat, double slon,
                                                  double dlat, double dlon) { return 0.0; }

int AddCanvasContextMenuItem(wxMenuItem *pitem, opencpn_plugin *pplugin) { return 1; }
int AddCanvasMenuItem(wxMenuItem *pitem, opencpn_plugin *pplugin, const char *name) { return 0; }
// void DimeWindow(wxWindow *win) {}
void DistanceBearingMercator_Plugin(double lat0, double lon0,
                                             double lat1, double lon1,
                                             double *brg, double *dist) {}

