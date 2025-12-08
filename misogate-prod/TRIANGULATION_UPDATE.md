# Triangulation Algorithm Update

## Summary

Replaced the complex magnetic dipole field model with a simpler triangulation algorithm that uses weighted averaging of sensor positions based on magnetic field strength.

## Changes Made

### 1. New Triangulation Function (`src/lora/position.c`)

Added `position_estimate_triangulation()` which implements:
- **Simple weighted averaging**: Each sensor's position is weighted by the magnitude of its measured magnetic field
- **Assumption**: Stronger magnetic field = closer to magnet = higher weight
- **Algorithm**:
  1. For each sensor, compute |B_magnet| = sqrt(Bx² + By² + Bz²)
  2. Filter out weak signals (< 100 milli-uT threshold)
  3. Weight each sensor position by its field strength
  4. Compute weighted average: position = Σ(weight × sensor_pos) / Σ(weight)
  5. Clamp result to valid range (0-1000)

### 2. Updated Main Estimation Function

Modified `position_estimate_2D()` to:
- **Primary method**: Use triangulation (was: dipole model)
- **Refinement**: Blend with lookup table if calibration points exist (70% triangulation, 30% lookup)
- **Fallback**: Pure lookup table if triangulation fails

### 3. Updated Header Documentation (`src/lora/position.h`)

- Added documentation for `position_estimate_triangulation()`
- Updated `position_estimate_2D()` description to reflect new approach

## Key Advantages

1. **Simplicity**: No complex nonlinear solver or dipole physics model
2. **Speed**: Much faster computation (no iterative Gauss-Newton)
3. **Robustness**: Less sensitive to model assumptions
4. **Intuitive**: Direct relationship between field strength and proximity

## Weighting Options

The current implementation uses linear weighting (`weight = B_magnitude`). You can adjust this in the code:

```c
float weight = B_magnitude;          // Linear (current)
// float weight = B_magnitude * B_magnitude;  // Quadratic (emphasizes closest sensor)
// float weight = sqrtf(B_magnitude);         // Square root (more balanced)
```

## Calibration Still Useful

The optional calibration points from Phase 2 are still used for refinement:
- If calibration points exist, the final position is a 70/30 blend of triangulation and lookup
- This helps correct for systematic errors in the simple triangulation model

## Testing Recommendations

1. **Verify sensor positions**: Ensure `g_sensor_pos[]` in `position.c` matches your physical setup
2. **Adjust threshold**: The 100 milli-uT threshold may need tuning based on your magnet strength
3. **Tune weighting**: Try different weight functions if results aren't optimal
4. **Compare with/without calibration**: Test both modes to see calibration impact

## Old Code (Still Available)

The dipole model functions are still present in the codebase:
- `position_estimate_dipole()` - Full Gauss-Newton solver
- `position_compute_dipole_field()` - Field model
- `position_compute_jacobian()` - Numerical derivatives

These are no longer called by `position_estimate_2D()` but remain available if needed.

