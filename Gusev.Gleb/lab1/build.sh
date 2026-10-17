#!/bin/sh
# Сборка демона reminder. Результат: ./reminder, промежуточные файлы удаляются.
# Использование: ./build.sh            — только демон
#                ./build.sh --test     — ещё собрать и прогнать тесты
set -eu

cd "$(dirname "$0")"

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT  # чистим даже при ошибке сборки

TESTS=OFF
[ "${1:-}" = "--test" ] && TESTS=ON

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS="$TESTS"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

cp "$BUILD_DIR/reminder" ./reminder

if [ "$TESTS" = ON ]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

echo "Готово: $(pwd)/reminder"
