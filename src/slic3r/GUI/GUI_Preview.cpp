#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/PreviewData.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "AppConfig.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "GLCanvas3DManager.hpp"
#include "GLCanvas3D.hpp"
#include "PresetBundle.hpp"
#include "DoubleSlider.hpp"
#include "BitmapCache.hpp"

#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/checkbox.h>
#include <wx/display.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {
namespace GUI {

View3D::View3D(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, camera, view_toolbar, model, config, process);
}

View3D::~View3D()
{
    if (m_canvas_widget != nullptr)
    {
        _3DScene::remove_canvas(m_canvas_widget);
        delete m_canvas_widget;
        m_canvas = nullptr;
    }
}

bool View3D::init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = GLCanvas3DManager::create_wxglcanvas(this);
    _3DScene::add_canvas(m_canvas_widget, bed, camera, view_toolbar);
    m_canvas = _3DScene::get_canvas(this->m_canvas_widget);

    m_canvas->allow_multisample(GLCanvas3DManager::can_multisample());
    // XXX: If have OpenGL
    m_canvas->enable_picking(true);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    m_canvas->enable_undoredo_toolbar(true);
    m_canvas->enable_labels(true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

int View3D::check_volumes_outside_state() const
{
    return (m_canvas != nullptr) ? m_canvas->check_volumes_outside_state() : false;
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        //m_canvas->render();
        m_canvas->set_as_dirty();
}

Preview::Preview(
    wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, 
    BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, std::function<void()> schedule_background_process_func)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
    , m_double_slider_sizer(nullptr)
    , m_label_view_type(nullptr)
    , m_choice_view_type(nullptr)
    , m_label_show_features(nullptr)
    , m_combochecklist_features(nullptr)
    , m_checkbox_travel(nullptr)
    , m_checkbox_retractions(nullptr)
    , m_checkbox_unretractions(nullptr)
    , m_checkbox_shells(nullptr)
    , m_checkbox_legend(nullptr)
    , m_config(config)
    , m_process(process)
    , m_gcode_preview_data(gcode_preview_data)
    , m_number_extruders(1)
    , m_preferred_color_mode("feature")
    , m_loaded(false)
    , m_enabled(false)
    , m_schedule_background_process(schedule_background_process_func)
#ifdef __linux__
    , m_volumes_cleanup_required(false)
#endif // __linux__
{
    if (init(parent, bed, camera, view_toolbar, model))
    {
        show_hide_ui_elements("none");
        load_print();
    }
}

enum ScreenWidth {
    large,
    medium,
    tiny
};

bool Preview::init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    //get display size to see if we have to compress the labels

    const auto idx = wxDisplay::GetFromWindow(parent);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);
    wxRect screen = display.GetClientArea();
    ScreenWidth width_screen = ScreenWidth::large;
    if (screen.width < 1900)
        width_screen = ScreenWidth::medium;
    if (screen.width < 1600)
        width_screen = ScreenWidth::tiny;

    m_canvas_widget = GLCanvas3DManager::create_wxglcanvas(this);
    _3DScene::add_canvas(m_canvas_widget, bed, camera, view_toolbar);
    m_canvas = _3DScene::get_canvas(this->m_canvas_widget);
    m_canvas->allow_multisample(GLCanvas3DManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->enable_legend_texture(true);
    m_canvas->enable_dynamic_background(true);

    m_double_slider_sizer = new wxBoxSizer(wxHORIZONTAL);
    create_double_slider();

    m_label_view_type = new wxStaticText(this, wxID_ANY, _(L("View")));

    m_choice_view_type = new wxChoice(this, wxID_ANY);
    m_choice_view_type->Append(_(L(width_screen == tiny ? "Feature": "Feature type")));
    m_choice_view_type->Append(_(L("Height")));
    m_choice_view_type->Append(_(L("Width")));
    m_choice_view_type->Append(_(L("Speed")));
    m_choice_view_type->Append(_(L(width_screen == tiny ? "Fan" : "Fan speed")));
    m_choice_view_type->Append(_(L(width_screen == tiny ? "Vol. flow" :"Volumetric flow rate")));
    m_choice_view_type->Append(_(L("Tool")));
    m_choice_view_type->Append(_(L("Filament")));
    m_choice_view_type->Append(_(L(width_screen == tiny ? "Color":"Color Print")));
    m_choice_view_type->SetSelection(0);

    m_label_show_features = new wxStaticText(this, wxID_ANY, _(L("Show")));
    m_combochecklist_features = new wxComboCtrl();
    m_combochecklist_features->Create(this, wxID_ANY, _(L("Extrusion type")), wxDefaultPosition, 
        wxSize((width_screen == large ? 35: (width_screen == medium ?20:15)) * wxGetApp().em_unit(), -1), wxCB_READONLY);
    std::string feature_text = GUI::into_u8(_(L("Feature types")));
    std::string feature_items = GUI::into_u8(
        _(L("Perimeter")) + "|" +
        _(L("External perimeter")) + "|" +
        _(L("Overhang perimeter")) + "|" +
        _(L("Internal infill")) + "|" +
        _(L("Solid infill")) + "|" +
        _(L("Top solid infill")) + "|" +
        _(L("Bridge infill")) + "|" +
        _(L("Gap fill")) + "|" +
        _(L("Skirt")) + "|" +
        _(L("Support material")) + "|" +
        _(L(width_screen == large? "Support material interface": "Sup. mat. interface")) + "|" +
        _(L("Wipe tower")) + "|" +
        _(L("Mill")) + "|" +
        _(L("Custom"))
    );
    Slic3r::GUI::create_combochecklist(m_combochecklist_features, feature_text, feature_items, true);

    m_checkbox_travel = new wxCheckBox(this, wxID_ANY, _(L("Travel")));
    m_checkbox_retractions = new wxCheckBox(this, wxID_ANY, _(L(width_screen == tiny ? "Retr." : "Retractions")));
    m_checkbox_unretractions = new wxCheckBox(this, wxID_ANY, _(L(width_screen == tiny ? "Unre." : "Unretractions")));
    m_checkbox_shells = new wxCheckBox(this, wxID_ANY, _(L("Shells")));
    m_checkbox_legend = new wxCheckBox(this, wxID_ANY, _(L("Legend")));
    m_checkbox_legend->SetValue(true);

    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);
    top_sizer->Add(m_double_slider_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_sizer->Add(m_label_view_type, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_choice_view_type, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_label_show_features, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_combochecklist_features, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(20);
    bottom_sizer->Add(m_checkbox_travel, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_retractions, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_unretractions, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_shells, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(20);
    bottom_sizer->Add(m_checkbox_legend, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(top_sizer, 1, wxALL | wxEXPAND, 0);
    main_sizer->Add(bottom_sizer, 0, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();
    
    // sets colors for gcode preview extrusion roles
    std::vector<std::string> extrusion_roles_colors = {
        "Perimeter", "FFFF66",
        "External perimeter", "FFA500",
        "Overhang perimeter", "0000FF",
        "Internal infill", "B1302A",
        "Solid infill", "D732D7",
        "Top solid infill", "FF1A1A",
        "Bridge infill", "9999FF",
        "Thin wall", "FFB000",
        "Gap fill", "FFFFFF",
        "Skirt", "845321",
        "Support material", "00FF00",
        "Support material interface", "008000",
        "Wipe tower", "B3E3AB",
        "Mill", "B3B3B3",
        "Custom", "28CC94"
    };
    m_gcode_preview_data->set_extrusion_paths_colors(extrusion_roles_colors);

    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas_widget != nullptr)
    {
		_3DScene::remove_canvas(m_canvas_widget);
        delete m_canvas_widget;
        m_canvas = nullptr;
    }
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void Preview::set_number_extruders(unsigned int number_extruders)
{
    if (m_number_extruders != number_extruders)
    {
        m_number_extruders = number_extruders;
        int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
        int type = (number_extruders > 1) ? tool_idx /* color by a tool number */  : 0; // color by a feature type
        m_choice_view_type->SetSelection(type);
        if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;

        m_preferred_color_mode = (type == tool_idx) ? "tool_or_feature" : "feature";
    }
}

void Preview::set_enabled(bool enabled)
{
    m_enabled = enabled;
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string& direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_print(bool keep_z_range)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range);
    else if (tech == ptSLA)
        load_print_as_sla();

    Layout();
}

void Preview::reload_print(bool keep_volumes)
{
#ifdef __linux__
    // We are getting mysterious crashes on Linux in gtk due to OpenGL context activation GH #1874 #1955.
    // So we are applying a workaround here: a delayed release of OpenGL vertex buffers.
    if (!IsShown())
    {
        m_volumes_cleanup_required = !keep_volumes;
        return;
    }
#endif /* __linux __ */
    if (
#ifdef __linux__
        m_volumes_cleanup_required || 
#endif /* __linux__ */
        !keep_volumes)
    {
        m_canvas->reset_volumes();
        m_canvas->reset_legend_texture();
        m_loaded = false;
#ifdef __linux__
        m_volumes_cleanup_required = false;
#endif /* __linux__ */
    }

    load_print();
}

void Preview::refresh_print()
{
    m_loaded = false;

    if (!IsShown())
        return;

    load_print(true);
}

void Preview::msw_rescale()
{
    // rescale slider
    if (m_slider) m_slider->msw_rescale();

    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    // rescale legend
    refresh_print();
}

void Preview::move_double_slider(wxKeyEvent& evt)
{
    if (m_slider) 
        m_slider->OnKeyDown(evt);
}

void Preview::edit_double_slider(wxKeyEvent& evt)
{
    if (m_slider) 
        m_slider->OnChar(evt);
}

void Preview::bind_event_handlers()
{
    this->Bind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Bind(wxEVT_CHOICE, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Bind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_checkbox_travel->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_travel, this);
    m_checkbox_retractions->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_retractions, this);
    m_checkbox_unretractions->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_unretractions, this);
    m_checkbox_shells->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_shells, this);
    m_checkbox_legend->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_legend, this);
}

