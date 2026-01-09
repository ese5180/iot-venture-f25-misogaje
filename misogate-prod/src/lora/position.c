/**
 * @file position.c
 * @brief Magnetic dipole-based position estimation using 3-axis magnetometers
 *
 * This module implements position estimation of a permanent magnet using the
 * magnetic dipole field model. Given measurements from 3 three-axis
 * magnetometers, it solves for the magnet position (x, y) in a known plane.
 *
 * The approach:
 * 1. Each sensor measures B_total = B_earth + B_offsets + B_magnet
 * 2. Baseline calibration captures B_earth + B_offsets (no magnet present)
 * 3. During operation, B_magnet = B_measured - B_baseline
 * 4. Solve for (x, y, M) using nonlinear least squares (Gauss-Newton)
 *
 * Dipole field model:
 *   r = sensor_pos - magnet_pos
 *   r_hat = r / |r|
 *   B = (M / |r|^3) * (3 * (m_hat · r_hat) * r_hat - m_hat)
 *
 * Where m_hat is the fixed dipole orientation unit vector.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <math.h>
#include <string.h>

#include "position.h"

LOG_MODULE_REGISTER(position, LOG_LEVEL_INF);

/* ------------ Static Configuration ------------ */

/**
 * Sensor positions in the coordinate frame.
 * Sensors are in the z=0 plane. Positions are in the 0-1000 coordinate system.
 *
 * IMPORTANT: Update these values to match your actual physical setup!
 */
static struct sensor_pos g_sensor_pos[MAX_NODES + 1] = {
    {500.0f, 1000.0f, 0.0f}, /* index 0 unused */
    {500.0f, 1000.0f, 0.0f}, /* sensor 1 - top-middle */
    {1000.0f, 0.0f, 0.0f},   /* sensor 2 - bottom-right corner */
    {0.0f, 0.0f, 0.0f},      /* sensor 3 - bottom-left corner */
};

/* Legacy 2D positions for backwards compatibility */
static struct node_pos g_node_pos[MAX_NODES + 1] = {
    {0.0f, 0.0f},
    {500.0f, 1000.0f},
    {1000.0f, 0.0f},
    {0.0f, 0.0f},
};

/**
 * Dipole orientation unit vector.
 * Assumes magnet is oriented with north pole pointing up (+Z).
 * Change this if your magnet has a different orientation.
 */
static struct dipole_orientation g_m_hat = {0.0f, 0.0f, 1.0f};

/**
 * Height of magnet plane above sensor plane.
 */
static float g_z0 = MAGNET_PLANE_HEIGHT_Z0;

/**
 * Last valid estimate (used as initial guess for next iteration)
 */
static struct position_estimate g_last_estimate = {
    .x = 500.0f,
    .y = 500.0f,
    .M = 1000.0f, /* Initial guess for dipole strength */
    .valid = false};

/* ------------ Vector Math Utilities ------------ */

static inline float vec3_dot(const struct vec3_f *a, const struct vec3_f *b) {
  return a->x * b->x + a->y * b->y + a->z * b->z;
}

static inline float vec3_norm(const struct vec3_f *v) {
  return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
}

static inline void vec3_scale(struct vec3_f *v, float s) {
  v->x *= s;
  v->y *= s;
  v->z *= s;
}

static inline void vec3_sub(struct vec3_f *out, const struct vec3_f *a,
                            const struct vec3_f *b) {
  out->x = a->x - b->x;
  out->y = a->y - b->y;
  out->z = a->z - b->z;
}

/* Convert integer vector to float */
static inline void vec3_i32_to_f(struct vec3_f *out,
                                 const struct vec3_i32 *in) {
  out->x = (float)in->x;
  out->y = (float)in->y;
  out->z = (float)in->z;
}

/* ------------ Dipole Field Model Implementation ------------ */

