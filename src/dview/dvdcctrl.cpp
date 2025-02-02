/***********************************************************************************************************************
*  WEX, Copyright (c) 2008-2017, Alliance for Sustainable Energy, LLC. All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
*  following conditions are met:
*
*  (1) Redistributions of source code must retain the above copyright notice, this list of conditions and the following
*  disclaimer.
*
*  (2) Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
*  following disclaimer in the documentation and/or other materials provided with the distribution.
*
*  (3) Neither the name of the copyright holder nor the names of any contributors may be used to endorse or promote
*  products derived from this software without specific prior written permission from the respective party.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
*  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER, THE UNITED STATES GOVERNMENT, OR ANY CONTRIBUTORS BE LIABLE FOR
*  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
*  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
*  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
*  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************************************************************/

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <wx/app.h>
#include <wx/busyinfo.h>
#include <wx/config.h>
#include <wx/tokenzr.h>

#include "wx/srchctrl.h"

#include "wex/plot/pllineplot.h"
#include "wex/dview/dvdcctrl.h"
#include "wex/dview/dvselectionlist.h"
#include "wex/dview/dvtimeseriesdataset.h"

static const wxString NO_UNITS("ThereAreNoUnitsForThisAxis.");

enum {
    wxID_DC_DATA_SELECTOR = wxID_HIGHEST + 1
};

wxDVDCCtrl::wxDVDCCtrl(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                       const wxSize &size, long style, const wxString &name)
        : wxPanel(parent, id, pos, size, style, name) {
    wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(topSizer);
    m_srchCtrl = NULL;
    m_plotSurface = new wxPLPlotCtrl(this, wxID_ANY);
    m_plotSurface->SetBackgroundColour(*wxWHITE);
    m_plotSurface->ShowTitle(false);
    m_plotSurface->ShowLegend(false);
    topSizer->Add(m_plotSurface, 1, wxEXPAND | wxALL, 10);

    m_srchCtrl = new wxSearchCtrl(this, -1, wxEmptyString, wxDefaultPosition, wxSize(150, -1), 0);
    m_dataSelector = new wxDVSelectionListCtrl(this, wxID_DC_DATA_SELECTOR, 1);
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_srchCtrl, 0, wxALL | wxEXPAND, 0);
    sizer->Add(m_dataSelector, 0, wxALL | wxALIGN_CENTER, 0);

    topSizer->Add(sizer, 0, wxEXPAND, 0);
}

wxDVDCCtrl::~wxDVDCCtrl() {
    for (size_t i = 0; i < m_plots.size(); i++) {
        // remove it first in case it's shown to release ownership
        m_plotSurface->RemovePlot(m_plots[i]->plot);

        // destructor of PlotSet will delete the actual plot
        delete m_plots[i];
    }
}

void wxDVDCCtrl::ReadState(std::string filename) {
    wxConfig cfg("DView", "NREL");

    wxString s;
    bool success;
    bool debugging = false;

    std::string prefix = "/AppState/" + filename + "/DurationCurve/";

    std::string key("");

    key = prefix + "Selections";
    success = cfg.Read(key, &s);
    if (debugging) assert(success);

    wxStringTokenizer tokenizer(s, ",");
    while (tokenizer.HasMoreTokens()) {
        wxString str = tokenizer.GetNextToken();
        SelectDataSetAtIndex(wxAtoi(str));
    }

    if (GetDataSelectionList()->GetSelectedNamesInCol().size() == 0) {
        SelectDataSetAtIndex(0);
    }
}

void wxDVDCCtrl::WriteState(std::string filename) {
    wxConfig cfg("DView", "NREL");

    bool success;
    bool debugging = false;
    std::stringstream ss;

    std::string prefix = "/AppState/" + filename + "/DurationCurve/";

    std::string key("");

    auto selections = m_dataSelector->GetSelectionsInCol();
    for (auto selection : selections) {
        ss << selection;
        ss << ',';
    }
    key = prefix + "Selections";
    success = cfg.Write(key, ss.str().c_str());
    if (debugging) assert(success);
}

BEGIN_EVENT_TABLE(wxDVDCCtrl, wxPanel)
                EVT_DVSELECTIONLIST(wxID_DC_DATA_SELECTOR, wxDVDCCtrl::OnDataChannelSelection)
                EVT_TEXT(wxID_ANY, wxDVDCCtrl::OnSearch)
