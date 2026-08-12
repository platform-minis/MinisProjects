#pragma once
/**
 * Hydra — szybka transformata Fouriera dla sygnału rzeczywistego.
 *
 * Własna, a nie z biblioteki, i to jest decyzja o zależnościach, nie
 * o matematyce. TensorFlow Lite Micro niesie kissfft, ale widmo przydaje się
 * także tam, gdzie modelu nie ma: próg na paśmie, wykrycie tonu, podgląd.
 * Element liczący widmo, który wymaga pięciu megabajtów biblioteki uczącej,
 * byłby w takim projekcie nie do przyjęcia.
 *
 * Radix-2 w miejscu, bez tablicy współczynników obrotu. Tablica oszczędza
 * wywołania `sinf`/`cosf`, ale zajmuje pamięć proporcjonalną do rozmiaru
 * przekształcenia — a na MCU to właśnie pamięci brakuje najpierw. Przy 256
 * punktach i taktowaniu rzędu 100 MHz liczenie zajmuje ułamek milisekundy.
 *
 * ## Sygnał rzeczywisty, nie zespolony
 *
 * Próbki z mikrofonu i z czujnika są rzeczywiste, więc widmo jest symetryczne
 * i połowa wyniku nie niesie nic nowego. Zwracamy `n/2 + 1` prążków — od
 * składowej stałej do Nyquista.
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace util {

/**
 * Liczy widmo **mocy** sygnału rzeczywistego.
 *
 * @param samples  wejście, `n` próbek; **niszczone** — bufor służy też za
 *                 przestrzeń roboczą części rzeczywistej
 * @param scratch  bufor na część urojoną, `n` elementów
 * @param n        rozmiar przekształcenia, potęga dwójki (co najmniej 4)
 * @param power    wynik: `n/2 + 1` wartości mocy (kwadrat modułu)
 *
 * Moc, a nie amplituda, bo tego chce bank filtrów mel — a pierwiastek liczony
 * po drodze i tak zostałby zaraz podniesiony do kwadratu.
 *
 * Zwraca `false`, gdy `n` nie jest potęgą dwójki albo jest za małe. Cicha
 * zgoda na zły rozmiar dałaby widmo, które wygląda poprawnie i jest nieprawdą.
 */
bool powerSpectrum(float* samples, float* scratch, u16 n, float* power);

/** Czy `n` jest potęgą dwójki nie mniejszą niż 4. */
constexpr bool isValidFftSize(u16 n) {
    return n >= 4 && (n & (n - 1)) == 0;
}

/** Liczba prążków widma dla przekształcenia o rozmiarze `n`. */
constexpr u16 spectrumBins(u16 n) { return static_cast<u16>(n / 2 + 1); }

/**
 * Okno Hanninga na miejscu.
 *
 * Bez okna każdy blok kończy się skokiem do zera, a skok w dziedzinie czasu
 * rozlewa się po całym widmie — ton o jednej częstotliwości daje wtedy
 * grzebień prążków zamiast jednego. Objaw wygląda jak zaszumiony sygnał
 * i prowadzi do szukania usterki w mikrofonie.
 */
void applyHann(float* samples, u16 n);

}  // namespace util
}  // namespace hydra