void Preview::unbind_event_handlers()
{
    this->Unbind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Unbind(wxEVT_CHOICE, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Unbind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_checkbox_travel->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_travel, this);
    m_checkbox_retractions->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_retractions, this);
    m_checkbox_unretractions->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_unretractions, this);
    m_checkbox_shells->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_shells, this);
    m_checkbox_legend->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_legend, this);
}

void Preview::show_hide_ui_elements(const std::string& what)
{
    bool enable = (what == "full");
    m_label_show_features->Enable(enable);
    m_combochecklist_features->Enable(enable);
    m_checkbox_travel->Enable(enable); 
    m_checkbox_retractions->Enable(enable);
    m_checkbox_unretractions->Enable(enable);
    m_checkbox_shells->Enable(enable);
    m_checkbox_legend->Enable(enable);

    enable = (what != "none");
    m_label_view_type->Enable(enable);
    m_choice_view_type->Enable(enable);

    bool visible = (what != "none");
    m_label_show_features->Show(visible);
    m_combochecklist_features->Show(visible);
    m_checkbox_travel->Show(visible);
    m_checkbox_retractions->Show(visible);
    m_checkbox_unretractions->Show(visible);
    m_checkbox_shells->Show(visible);
    m_checkbox_legend->Show(visible);
    m_label_view_type->Show(visible);
    m_choice_view_type->Show(visible);
}

