#ifndef POSITION_H
#define POSITION_H

#include "calibration.h"
#include "lora.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------ Configuration ------------ */

/**
 * @brief Height of the magnet plane above the sensor plane (in mm or arbitrary
 * units)
 *
 * The sensors are assumed to be in the z=0 plane, and the magnet moves in z=Z0
 * plane. Set this based on your physical setup. Units should match sensor
 * position units. Example: If sensor positions are in mm and magnet is 50mm
 * above sensors, Z0 = 50.0f
 */
#define MAGNET_PLANE_HEIGHT_Z0 20.0f

/**
 * @brief Gauss-Newton solver configuration
 */
#define GN_MAX_ITERATIONS 20
#define GN_CONVERGENCE_THRESHOLD 0.1f /* Position change threshold to stop */
#define GN_DAMPING_FACTOR 0.5f        /* Damping for stability */

/* ------------ Sensor and Magnet Configuration ------------ */

/**
 * @brief Physical sensor position in 3D space
 */
struct sensor_pos {
  float x;
  float y;
  float z;
};

/**
 * @brief Node position structure (legacy, kept for compatibility)
 */
struct node_pos {
  float x;
  float y;
};

/**
 * @brief Dipole orientation unit vector
 *
 * Represents the fixed orientation of the permanent magnet's dipole moment.
 * Common cases:
 * - Magnet pointing up (z+): m_hat = (0, 0, 1)
 * - Magnet pointing down (z-): m_hat = (0, 0, -1)
 */
struct dipole_orientation {
  float mx;
  float my;
  float mz;
};

/**
 * @brief Estimation result from the position solver
 */
struct position_estimate {
  float x;        /* Estimated X position (0-1000) */
  float y;        /* Estimated Y position (0-1000) */
  float M;        /* Estimated dipole moment scale factor */
  float error;    /* Final residual error (sum of squared differences) */
  int iterations; /* Number of iterations used */
  bool converged; /* Whether the solver converged */
  bool valid;     /* Whether this estimate contains valid data */
};

/* ------------ Public API ------------ */

/**
 * @brief Initialize the position estimation module
 *
 * Sets up sensor positions and dipole orientation.
 */
void position_init(void);

/**
 * @brief Set the sensor positions in 3D space
 *
 * @param positions Array of sensor positions (indexed 1 to MAX_NODES)
 */
void position_set_sensor_positions(const struct sensor_pos *positions);

/**
 * @brief Set the dipole orientation unit vector
 *
 * @param mx X component of unit vector
 * @param my Y component of unit vector
 * @param mz Z component of unit vector
 */
void position_set_dipole_orientation(float mx, float my, float mz);

/**
 * @brief Compute the magnitude of the B vector
 *
 * @param x_uT_milli X component in milli-microTesla
 * @param y_uT_milli Y component in milli-microTesla
 * @param z_uT_milli Z component in milli-microTesla
 * @return Magnitude |B| in milli-microTesla
 */
int32_t position_compute_absB(int32_t x_uT_milli, int32_t y_uT_milli,
                              int32_t z_uT_milli);

/**
 * @brief Estimate 2D position using dipole field model and Gauss-Newton solver
 *
 * This is the physics-based approach: Given the measured magnet-induced fields
 * at each sensor (B_measured - B_baseline), solve for the magnet position (x,
 * y) and dipole moment scale M using nonlinear least squares.
 *
 * @param nodes Array of node states with current 3D field measurements
 * @param initial_guess Optional initial guess (NULL for center of region)
 * @param result Output estimation result
 * @return true if position was estimated successfully, false otherwise
 */
bool position_estimate_dipole(const struct node_state *nodes,
                              const struct position_estimate *initial_guess,
                              struct position_estimate *result);

/**
 * @brief Estimate 2D position using calibration lookup table (fallback method)
 *
 * Uses inverse-distance weighted interpolation based on calibration points.
 * This is more robust when the dipole model assumptions don't hold well.
 *
 * @param nodes Array of node states (indexed by node ID)
 * @param calib_points Calibration points array
 * @param calib_count Number of calibration points
 * @param out_x Output X position (0-1000)
 * @param out_y Output Y position (0-1000)
 * @return true if position was estimated, false if not enough data
 */
