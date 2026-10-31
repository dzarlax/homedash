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
- [ ] Check manual restore, rejection of a second preview without a deadline extension or baseline change, and power-cycle behavior.
- [ ] Check network loss during preview. Local monotonic timeout and restore must work without the network.
- [ ] Check that the dedicated restore worker starts before accepting a profile and performs no PWM writes at startup. Failed mutex/task allocation must keep brightness disabled.
- [ ] Check automatic restore without the LVGL loop running. The worker polls every 100 ms; scheduling and I2C latency can delay the write beyond the 20-second deadline. A whole-MCU hang or bus failure is outside this software guarantee.
- [ ] Check simultaneous preview, timeout, manual restore and telemetry calls. Only one preview may be accepted, and its original restore value must survive competing requests.
- [ ] Confirm local day/night scheduling and wake-touch override behavior using the existing scheduler, without changing its decision source.

Keep all measurements and evidence in the profile review record. This software profile is not a substitute for board documentation or hardware validation.

## Documentation review, 2026-10-31

The official [Waveshare ESP-IDF example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B/tree/c652c902db607f7ffb376257393cfd7657aa6428/examples/ESP-IDF/16_LVGL_UI)
uses address `0x24`, PWM command `0x05`, caps its input at `97`, and converts
that input to a byte by multiplying by `255 / 100`. Its PWM initialization
explicitly writes `50`. This is not a documented power-on value, readback, or
measured calibration. The inspected driver exposes GPIO and ADC reads but no
PWM readback API. Do not invent a read command or use the demo value as baseline.
The user's board photo confirms the model but does not establish its hardware
revision. Physical activation remains pending baseline and board verification.

## Explicitly approved reference experiment

On 2026-10-31 the owner chose to establish a new reference command 50 and test
50 → 60 → 50, accepting that the unknown previous brightness is not preserved.
This bounded diagnostic exception does not validate a full calibration profile.
Use only the opt-in reference-test build and its two explicit Settings actions:
first set 50, then confirm screen readability before the 20-second test at 60.
Record physical readability, actual dimming direction, restoration and errors.
Do not mark `baseline_verified` or measured endpoints from documentation or
consent alone. The reference is physically established only after the device
executes the write and the observer confirms the screen. The standard build
keeps the experiment disabled. No automatic test begins at boot.