void Preview::reset_sliders(bool reset_all)
{
    m_enabled = false;
//    reset_double_slider();
    if (reset_all)
        m_double_slider_sizer->Hide((size_t)0);
    else
        m_double_slider_sizer->GetItem(size_t(0))->GetSizer()->Hide(1);
}

void Preview::update_sliders(const std::vector<double>& layers_z, bool keep_z_range)
{
    m_enabled = true;

    update_double_slider(layers_z, keep_z_range);
    m_double_slider_sizer->Show((size_t)0);

    Layout();
}

void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

void Preview::on_choice_view_type(wxCommandEvent& evt)
{
    m_preferred_color_mode = (m_choice_view_type->GetStringSelection() == L("Tool")) ? "tool" : "feature";
    int selection = m_choice_view_type->GetCurrentSelection();
    if ((0 <= selection) && (selection < (int)GCodePreviewData::Extrusion::Num_View_Types))
        m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)selection;

    reload_print();
}

void Preview::on_combochecklist_features(wxCommandEvent& evt)
{
    int flags = Slic3r::GUI::combochecklist_get_flags(m_combochecklist_features);
    m_gcode_preview_data->extrusion.role_flags = (unsigned int)flags;
    refresh_print();
}

void Preview::on_checkbox_travel(wxCommandEvent& evt)
{
    m_gcode_preview_data->travel.is_visible = m_checkbox_travel->IsChecked();
    m_gcode_preview_data->ranges.feedrate.set_mode(GCodePreviewData::FeedrateKind::TRAVEL, m_gcode_preview_data->travel.is_visible);
    // Rather than refresh, reload print so that speed color ranges get recomputed (affected by travel visibility)
    reload_print();
}

