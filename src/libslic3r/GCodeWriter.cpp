#include "GCodeWriter.hpp"
#include "CustomGCode.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <assert.h>
#include <math.h> // need math for the sqrt function

#define FLAVOR_IS(val) this->config.gcode_flavor == val
#define FLAVOR_IS_NOT(val) this->config.gcode_flavor != val
#define COMMENT(comment) if (this->config.gcode_comments && !comment.empty()) gcode << " ; " << comment;
#define PRECISION(val, precision) std::fixed << std::setprecision(precision) << val
#define XYZF_NUM(val) PRECISION(val, 3)
#define E_NUM(val) PRECISION(val, 5)

/* 
OpenFL is the flavor used by Formlabs Form1 and Form1+ with OpenFL firmware.
For more information, see the OpenSourceMachining fork of OpenFL at https://openfl.dev.
The resulting files are FLP files. This flavor does not use g-code.
*/

namespace Slic3r {

    std::string GCodeWriter::PausePrintCode = "M601";

void GCodeWriter::apply_print_config(const PrintConfig &print_config)
{
    this->config.apply(print_config, true);
    m_extrusion_axis = this->config.get_extrusion_axis();
    m_single_extruder_multi_material = print_config.single_extruder_multi_material.value;
    m_max_acceleration = std::lrint((print_config.gcode_flavor.value == gcfMarlin || print_config.gcode_flavor.value == gcfLerdge || print_config.gcode_flavor.value == gcfKlipper) ?
        print_config.machine_max_acceleration_extruding.values.front() : 0);
}

void GCodeWriter::set_extruders(std::vector<uint16_t> extruder_ids)
{
    std::sort(extruder_ids.begin(), extruder_ids.end());
    m_extruders.clear();
    m_extruders.reserve(extruder_ids.size());
    for (uint16_t extruder_id : extruder_ids)
        m_extruders.emplace_back(Extruder(extruder_id, &this->config));

    /*  we enable support for multiple extruder if any extruder greater than 0 is used
        (even if prints only uses that one) since we need to output Tx commands
        first extruder has index 0 */
    this->multiple_extruders = this->multiple_extruders || (*std::max_element(extruder_ids.begin(), extruder_ids.end())) > 0;
}

void GCodeWriter::set_mills(std::vector<uint16_t> mill_ids)
{
    std::sort(mill_ids.begin(), mill_ids.end());
    m_millers.clear();
    m_millers.reserve(mill_ids.size());
    for (uint16_t mill_id : mill_ids) {
        m_millers.emplace_back(Mill(mill_id, &this->config));
    }

    /*  we enable support for multiple extruder */
    this->multiple_extruders = this->multiple_extruders || !mill_ids.empty();
}

std::string GCodeWriter::preamble()
{

    std::ostringstream gcode;
    if (FLAVOR_IS(gcfopenfl))
        return "";
        
    
    if (FLAVOR_IS_NOT(gcfMakerWare) || FLAVOR_IS_NOT(gcfopenfl))
        gcode << "G21 ; set units to millimeters\n";
        gcode << "G90 ; use absolute coordinates\n";

    if (FLAVOR_IS(gcfRepRap) || FLAVOR_IS(gcfMarlin) || FLAVOR_IS(gcfTeacup) || FLAVOR_IS(gcfRepetier) || FLAVOR_IS(gcfSmoothie)
		 || FLAVOR_IS(gcfKlipper) || FLAVOR_IS(gcfLerdge) || FLAVOR_IS_NOT(gcfopenfl)) {
        if (this->config.use_relative_e_distances || FLAVOR_IS_NOT(gcfopenfl)) {
            gcode << "M83 ; use relative distances for extrusion\n";
        } else {
            gcode << "M82 ; use absolute distances for extrusion\n";
        }
        gcode << this->reset_e(true);
    }
    
    return gcode.str();
}

std::string GCodeWriter::postamble() const
{
    std::ostringstream gcode;
    if (FLAVOR_IS(gcfMachinekit))
          gcode << "M2 ; end of program\n";
    return gcode.str();
}

std::string GCodeWriter::set_temperature(unsigned int temperature, bool wait, int tool) const
{
    //assign temperature to the laser_power variable (for OpenFL)
    laser_power = temperature;

    if (wait && (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish || FLAVOR_IS(gcfopenfl))))
        return "";    

    
    std::string code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup)) {
        code = "M109";
        comment = "set temperature and wait for it to be reached";
    } else {
        if (FLAVOR_IS_NOT(gcfopenfl))
        code = "M104";
        comment = "set temperature";
    }
    
    std::ostringstream gcode;
    gcode << code << " ";
    if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit) || FLAVOR_IS_NOT(gcfopenfl)) {
        gcode << "P";
    } else {
        gcode << "S";
    }

    gcode << temperature;
    if (tool != -1 && 
        ( (this->multiple_extruders && ! m_single_extruder_multi_material) ||
          FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) ) {
        gcode << " T" << tool;
    }
    gcode << " ; " << comment << "\n";
    
    if (FLAVOR_IS(gcfTeacup) && wait)
        gcode << "M116 ; wait for temperature to be reached\n";
    
    return gcode.str();
}

