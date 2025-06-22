/***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#ifndef _WEATHER_ROUTING_BOAT_DIALOG_H_
#define _WEATHER_ROUTING_BOAT_DIALOG_H_

#include <wx/fileconf.h>

#include "EditPolarDialog.h"

class WeatherRouting;
class CrossOverGenerationThread;

class BoatDialog : public BoatDialogBase {
public:
  BoatDialog(WeatherRouting& weatherrouting);
  ~BoatDialog();

  void LoadPolar(const wxString& filename);

  Boat m_Boat;
  wxString m_boatpath;

private:
  void OnMouseEventsPolarPlot(wxMouseEvent& event);

  void OnPaintPlot(wxPaintEvent& event);
  void OnUpdatePlot(wxSizeEvent& event) { OnUpdatePlot(); }
  void OnPaintCrossOverChart(wxPaintEvent& event);
  void OnOverlapPercentage(wxSpinEvent& event);
  void OnSizePlot(wxSizeEvent& event) { RefreshPlots(); }
  void OnVMGWindSpeed(wxSpinEvent& event) { UpdateVMG(); }
  void OnUpdatePlot();
  void OnUpdatePlot(wxCommandEvent& event) { OnUpdatePlot(); }
  void OnUpdatePlot(wxSpinEvent& event) { OnUpdatePlot(); }
  /**
   * Opens a boat configuration file.
   *
   * This method displays a file dialog allowing the user to select an XML boat
   * configuration file. Upon selection, it stores the selected directory as the
   * default path for future boat file operations, then attempts to open the
   * boat file.
   *
   * A boat configuration file contains a collection of polar files, typically
   * with different polars representing different sail configurations or
   * conditions. The file defines how the boat performs at different wind angles
   * and speeds across various sail settings.
   *
   * If the file is successfully loaded, the polar list is repopulated with all
   * the polars from the boat configuration and the plots are refreshed. If
   * loading fails, an error message is displayed.
   *
   * @param event The command event (unused)
   * @see m_Boat.OpenXML() For the actual file parsing functionality
   * @see RepopulatePolars() For updating the polar list display
   * @see UpdateVMG() For recalculating velocity made good values
   * @see RefreshPlots() For updating the polar plots
   */
  void OnOpenBoat(wxCommandEvent& event);
  /**
   * Saves the current boat configuration to the specified file.
   *
   * This is the core implementation that handles saving the boat configuration
   * with all its associated polar files. It waits for any crossover generation
   * thread to complete, then determines the file path (prompting for one if
   * none is set), and saves the boat configuration in XML format.
   *
   * The boat configuration contains the collection of all polar files that
   * define the boat's performance characteristics under different sail
   * configurations and wind conditions.
   *
   * On successful save, it updates any weather routing configurations using
   * this boat through the WeatherRouting class and closes the dialog. On
   * failure, it displays an error message.
   *
   * This method is called by both OnSaveBoat and OnSaveAsBoat.
   */
  void SaveBoat();

  /**
   * Saves the current boat configuration to a new file.
   *
   * Clears the current file path and calls SaveBoat(), which will prompt the
   * user for a new file name and location.
   *
   * @param event The command event (unused)
   * @see SaveBoat() For the actual file saving functionality
   */
  void OnSaveAsBoat(wxCommandEvent& event);
  /**
   * Saves the current boat configuration using SaveBoat().
   *
   * This is a direct call to SaveBoat(), which will use the existing file path
   * if available, or prompt for a new one if not.
   *
   * @param event The command event (unused)
   * @see SaveBoat() For the actual file saving functionality
   */
  void OnSaveBoat(wxCommandEvent& event) { SaveBoat(); }
  /**
   * Closes the boat dialog.
   *
   * Hides the boat dialog without saving any changes. This method is called
   * when the user clicks the close button or cancels the dialog.
   *
   * @param event The command event (unused)
   */
  void OnClose(wxCommandEvent& event);
  /**
   * Loads a boat configuration file from the specified path.
   *
   * This method attempts to open and load a boat configuration file, which
   * contains multiple polar files defining the boat's performance under
   * different sail configurations. It sets the boat path, updates the dialog
   * title to reflect the loaded file, and loads the XML configuration
   * containing all the polar data.
   *
   * On success, it updates the polar list display with all polars found in the
   * configuration.
   *
   * @param filename The path to the boat configuration file to load
   * @return True if the file was loaded successfully, false otherwise
   * @see m_Boat.OpenXML() For the actual file parsing
   * @see RepopulatePolars() For updating the polar list display
   */
  void LoadFile(bool switched = false);
  void OnPolarSelected(wxListEvent& event) { OnPolarSelected(); }
  void OnPolarSelected();

  void OnUpPolar(wxCommandEvent& event);
  void OnDownPolar(wxCommandEvent& event);
  void OnEditPolar(wxCommandEvent& event);
  void OnAddPolar(wxCommandEvent& event);
  void OnRemovePolar(wxCommandEvent& event);

  void GenerateCrossOverChart();
  void OnEvtThread(wxThreadEvent& event);

  void RepopulatePolars();

  void UpdateCursorInfo();
  void UpdateBestVMGInfo();

  void RefreshPlots() {
    m_PlotWindow->Refresh();
    m_CrossOverChart->Refresh();
  }

  wxString FormatVMG(double W, double VW);
  void UpdateVMG();
  void PlotVMG(wxPaintDC& dc, double lW, double W, double scale, int plottype);
  long SelectedPolar() {
    return m_lPolars->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  }

  WeatherRouting& m_WeatherRouting;

  double m_PlotScale;
  int m_MouseW;

  // Interactive hover state
  int m_HoveredWindSpeedIndex;
  bool m_ShowHoverInfo;

  // Cursor tracking for information display
  wxPoint m_CursorPosition;
  bool m_CursorValid;
  double m_CursorWindAngle;
  double m_CursorWindSpeed;
  double m_CursorBoatSpeed;
  double m_CursorVMG;
  double m_CursorVMGAngle;  // Optimal VMG angle at cursor position

  bool m_CrossOverRegenerate;
  CrossOverGenerationThread* m_CrossOverGenerationThread;
};

#endif
