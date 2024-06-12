// AdaptivePAProcessor.hpp
// OrcaSlicer
//
// Header file for the AdaptivePAProcessor class, responsible for processing G-code layers for the purposes of applying adaptive pressure advance.

#ifndef ADAPTIVEPAPROCESSOR_H
#define ADAPTIVEPAPROCESSOR_H

#include <string>
#include <sstream>
#include <regex>
#include <memory>
#include "AdaptivePAInterpolator.hpp"

namespace Slic3r {

// Forward declaration of GCode class
class GCode;

/**
 * @brief Class for processing G-code layers with adaptive pressure advance.
 */
class AdaptivePAProcessor {
public:
    /**
     * @brief Constructor for AdaptivePAProcessor.
     *
     * This constructor initializes the AdaptivePAProcessor with a reference to a GCode object.
     * It also initializes the configuration reference, pressure advance interpolation object,
     * and regular expression patterns used for processing the G-code.
     *
     * @param gcodegen A reference to the GCode object that generates the G-code.
     */
    AdaptivePAProcessor(GCode &gcodegen);
    
    /**
     * @brief Processes a layer of G-code and applies adaptive pressure advance.
     *
     * This method processes the G-code for a single layer, identifying the appropriate
     * pressure advance settings and applying them based on the current state and configurations.
     *
     * @param gcode A string containing the G-code for the layer.
     * @return A string containing the processed G-code with adaptive pressure advance applied.
     */
    std::string process_layer(std::string &&gcode);
    
private:
    GCode &m_gcodegen; ///< Reference to the GCode object.
    const PrintConfig &m_config; ///< Reference to the print configuration.
    double m_last_predicted_pa; ///< Last predicted pressure advance value.
    double m_last_feedrate; ///< Last known feed rate.
    int m_last_extruder_id; ///< Last used extruder ID.
    std::unique_ptr<AdaptivePAInterpolator> m_AdaptivePAInterpolator; ///< Interpolator for pressure advance values.

    std::regex m_pa_change_pattern; ///< Regular expression to detect PA_CHANGE pattern.
    std::regex m_g1_f_pattern; ///< Regular expression to detect G1 F pattern.
    std::smatch m_match; ///< Match results for regular expressions.

};

} // namespace Slic3r

#endif // ADAPTIVEPAPROCESSOR_H