END_EVENT_TABLE()

// *** DATA SET FUNCTIONS ***
void wxDVDCCtrl::AddDataSet(wxDVTimeSeriesDataSet *d, bool update_ui) {
    m_dataSelector->Append(d->GetTitleWithUnits(), d->GetGroupName());
    m_plots.push_back(new PlotSet(d));

    if (update_ui)
        Layout();
}

void wxDVDCCtrl::RemoveDataSet(wxDVTimeSeriesDataSet *d) {
    int index = -1;
    for (size_t i = 0; i < m_plots.size(); i++)
        if (m_plots[i]->dataset == d)
            index = i;

    if (index < 0)
        return;

    if (std::find(m_currentlyShownIndices.begin(), m_currentlyShownIndices.end(), index)
        != m_currentlyShownIndices.end())
        HidePlotAtIndex(index);  // TODO

    m_dataSelector->RemoveAt(index);
    m_plotSurface->RemovePlot(m_plots[index]->plot);
    delete m_plots[index]; // deletes the associated plot
    m_plots.erase(m_plots.begin() + index);

    Layout();
    Refresh();
}

void wxDVDCCtrl::RemoveAllDataSets() {
    m_dataSelector->RemoveAll();

    for (size_t i = 0; i < m_plots.size(); i++) {
        // remove it first in case it's shown to release ownership
        m_plotSurface->RemovePlot(m_plots[i]->plot);
        delete m_plots[i];
    }
    m_plots.clear();

    m_plotSurface->SetYAxis1(NULL);
    m_plotSurface->SetYAxis2(NULL);
    Layout();
    Refresh();
}

//Member functions
void wxDVDCCtrl::CalculateDCPlotData(PlotSet *p) {
    //This method assumes uniform time step for simplicity.
    wxDVTimeSeriesDataSet *d = p->dataset;
    if (p->plot != 0)
        return;

    wxBeginBusyCursor();
    wxBusyInfo("Please wait, calculating duration curve for " + d->GetSeriesTitle() + "...");

    std::vector<double> sortedData;

    int len = d->Length();
    sortedData.resize(len, 0.0);
    for (int i = 0; i < len; i++)
        sortedData[i] = d->At(i).y;

    std::sort(sortedData.begin(), sortedData.end());

    std::vector<wxRealPoint> pd;
    pd.reserve(len);
    for (int i = 0; i < len; i++)
        pd.push_back(wxRealPoint(i * d->GetTimeStep(), sortedData[len - i - 1]));

    p->plot = new wxPLLinePlot(pd, d->GetSeriesTitle() + " (" + d->GetUnits() + ")");
    p->plot->SetXDataLabel(_("Hours equaled or exceeded"));
    p->plot->SetYDataLabel(p->plot->GetLabel());

    wxEndBusyCursor();
}

void wxDVDCCtrl::ShowPlotAtIndex(int index) {
    wxString YLabelText;
    size_t NumY1AxisSelections = 0;
    size_t NumY2AxisSelections = 0;
    if (index >= 0 && index < static_cast<int>(m_plots.size())) {
        CalculateDCPlotData(m_plots[index]);
        m_plots[index]->plot->SetColour(m_dataSelector->GetColourForIndex(index));

        wxPLPlotCtrl::AxisPos yap = wxPLPlotCtrl::Y_LEFT;
        wxString y1Units = NO_UNITS, y2Units = NO_UNITS;

        if (m_plotSurface->GetYAxis1())
            y1Units = m_plotSurface->GetYAxis1()->GetUnits();

        if (m_plotSurface->GetYAxis2())
            y2Units = m_plotSurface->GetYAxis2()->GetUnits();

        wxString units = m_plots[index]->dataset->GetUnits();

        if (m_plotSurface->GetYAxis1() && y1Units == units)
            yap = wxPLPlotCtrl::Y_LEFT;
        else if (m_plotSurface->GetYAxis2() && y2Units == units)
            yap = wxPLPlotCtrl::Y_RIGHT;
        else if (m_plotSurface->GetYAxis1() == 0)
            yap = wxPLPlotCtrl::Y_LEFT;
        else
            yap = wxPLPlotCtrl::Y_RIGHT;

        m_plotSurface->AddPlot(m_plots[index]->plot, wxPLPlotCtrl::X_BOTTOM, yap);
        m_plotSurface->GetAxis(yap)->SetUnits(units);

        YLabelText = units;
        for (int i = 0; i < m_dataSelector->Length(); i++) {
            if (m_dataSelector->IsSelected(i, 0) && m_plots[i]->dataset->GetUnits() == units) {
                if (yap == wxPLPlotCtrl::Y_LEFT) {
                    NumY1AxisSelections++;
                } else {
                    NumY2AxisSelections++;
                }
            }
        }
        if ((NumY1AxisSelections == 1 && yap == wxPLPlotCtrl::Y_LEFT) ||
            (NumY2AxisSelections == 1 && yap == wxPLPlotCtrl::Y_RIGHT)) {
            YLabelText = m_plots[index]->dataset->GetLabel();
        }
        m_plotSurface->GetAxis(yap)->SetLabel(YLabelText);
        m_plotSurface->GetXAxis1()->SetLabel("Hours Equaled or Exceeded");
        RefreshDisabledCheckBoxes();
        m_plotSurface->Invalidate();
        m_plotSurface->Refresh();
    }
}

