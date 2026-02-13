# Сборка проекта (Windows/MSYS2 + GCC + Qt6)

Этот документ фиксирует рабочий процесс установки инструментов и сборки `journal_app`.

## 1. Установка MSYS2

1. Установите MSYS2: https://www.msys2.org/
2. Откройте терминал `MSYS2 MINGW64`.
3. Обновите пакеты:

```bash
pacman -Syu
```

Закройте терминал и откройте `MSYS2 MINGW64` снова.

## 2. Установка toolchain и Qt6

В `MSYS2 MINGW64` выполните:

```bash
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-make \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-qt6-tools
```

## 3. Проверка окружения

В том же терминале:

```bash
g++ --version
cmake --version
qmake6 --version
```

Важно: собирайте проект именно из окружения `MSYS2 MINGW64`, чтобы `gcc`, `cmake` и `Qt6` были из одной среды.

## 4. Сборка проекта локально

Из корня репозитория:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

## 5. CI/CD на GitHub Actions

Для автоматической сборки используется workflow:

- `.github/workflows/build.yml`

Там настроены 2 job:

1. `ubuntu-latest` с установкой `qt6-base-dev` и сборкой через CMake.
2. `windows-latest` через `msys2/setup-msys2` (GCC + Qt6 + Ninja).

### Готовое приложение (portable .zip)

В workflow добавлен отдельный ручной сценарий (`workflow_dispatch`), который:
1. собирает Windows-версию в Release;
2. подтягивает Qt runtime через `windeployqt`;
3. публикует готовый артефакт `journal_app-windows-portable.zip`.

Как запускать:
1. GitHub -> `Actions` -> workflow `Build`.
2. Нажать `Run workflow`.
3. Оставить `package_windows=true` и запустить.
4. После завершения открыть запуск workflow и скачать артефакт `journal_app-windows-portable`.

## 6. Важные замечания

1. Зависимости должны устанавливаться в окружении (локально или в CI), а не через `execute_process()` внутри `CMakeLists.txt`.
2. В проекте используется проверка на GNU-компилятор, поэтому сборка ожидается на GCC/MinGW.