std::string GCodeWriter::set_bed_temperature(unsigned int temperature, bool wait)
{
    if (temperature == m_last_bed_temperature && (! wait || m_last_bed_temperature_reached) || FLAVOR_IS_NOT(gcfopenfl))
        return std::string();

    m_last_bed_temperature = temperature;
    m_last_bed_temperature_reached = wait;

    std::string code, comment;
    if (wait && FLAVOR_IS_NOT(gcfTeacup)) {
        if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish) || FLAVOR_IS_NOT(gcfopenfl)) {
            code = "M109";
        } else {
            if (FLAVOR_IS_NOT(gcfopenfl))
            code = "M190";
        }
        comment = "set bed temperature and wait for it to be reached";
    } else {
        if (FLAVOR_IS_NOT(gcfopenfl))
        code = "M140";
        comment = "set bed temperature";
    }
    
    std::ostringstream gcode;
    gcode << code << " ";
    if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit) || FLAVOR_IS_NOT(gcfopenfl)) {
        gcode << "P";
    } else {
        gcode << "S";
    }
    gcode << temperature << " ; " << comment << "\n";
    
    if (FLAVOR_IS(gcfTeacup) && wait)
        gcode << "M116 ; wait for bed temperature to be reached\n";
    
    return gcode.str();
}

std::string GCodeWriter::set_fan(unsigned int speed, bool dont_save)
{
    std::ostringstream gcode;
    if (m_last_fan_speed != speed || dont_save) {
        if (!dont_save) m_last_fan_speed = speed;
        
        if (speed == 0 || FLAVOR_IS_NOT(gcfopenfl)) {
            if (FLAVOR_IS(gcfTeacup)) {
                gcode << "M106 S0";
            } else if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
                gcode << "M127";
            } else {
                if (FLAVOR_IS_NOT(gcfopenfl))
                gcode << "M107";
            }
            if (this->config.gcode_comments || FLAVOR_IS_NOT(gcfopenfl)) gcode << " ; disable fan";
            gcode << "\n";
        } else {
            if (FLAVOR_IS(gcfMakerWare) || FLAVOR_IS(gcfSailfish)) {
                gcode << "M126";
            } else {
                gcode << "M106 ";
                if (FLAVOR_IS(gcfMach3) || FLAVOR_IS(gcfMachinekit)) {
                    gcode << "P";
                } else {
                    gcode << "S";
                }
                gcode << (255.0 * speed / 100.0);
            }
            if (this->config.gcode_comments) gcode << " ; enable fan";
            gcode << "\n";
        }
    }
    return gcode.str();
}

std::string GCodeWriter::set_acceleration(unsigned int acceleration)
{
    // Clamp the acceleration to the allowed maximum.
    if (m_max_acceleration > 0 && acceleration > m_max_acceleration)
        acceleration = m_max_acceleration;

    if (acceleration == 0 || acceleration == m_last_acceleration)
        return std::string();
    
    m_last_acceleration = acceleration;
    
    std::ostringstream gcode;
    if (FLAVOR_IS(gcfRepetier)) {
        // M201: Set max printing acceleration
        gcode << "M201 X" << acceleration << " Y" << acceleration;
        if (this->config.gcode_comments) gcode << " ; adjust acceleration";
        gcode << "\n";
        // M202: Set max travel acceleration
        gcode << "M202 X" << acceleration << " Y" << acceleration;
    } else if (FLAVOR_IS(gcfopenfl)) {
        return "";
    } else {
        // M204: Set default acceleration
        gcode << "M204 S" << acceleration;
    }
    if (this->config.gcode_comments) gcode << " ; adjust acceleration";
    gcode << "\n";
    
    return gcode.str();
}