void wxDVDCCtrl::HidePlotAtIndex(int index, bool update) {
    if (index < 0 || index >= static_cast<int>(m_plots.size()))
        return;

    wxString YLabelText = "";
    size_t NumY1AxisSelections = 0;
    size_t NumY2AxisSelections = 0;
    int FirstY1AxisSelectionIndex = -1;
    int FirstY2AxisSelectionIndex = -1;
    //wxPLPlotCtrl::AxisPos yap = wxPLPlotCtrl::Y_LEFT;
    wxString y1Units = NO_UNITS, y2Units = NO_UNITS;
    //int SelIndex = -1;
    m_plotSurface->RemovePlot(m_plots[index]->plot);

    if (m_plotSurface->GetYAxis1())
        y1Units = m_plotSurface->GetYAxis1()->GetUnits();

    if (m_plotSurface->GetYAxis2())
        y2Units = m_plotSurface->GetYAxis2()->GetUnits();
    std::vector<int> currently_shown = m_dataSelector->GetSelectionsInCol();

    for (size_t j = 0; j < currently_shown.size(); j++) {
        if (m_plots[currently_shown[j]]->dataset->GetUnits() == y1Units) {
            NumY1AxisSelections++;
            if (FirstY1AxisSelectionIndex == -1) { FirstY1AxisSelectionIndex = currently_shown[j]; }
        }
        if (m_plots[currently_shown[j]]->dataset->GetUnits() == y2Units) {
            NumY2AxisSelections++;
            if (FirstY2AxisSelectionIndex == -1) { FirstY2AxisSelectionIndex = currently_shown[j]; }
        }
    }

    if (NumY1AxisSelections > 0) {
        YLabelText = y1Units;
        if (NumY1AxisSelections == 1 &&
            FirstY1AxisSelectionIndex > -1) { YLabelText = m_plots[FirstY1AxisSelectionIndex]->dataset->GetLabel(); }
        if (m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)) {
            m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)->SetUnits(y1Units);
            m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)->SetLabel(YLabelText);
        }

        if (NumY2AxisSelections > 0) {
            YLabelText = y2Units;
            if (NumY2AxisSelections == 1 && FirstY2AxisSelectionIndex >
                                            -1) { YLabelText = m_plots[FirstY2AxisSelectionIndex]->dataset->GetLabel(); }
            if (m_plotSurface->GetAxis(wxPLPlotCtrl::Y_RIGHT)) {
                m_plotSurface->GetAxis(wxPLPlotCtrl::Y_RIGHT)->SetUnits(y2Units);
                m_plotSurface->GetAxis(wxPLPlotCtrl::Y_RIGHT)->SetLabel(YLabelText);
            }
        } else {
            m_plotSurface->SetYAxis2(NULL);
        }
    } else if (NumY2AxisSelections > 0)    //We deselected the last variable with Y1 units, so move Y2 to Y1
    {
        m_plotSurface->SetYAxis1(NULL); //Force rescaling.
        m_plotSurface->SetYAxis2(NULL);
        //Set the y axis to the left side (instead of the right)
        for (size_t j = 0; j < currently_shown.size(); j++) {
            m_plots[currently_shown[j]]->axisPosition = wxPLPlotCtrl::Y_LEFT;
            m_plotSurface->RemovePlot(m_plots[currently_shown[j]]->plot);
            m_plotSurface->AddPlot(m_plots[currently_shown[j]]->plot, wxPLPlotCtrl::X_BOTTOM, wxPLPlotCtrl::Y_LEFT);
        }

        YLabelText = y2Units;
        if (NumY2AxisSelections == 1 &&
            FirstY2AxisSelectionIndex > -1) { YLabelText = m_plots[FirstY2AxisSelectionIndex]->dataset->GetLabel(); }
        if (m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)) {
            m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)->SetUnits(y2Units);
            m_plotSurface->GetAxis(wxPLPlotCtrl::Y_LEFT)->SetLabel(YLabelText);
        }

        m_plotSurface->SetYAxis2(NULL);
    } else {
        m_plotSurface->SetYAxis1(NULL);
        m_plotSurface->SetYAxis2(NULL);
    }

    RefreshDisabledCheckBoxes();

    if (update) {
        m_plotSurface->Invalidate();
        m_plotSurface->Refresh();
    }
}

