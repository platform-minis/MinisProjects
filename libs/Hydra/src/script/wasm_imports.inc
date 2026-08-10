// Wygenerowane przez tools/gen_bindings.py z tools/wasm_bindings.def.
// Nie edytuj ręcznie — zmiany przepadną przy regeneracji.

const Import kCoreImports[] = {
    {"millis", "i()",  wasmMillis},
    {"micros", "i()",  wasmMicros},
    {"delay",  "v(i)", wasmDelay},
};

const Import kLogImports[] = {
    {"log", "v(iii)", wasmLog},
};

const Import kGpioImports[] = {
    {"gpio_mode",   "i(ii)", wasmGpioMode},
    {"gpio_write",  "i(ii)", wasmGpioWrite},
    {"gpio_read",   "i(i)",  wasmGpioRead},
    {"gpio_toggle", "i(i)",  wasmGpioToggle},
};

const Import kAdcImports[] = {
    {"adc_raw", "i(i)", wasmAdcRaw},
    {"adc_mv",  "i(i)", wasmAdcMv},
};

const Import kPwmImports[] = {
    {"pwm_setup",   "i(ii)", wasmPwmSetup},
    {"pwm_duty",    "i(ii)", wasmPwmDuty},
    {"pwm_us",      "i(ii)", wasmPwmUs},
    {"pwm_release", "i(i)",  wasmPwmRelease},
};

const Import kEventImports[] = {
    {"event_emit",    "v(iifi)", wasmEventEmit},
    {"event_name_id", "i(ii)",   wasmEventNameId},
};

const Import kI2cImports[] = {
    {"i2c_ping",  "i(ii)",    wasmI2cPing},
    {"i2c_scan",  "i(iii)",   wasmI2cScan},
    {"i2c_read",  "i(iiiii)", wasmI2cRead},
    {"i2c_write", "i(iiiii)", wasmI2cWrite},
};

/** Grupa importów wraz z liczebnością — do pętli linkującej. */
struct ImportGroup {
    const Import* imports;
    size_t        count;
    bool BindingSet::*flag;
};

const ImportGroup kImportGroups[] = {
    {kCoreImports, 3, &BindingSet::core},
    {kLogImports, 1, &BindingSet::log},
    {kGpioImports, 4, &BindingSet::gpio},
    {kAdcImports, 2, &BindingSet::adc},
    {kPwmImports, 4, &BindingSet::pwm},
    {kEventImports, 2, &BindingSet::event},
    {kI2cImports, 4, &BindingSet::i2c},
};
