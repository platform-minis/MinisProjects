#pragma once
/**
 * Płytka aplikacyjna: rover na ESP32-S3 — przykład z rozdz. 5 specyfikacji.
 *
 * Pokazuje właściwy podział: plik płytki modułu opisuje sam moduł, a plik
 * płytki urządzenia dokłada nazwy logiczne tego, co do niego podłączono.
 * Aplikacja mówi Pin::MotorLeftPwm, nigdy „GPIO 17".
 */

#include "esp32s3_pico.hpp"

#undef HYDRA_BOARD_NAME
#define HYDRA_BOARD_NAME "rover-s3"

namespace Pin {

// --- napęd (mostek H DRV8833: PWM + kierunek na kanał) ---
constexpr ::hydra::hal::PinNum MotorLeftPwm   = 17;
constexpr ::hydra::hal::PinNum MotorLeftDir   = 18;
constexpr ::hydra::hal::PinNum MotorRightPwm  = 15;
constexpr ::hydra::hal::PinNum MotorRightDir  = 16;

// --- enkodery kwadraturowe (na ESP32 dekodowane jednostką PCNT) ---
constexpr ::hydra::hal::PinNum EncoderLeftA   = 4;
constexpr ::hydra::hal::PinNum EncoderLeftB   = 5;
constexpr ::hydra::hal::PinNum EncoderRightA  = 6;
constexpr ::hydra::hal::PinNum EncoderRightB  = 7;

// --- czujniki i zasilanie ---
constexpr ::hydra::hal::PinNum ImuInterrupt   = 10;  ///< data-ready z IMU
constexpr ::hydra::hal::PinNum BatterySense   = 1;   ///< ADC za dzielnikiem 1:2
constexpr ::hydra::hal::PinNum EStopButton    = 14;  ///< zwarcie do masy

}  // namespace Pin

/** Dzielnik pomiaru baterii — wpisywany do kalibracji ADC przy starcie. */
#define HYDRA_ROVER_BATTERY_DIVIDER_NUM 2
#define HYDRA_ROVER_BATTERY_DIVIDER_DEN 1
