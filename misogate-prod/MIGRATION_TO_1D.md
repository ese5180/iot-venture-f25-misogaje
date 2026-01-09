# Migration to 1D Position Tracking

## Overview
The system has been converted from 2D triangulation to 1D linear position tracking based solely on magnetic field magnitude.

## Architecture Changes

### Sensor Layout
- **Previous**: Sensors arranged in 2D plane with (x, y) coordinates
- **Current**: Sensors arranged linearly:
  - Node 1: position 0
  - Node 2: position 500
  - Node 3: position 1000

### Position Estimation
- **Previous**: Dipole field model with Gauss-Newton solver for (x, y) position
- **Current**: Magnitude-based inverse-distance weighting for single position value (0-1000)

### Calibration Input
- **Previous**: User enters "X Y" coordinates (e.g., "250 500")
- **Current**: User enters single position value (e.g., "250")

### MQTT Output Format
- **Previous**: `{"x": 250, "y": 500}`
- **Current**: `{"position": 250}`

## Files Modified

### 1. `src/lora/position.h`
- Simplified `struct sensor_pos` to contain only single `position` field
- Removed `struct node_pos` and `struct dipole_orientation`
- Removed `struct position_estimate` (was used for Gauss-Newton solver)
- Replaced `position_estimate_2D()` with `position_estimate_1D()`
- Removed dipole field model functions (`position_compute_dipole_field`, `position_compute_jacobian`)

### 2. `src/lora/position.c`
- Complete rewrite for 1D estimation
- Removed complex dipole physics model and Gauss-Newton solver (~450 lines)
- Implemented simple magnitude-based position estimation:
  - `position_estimate_simple()`: Weighted average based on field strength
  - `position_estimate_lookup()`: Calibration-based interpolation in magnitude space
  - `position_estimate_1D()`: Main function that blends both methods
- Updated sensor positions to linear arrangement (0, 500, 1000)

### 3. `src/lora/calibration.h`
- Changed `struct calib_point`:
  - `int x, y` → `int position` (single value 0-1000)
  - Field magnitude (`node_absB`) is now primary measurement

### 4. `src/lora/calibration.c`
- Updated help text to reflect 1D calibration
- Changed command parsing from "X Y" to single position "P"
- Updated status display to show position instead of (x, y)

### 5. `src/lora/lora.h`
- Simplified `struct lora_position`:
  - `int x, y` → `int position`
- Updated function documentation for 1D tracking

### 6. `src/lora/lora.c`
- Updated position storage structure
- Changed from `position_estimate_2D()` to `position_estimate_1D()`
- Modified MQTT publish format: `{"x":..., "y":...}` → `{"position":...}`
- Updated logging to show 1D position

## Usage Instructions

### Calibration Process

#### Phase 1: Baseline Calibration (UNCHANGED)
1. Remove magnet from tracking area
2. System automatically captures ambient field
3. Wait for all sensors to show "READY"
4. Type `DONE` to proceed

#### Phase 2: Position Calibration (UPDATED)
1. Place magnet at known position along the linear axis (0-1000)
2. Enter the position value (e.g., `250` for position 250)
3. System captures readings for 15 seconds
4. Repeat for multiple positions for better accuracy
5. Type `START` to begin tracking mode (or skip position calibration)

**Example calibration session:**
```
> 0        # Calibrate at left end (node 1)
> 250      # Calibrate at 1/4 position
> 500      # Calibrate at center (node 2)
> 750      # Calibrate at 3/4 position
> 1000     # Calibrate at right end (node 3)
> START    # Begin tracking
```

### MQTT Data Format

Position updates are published at 100ms intervals:

```json
{"position": 432}
```

Where `position` is an integer from 0 to 1000 representing the magnet's location along the linear axis.

## Algorithm Details

### Pairwise Interpolation with Direction Detection

**Challenge**: The center sensor (node 2 at position 500) creates ambiguity because its field magnitude increases whether the magnet approaches from position 0 or position 1000.

**Solution**: Use pairwise interpolation based on which side the magnet is on:

1. **Determine direction**: Compare field₁ (node 1) vs field₃ (node 3)

2. **If field₃ > field₁**: Magnet is on **RIGHT side** (500-1000)
   - Use only sensors 2 and 3
   - Interpolate between positions 500 and 1000
   - Position = (w₂ × 500 + w₃ × 1000) / (w₂ + w₃)
   - Where w = field²

3. **If field₁ > field₃**: Magnet is on **LEFT side** (0-500)
   - Use only sensors 1 and 2
   - Interpolate between positions 0 and 500
   - Position = (w₁ × 0 + w₂ × 500) / (w₁ + w₂)
   - Where w = field²

This cleanly resolves the ambiguity by using only the two relevant sensors for each half of the tracking range.

**Example scenarios**:
- **field₁=8000, field₂=3000, field₃=500**: field₁ > field₃ → LEFT side, interpolate between nodes 1 and 2
- **field₁=500, field₂=3000, field₃=8000**: field₃ > field₁ → RIGHT side, interpolate between nodes 2 and 3
- **field₁=2000, field₂=9000, field₃=2000**: field₁ ≈ field₃ → approximately at center (500)

### Calibration-Based Lookup
Uses inverse-distance weighted interpolation in field-magnitude space:
- Compares current field magnitudes to calibrated values
- Finds closest calibration points in magnitude-space
- Interpolates position using inverse-distance weights

### Blending Strategy
When calibration data exists:
- 70% weight to lookup-based estimate
- 30% weight to simple magnitude estimate
- Provides robustness against calibration drift

## Benefits of 1D Tracking

1. **Simpler Implementation**: No complex physics model or iterative solver
2. **Faster Computation**: Direct calculation vs. 20 iterations of Gauss-Newton
3. **Easier Calibration**: Single position value vs. (x, y) coordinates
4. **More Intuitive**: Linear position easier to understand and use
5. **Lower Memory**: Removed ~400 lines of dipole model code

## Backward Compatibility

- `lora_get_position_rel()` still works (returns position 0-1000)
- Baseline calibration process unchanged
- MQTT topic unchanged (only payload format changed)
- 3D field vectors still captured for future use

## Testing Recommendations

1. **Basic functionality**: Verify position updates publish to MQTT
2. **Calibration**: Test single-value position input
3. **Edge cases**: Test positions at 0, 500, 1000 (sensor locations)
4. **Interpolation**: Test positions between sensors
5. **Field strength**: Test with varying magnet distances

