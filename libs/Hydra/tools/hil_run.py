#!/usr/bin/env python3
"""
Hydra — harness testów na fizycznym sprzęcie (rozdz. 12).

Steruje urządzeniem przez shell diagnostyczny i sprawdza odpowiedzi. To dlatego
shell wypisuje wyniki także w postaci klucz=wartość: człowiek czyta tabelkę,
a ten skrypt rozbiera to samo jednym wyrażeniem.

    tools/hil_run.py --port /dev/ttyUSB0 --suite basic

Wymaga pyserial. Kod wyjścia zero oznacza, że wszystkie sprawdzenia przeszły.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    print("brak modułu pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(2)


PROMPT = b"hydra> "


class Device:
    """Urządzenie po drugiej stronie portu szeregowego."""

    def __init__(self, port, baud, timeout):
        self.serial = serial.Serial(port, baud, timeout=timeout)
        self.timeout = timeout

    def wait_for_prompt(self, deadline):
        """Czeka na zachętę shella. Urządzenie po resecie potrzebuje chwili."""
        buffer = b""
        while time.time() < deadline:
            buffer += self.serial.read(64)
            if PROMPT in buffer:
                return True
        return False

    def command(self, line):
        """Wysyła komendę i zwraca wszystko do kolejnej zachęty."""
        self.serial.reset_input_buffer()
        self.serial.write(line.encode() + b"\r\n")
        self.serial.flush()

        deadline = time.time() + self.timeout
        buffer = b""
        while time.time() < deadline:
            buffer += self.serial.read(256)
            if buffer.count(PROMPT) >= 1 and buffer.endswith(PROMPT):
                break
        return buffer.decode("utf-8", errors="replace")

    def fields(self, line):
        """Rozbiera odpowiedź na słownik par klucz=wartość."""
        text = self.command(line)
        found = {}
        for match in re.finditer(r"^([a-z_0-9]+)=(.*)$", text, re.MULTILINE):
            found[match.group(1)] = match.group(2).strip()
        return found, text


class Report:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, condition, description, detail=""):
        if condition:
            self.passed += 1
            print(f"  \033[32mOK\033[0m   {description}")
        else:
            self.failed += 1
            print(f"  \033[31mBŁĄD\033[0m {description}")
            if detail:
                for row in detail.strip().splitlines()[:10]:
                    print(f"        {row}")

    def summary(self):
        total = self.passed + self.failed
        color = "\033[32m" if self.failed == 0 else "\033[31m"
        print(f"\n{color}{self.passed}/{total} sprawdzeń przeszło\033[0m")
        return 0 if self.failed == 0 else 1


def suite_basic(device, report):
    """Sprawdzenia, które muszą przejść na każdym urządzeniu z Hydrą."""
    fields, raw = device.fields("version")
    report.check("hydra" in fields, "urządzenie odpowiada i podaje wersję", raw)
    report.check(fields.get("platform", "") != "", "platforma rozpoznana", raw)

    fields, raw = device.fields("ps")
    tasks = int(fields.get("tasks", "0"))
    report.check(tasks > 0, f"taski działają (znaleziono {tasks})", raw)

    fields, raw = device.fields("top")
    report.check("uptime_s" in fields, "wskaźniki obciążenia dostępne", raw)

    # Zapas stosu zerowy oznacza platformę bez pomiaru; niezerowy i mały
    # to sygnał, że któryś task jest o krok od przepełnienia.
    stack = int(fields.get("stack_min", "0"))
    report.check(stack == 0 or stack > 256,
                 f"zapas stosu bezpieczny (najgorszy {stack} B)", raw)

    # Gubione zdarzenia to najczęściej za mała skrzynka albo task, który nie
    # nadąża — objaw, którego na hoście nie widać, bo tam wszystko jest szybkie.
    dropped = int(fields.get("events_dropped", "0"))
    report.check(dropped == 0, f"magistrala nie gubi zdarzeń ({dropped})", raw)

    dropped_logs = int(fields.get("log_dropped", "0"))
    report.check(dropped_logs == 0, f"logi nie są gubione ({dropped_logs})", raw)

    heap = int(fields.get("heap_free", "0"))
    report.check(heap > 8192, f"zapas sterty wystarczający ({heap} B)", raw)

    fields, raw = device.fields("hal")
    report.check(fields.get("backend", "").startswith("arduino"),
                 "warstwa sprzętowa zainstalowana", raw)


def suite_i2c(device, report, expected):
    """Sprawdza, że układy na magistrali odpowiadają."""
    fields, raw = device.fields("i2c scan")
    found = int(fields.get("found", "0"))
    report.check(found > 0, f"magistrala I2C odpowiada ({found} układów)", raw)

    for address in expected:
        report.check(address.lower() in raw.lower(),
                     f"układ {address} obecny na magistrali", raw)


def suite_leak(device, report, rounds=20):
    """
    Szuka wycieku: sterta po serii komend nie powinna systematycznie maleć.
    Zgodnie z założeniem frameworka po App::begin() nie ma już alokacji, więc
    każdy stały ubytek jest błędem, a nie normalną pracą.
    """
    fields, _ = device.fields("top")
    before = int(fields.get("heap_free", "0"))
    if before == 0:
        report.check(False, "odczyt sterty przed próbą")
        return

    for _ in range(rounds):
        device.command("ps")
        device.command("version")

    fields, raw = device.fields("top")
    after = int(fields.get("heap_free", "0"))
    lost = before - after

    # Kilkaset bajtów mieści się w rozrzucie fragmentacji; ubytek rosnący
    # z liczbą powtórzeń to już wyciek.
    report.check(lost < 512,
                 f"brak wycieku po {rounds} rundach (ubytek {lost} B)", raw)


def suite_storage(device, report):
    """Sprawdza, że konfiguracja trwała naprawdę przeżywa zapis."""
    marker = f"hil{int(time.time()) % 100000}"
    fields, raw = device.fields(f"cfg set hiltest probe {marker}")
    report.check(fields.get("probe") == marker, "zapis konfiguracji", raw)

    fields, raw = device.fields("cfg get hiltest probe")
    report.check(fields.get("probe") == marker, "odczyt zapisanej wartości", raw)

    fields, raw = device.fields("cfg erase hiltest probe")
    report.check("erased" in fields, "kasowanie wpisu", raw)


def main():
    parser = argparse.ArgumentParser(description="Testy Hydry na fizycznym sprzęcie")
    parser.add_argument("--port", required=True, help="port szeregowy urządzenia")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--boot-timeout", type=float, default=15.0,
                        help="ile czekać na zachętę shella po resecie")
    parser.add_argument("--suite", default="basic",
                        choices=["basic", "i2c", "storage", "leak", "all"])
    parser.add_argument("--expect-i2c", nargs="*", default=[],
                        help="adresy, które muszą znaleźć się na magistrali")
    args = parser.parse_args()

    device = Device(args.port, args.baud, args.timeout)

    print(f"port {args.port}, oczekiwanie na urządzenie...")
    if not device.wait_for_prompt(time.time() + args.boot_timeout):
        # Urządzenie mogło wystartować przed podłączeniem portu — enter
        # wywołuje zachętę bez restartu.
        device.serial.write(b"\r\n")
        if not device.wait_for_prompt(time.time() + args.timeout):
            print("urządzenie nie odpowiada", file=sys.stderr)
            return 2

    report = Report()
    if args.suite in ("basic", "all"):
        print("\nzestaw podstawowy:")
        suite_basic(device, report)
    if args.suite in ("i2c", "all"):
        print("\nmagistrala I2C:")
        suite_i2c(device, report, args.expect_i2c)
    if args.suite in ("storage", "all"):
        print("\npamięć trwała:")
        suite_storage(device, report)
    if args.suite in ("leak", "all"):
        print("\nszczelność pamięci:")
        suite_leak(device, report)

    return report.summary()


if __name__ == "__main__":
    sys.exit(main())
