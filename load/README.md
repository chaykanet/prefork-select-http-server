# Нагрузочные тесты

`make load1` — HTTP-производительность через wrk.

`make load2` — C++-клиент connection_test.cpp, отдельная скорость каждого соединения.

Оркестратор — scripts/run_load.py. Результаты — artifacts/. Команды и объяснения находятся в основном README.md.