std::string GCodeWriter::reset_e(bool force)
{
    if (FLAVOR_IS(gcfMach3)
        || FLAVOR_IS(gcfMakerWare)
        || FLAVOR_IS(gcfSailfish))
        return "";
    
    if (m_tool != nullptr) {
        if (m_tool->E() == 0. && ! force)
            return "";
        m_tool->reset_E();
    }

    if (! m_extrusion_axis.empty() && ! this->config.use_relative_e_distances) {
        std::ostringstream gcode;
        gcode << "G92 " << m_extrusion_axis << "0";
        if (this->config.gcode_comments) gcode << " ; reset extrusion distance";
        gcode << "\n";
        return gcode.str();
    } else {
        return "";
    }
}

std::string GCodeWriter::update_progress(unsigned int num, unsigned int tot, bool allow_100) const
{
    if (FLAVOR_IS_NOT(gcfMakerWare) && FLAVOR_IS_NOT(gcfSailfish))
        return "";
    
    unsigned int percent = (unsigned int)floor(100.0 * num / tot + 0.5);
    if (!allow_100) percent = std::min(percent, (unsigned int)99);
    
    std::ostringstream gcode;
    gcode << "M73 P" << percent;
    if (this->config.gcode_comments) gcode << " ; update progress";
    gcode << "\n";
    return gcode.str();
}

std::string GCodeWriter::toolchange_prefix() const
{
    return FLAVOR_IS(gcfMakerWare) ? "M135 T" :
           FLAVOR_IS(gcfSailfish) ? "M108 T" :
           FLAVOR_IS(gcfKlipper) ? "ACTIVATE_EXTRUDER EXTRUDER=extruder" :
           "T";
}

std::string GCodeWriter::toolchange(unsigned int tool_id)
{
    // set the new extruder
	/*auto it_extruder = Slic3r::lower_bound_by_predicate(m_extruders.begin(), m_extruders.end(), [tool_id](const Extruder &e) { return e.id() < tool_id; });
    assert(it_extruder != m_extruders.end() && it_extruder->id() == extruder_id);*/
    //less optimized but it's easier to modify and it's not needed, as it's not called often.
    bool found = false;
    for (Extruder& extruder : m_extruders) {
        if (tool_id == extruder.id()) {
            m_tool = &extruder;
            found = true;
            break;
        }
    }
    if (!found) {
        for (Tool& mill : m_millers) {
            if (tool_id == mill.id()) {
                m_tool = &mill;
                found = true;
                break;
            }
        }
    }

    // return the toolchange command
    // if we are running a single-extruder setup, just set the extruder and return nothing
    std::ostringstream gcode;
    if (this->multiple_extruders) {
        gcode << this->toolchange_prefix() << tool_id;
        if (this->config.gcode_comments)
            gcode << " ; change extruder";
        gcode << "\n";
        gcode << this->reset_e(true);
    }
    return gcode.str();
}

std::string GCodeWriter::set_speed(double F, const std::string &comment, const std::string &cooling_marker) const
{        
    std::ostringstream gcode;
    // Convert mm per min to ticks per second
    // Divide 60,000 (ticks per second) by the feed rate divided by 60
    // The XY Move functions will multiply the XY distance by this number
    // The result will be the number of ticks between two X/Y points

    if (F > 0){ // if F is zero, we will use the travel speed instead.
        m_last_speed = (m_laser_ticks / (F/60));
    } else {
        m_last_speed = (m_laser_ticks / ((this->config.travel_speed.value)/60));
    }

    if (FLAVOR_IS(gcfopenfl)){
        assert(F > 0.);
        assert(F < 100000.);
        gcode << "";
        return gcode.str();
    } else {
        assert(F > 0.);
        assert(F < 100000.);
        std::ostringstream gcode;
        gcode << "G1 F" << XYZF_NUM(F);
        COMMENT(comment);
        gcode << cooling_marker;
        gcode << "\n";
        return gcode.str();
    }
}