void Preview::on_checkbox_retractions(wxCommandEvent& evt)
{
    m_gcode_preview_data->retraction.is_visible = m_checkbox_retractions->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_unretractions(wxCommandEvent& evt)
{
    m_gcode_preview_data->unretraction.is_visible = m_checkbox_unretractions->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_shells(wxCommandEvent& evt)
{
    m_gcode_preview_data->shell.is_visible = m_checkbox_shells->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_legend(wxCommandEvent& evt)
{
    m_canvas->enable_legend_texture(m_checkbox_legend->IsChecked());
    m_canvas_widget->Refresh();
}

void Preview::update_view_type(bool slice_completed)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->project_config;

    const wxString& choice = !wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes.empty() /*&&
                             (wxGetApp().extruders_edited_cnt()==1 || !slice_completed) */? 
                                _(L("Color Print")) :
                                config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values.size() > 1 ?
                                    _(L("Tool")) : 
                                    _(L("Feature type"));

    int type = m_choice_view_type->FindString(choice);
    if (m_choice_view_type->GetSelection() != type) {
        m_choice_view_type->SetSelection(type);
        if (0 <= type && type < (int)GCodePreviewData::Extrusion::Num_View_Types)
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
        m_preferred_color_mode = "feature";
    }
}

void Preview::create_double_slider()
{
    m_slider = new DoubleSlider::Control(this, wxID_ANY, 0, 0, 0, 100);
    bool sla_print_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;
    bool sequential_print = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects");
    m_slider->SetDrawMode(sla_print_technology, sequential_print);

    m_double_slider_sizer->Add(m_slider, 0, wxEXPAND, 0);

    // sizer, m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_double_slider_from_canvas, this);
    m_canvas_widget->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SHIFT)
            m_slider->UseDefaultColors(true);
        event.Skip();
    });

    m_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_sliders_scroll_changed, this);


    Bind(DoubleSlider::wxCUSTOMEVT_TICKSCHANGED, [this](wxEvent&) {
        Model& model = wxGetApp().plater()->model();
        model.custom_gcode_per_print_z = m_slider->GetTicksValues();
        m_schedule_background_process();

        update_view_type(false);

        reload_print();
    });
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double>& zs, double &z, double eps)
{
    if (zs.empty())
        return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        -- it_l;
        if (z - *it_l < eps)
            return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps)
            return 0;
    } else {
        auto it_l = it_h;
        -- it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) {
            return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin());
        }
    }
    return -1;
}