bool position_estimate_lookup(const struct node_state *nodes,
                              const struct calib_point *calib_points,
                              int calib_count, float *out_x, float *out_y);

/**
 * @brief Simple triangulation using weighted averaging of sensor positions
 *
 * Uses magnetic field strength as a measure of proximity. Sensors measuring
 * stronger fields are assumed to be closer to the magnet and receive higher
 * weight in the position average.
 *
 * Algorithm:
 * 1. For each sensor, compute |B_magnet| (magnitude of magnet-induced field)
 * 2. Weight each sensor position by its field strength
 * 3. Compute weighted average to estimate magnet position
 *
 * @param nodes Array of node states with baseline-subtracted 3D field
 * measurements
 * @param out_x Output X position (0-1000)
 * @param out_y Output Y position (0-1000)
 * @return true if position was estimated, false if not enough data
 */
bool position_estimate_triangulation(const struct node_state *nodes,
                                     float *out_x, float *out_y);

/**
 * @brief Main position estimation function
 *
 * Uses simple triangulation with optional calibration refinement. Strategy:
 * 1. Compute position using weighted triangulation based on field strength
 * 2. If calibration points exist, blend with lookup table for refinement
 * 3. Falls back to lookup-only if triangulation fails
 *
 * @param nodes Array of node states (indexed by node ID)
 * @param calib_points Calibration points array (can be NULL)
 * @param calib_count Number of calibration points
 * @param out_x Output X position (0-1000)
 * @param out_y Output Y position (0-1000)
 * @return true if position was estimated, false if not enough data
 */
bool position_estimate_2D(const struct node_state *nodes,
                          const struct calib_point *calib_points,
                          int calib_count, float *out_x, float *out_y);

/**
 * @brief Get the physical position of a sensor node
 *
 * @param node_id Node ID (1 to MAX_NODES)
 * @return Pointer to node position, or NULL if invalid ID
 */
const struct node_pos *position_get_node_pos(int node_id);

/**
 * @brief Get the 3D position of a sensor
 *
 * @param node_id Node ID (1 to MAX_NODES)
 * @return Pointer to sensor position, or NULL if invalid ID
 */
const struct sensor_pos *position_get_sensor_pos(int node_id);

/* ------------ Dipole Field Model Functions ------------ */

/**
 * @brief Compute the expected magnetic field at a point from a dipole
 *
 * Uses the dipole field equation:
 * B = (M / r^3) * (3 * (m_hat · r_hat) * r_hat - m_hat)
 *
 * Where:
 * - r is the vector from magnet to sensor
 * - m_hat is the dipole orientation unit vector
 * - M is the dipole moment magnitude (incorporates μ0/4π)
 *
 * @param magnet_x Magnet X position
 * @param magnet_y Magnet Y position
 * @param M Dipole moment scale factor
 * @param sensor Sensor position
 * @param B_out Output: computed field at sensor location
 */
void position_compute_dipole_field(float magnet_x, float magnet_y, float M,
                                   const struct sensor_pos *sensor,
                                   struct vec3_f *B_out);

/**
 * @brief Compute the Jacobian of the dipole field w.r.t. parameters (x, y, M)
 *
 * Used by the Gauss-Newton solver. Computes partial derivatives of each
 * field component with respect to each parameter.
 *
 * @param magnet_x Magnet X position
 * @param magnet_y Magnet Y position
 * @param M Dipole moment scale factor
 * @param sensor Sensor position
 * @param J_out Output: 3x3 Jacobian matrix [dBx/dx, dBx/dy, dBx/dM; ...]
 */
void position_compute_jacobian(float magnet_x, float magnet_y, float M,
                               const struct sensor_pos *sensor,
                               float J_out[3][3]);

#endif /* POSITION_H */