std::string GCodeWriter::travel_to_xy(const Vec2d &point, const std::string &comment)
{
    /* This section defines an XY travel move for OpenFL.
    Laser power is set to zero, but ticks are still needed. 
    */
    if (FLAVOR_IS(gcfopenfl)){
        double m_side_x;
        double m_side_y;
        double m_last_pos_x = m_pos.x();
        double m_last_pos_y = m_pos.y();
        m_pos.x() = point.x();
        m_pos.y() = point.y();            

        // Using the Pythagorean theorum to find the distance between the current position and the next position.

        if (m_last_pos_x > 0 && m_last_pos_y > 0){ // if the starting point is not the origin point, do this:
            m_side_x = (m_last_pos_x - m_pos.x()) * (m_last_pos_x - m_pos.x());
            m_side_y = (m_last_pos_y - m_pos.y()) * (m_last_pos_y - m_pos.y());
        } else { // otherwise, do this because it the starting point is the origin
            m_side_x = m_pos.x() * m_pos.x();
            m_side_y = m_pos.y() * m_pos.y();
        }

        double m_distance = sqrt(m_side_x + m_side_y);

        std::ostringstream gcode;
        gcode << "0x01 LaserPowerLevel 0\n";
        gcode << "0x00 XYMove 1\n";
        gcode << "  LaserPoint(";
        gcode << "x=" << round(point.x() * 524.28);
        gcode << ", y=" << round(point.y() * 524.28);
        gcode << "\n SPEED = "; gcode << m_last_speed; gcode << "\n";
        gcode << ", dt=" << round(m_last_speed * m_distance);
        gcode << ")\n";
        return gcode.str();

    // XY travel moves for all other flavors.
    } else {
        m_pos.x() = point.x();
        m_pos.y() = point.y();
    
        std::ostringstream gcode;
        gcode << "G1 X" << XYZF_NUM(point.x())
            <<   " Y" << XYZF_NUM(point.y())
            <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);
        COMMENT(comment);
        gcode << "\n";
        return gcode.str();
    }
}

std::string GCodeWriter::travel_to_xyz(const Vec3d &point, const std::string &comment)
{
    /*  If target Z is lower than current Z but higher than nominal Z we
        don't perform the Z move but we only move in the XY plane and
        adjust the nominal Z by reducing the lift amount that will be 
        used for unlift. */

    if (!this->will_move_z(point.z())) {
        double nominal_z = m_pos.z() - m_lifted;
        m_lifted -= (point.z() - nominal_z);
        // In case that retract_lift == layer_height we could end up with almost zero in_m_lifted
        // and a retract could be skipped (https://github.com/prusa3d/PrusaSlicer/issues/2154
        if (std::abs(m_lifted) < EPSILON)
            m_lifted = 0.;
        return this->travel_to_xy(to_2d(point));
    }
    
    /*  In all the other cases, we perform an actual XYZ move and cancel
        the lift. */

    /* This section defines a xyz travel moves for OpenFL. This is
     functionally similar to print to xyz, read that description.
     We do not want to use this if at all possible. The command
     will not move the Z axis, but it's best to not use this. */
    if (FLAVOR_IS(gcfopenfl)) {
        double m_side_x;
        double m_side_y;
        m_lifted = 0;
        double m_last_pos_x = m_pos.x();
        double m_last_pos_y = m_pos.y();
        m_pos.x() = point.x();
        m_pos.y() = point.y();

        // Using the Pythagorean theorum to find the distance between the current position and the next position.

        if (m_last_pos_x > 0 && m_last_pos_y > 0){ // if the starting point is not the origin point, do this:
            m_side_x = (m_last_pos_x - m_pos.x()) * (m_last_pos_x - m_pos.x());
            m_side_y = (m_last_pos_y - m_pos.y()) * (m_last_pos_y - m_pos.y());
        } else { // otherwise, do this because it the starting point is the origin
            m_side_x = m_pos.x() * m_pos.x();
            m_side_y = m_pos.y() * m_pos.y();
        }

        double m_distance = sqrt(m_side_x + m_side_y);
        
        std::ostringstream gcode;
        gcode << "0x01 LaserPowerLevel 0\n";
        gcode << "0x00 XYMove 1\n";
        gcode << "  LaserPoint(";
        gcode << "x=" << round(m_pos.x() * 524.28);
        gcode << ", y=" << round(m_pos.y() * 524.28);
        gcode << "\n SPEED = "; gcode << m_last_speed; gcode << "\n";
        gcode << ", dt=" << round(m_last_speed * m_distance);
        gcode << ")\n";
        return gcode.str();

        m_lifted = 0;
        m_pos = point;

    } else {
        std::ostringstream gcode;
        gcode << "G1 X" << XYZF_NUM(point.x())
              <<   " Y" << XYZF_NUM(point.y())
              <<   " Z" << XYZF_NUM(point.z())
              <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);
        COMMENT(comment);
        gcode << "\n";
        return gcode.str();
    }
}