void position_compute_dipole_field(float magnet_x, float magnet_y, float M,
                                   const struct sensor_pos *sensor,
                                   struct vec3_f *B_out) {
  /* Vector from magnet to sensor: r = sensor_pos - magnet_pos */
  struct vec3_f r;
  r.x = sensor->x - magnet_x;
  r.y = sensor->y - magnet_y;
  r.z = sensor->z - g_z0; /* Sensor at z=0, magnet at z=z0 */

  float r_norm = vec3_norm(&r);

  /* Avoid division by zero */
  if (r_norm < 1.0f) {
    r_norm = 1.0f;
  }

  /* Unit vector r_hat */
  struct vec3_f r_hat = {r.x / r_norm, r.y / r_norm, r.z / r_norm};

  /* Dipole orientation as vec3_f */
  struct vec3_f m_hat = {g_m_hat.mx, g_m_hat.my, g_m_hat.mz};

  /* m_hat · r_hat */
  float m_dot_r = vec3_dot(&m_hat, &r_hat);

  /* B_unit = 3 * (m_hat · r_hat) * r_hat - m_hat */
  struct vec3_f B_unit;
  B_unit.x = 3.0f * m_dot_r * r_hat.x - m_hat.x;
  B_unit.y = 3.0f * m_dot_r * r_hat.y - m_hat.y;
  B_unit.z = 3.0f * m_dot_r * r_hat.z - m_hat.z;

  /* B = (M / r^3) * B_unit */
  float r_cubed = r_norm * r_norm * r_norm;
  /* Prevent infinite values if r_cubed is too small (already handled by r_norm
   * check, but safety first) */
  if (r_cubed < 1.0f) {
    r_cubed = 1.0f;
  }
  float scale = M / r_cubed;

  B_out->x = scale * B_unit.x;
  B_out->y = scale * B_unit.y;
  B_out->z = scale * B_unit.z;
}

/**
 * Compute numerical Jacobian using finite differences.
 * More robust than analytical derivatives for this application.
 */
void position_compute_jacobian(float magnet_x, float magnet_y, float M,
                               const struct sensor_pos *sensor,
                               float J_out[3][3]) {
  const float eps_pos = 1.0f; /* Position step for numerical derivative (1mm) */
  const float eps_M = 0.001f * fabsf(M); /* M step (relative) */

  struct vec3_f B_plus, B_minus;

  /* dB/dx */
  position_compute_dipole_field(magnet_x + eps_pos, magnet_y, M, sensor,
                                &B_plus);
  position_compute_dipole_field(magnet_x - eps_pos, magnet_y, M, sensor,
                                &B_minus);
  J_out[0][0] = (B_plus.x - B_minus.x) / (2.0f * eps_pos);
  J_out[1][0] = (B_plus.y - B_minus.y) / (2.0f * eps_pos);
  J_out[2][0] = (B_plus.z - B_minus.z) / (2.0f * eps_pos);

  /* dB/dy */
  position_compute_dipole_field(magnet_x, magnet_y + eps_pos, M, sensor,
                                &B_plus);
  position_compute_dipole_field(magnet_x, magnet_y - eps_pos, M, sensor,
                                &B_minus);
  J_out[0][1] = (B_plus.x - B_minus.x) / (2.0f * eps_pos);
  J_out[1][1] = (B_plus.y - B_minus.y) / (2.0f * eps_pos);
  J_out[2][1] = (B_plus.z - B_minus.z) / (2.0f * eps_pos);

  /* dB/dM */
  float eps_M_actual = fmaxf(eps_M, 1.0f);
  position_compute_dipole_field(magnet_x, magnet_y, M + eps_M_actual, sensor,
                                &B_plus);
  position_compute_dipole_field(magnet_x, magnet_y, M - eps_M_actual, sensor,
                                &B_minus);
  J_out[0][2] = (B_plus.x - B_minus.x) / (2.0f * eps_M_actual);
  J_out[1][2] = (B_plus.y - B_minus.y) / (2.0f * eps_M_actual);
  J_out[2][2] = (B_plus.z - B_minus.z) / (2.0f * eps_M_actual);
}

/* ------------ Gauss-Newton Solver ------------ */

/**
 * Solve a 3x3 linear system Ax = b using Cramer's rule.
 * Simple and adequate for our small system.
 */
