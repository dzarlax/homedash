# Brightness calibration checklist

This checklist gathers measurements for a reviewed, versioned software profile. It does not authorize physical activation. Do not expose or issue raw PWM commands, and do not sweep PWM values to discover limits.

## Establish the board facts

- [ ] Record the exact board identifier and hardware revision from the physical board markings. Do not infer identity or revision from a product name alone.
- [ ] Obtain vendor documentation or the schematic for this exact revision. Record the backlight driver, PWM path, polarity, register behavior, and documented safe operating limits.
- [ ] Confirm that the IO expander address and PWM register in the firmware match that documentation. Investigate discrepancies before any calibration.
- [ ] Establish the current PWM baseline without changing it, using a trustworthy readback or an independently documented known state. Record the method and evidence. If the current value cannot be established, stop.
- [ ] Determine the safe minimum and maximum from vendor/schematic evidence or an already authorized, controlled measurement procedure. Do not discover bounds by blind sweeps.
- [ ] Record measured PWM values for the intended 10% and 100% endpoints, including polarity. Confirm they are distinct and inside the supported raw command cap (0–97) and established safe range.
- [ ] Assign profile format version `1` and a nonzero profile revision. Preserve the board identifier, hardware revision, range, endpoints, baseline, and evidence reference together.

## Review and activate separately

- [ ] Have the measured profile reviewed against the exact board revision and its evidence before any firmware caller applies it.
- [ ] Confirm the profile is still unconfigured at boot and that applying it itself performs no hardware write.
- [ ] With explicit hardware authorization and the device in the correct mode, check the unchanged baseline across a power cycle before enabling brightness controls.
- [ ] Check a time-limited preview and automatic restore. If restore fails, verify that the failure is visible and that retry remains possible; do not report the requested value as applied until the write succeeds.
- [ ] After any failed restore or rollback that leaves the physical output uncertain, stop brightness use. Re-establish the actual current PWM baseline from trustworthy readback or board-specific evidence, record the recovery evidence, and review a fresh profile revision before reapplying a profile. Reapplying the old profile alone does not establish the hardware state.
- [ ] Check manual restore, preview replacement, and power-cycle behavior.
- [ ] Check network loss during preview. Local monotonic timeout and restore must work without the network.
- [ ] Confirm local day/night scheduling and wake-touch override behavior using the existing scheduler, without changing its decision source.

Keep all measurements and evidence in the profile review record. This software profile is not a substitute for board documentation or hardware validation.