std::string GCodeWriter::travel_to_z(double z, const std::string &comment)
{
    /*  If target Z is lower than current Z but higher than nominal Z
        we don't perform the move but we only adjust the nominal Z by
        reducing the lift amount that will be used for unlift. */


    if (!this->will_move_z(z)) {
        double nominal_z = m_pos.z() - m_lifted;
        m_lifted -= (z - nominal_z);
        if (std::abs(m_lifted) < EPSILON)
            m_lifted = 0.;
        return "";
    }
    
    /*  In all the other cases, we perform an actual Z move and cancel
        the lift. */
    m_lifted = 0;
    return this->_travel_to_z(z, comment);
}

std::string GCodeWriter::_travel_to_z(double z, const std::string &comment)
{

    /* This section sets Z travel FLP commands for the OpenFL Flavor (Formlabs Form1+)
    Formlabs uses FLP format, not g-code. OpenFL Needs Z moves to be relative, not absolute
    The variable m_z_move will hold the value for the next z move and is calculated based on 
    the last z position (m_last_z).
    */

    if(FLAVOR_IS(gcfopenfl)){ 
        std::ostringstream gcode;
        // declare variables
        const double microsteps_5mm = 2000.0;
        double m_z_move; // layer height variable
        double m_last_z = m_pos.z(); // hold the value of the last Z move
        m_pos.z() = z; // value of next Z move in mm
        

        if (m_last_z > 0.){ // If this is not the first layer do this:
            m_z_move = (m_pos.z() - m_last_z) * 400; // layer height = next z move minus last z move times 400 microsteps
            gcode << "0x04 ZFeedRate " << XYZF_NUM(this->config.travel_speed.value); // FLP feed rate command
            gcode << "\n";
            gcode << "0x03 ZMove 2000"; // 5mm peel lift (in microsteps)
            gcode << "\n";
            gcode << "0x03 ZMove ";
            gcode << int(m_z_move - microsteps_5mm); // unpeel and reset for next layer (in microsteps)
            printf("%11.6f ", m_last_z);
            printf("%11.6f ", m_pos.z());
            printf("%11.2f ", m_z_move);
            printf("%11.6f ", m_z_move);
            printf("%11.6f ", (m_z_move - microsteps_5mm));
            gcode << "\n";
            return gcode.str();
        } else { // otherwise do this, because this is the first layer:
            m_z_move = m_pos.z() * 400; 
            gcode << "0x04 ZFeedRate " << XYZF_NUM(this->config.travel_speed.value); // FLP feed rate command
            gcode << "\n";
            gcode << "0x03 ZMove ";
            gcode << int(m_z_move); // first layer height in microsteps
            printf("%11.6f ", m_last_z);
            printf("%11.6f ", m_pos.z());
            printf("%11.2f ", m_z_move);
            printf("%11.6f ", m_z_move);
            printf("%11.6f ", (m_z_move - microsteps_5mm));
            gcode << "\n";
            return gcode.str();
        }

    // This sets Z travel for all g-code flavors
    } else {
        m_pos.z() = z;
        
        std::ostringstream gcode;
        gcode << "G1 Z" << XYZF_NUM(z)
              <<   " F" << XYZF_NUM(this->config.travel_speed.value * 60.0);
        COMMENT(comment);
        gcode << "\n";
        return gcode.str();

    /* variable for storing the Z position at the end of the travel move,
    so we can subtract it from m_pos.z()  
    */

    }
}

bool GCodeWriter::will_move_z(double z) const
{
    /* If target Z is lower than current Z but higher than nominal Z
        we don't perform an actual Z move. */

    if (m_lifted > 0) {
        double nominal_z = m_pos.z() - m_lifted;
        if (z >= nominal_z && z <= m_pos.z())
            return false;
    }
    return true;
}