void Preview::check_slider_values(std::vector<CustomGCode::Item>& ticks_from_model,
                                  const std::vector<double>& layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](CustomGCode::Item val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.print_z - DoubleSlider::epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

void Preview::update_double_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    // Save the initial slider span.
    double z_low        = m_slider->GetLowerValueD();
    double z_high       = m_slider->GetHigherValueD();
    bool   was_empty    = m_slider->GetMaxValue() == 0;
    bool force_sliders_full_range = was_empty;
    if (!keep_z_range)
    {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_slider->GetMaxValueD()) > DoubleSlider::epsilon()/*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool   snap_to_min = force_sliders_full_range || m_slider->is_lower_at_min();
	bool   snap_to_max  = force_sliders_full_range || m_slider->is_higher_at_max();

    // Detect and set manipulation mode for double slider
    update_double_slider_mode();

    CustomGCode::Info &ticks_info_from_model = wxGetApp().plater()->model().custom_gcode_per_print_z;
    check_slider_values(ticks_info_from_model.gcodes, layers_z);

    m_slider->SetSliderValues(layers_z);
    assert(m_slider->GetMinValue() == 0);
    m_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low  = 0;
    int idx_high = m_slider->GetMaxValue();
    if (! layers_z.empty()) {
        if (! snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (! snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }
    m_slider->SetSelectionSpan(idx_low, idx_high);

    m_slider->SetTicksValues(ticks_info_from_model);

    bool sla_print_technology = wxGetApp().plater()->printer_technology() == ptSLA;
    bool sequential_print = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects");
    m_slider->SetDrawMode(sla_print_technology, sequential_print);

    m_slider->SetExtruderColors(wxGetApp().plater()->get_extruder_colors_from_plater_config());
}

void Preview::update_double_slider_mode()
{
    //    true  -> single-extruder printer profile OR 
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool    one_extruder_printed_model = true;

    // extruder used for whole model for multi-extruder printer profile
    int     only_extruder = -1; 

    if (wxGetApp().extruders_edited_cnt() > 1)
    {
        const ModelObjectPtrs& objects = wxGetApp().plater()->model().objects;

        // check if whole model uses just only one extruder
        if (!objects.empty())
        {
            const int extruder = objects[0]->config.has("extruder") ?
                                 objects[0]->config.option("extruder")->getInt() : 0;

            auto is_one_extruder_printed_model = [objects, extruder]()
            {
                for (ModelObject* object : objects)
                {
                    if (object->config.has("extruder") &&
                        object->config.option("extruder")->getInt() != extruder)
                        return false;

                    if (object->volumes.size() > 1)
                        for (ModelVolume* volume : object->volumes)
                            if (volume->config.has("extruder") &&
                                volume->config.option("extruder")->getInt() != extruder)
                                return false;

                    for (const auto& range : object->layer_config_ranges)
                        if (range.second.has("extruder") &&
                            range.second.option("extruder")->getInt() != extruder)
                            return false;
                }
                return true;
            };

            if (is_one_extruder_printed_model())
                only_extruder = extruder;
            else
                one_extruder_printed_model = false;
        }
    }

    m_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
}

void Preview::reset_double_slider()
{
    m_slider->SetHigherValue(0);
    m_slider->SetLowerValue(0);
}

void Preview::update_double_slider_from_canvas(wxKeyEvent& event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    if (key == 'U' || key == 'D') {
        const int new_pos = key == 'U' ? m_slider->GetHigherValue() + 1 : m_slider->GetHigherValue() - 1;
        m_slider->SetHigherValue(new_pos);
		if (event.ShiftDown() || m_slider->is_one_layer()) m_slider->SetLowerValue(m_slider->GetHigherValue());
    }
    else if (key == 'L') {
        m_checkbox_legend->SetValue(!m_checkbox_legend->GetValue());
        auto evt = wxCommandEvent();
        on_checkbox_legend(evt);
    }
    else if (key == 'S')
        m_slider->ChangeOneLayerLock();
    else if (key == WXK_SHIFT)
        m_slider->UseDefaultColors(false);
    else
        event.Skip();
}

void Preview::load_print_as_fff(bool keep_z_range)
{
    if (m_loaded || m_process->current_printer_technology() != ptFFF)
        return;

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    bool has_layers = false;
    const Print *print = m_process->fff_print();
    if (print->is_step_done(posSlice)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->layers().empty()) {
                has_layers = true;
                break;
            }
    }
	if (print->is_step_done(posSupportMaterial)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->support_layers().empty()) {
                has_layers = true;
                break;
            }
    }

    if (! has_layers)
    {
        reset_sliders(true);
        m_canvas->reset_legend_texture();
        m_canvas_widget->Refresh();
        return;
    }

    if (m_preferred_color_mode == "tool_or_feature")
    {
        // It is left to Slic3r to decide whether the print shall be colored by the tool or by the feature.
        // Color by feature if it is a single extruder print.
        unsigned int number_extruders = (unsigned int)print->extruders().size();
        int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
        int type = (number_extruders > 1) ? tool_idx /* color by a tool number */ : 0; // color by a feature type
        m_choice_view_type->SetSelection(type);
        if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
        // If the->SetSelection changed the following line, revert it to "decide yourself".
        m_preferred_color_mode = "tool_or_feature";
    }

    bool gcode_preview_data_valid = print->is_step_done(psGCodeExport) && ! m_gcode_preview_data->empty();
    // Collect colors per extruder.
    std::vector<std::string> colors;
    std::vector<CustomGCode::Item> color_print_values = {};
    // set color print values, if it si selected "ColorPrint" view type
    if (m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::ColorPrint)
    {
        colors = wxGetApp().plater()->get_colors_for_color_print();
        colors.push_back("#808080"); // gray color for pause print or custom G-code 

        if (!gcode_preview_data_valid)
            color_print_values = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
    }
    else if ((m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::Filament))
    {
        const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"));
        const ConfigOptionStrings* filamemts_opt = dynamic_cast<const ConfigOptionStrings*>(m_config->option("filament_colour"));
        unsigned int colors_count = std::max((unsigned int)extruders_opt->values.size(), (unsigned int)filamemts_opt->values.size());

        unsigned char rgb[3];
        for (unsigned int i = 0; i < colors_count; ++i)
        {
            std::string color = m_config->opt_string("filament_colour", i);
            if (!BitmapCache::parse_color(color, rgb))
            {
                color = "#FFFFFF";
            }

            colors.emplace_back(color);
        }
        color_print_values.clear();
    }
    else if (gcode_preview_data_valid || (m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::Tool))
    {
        colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
        color_print_values.clear();
    }

    if (IsShown())
    {
        m_canvas->set_selected_extruder(0);
        if (gcode_preview_data_valid) {
            // Load the real G-code preview.
            m_canvas->load_gcode_preview(*m_gcode_preview_data, colors);
            m_loaded = true;
        } else {
            // Load the initial preview based on slices, not the final G-code.
            m_canvas->load_preview(colors, color_print_values);
        }
        show_hide_ui_elements(gcode_preview_data_valid ? "full" : "simple");
        // recalculates zs and update sliders accordingly
        std::vector<double> zs = m_canvas->get_current_print_zs(true);
        if (zs.empty()) {
            // all layers filtered out
            reset_sliders(true);
            m_canvas_widget->Refresh();
        } else
            update_sliders(zs, keep_z_range);
    }
}