void wxDVDCCtrl::RefreshDisabledCheckBoxes() {
    std::vector<int> currently_shown = m_dataSelector->GetSelectionsInCol();
    if (currently_shown.size() < 2) {
        for (size_t i = 0; i < m_plots.size(); i++)
            m_dataSelector->Enable(i, 0, true);
        return;
    }

    wxString units1 = m_plots[currently_shown[0]]->dataset->GetUnits();
    wxString units2;
    bool units2Set = false;

    for (size_t i = 1; i < currently_shown.size(); i++) {
        if (m_plots[currently_shown[i]]->dataset->GetUnits() != units1) {
            units2 = m_plots[currently_shown[i]]->dataset->GetUnits();
            units2Set = true;
            break;
        }
    }

    if (!units2Set) {
        for (size_t i = 0; i < m_plots.size(); i++)
            m_dataSelector->Enable(i, 0, true);
        return;
    } else {
        for (size_t i = 0; i < m_plots.size(); i++) {
            m_dataSelector->Enable(i, 0, units1 == m_plots[i]->dataset->GetUnits()
                                         || units2 == m_plots[i]->dataset->GetUnits());
        }
    }
}

wxDVSelectionListCtrl *wxDVDCCtrl::GetDataSelectionList() {
    return m_dataSelector;
}

void wxDVDCCtrl::SetSelectedNames(const wxString &names, bool restrictToSmallDataSets) {
    //ClearAllChannelSelections();  is this necessary?

    wxStringTokenizer tkz(names, ";");

    while (tkz.HasMoreTokens()) {
        wxString token = tkz.GetNextToken();

        int index = m_dataSelector->SelectRowWithNameInCol(token, 0);
        if (index != -1) {
            if (!(restrictToSmallDataSets && m_plots[index]->dataset->Length() > 8760 * 2))
                ShowPlotAtIndex(index);
        }
    }
}

void wxDVDCCtrl::SelectDataSetAtIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_plots.size())) return;

    m_dataSelector->SelectRowInCol(index);
    ShowPlotAtIndex(index);
}

int wxDVDCCtrl::GetNumberOfSelections() {
    return m_dataSelector->GetNumberOfSelections();
}

// *** EVENT HANDLERS ***
void wxDVDCCtrl::OnDataChannelSelection(wxCommandEvent &) {
    int row;
    bool isChecked;
    m_dataSelector->GetLastEventInfo(&row, NULL, &isChecked);

    if (isChecked)
        ShowPlotAtIndex(row);
    else
        HidePlotAtIndex(row);

    m_plotSurface->Refresh();
}

void wxDVDCCtrl::OnSearch(wxCommandEvent &) {
    m_dataSelector->Filter(m_srchCtrl->GetValue().Lower());
}

wxDVDCCtrl::PlotSet::PlotSet(wxDVTimeSeriesDataSet *ds) {
    dataset = ds;
    plot = 0;
}

wxDVDCCtrl::PlotSet::~PlotSet() {
    if (plot != 0)
        delete plot;
}
