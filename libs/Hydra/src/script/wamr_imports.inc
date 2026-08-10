// Wygenerowane przez tools/gen_bindings.py z tools/wasm_bindings.def.
// Nie edytuj ręcznie — zmiany przepadną przy regeneracji.

NativeSymbol kCoreWamrImports[] = {
    {"millis", reinterpret_cast<void*>(wamr_wasmMillis),   "()i", nullptr},
    {"micros", reinterpret_cast<void*>(wamr_wasmMicros),   "()i", nullptr},
    {"delay",  reinterpret_cast<void*>(wamr_wasmDelay),    "(i)", nullptr},
};

NativeSymbol kLogWamrImports[] = {
    {"log", reinterpret_cast<void*>(wamr_wasmLog),   "(iii)", nullptr},
};

NativeSymbol kGpioWamrImports[] = {
    {"gpio_mode",   reinterpret_cast<void*>(wamr_wasmGpioMode),     "(ii)i", nullptr},
    {"gpio_write",  reinterpret_cast<void*>(wamr_wasmGpioWrite),    "(ii)i", nullptr},
    {"gpio_read",   reinterpret_cast<void*>(wamr_wasmGpioRead),     "(i)i", nullptr},
    {"gpio_toggle", reinterpret_cast<void*>(wamr_wasmGpioToggle),   "(i)i", nullptr},
};

NativeSymbol kAdcWamrImports[] = {
    {"adc_raw", reinterpret_cast<void*>(wamr_wasmAdcRaw),   "(i)i", nullptr},
    {"adc_mv",  reinterpret_cast<void*>(wamr_wasmAdcMv),    "(i)i", nullptr},
};

NativeSymbol kPwmWamrImports[] = {
    {"pwm_setup",   reinterpret_cast<void*>(wamr_wasmPwmSetup),     "(ii)i", nullptr},
    {"pwm_duty",    reinterpret_cast<void*>(wamr_wasmPwmDuty),      "(ii)i", nullptr},
    {"pwm_us",      reinterpret_cast<void*>(wamr_wasmPwmUs),        "(ii)i", nullptr},
    {"pwm_release", reinterpret_cast<void*>(wamr_wasmPwmRelease),   "(i)i", nullptr},
};

NativeSymbol kEventWamrImports[] = {
    {"event_emit",    reinterpret_cast<void*>(wamr_wasmEventEmit),     "(iifi)", nullptr},
    {"event_name_id", reinterpret_cast<void*>(wamr_wasmEventNameId),   "(ii)i", nullptr},
};

NativeSymbol kI2cWamrImports[] = {
    {"i2c_ping",  reinterpret_cast<void*>(wamr_wasmI2cPing),    "(ii)i", nullptr},
    {"i2c_scan",  reinterpret_cast<void*>(wamr_wasmI2cScan),    "(iii)i", nullptr},
    {"i2c_read",  reinterpret_cast<void*>(wamr_wasmI2cRead),    "(iiiii)i", nullptr},
    {"i2c_write", reinterpret_cast<void*>(wamr_wasmI2cWrite),   "(iiiii)i", nullptr},
};

/**
 * Grupa importów WAMR wraz z liczebnością.
 *
 * Tablice celowo nie są `const`: WAMR sortuje je w miejscu przy
 * rejestracji, a zapis do stałych danych kończy się wywrotką.
 */
struct WamrImportGroup {
    NativeSymbol* imports;
    u32           count;
    bool BindingSet::*flag;
};

const WamrImportGroup kWamrImportGroups[] = {
    {kCoreWamrImports, 3, &BindingSet::core},
    {kLogWamrImports, 1, &BindingSet::log},
    {kGpioWamrImports, 4, &BindingSet::gpio},
    {kAdcWamrImports, 2, &BindingSet::adc},
    {kPwmWamrImports, 4, &BindingSet::pwm},
    {kEventWamrImports, 2, &BindingSet::event},
    {kI2cWamrImports, 4, &BindingSet::i2c},
};
