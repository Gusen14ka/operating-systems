#!/bin/sh
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
    # Тесты не должны прерывать скрипт до сообщения о логах
    set +e
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    TEST_RC=$?
    set -e
    echo "Подробные логи тестов: $(pwd)/test-logs/"
    [ "$TEST_RC" -eq 0 ] || { echo "Есть упавшие тесты" >&2; exit "$TEST_RC"; }
fi

echo "Готово: $(pwd)/reminder"