void Preview::load_print_as_sla()
{
    if (m_loaded || (m_process->current_printer_technology() != ptSLA))
        return;

    unsigned int n_layers = 0;
    const SLAPrint* print = m_process->sla_print();

    std::vector<double> zs;
    double initial_layer_height = print->material_config().initial_layer_height.value;
    for (const SLAPrintObject* obj : print->objects())
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty())
        {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (auto& rec : obj->get_slice_index())
                zs.emplace_back(initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    sort_remove_duplicates(zs);

    m_canvas->reset_clipping_planes_cache();

    n_layers = (unsigned int)zs.size();
    if (n_layers == 0)
    {
        reset_sliders(true);
        m_canvas_widget->Refresh();
    }

    if (IsShown())
    {
        m_canvas->load_sla_preview();
        show_hide_ui_elements("none");

        if (n_layers > 0)
            update_sliders(zs);

        m_loaded = true;
    }
}

void Preview::on_sliders_scroll_changed(wxCommandEvent& event)
{
    if (IsShown())
    {
        PrinterTechnology tech = m_process->current_printer_technology();
        if (tech == ptFFF)
        {
            m_canvas->set_toolpaths_range(m_slider->GetLowerValueD() - 1e-6, m_slider->GetHigherValueD() + 1e-6);
            m_canvas->render();
            m_canvas->set_use_clipping_planes(false);
        }
        else if (tech == ptSLA)
        {
            m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -m_slider->GetLowerValueD()));
            m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), m_slider->GetHigherValueD()));
            m_canvas->set_use_clipping_planes(m_slider->GetHigherValue() != 0);
            m_canvas->render();
        }
    }
}


} // namespace GUI
} // namespace Slic3r
