#!/bin/bash
# Демонизация и жизненный цикл процесса
. "$(dirname "$0")/lib.sh"

suite "Демонизация и жизненный цикл процесса"

case_D01() {
    write_config
    start_daemon || return
    expect_true "pid-файл создан в \$XDG_RUNTIME_DIR" file_exists "$PIDFILE"
    expect_true "процесс из pid-файла жив" is_alive "$DAEMON_PID"
    expect_eq "имя процесса (/proc/<pid>/comm)" "$(cat "/proc/$DAEMON_PID/comm")" reminder
    expect_eq "запускающая команда вернула управление с кодом 0" "$RUN_RC" 0
    expect_syslog "в журнале сообщение о запуске с абсолютным путём конфига" \
        "запущен, конфиг $CONFIG"
}
run_case D01 "Запуск: демон уходит в фон и записывает свой pid" case_D01

case_D02() {
    write_config
    start_daemon || return
    local pid="$DAEMON_PID"
    step "сверяем /proc/$pid со свойствами демона"
    expect_ne "родитель — не запускавший шелл (pid $$)" "$(proc_field "$pid" 4)" "$$"
    expect_eq "нет управляющего терминала (tty_nr = 0)" "$(proc_field "$pid" 7)" 0
    expect_ne "своя сессия, не сессия теста (setsid)" "$(proc_field "$pid" 6)" "$(proc_field $$ 6)"
    expect_ne "не лидер сессии (второй fork после setsid)" "$(proc_field "$pid" 6)" "$pid"
    expect_eq "рабочий каталог — /" "$(readlink "/proc/$pid/cwd")" /
    local fd
    for fd in 0 1 2; do
        expect_eq "дескриптор $fd перенаправлен в /dev/null" "$(readlink "/proc/$pid/fd/$fd")" /dev/null
    done
    expect_eq "umask сброшен в 0000" "$(awk '/^Umask/ {print $2}' "/proc/$pid/status")" 0000
}
run_case D02 "Отвязка от терминала и сессии (двойной fork, setsid, chdir, /dev/null)" case_D02

case_D03() {
    write_config
    start_daemon || return
    step "отправляем SIGTERM"
    kill -TERM "$DAEMON_PID"
    wait_until "процесс завершился" 5 is_dead "$DAEMON_PID" || return
    expect_false "pid-файл удалён" file_exists "$PIDFILE"
    expect_syslog "в журнале сообщение о выходе" "получен SIGTERM, завершение работы"
}
run_case D03 "SIGTERM: сообщение в журнал, удаление pid-файла, выход" case_D03

case_D04() {
    write_config
    start_daemon || return
    step "отправляем SIGINT (как Ctrl+C)"
    kill -INT "$DAEMON_PID"
    wait_until "процесс завершился" 5 is_dead "$DAEMON_PID" || return
    expect_false "pid-файл удалён" file_exists "$PIDFILE"
}
run_case D04 "SIGINT обрабатывается так же, как SIGTERM" case_D04

case_D05() {
    step "запуск с несуществующим конфигом"
    run -C "$TEST_DIR" "$DAEMON" nonexistent.conf
    expect_ne "код выхода ненулевой" "$RUN_RC" 0
    expect_contains "в stderr назван проблемный файл" "$RUN_ERR" nonexistent.conf
    expect_false "pid-файл не создан" file_exists "$PIDFILE"
    expect_eq "в фоне ничего не осталось" "$(pgrep -fc -- "$DAEMON nonexistent.conf")" 0
}
run_case D05 "Несуществующий конфиг: ошибка в stderr, демон не стартует" case_D05

case_D06() {
    step "reminder --help"
    run "$DAEMON" --help
    expect_eq "код выхода 0" "$RUN_RC" 0
    expect_contains "выведена справка" "$RUN_ERR" "Использование"
    step "reminder a b (лишний аргумент)"
    run "$DAEMON" a b
    expect_ne "код выхода ненулевой" "$RUN_RC" 0
    expect_false "pid-файл не создан" file_exists "$PIDFILE"
}
run_case D06 "Аргументы командной строки: --help и лишние аргументы" case_D06

finish
