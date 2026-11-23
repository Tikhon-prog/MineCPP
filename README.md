# MineCPP

**MineCPP** — офлайн-лаунчер Minecraft: Java Edition на C++20. Первая версия (`0.1.0`) реализует локальное обнаружение версий, импорт и извлечение клиентского JAR, а также запуск уже установленной vanilla-версии без сетевого входа.

> Важно о сходстве с Horizon: Horizon — не обычный лаунчер Java Edition. Это отдельная нативная реализация игры; его `horizon-prepare` преобразует файлы уже установленной Minecraft Java Edition в ресурсы для собственного движка. MineCPP 0.1.0 пока является именно лаунчером Java Edition: он берёт существующий JAR, извлекает его в профиль и запускает игру через установленную Java. Нативный C++-движок — отдельный, гораздо более крупный этап и не заявляется как уже реализованный.

## Что уже работает

- Поиск установленных версий в стандартной папке `.minecraft` или в указанном каталоге.
- Список версий и локальных профилей MineCPP.
- Проверка JAR и подсчёт `.class`-файлов.
- Извлечение JAR/ZIP с проверкой CRC и защитой от Zip Slip.
- Импорт существующей версии: копирование её `.json` и `.jar`, извлечение содержимого в отдельный профиль. Исходная установка Minecraft не изменяется.
- Офлайн-запуск vanilla-профиля по локальному JSON-описанию, локальным библиотекам и локальным assets.
- Построение classpath и извлечение native-библиотек для текущей ОС.
- Офлайн-идентичность игрока (локальное имя и UUID `OfflinePlayer`).
- CMake, тест ZIP-импорта и CI-сборки Linux/Windows/macOS.

## Принципиальные ограничения 0.1.0

- Это первая CLI-версия; графического интерфейса пока нет.
- MineCPP не скачивает игровые файлы и не обращается к серверу аутентификации. Перед импортом Minecraft уже должен быть установлен и хотя бы один раз подготовлен официальным лаунчером или другим совместимым менеджером. Запущенный клиент сохраняет собственные сетевые возможности игры; MineCPP не является системным сетевым firewall.
- Для запуска нужны локальные `libraries/`, `assets/`, версия JSON и подходящая Java. Один JAR сам по себе не содержит всех зависимостей Minecraft.
- Поддерживается обычный vanilla layout. Наследуемые и модифицированные версии (Forge/Fabric/Quilt), сложные кастомные профили и несколько слоёв наследования пока не поддерживаются.
- Офлайн-профиль не выполняет Microsoft-аутентификацию. Он предназначен прежде всего для одиночной игры и серверов, допускающих offline-mode; online-mode серверы потребуют действительную авторизацию. Сам лаунчер не скачивает данные, но не блокирует сетевые обращения, которые может выполнять сама игра.
- ZIP64 и зашифрованные ZIP-записи не поддерживаются.
- Нативный движок Minecraft на C++ ещё не реализован.

## Сборка

Нужны C++20-компилятор, CMake 3.20+ и zlib development package.

### Linux (Debian/Ubuntu/Lubuntu)

```bash
sudo apt install build-essential cmake zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/minecpp --help
```

### Windows

Установите Visual Studio 2022 Build Tools с компонентом C++ и CMake, а также zlib. Затем из Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\minecpp.exe --help
```

### macOS

```bash
xcode-select --install
brew install cmake zlib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Использование

Путь по умолчанию: `~/.minecraft` на Linux, `%APPDATA%\\.minecraft` на Windows, `~/Library/Application Support/minecraft` на macOS. Профили MineCPP хранятся отдельно в пользовательском каталоге данных.

```bash
# Посмотреть локально установленные версии
minecpp versions

# Проверить, что есть всё необходимое для офлайн-запуска
minecpp doctor

# Импортировать версию; JAR будет извлечён в профиль MineCPP
minecpp import 1.20.4

# Или указать JAR и JSON версии отдельно
minecpp import 1.20.4 --jar "/path/to/1.20.4.jar" --metadata "/path/to/1.20.4.json"

# Посмотреть импортированные профили
minecpp profiles

# Сформировать команду Java, не запуская игру
minecpp launch 1.20.4 --username Tikhon --dry-run

# Запустить офлайн
minecpp launch 1.20.4 --username Tikhon
```

Пути можно переопределить:

```bash
minecpp import 1.20.4 --minecraft-dir "/path/to/.minecraft" --data-dir "/path/to/MineCPP-data"
minecpp launch 1.20.4 --username Player --minecraft-dir "/path/to/.minecraft" --data-dir "/path/to/MineCPP-data"
```

## Тесты

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Офлайн-гарантия

В исходном коде MineCPP 0.1.0 нет сетевого клиента, загрузчика версий или механизма авторизации. Программа читает только локальные файлы, запускает локальную Java и не делает фоновых сетевых запросов.
