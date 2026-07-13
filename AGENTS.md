# Guide for AI Agents Working with MUIM (project name)

## Project overview

- Type: Embedded firmware for measuring module control module controllers (МУИМ - модуль управления измерительным модулем)
- Language: C (C99/C11), C++17
- Build System: CMake (version 3.22+)
- Codebase History: Evolved from MUIM

## Architecture and Structure

### Core Components

Core (Core/) директория с ядром кода
Im part (Im/) модулеспецифичная часть
Muim part (Muim/) модуленезависимая часть, тут ведется основная разработка
Test (Test/) тесты
Drivers(Drivers/) это STM32 CMSIS/HAL
ThirdParty (ThirdParty/) ПО поставляемое третьей стороной, as is
CI (ci/) скрипты для гитлаба
cmake (cmake/) различные скрипты для автосборки, версионирования
Documentation (DOC/) документация по проекту

### Directory Structure

Use `DOC/project_tree.md` only as a high-level repository layout reference. Do not edit it unless explicitly asked. Before code changes, verify relevant current files with `rg --files` or targeted `rg`.

## Coding Conventions

### Naming Conventions

Types:
Use `_t` for typedef'd data/struct types: `rb_t`, `rb_ctrl_t`, `error_desc_t`, `flash_event_record_t`.
Use `_s` for register/bitfield layout structs: `sdram_flag_s`, `reg_data32_s`.

Enums:
Use `_e` for enum types: `error_e`, `pwr_e`.
Use `kPascalCase` for values: `kFlashStorageFormatOk`, `kErrorLevelWarning`.

Functions:
Use existing module prefix style: `NvStorageInit()`, `MuimScpiSystemRegFindByName()`, `SdramPatternNext()`.
Preserve established subsystem styles: `rbPush()`, `SDRAM_IsIdle()`, `SCPI_SystemRegisterQ()`, `LED_set()`.

Variables:
Mostly `lower_snake_case`: `word_count`, `current_addr`, `last_log_sec`, `start_value`, `has_numeric_addr`.

Booleans:
Boolean functions use `Is`/`Has`: `NvStorageIsAvailable()`, `NvStorageHasEventLog()`, `SDRAM_IsIdle()`.

Constants:
Use upper case with underscores for `#define` constants: `FLASH_STORAGE_SERIAL_MAX_LEN`, `SCPI_INPUT_BUFFER_LENGTH`.

Macros:
Use upper case with underscores: `MUIM_SCPI_SYSTEM_REG_RO`, `DEBUG_PRINT`, `TOGGLE_BIT`, `MAX`.

### Code Style

Indentation: 2-space (no tabs).
Braces: Allman, opening brace on a new line.
Line length: per .clang-format is 80 columns.
Comments/Doxygen: Explain WHY, not WHAT. Stay near declarations and document non-obvious constraints/meaning.
Header Guards: prefer #pragma once, legacy/generated headers may keep include guards.

## Build System

Use CMake out-of-source builds in `build/`; the firmware target is `muim` for STM32H743 via `gcc-arm-none-eabi`, with sources/includes generated into `SourceList.cmake` and `IncludeList.cmake` by `Muim/Tools/getSourceList.sh` and `Muim/Tools/getIncludeList.sh`.
Do not manually maintain source lists in `CMakeLists.txt`; regenerate the lists when files are added or removed.

## ARM Cortex-M7 / STM32H7

Follow the short rules below for DMA/cache, MMIO, ISR state, IRQ priorities, and fault paths.
For rationale and examples, see `DOC/ai/arm_cortex_m7_notes.md`.

D-cache/DMA: D-cache is enabled; DMA buffers in cacheable memory require explicit clean/invalidate over full 32-byte cache lines, or placement in a non-cacheable MPU region.

Barriers/MMIO: Preserve `volatile` for raw MMIO; do not add `__DSB()` mechanically, use barriers only when hardware ordering/completion matters: DMA start, FIFO/status handoff, peripheral reset/disable, MPU/cache/system-control changes, sleep/reset/fault paths.

ISR/shared state: `volatile` is only for ISR-visible flags; it does not make compound updates atomic. Protect non-atomic shared state with minimal critical sections or another synchronization mechanism.

NVIC: Do not assign new IRQs priority `0` by default; justify priority, preemption, and calls into HAL/shared code.

Fault paths: Preserve CFSR/HFSR/BFAR/MMFAR and stacked-register capture; no heap allocation or long blocking waits in HardFault/DMA/low-level fault paths.

Peripheral drivers: Define ownership, blocking behavior, timeout policy, ISR/DMA interaction, and error propagation; no polling loops without timeout.

## Review Mode

For explicit review requests, use `DOC/ai/code_review.md`.
Do not apply the review answer format to normal implementation tasks.




Отвечай как senior/principal C++ engineer и reviewer production embedded-кода.

Требования к ответу:

- Не упрощай технические детали.
- Не сглаживай ошибки и проблемы.
- Если решение плохое — прямо скажи почему.
- Если есть UB/race condition/design smell — укажи явно.
- Не придумывай факты.
- Если информации недостаточно — так и скажи.
- Разделяй:
  - факты,
  - предположения,
  - best practices,
  - личные компромиссы индустрии.
- Для спорных моментов объясняй trade-offs.
- При ревью:
  - оцени архитектуру,
  - lifetime,
  - ownership,
  - exception safety,
  - thread safety,
  - cache/performance implications,
  - embedded constraints.
- Не пиши “можно улучшить”.
  Пиши конкретно:
  - что плохо,
  - насколько плохо,
  - чем грозит,
  - как исправить.
- Если код потенциально опасен — говори это прямо.
- Не пытайся поддерживать мотивацию.
- Приоритет: техническая корректность > дружелюбность > краткость.

Если есть неоднозначность — сначала перечисли возможные интерпретации.
Не выбирай молча.

Не давай сразу готовый код.
Сначала объясни:
- как думать,
- какие тут подводные камни,
- какие есть варианты архитектуры.

Формат ответа:
1. Главная проблема.
2. Почему это проблема.
3. Насколько критично.
4. Как исправить.
5. Как сделал бы production senior engineer.