std::string GCodeWriter::extrude_to_xy(const Vec2d &point, double dE, const std::string &comment)
{
    
    // This section configures XY print moves for OpenFL/Formlabs Form1+.
    // the m_last_speed command converts the feed rate from mm per minute to 
    // ticks per second. The laser operates at 60,000 ticks per second.
    // Then ticks per second are multiplied by the extrude distance.
    // The result is a dt number, which is the amount of time the 
    // galvos will take to move the laser from the current point to 
    // the next point. 
    // The laser_power variable is linked to the extruder temperature 
    // and is defined in mW (maximum is 64mW).

    if (FLAVOR_IS(gcfopenfl)) {
        double m_side_x;
        double m_side_y;
        double m_last_pos_x = m_pos.x();
        double m_last_pos_y = m_pos.y();
        m_pos.x() = point.x();
        m_pos.y() = point.y();

        // Using the Pythagorean theorum to find the distance between the current position and the next position.

        if (m_last_pos_x > 0 && m_last_pos_y > 0){ // if the starting point is not the origin point, do this:
            m_side_x = (m_last_pos_x - m_pos.x()) * (m_last_pos_x - m_pos.x());
            m_side_y = (m_last_pos_y - m_pos.y()) * (m_last_pos_y - m_pos.y());
        } else { // otherwise, do this because the starting point is the origin
            m_side_x = m_pos.x() * m_pos.x();
            m_side_y = m_pos.y() * m_pos.y();
        }
        double m_distance = sqrt(m_side_x + m_side_y);

        std::ostringstream gcode;
        gcode << "0x01 LaserPowerLevel ";
        gcode << laser_power; 
        gcode << "\n";
        gcode << "0x00 XYMove 1\n";
        gcode << "  LaserPoint(";
        gcode << "x=" << round(m_pos.x() * 524.28);
        gcode << ", y=" << round(m_pos.y() * 524.28);
        gcode << "\n SPEED = "; gcode << m_last_speed; gcode << "\n";
        gcode << ", dt=" << round(m_last_speed * m_distance);
        gcode << ")\n";
        return gcode.str();
    // This is the XY extrusion settings for all other flavors
    } else {
        m_pos.x() = point.x();
        m_pos.y() = point.y();
        bool is_extrude = m_tool->extrude(dE) != 0;
        
        std::ostringstream gcode;
        gcode << "G1 X" << XYZF_NUM(point.x())
            << " Y" << XYZF_NUM(point.y());
        if(is_extrude)
            gcode <<    " " << m_extrusion_axis << E_NUM(m_tool->E());
        COMMENT(comment);
        gcode << "\n";
        return gcode.str();
    }
}

std::string GCodeWriter::extrude_to_xyz(const Vec3d &point, double dE, const std::string &comment)
{
    // For OpenFL, this section is functionally the same as the extrude_to_xy.
    // We never move the Z axis while the laser is on, so the power level is set to 0 here.
    // If possible, SuperSlicer should be configured to never extrude while moving the Z axis, 
    // So, the goal is for these settings to never be used.

        if (FLAVOR_IS(gcfopenfl)) {
            double m_side_x;
            double m_side_y;
            double m_last_pos_x = m_pos.x();
            double m_last_pos_y = m_pos.y();
            m_pos.x() = point.x();
            m_pos.y() = point.y();

            // Using the Pythagorean theorum to find the distance between the current position and the next position.

            if (m_last_pos_x > 0 && m_last_pos_y > 0){ // if the starting point is not the origin point, do this:
                m_side_x = (m_last_pos_x - m_pos.x()) * (m_last_pos_x - m_pos.x());
                m_side_y = (m_last_pos_y - m_pos.y()) * (m_last_pos_y - m_pos.y());
            } else { // otherwise, do this because it the starting point is the origin
                m_side_x = m_pos.x() * m_pos.x();
                m_side_y = m_pos.y() * m_pos.y();
            }

            double m_distance = sqrt(m_side_x + m_side_y);
            
            std::ostringstream gcode;
            gcode << "XYZ TEST - 0x01 LaserPowerLevel 0\n";
            gcode << "0x00 XYMove 1\n";
            gcode << "  LaserPoint(";
            gcode << "x=" << round(m_pos.x() * 524.28);
            gcode << ", y=" << round(m_pos.y() * 524.28);
            gcode << "\n SPEED = "; gcode << m_last_speed; gcode << "\n";
            gcode << ", dt=" << round(m_last_speed * m_distance);
            gcode << ")\n";
            return gcode.str();
        // These are the settings for all other flavors.
        } else {
            m_pos.x() = point.x();
            m_pos.y() = point.y();
            m_lifted = 0;
            bool is_extrude = m_tool->extrude(dE) != 0;
            
            std::ostringstream gcode;
            gcode << "G1 X" << XYZF_NUM(point.x())
                << " Y" << XYZF_NUM(point.y())
                << " Z" << XYZF_NUM(point.z() + m_pos.z());
            if (is_extrude)
                    gcode <<    " " << m_extrusion_axis << E_NUM(m_tool->E());
            COMMENT(comment);
            gcode << "\n";
            return gcode.str();
        }
}