static bool solve_3x3(float A[3][3], float b[3], float x[3]) {
  /* Compute determinant of A */
  float det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) -
              A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
              A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);

  if (fabsf(det) < 1e-10f) {
    return false; /* Singular matrix */
  }

  float inv_det = 1.0f / det;

  /* Compute inverse using cofactors */
  float Ainv[3][3];
  Ainv[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) * inv_det;
  Ainv[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * inv_det;
  Ainv[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * inv_det;
  Ainv[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) * inv_det;
  Ainv[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * inv_det;
  Ainv[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * inv_det;
  Ainv[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) * inv_det;
  Ainv[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * inv_det;
  Ainv[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * inv_det;

  /* x = Ainv * b */
  x[0] = Ainv[0][0] * b[0] + Ainv[0][1] * b[1] + Ainv[0][2] * b[2];
  x[1] = Ainv[1][0] * b[0] + Ainv[1][1] * b[1] + Ainv[1][2] * b[2];
  x[2] = Ainv[2][0] * b[0] + Ainv[2][1] * b[1] + Ainv[2][2] * b[2];

  return true;
}

/**
 * Gauss-Newton solver for dipole position estimation.
 *
 * Minimizes: sum over sensors of ||B_measured - B_model(x, y, M)||^2
 *
 * The Gauss-Newton update is: theta_new = theta - (J^T J)^{-1} J^T r
 * Where J is the Jacobian and r is the residual vector.
 */
bool position_estimate_dipole(const struct node_state *nodes,
                              const struct position_estimate *initial_guess,
                              struct position_estimate *result) {
  /* Count valid sensors */
  int valid_sensors = 0;
  for (int i = 1; i <= MAX_NODES; i++) {
    if (nodes[i].have_baseline) {
      valid_sensors++;
    }
  }

  if (valid_sensors < 2) {
    LOG_WRN("Not enough valid sensors for dipole estimation: %d",
            valid_sensors);
    return false;
  }

  /* Initialize estimate */
  float theta[3]; /* [x, y, M] */
  if (initial_guess && initial_guess->converged) {
    theta[0] = initial_guess->x;
    theta[1] = initial_guess->y;
    theta[2] = initial_guess->M;
  } else if (g_last_estimate.converged) {
    theta[0] = g_last_estimate.x;
    theta[1] = g_last_estimate.y;
    theta[2] = g_last_estimate.M;
  } else {
    /* Smart initialization: Start at the sensor with the strongest signal */
    float max_B2 = -1.0f;
    int max_node = 0;

    for (int i = 1; i <= MAX_NODES; i++) {
      if (nodes[i].have_baseline) {
        float B2 = (float)nodes[i].last_B_mag.x * nodes[i].last_B_mag.x +
                   (float)nodes[i].last_B_mag.y * nodes[i].last_B_mag.y +
                   (float)nodes[i].last_B_mag.z * nodes[i].last_B_mag.z;
        if (B2 > max_B2) {
          max_B2 = B2;
          max_node = i;
        }
      }
    }

    if (max_node > 0) {
      theta[0] = g_sensor_pos[max_node].x;
      theta[1] = g_sensor_pos[max_node].y;
      /* Offset slightly to avoid singularity if r=0 */
      theta[0] += 0.1f;
      theta[1] += 0.1f;
    } else {
      /* Default: center of region */
      theta[0] = 500.0f;
      theta[1] = 500.0f;
    }

    theta[2] = 1.0e10f; /* Improved initial M guess */
  }

  float last_error = 1e10f;
  int iter;

  for (iter = 0; iter < GN_MAX_ITERATIONS; iter++) {
    /*
     * Build the normal equations: (J^T J) delta = J^T r
     * Where J is the full Jacobian (3*N x 3) and r is residuals (3*N x 1)
     *
     * We accumulate J^T J and J^T r directly.
     */
    float JtJ[3][3] = {{0}};
    float Jtr[3] = {0};
    float total_error = 0.0f;

    for (int nid = 1; nid <= MAX_NODES; nid++) {
      if (!nodes[nid].have_baseline) {
        continue;
      }

      const struct sensor_pos *sensor = &g_sensor_pos[nid];

      /* Get measured magnet-induced field (already baseline-subtracted) */
      struct vec3_f B_measured;
      vec3_i32_to_f(&B_measured, &nodes[nid].last_B_mag);

      /* Compute model prediction */
      struct vec3_f B_model;
      position_compute_dipole_field(theta[0], theta[1], theta[2], sensor,
                                    &B_model);

      /* Residual: r = B_measured - B_model */
      struct vec3_f r;
      vec3_sub(&r, &B_measured, &B_model);

      /* Accumulate error */
      total_error += r.x * r.x + r.y * r.y + r.z * r.z;

      /* Compute Jacobian for this sensor */
      float J[3][3]; /* J[component][parameter] = dB_component/d_parameter */
      position_compute_jacobian(theta[0], theta[1], theta[2], sensor, J);

      /* Accumulate J^T J */
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          JtJ[i][j] +=
              J[0][i] * J[0][j] + J[1][i] * J[1][j] + J[2][i] * J[2][j];
        }
      }

      /* Accumulate J^T r */
      Jtr[0] += J[0][0] * r.x + J[1][0] * r.y + J[2][0] * r.z;
      Jtr[1] += J[0][1] * r.x + J[1][1] * r.y + J[2][1] * r.z;
      Jtr[2] += J[0][2] * r.x + J[1][2] * r.y + J[2][2] * r.z;
    }

    /* Add Levenberg-Marquardt damping for stability */
    float damping = GN_DAMPING_FACTOR * total_error;
    JtJ[0][0] += damping;
    JtJ[1][1] += damping;
    JtJ[2][2] += damping;

    /* Solve (J^T J) delta = J^T r */
    float delta[3];
    if (!solve_3x3(JtJ, Jtr, delta)) {
      LOG_WRN("Gauss-Newton: singular matrix at iteration %d", iter);
      break;
    }

    /* Update parameters */
    theta[0] += delta[0];
    theta[1] += delta[1];
    theta[2] += delta[2];

    /* Clamp position to valid range */
    if (theta[0] < -100.0f)
      theta[0] = -100.0f;
    if (theta[0] > 1100.0f)
      theta[0] = 1100.0f;
    if (theta[1] < -100.0f)
      theta[1] = -100.0f;
    if (theta[1] > 1100.0f)
      theta[1] = 1100.0f;

    /* Ensure M is positive */
    if (theta[2] < 100.0f)
      theta[2] = 100.0f;

    /* Check for convergence */
    float pos_change = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
    if (pos_change < GN_CONVERGENCE_THRESHOLD && iter > 2) {
      LOG_DBG("GN converged at iteration %d, pos_change=%.3f", iter,
              (double)pos_change);
      break;
    }

    /* Check if error is increasing (diverging) */
    if (total_error > last_error * 1.5f && iter > 3) {
      LOG_WRN("GN diverging at iteration %d", iter);
      break;
    }

    last_error = total_error;
  }

  /* Store result */
  result->x = theta[0];
  result->y = theta[1];
  result->M = theta[2];
  result->error = last_error;
  result->iterations = iter;
  result->converged = (iter < GN_MAX_ITERATIONS);

  /* Update last estimate for next iteration */
  if (result->converged) {
    g_last_estimate = *result;
  }

  LOG_INF("GN result: x=%.1f y=%.1f M=%.1f err=%.1f iter=%d", (double)result->x,
          (double)result->y, (double)result->M, (double)result->error,
          result->iterations);

  return true;
}

/* ------------ Lookup Table Method (Fallback) ------------ */

/**
 * Inverse-distance weighted interpolation using calibration points.
 * Compares current 3D field vectors to calibrated vectors.
 */
bool position_estimate_lookup(const struct node_state *nodes,
                              const struct calib_point *calib_points,
                              int calib_count, float *out_x, float *out_y) {
  if (calib_count < 2 || calib_points == NULL) {
    return false;
  }

  float sum_w = 0.0f;
  float wx = 0.0f;
  float wy = 0.0f;

  for (int i = 0; i < calib_count; i++) {
    const struct calib_point *cp = &calib_points[i];

    /* Compute distance in 3D field space between current and calibration */
    float dist_sq = 0.0f;
    int valid_nodes = 0;

    for (int nid = 1; nid <= MAX_NODES; nid++) {
      if (!cp->node_valid[nid] || !nodes[nid].have_baseline) {
        continue;
      }

      /* Use 3D field difference */
      float dx = (float)(nodes[nid].last_B_mag.x - cp->node_B_mag[nid].x);
      float dy = (float)(nodes[nid].last_B_mag.y - cp->node_B_mag[nid].y);
      float dz = (float)(nodes[nid].last_B_mag.z - cp->node_B_mag[nid].z);

      dist_sq += dx * dx + dy * dy + dz * dz;
      valid_nodes++;
    }

    if (valid_nodes < 2) {
      continue;
    }

    /* Normalize by number of valid nodes */
    dist_sq /= (float)valid_nodes;

    /* Avoid division by zero */
    if (dist_sq < 1.0f) {
      dist_sq = 1.0f;
    }

    /* Weight inversely proportional to distance squared */
    float w = 1.0f / dist_sq;

    sum_w += w;
    wx += w * (float)cp->x;
    wy += w * (float)cp->y;
  }

  if (sum_w <= 0.0f) {
    return false;
  }

  *out_x = wx / sum_w;
  *out_y = wy / sum_w;
  return true;
}

/* ------------ Simple Triangulation Using Linear Averaging ------------ */

/**
 * @brief Simple triangulation using inverse-distance weighted averaging
 *
 * This method assumes that magnetic field strength decreases with distance.
 * It weights each sensor's position by a function of the measured field
 * strength, then computes a weighted average to estimate the magnet position.
 *
 * Algorithm:
 * 1. For each sensor, compute |B_magnet| (magnitude of magnet-induced field)
 * 2. Use field strength as a proxy for proximity (stronger = closer)
 * 3. Weight each sensor's position by its field strength
 * 4. Compute weighted average position
 */
bool position_estimate_triangulation(const struct node_state *nodes,
                                     float *out_x, float *out_y) {
  float sum_weights = 0.0f;
  float weighted_x = 0.0f;
  float weighted_y = 0.0f;
  int valid_sensors = 0;

  for (int i = 1; i <= MAX_NODES; i++) {
    if (!nodes[i].have_baseline) {
      continue;
    }

    /* Compute magnitude of magnet-induced field */
    float Bx = (float)nodes[i].last_B_mag.x;
    float By = (float)nodes[i].last_B_mag.y;
    float Bz = (float)nodes[i].last_B_mag.z;
    float B_magnitude = sqrtf(Bx * Bx + By * By + Bz * Bz);

    /* Skip sensors with very weak signal (likely noise) */
    if (B_magnitude < 100.0f) /* Threshold: 100 milli-uT */
    {
      continue;
    }

    /*
     * Weight by field strength.
     * Stronger field = closer to magnet = higher weight.
     *
     * We use B_magnitude directly as weight, which assumes the magnet
     * is closer to sensors with stronger fields.
     *
     * Alternative weighting schemes:
     * - B_magnitude^2 (more aggressive, emphasizes closest sensor)
     * - B_magnitude^0.5 (less aggressive, more balanced)
     */
    float weight = B_magnitude;

    /* Get sensor position */
    const struct sensor_pos *sensor = &g_sensor_pos[i];

    /* Accumulate weighted position */
    weighted_x += weight * sensor->x;
    weighted_y += weight * sensor->y;
    sum_weights += weight;
    valid_sensors++;

    LOG_DBG("Sensor %d: B=%.1f weight=%.1f pos=(%.1f, %.1f)", i,
            (double)B_magnitude, (double)weight, (double)sensor->x,
            (double)sensor->y);
  }

  /* Need at least 2 sensors for reasonable estimate */
  if (valid_sensors < 2 || sum_weights <= 0.0f) {
    LOG_WRN("Not enough valid sensors for triangulation: %d", valid_sensors);
    return false;
  }

  /* Compute weighted average */
  *out_x = weighted_x / sum_weights;
  *out_y = weighted_y / sum_weights;

  /* Clamp to valid range */
  if (*out_x < 0.0f)
    *out_x = 0.0f;
  if (*out_x > 1000.0f)
    *out_x = 1000.0f;
  if (*out_y < 0.0f)
    *out_y = 0.0f;
  if (*out_y > 1000.0f)
    *out_y = 1000.0f;

  LOG_INF("Triangulation result: x=%.1f y=%.1f (from %d sensors)",
          (double)*out_x, (double)*out_y, valid_sensors);

  return true;
}

/* ------------ Main Position Estimation ------------ */

bool position_estimate_2D(const struct node_state *nodes,
                          const struct calib_point *calib_points,
                          int calib_count, float *out_x, float *out_y) {
  /* Use simple triangulation method */
  if (position_estimate_triangulation(nodes, out_x, out_y)) {
    /* If we have calibration data, blend with lookup for refinement */
    if (calib_count >= 2 && calib_points != NULL) {
      float lookup_x, lookup_y;
      if (position_estimate_lookup(nodes, calib_points, calib_count, &lookup_x,
                                   &lookup_y)) {
        /* Blend triangulation with lookup table (70% triangulation, 30% lookup)
         */
        *out_x = 0.7f * (*out_x) + 0.3f * lookup_x;
        *out_y = 0.7f * (*out_y) + 0.3f * lookup_y;

        LOG_DBG("Blended with lookup: final=(%.1f, %.1f)", (double)*out_x,
                (double)*out_y);
      }
    }

    return true;
  }

  /* Fallback to lookup-only if triangulation fails */
  if (calib_count >= 2 && calib_points != NULL) {
    return position_estimate_lookup(nodes, calib_points, calib_count, out_x,
                                    out_y);
  }

  return false;
}

/* ------------ Public API Implementation ------------ */

void position_init(void) {
  /* Initialize with default sensor positions */
  LOG_INF("Position module initialized");
  LOG_INF("Sensor 1: (%.0f, %.0f, %.0f)", (double)g_sensor_pos[1].x,
          (double)g_sensor_pos[1].y, (double)g_sensor_pos[1].z);
  LOG_INF("Sensor 2: (%.0f, %.0f, %.0f)", (double)g_sensor_pos[2].x,
          (double)g_sensor_pos[2].y, (double)g_sensor_pos[2].z);
  LOG_INF("Sensor 3: (%.0f, %.0f, %.0f)", (double)g_sensor_pos[3].x,
          (double)g_sensor_pos[3].y, (double)g_sensor_pos[3].z);
  LOG_INF("Magnet plane height z0=%.1f", (double)g_z0);
  LOG_INF("Dipole orientation m_hat=(%.2f, %.2f, %.2f)", (double)g_m_hat.mx,
          (double)g_m_hat.my, (double)g_m_hat.mz);
}

void position_set_sensor_positions(const struct sensor_pos *positions) {
  for (int i = 1; i <= MAX_NODES; i++) {
    g_sensor_pos[i] = positions[i];
    g_node_pos[i].x = positions[i].x;
    g_node_pos[i].y = positions[i].y;
  }
}

void position_set_dipole_orientation(float mx, float my, float mz) {
  /* Normalize to unit vector */
  float norm = sqrtf(mx * mx + my * my + mz * mz);
  if (norm > 0.0f) {
    g_m_hat.mx = mx / norm;
    g_m_hat.my = my / norm;
    g_m_hat.mz = mz / norm;
  }
}

int32_t position_compute_absB(int32_t x_uT_milli, int32_t y_uT_milli,
                              int32_t z_uT_milli) {
  int64_t x = x_uT_milli;
  int64_t y = y_uT_milli;
  int64_t z = z_uT_milli;

  long double xx = (long double)x * (long double)x;
  long double yy = (long double)y * (long double)y;
  long double zz = (long double)z * (long double)z;

  long double mag = sqrtl(xx + yy + zz);
  return (int32_t)mag;
}

const struct node_pos *position_get_node_pos(int node_id) {
  if (node_id < 1 || node_id > MAX_NODES) {
    return NULL;
  }
  return &g_node_pos[node_id];
}

const struct sensor_pos *position_get_sensor_pos(int node_id) {
  if (node_id < 1 || node_id > MAX_NODES) {
    return NULL;
  }
  return &g_sensor_pos[node_id];
}
