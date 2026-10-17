#!/bin/bash
# Защита от повторного запуска через pid-файл
. "$(dirname "$0")/lib.sh"

suite "Защита от повторного запуска (pid-файл + /proc)"

count_alive() {
    local pid n=0
    for pid in "$@"; do
        is_alive "$pid" && n=$((n + 1))
    done
    echo "$n"
}

case_R01() {
    write_config
    start_daemon || return
    local old="$DAEMON_PID"
    step "второй запуск при работающем первом (pid $old)"
    start_daemon || return
    local new="$DAEMON_PID"
    expect_ne "в pid-файле новый pid" "$new" "$old"
    expect_true "старый экземпляр завершён" is_dead "$old"
    expect_true "новый экземпляр работает" is_alive "$new"
    expect_eq "работает ровно один экземпляр" "$(count_alive "$old" "$new")" 1
    expect_syslog "старый экземпляр записал в журнал выход по SIGTERM" \
        "\[$old\]: получен SIGTERM, завершение работы"
}
run_case R01 "Повторный запуск: старый экземпляр получает SIGTERM, новый занимает pid-файл" case_R01

case_R02() {
    write_config
    spawn_helper sleep 300
    local foreign="$HELPER_PID"
    step "в pid-файле pid чужого процесса (sleep, pid $foreign)"
    echo "$foreign" > "$PIDFILE"
    start_daemon || return
    expect_true "чужой процесс не тронут" is_alive "$foreign"
    expect_ne "pid-файл перезаписан pid демона" "$(daemon_pid)" "$foreign"
}
run_case R02 "pid-файл указывает на чужой живой процесс — он не убивается" case_R02

case_R03() {
    write_config
    sh -c 'exit 0' &
    local dead=$!
    wait "$dead"
    step "в pid-файле pid уже завершившегося процесса ($dead)"
    echo "$dead" > "$PIDFILE"
    start_daemon || return
    expect_true "демон запустился" is_alive "$DAEMON_PID"
}
run_case R03 "pid-файл от упавшего процесса: демон стартует как обычно" case_R03

case_R04() {
    write_config
    step "в pid-файле мусор"
    echo "not-a-pid" > "$PIDFILE"
    start_daemon || return
    expect_eq "pid-файл перезаписан pid демона" "$(daemon_pid)" "$DAEMON_PID"
}
run_case R04 "pid-файл с мусором: демон стартует и перезаписывает его" case_R04

case_R05() {
    write_config
    # Процесс с именем reminder, который игнорирует SIGTERM — «зависший» демон.
    # Скрипт, а не exec sleep: имя процесса берётся из имени исполняемого файла.
    mkdir -p "$TEST_DIR/stuck"
    printf '#!/bin/sh\ntrap "" TERM\nwhile :; do sleep 0.2; done\n' > "$TEST_DIR/stuck/reminder"
    chmod +x "$TEST_DIR/stuck/reminder"
    spawn_helper "$TEST_DIR/stuck/reminder"
    local stuck="$HELPER_PID"
    _poll 2 test "$(cat "/proc/$stuck/comm" 2>/dev/null)" = reminder
    echo "$stuck" > "$PIDFILE"

    step "запуск при «зависшем» экземпляре (pid $stuck) — ждём таймаут 5 с"
    run -C "$TEST_DIR" "$DAEMON" reminder.conf
    expect_ne "код выхода ненулевой" "$RUN_RC" 0
    expect_contains "в stderr объяснение" "$RUN_ERR" "не завершился"
    expect_true "«зависший» процесс жив (не убит SIGKILL)" is_alive "$stuck"
    expect_eq "pid-файл не перезаписан" "$(daemon_pid)" "$stuck"
}
run_case R05 "Старый экземпляр не реагирует на SIGTERM: новый не стартует и сообщает об этом" case_R05

finish