std::string GCodeWriter::retract(bool before_wipe)
{
    double factor = before_wipe ? m_tool->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    return this->_retract(
        factor * m_tool->retract_length(),
        factor * m_tool->retract_restart_extra(),
        "retract"
    );
}

std::string GCodeWriter::retract_for_toolchange(bool before_wipe)
{
    double factor = before_wipe ? m_tool->retract_before_wipe() : 1.;
    assert(factor >= 0. && factor <= 1. + EPSILON);
    return this->_retract(
        factor * m_tool->retract_length_toolchange(),
        factor * m_tool->retract_restart_extra_toolchange(),
        "retract for toolchange"
    );
}

std::string GCodeWriter::_retract(double length, double restart_extra, const std::string &comment)
{
    std::ostringstream gcode;
    
    /*  If firmware retraction is enabled, we use a fake value of 1
        since we ignore the actual configured retract_length which 
        might be 0, in which case the retraction logic gets skipped. */
    if (this->config.use_firmware_retraction) length = 1;
    
    // If we use volumetric E values we turn lengths into volumes */
    if (this->config.use_volumetric_e) {
        double d = m_tool->filament_diameter();
        double area = d * d * PI/4;
        length = length * area;
        restart_extra = restart_extra * area;
    }
    
    double dE = m_tool->retract(length, restart_extra);
    if (dE != 0) {
        if (this->config.use_firmware_retraction) {
            if (FLAVOR_IS(gcfMachinekit))
                gcode << "G22 ; retract\n";
            else
                gcode << "G10 ; retract\n";
        } else {
            gcode << "G1 " << m_extrusion_axis << E_NUM(m_tool->E())
                           << " F" << float(m_tool->retract_speed() * 60.);
            COMMENT(comment);
            gcode << "\n";
        }
    }
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode << "M103 ; extruder off\n";
    
    return gcode.str();
}

std::string GCodeWriter::unretract()
{
    std::ostringstream gcode;
    
    if (FLAVOR_IS(gcfMakerWare))
        gcode << "M101 ; extruder on\n";
    
    double dE = m_tool->unretract();
    if (dE != 0) {
        if (this->config.use_firmware_retraction) {
            if (FLAVOR_IS(gcfMachinekit))
                 gcode << "G23 ; unretract\n";
            else
                 gcode << "G11 ; unretract\n";
            gcode << this->reset_e();
        } else {
            // use G1 instead of G0 because G0 will blend the restart with the previous travel move
            gcode << "G1 " << m_extrusion_axis << E_NUM(m_tool->E())
                           << " F" << float(m_tool->deretract_speed() * 60.);
            if (this->config.gcode_comments) gcode << " ; unretract";
            gcode << "\n";
        }
    }
    
    return gcode.str();
}

/*  If this method is called more than once before calling unlift(),
    it will not perform subsequent lifts, even if Z was raised manually
    (i.e. with travel_to_z()) and thus _lifted was reduced. */
std::string GCodeWriter::lift()
{
    // check whether the above/below conditions are met
    double target_lift = 0;
    if(this->tool_is_extruder()){
        //these two should be in the Tool class methods....
        double above = this->config.retract_lift_above.get_at(m_tool->id());
        double below = this->config.retract_lift_below.get_at(m_tool->id());
        if (m_pos.z() >= above && (below == 0 || m_pos.z() <= below))
            target_lift = m_tool->retract_lift();
    } else {
        target_lift = m_tool->retract_lift();
    }

    if (this->extra_lift > 0) {
        target_lift += this->extra_lift;
        this->extra_lift = 0;
    }

    // compare against epsilon because travel_to_z() does math on it
    // and subtracting layer_height from retract_lift might not give
    // exactly zero
    if (std::abs(m_lifted) < EPSILON && target_lift > 0) {
        m_lifted = target_lift;
        return this->_travel_to_z(m_pos.z() + target_lift, "lift Z");
    }
    return "";
}

std::string GCodeWriter::unlift()
{
    std::string gcode;
    if (m_lifted > 0) {
        gcode += this->_travel_to_z(m_pos.z() - m_lifted, "restore layer Z");
    }
    m_lifted = 0;
    return gcode;
}

}
