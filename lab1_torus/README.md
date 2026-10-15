# Лабораторная работа 1

**Вариант 7 — тор.** Выполнены вариант и дополнительные задания 1–5.

## Условие

Построить 3D-объект (тор), спроецировать его на экран с помощью матриц
перспективной и ортографической проекции и аффинных преобразований. Vulkan,
GLFW и ImGui; программа работает в реальном времени, проекцию и трансформации
объекта можно менять на лету.

| Задание | Где в интерфейсе |
|---|---|
| 1. Переключение перспективной и ортографической проекции | **Projection** |
| 2. Изменение позиции, поворота и растяжения фигуры | **Transform** |
| 3. Движение и вращение по сложной траектории, пауза, скорость, параметры траектории | **Animation** |
| 4. Изменение цвета фигуры | **Color** → ColorEdit |
| 5. Процедурные цвета вершин, умножаемые на выбранный цвет | **Color** → *Procedural vertex colors* |

Параметры самого тора (радиусы и плотность сетки) — секция **Torus**.

## Сборка и запуск

Нужны компилятор C++20, CMake, Ninja и Vulkan SDK (с `glslc`). При первой
настройке CMake скачивает GLFW, vk-bootstrap, VMA и ImGui, поэтому нужны
интернет и `git`.

Все команды выполняются **из папки лабы**: шейдеры загружаются по относительному
пути `shaders/*.spv`.

**Windows (MSYS2 UCRT64):**

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-shaderc
cmake --preset debug
cmake --build build-debug --parallel
./build-debug/vulkan-starter-app.exe
```

**macOS (Vulkan SDK с MoltenVK):**

```bash
source ~/VulkanSDK/<версия>/setup-env.sh
cmake --preset debug
cmake --build build-debug --parallel
./build-debug/vulkan-starter-app
```

Шейдеры `shaders/torus.vert` и `shaders/torus.frag` компилируются в SPIR-V
автоматически при сборке. Если CMake пишет `glslc not found`, шейдеры не
соберутся и программа не запустится.

## Структура

| Файл | Что |
|---|---|
| `source/application.cpp` | дескрипторы, графический конвейер, интерфейс, анимация, запись команд отрисовки |
| `source/math.hpp` | векторы и матрицы 4×4: перенос, повороты, масштаб, перспективная и ортографическая проекции |
| `source/torus.hpp`, `source/torus.cpp` | построение треугольной сетки тора и цветов вершин |
| `source/graphics.hpp`, `source/graphics.cpp` | создание и запись буферов, загрузка шейдеров |
| `shaders/torus.vert`, `shaders/torus.frag` | вершинный и фрагментный шейдеры |
| `source/main.cpp`, `source/graphics_internal.*`, `source/application.hpp` | стартовый код преподавателя: окно, инициализация Vulkan, swapchain, ImGui |

Стартовый код — [vladeemerr/vulkan-starter-app](https://github.com/vladeemerr/vulkan-starter-app)
(лицензия Apache 2.0, см. `LICENSE` и `NOTICE`).
