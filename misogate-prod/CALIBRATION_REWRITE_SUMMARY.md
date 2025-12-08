# Calibration & Position Tracking Rewrite Summary

## Overview

Complete rewrite of the calibration and position tracking system to use **ambient-only calibration** with **relative magnetic field measurements** between three nodes.

## Key Changes

### 1. Simplified Calibration Approach

**Before:**
- Two-phase calibration:
  1. Baseline calibration (ambient field)
  2. Manual position calibration (user places magnet at known positions)
- Required serial console interaction
- Lookup table-based position refinement

**After:**
- Single-phase ambient calibration only
- Automatic transition to running mode
- No manual position calibration needed
- Uses relative field strength between nodes

### 2. Linear Array Configuration

Fixed node positions:
- **Node 1**: Position 0 (left end)
- **Node 2**: Position 500 (center)
- **Node 3**: Position 1000 (right end)

### 3. Position Estimation Algorithm

**New approach uses relative magnetic field strength:**

1. Calculate magnet-induced field at each node: `B_mag = B_measured - B_ambient`
2. Compute field magnitude: `|B_mag|` for each node
3. Weight-based interpolation:
   - `weight_i = field_i ^ 2.5` (exponential weighting emphasizes strongest sensor)
   - `position = Σ(weight_i × position_i) / Σ(weight_i)`

**Result:**
- Magnet near Node 1 → field₁ >> field₂, field₃ → position ≈ 0
- Magnet near Node 3 → field₃ >> field₁, field₂ → position ≈ 1000
- Magnet between nodes → smooth interpolation

### 4. Files Modified

#### `src/lora/calibration.c` (Complete rewrite)
- Removed console input thread and manual calibration
- Simplified to ambient baseline capture only
- Automatic transition to running mode when all 3 nodes have baseline
- ~200 lines removed (525 → ~200 lines)

#### `src/lora/calibration.h` (Complete rewrite)
- Removed `calib_point` structure (kept forward declaration for compatibility)
- Removed `CALIB_READINGS_PER_POINT` and related constants
- Simplified API: removed `calibration_start_console()`, added `calibration_start()`
- Added stub `calibration_get_points()` for API compatibility (always returns NULL)

#### `src/lora/position.c` (Complete rewrite)
- Removed lookup table interpolation
- Implemented relative field-based position estimation
- Two estimation methods:
  1. **Primary**: Weighted average using field^exponent
  2. **Fallback**: Ratio-based interpolation
- Fixed node positions (no longer configurable)
- ~150 lines simplified

#### `src/lora/position.h` (Complete rewrite)
- Updated documentation to reflect relative field approach
- Noted that `position_set_sensor_positions()` is not supported (positions are fixed)
- Simplified API signatures

#### `src/lora/lora.c` (Minor updates)
- Changed `calibration_start_console()` → `calibration_start()`
- Removed `CALIB_STATE_WAITING_INPUT` handling
- Removed calibration point lookup (now passes NULL to position estimator)
- ~15 lines changed

#### `src/lora/lora.h` (Minor updates)
- Removed `CALIB_STATE_WAITING_INPUT` from enum
- Updated documentation for `lora_start_calibration()`

#### `src/main.c` (Comment update)
- Updated comments to reflect automatic ambient calibration

## Calibration States

**Reduced from 4 to 3 states:**

| State | Description |
|-------|-------------|
| `CALIB_STATE_IDLE` | Not yet started |
| `CALIB_STATE_BASELINE` | Capturing ambient field (no magnet) |
| ~~`CALIB_STATE_WAITING_INPUT`~~ | *Removed - no manual calibration* |
| `CALIB_STATE_RUNNING` | Normal tracking operation |

## Usage

### System Startup Sequence

1. **System boots** → `CALIB_STATE_IDLE`
2. **User removes magnet from tracking area** (important!)
3. **System calls `calibration_start()`** → `CALIB_STATE_BASELINE`
4. **System collects 15 readings per node** automatically
5. **All 3 nodes complete** → `CALIB_STATE_RUNNING` (automatic)
6. **User places magnet in tracking area** → Position tracking begins

### No User Interaction Required

- Old system: Required serial console commands (`DONE`, `START`, position values)
- New system: Completely automatic after `calibration_start()` is called

## Algorithm Parameters

### Configurable Constants

| Parameter | Value | Location | Description |
|-----------|-------|----------|-------------|
| `BASELINE_READINGS_REQUIRED` | 15 | calibration.h | Ambient field samples per node |
| `MIN_FIELD_THRESHOLD` | 500 m-uT | position.c | Minimum field to estimate position |
| `FIELD_WEIGHT_EXPONENT` | 2.5 | position.c | Exponential weight for strongest sensor |

### Tuning Recommendations

- **Increase `FIELD_WEIGHT_EXPONENT`** → More weight to closest sensor (sharper transitions)
- **Decrease `FIELD_WEIGHT_EXPONENT`** → Smoother interpolation (more gradual)
- **Adjust `MIN_FIELD_THRESHOLD`** based on magnet strength and sensor distance

## Benefits

1. **Simpler user experience**: No manual calibration steps
2. **Faster deployment**: Automatic baseline → running transition
3. **More robust**: No lookup table to maintain
4. **Physics-based**: Uses natural inverse-cube-law of magnetic dipoles
5. **Cleaner codebase**: ~350 lines of code removed

## Migration Notes

### API Compatibility

- `calibration_get_points()` - Now returns NULL (stub for compatibility)
- `position_estimate_1D()` - Ignores `calib_points` parameter
- `position_set_sensor_positions()` - Logs warning (positions are fixed)

### Breaking Changes

- Console-based calibration removed
- `CALIB_STATE_WAITING_INPUT` removed
- `calibration_start_console()` renamed to `calibration_start()`
- Lookup table calibration not supported

## Testing Recommendations

1. **Verify baseline capture**: Check logs show all 3 nodes reaching 15/15 readings
2. **Test endpoints**: Place magnet near Node 1 (expect ~0) and Node 3 (expect ~1000)
3. **Test center**: Place magnet near Node 2 (expect ~500)
4. **Test interpolation**: Move magnet smoothly along axis, verify smooth position changes
5. **Test field strength**: Ensure magnet produces > 500 m-uT at sensor locations

## Future Enhancements

Possible improvements to consider:

1. **Adaptive weighting**: Auto-adjust `FIELD_WEIGHT_EXPONENT` based on field gradient
2. **Outlier rejection**: Reject sensors with anomalous readings
3. **Kalman filtering**: Smooth position estimates over time
4. **Auto-baseline refresh**: Periodically update ambient baseline when no magnet detected

---

**Date**: December 8, 2025
**Author**: AI Assistant
**Status**: Complete rewrite ready for testing

