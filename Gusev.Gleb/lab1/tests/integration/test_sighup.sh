#!/bin/bash
# Перечитывание конфига по SIGHUP и скрипт add_event.sh
. "$(dirname "$0")/lib.sh"

suite "SIGHUP: перечитывание конфигурационного файла"

case_H01() {
    write_config
    start_daemon || return
    step "add_event.sh дописывает событие на текущую минуту"
    add_event -c "$CONFIG" $(now_minute) Событие H01
    expect_eq "скрипт завершился с кодом 0" "$RUN_RC" 0
    expect_contains "скрипт нашёл демона и отправил SIGHUP" "$RUN_OUT" "Демон (pid $DAEMON_PID)"
    wait_until "напоминание показано" 3 shown_count_is "Событие H01" 1
    expect_eq "pid не изменился — перечитал, а не перезапустился" "$(daemon_pid)" "$DAEMON_PID"
    expect_syslog "в журнале сообщение о SIGHUP" "получен SIGHUP, перечитываю конфиг"
}
run_case H01 "add_event.sh: событие подхватывается без перезапуска демона" case_H01

case_H02() {
    write_config "add_event $(now_minute) Уже показано"
    start_daemon || return
    wait_until "событие показано" 3 shown_count_is "Уже показано" 1 || return
    step "дописываем маркер и шлём SIGHUP"
    echo "add_event $(now_minute) Маркер" >> "$CONFIG"
    kill -HUP "$DAEMON_PID"
    wait_until "маркер показан — конфиг перечитан, тик прошёл" 3 shown_count_is "Маркер" 1 || return
    expect_eq "ранее показанное событие не повторилось" "$(shown_count "Уже показано")" 1
    expect_syslog "в журнале «уже показано 1»" "уже показано 1"
}
run_case H02 "После SIGHUP уже показанное событие не повторяется" case_H02

case_H03() {
    write_config
    start_daemon "$CONFIG" || return  # абсолютный путь
    step "правим конфиг вручную и шлём kill -HUP"
    echo "add_event $(now_minute) Вручную" >> "$CONFIG"
    kill -HUP "$DAEMON_PID"
    wait_until "напоминание показано" 3 shown_count_is "Вручную" 1
}
run_case H03 "Ручная правка конфига + kill -HUP" case_H03

case_H04() {
    write_config
    start_daemon reminder.conf || return  # относительный путь из каталога кейса
    expect_eq "рабочий каталог демона — / (относительный путь уже не сработал бы)" \
        "$(readlink "/proc/$DAEMON_PID/cwd")" /
    step "дописываем событие и шлём SIGHUP"
    echo "add_event $(now_minute) Относительный путь" >> "$CONFIG"
    kill -HUP "$DAEMON_PID"
    wait_until "конфиг найден по запомненному абсолютному пути — событие показано" 3 \
        shown_count_is "Относительный путь" 1
}
run_case H04 "Конфиг, заданный относительным путём, перечитывается после chdir(\"/\")" case_H04

case_H05() {
    write_config "add_event $(minute_at '+1 day') Завтра"
    start_daemon || return
    step "удаляем конфиг и шлём SIGHUP"
    rm -f "$CONFIG"
    kill -HUP "$DAEMON_PID"
    sleep 0.5
    expect_true "демон работает" is_alive "$DAEMON_PID"
    expect_eq "pid не изменился" "$(daemon_pid)" "$DAEMON_PID"
    expect_syslog "в журнале ошибка чтения конфига" "не удалось прочитать конфиг"
    step "возвращаем конфиг с событием на сейчас и шлём SIGHUP"
    write_config "add_event $(now_minute) Конфиг вернулся"
    kill -HUP "$DAEMON_PID"
    wait_until "после восстановления конфига демон снова работает с ним" 3 \
        shown_count_is "Конфиг вернулся" 1
}
run_case H05 "Конфиг удалён: SIGHUP не роняет демон, после восстановления всё работает" case_H05

case_H06() {
    local i lost=0 pid
    step "10 раз: запуск → SIGHUP сразу, как только pid появился в pid-файле"
    for i in $(seq 1 10); do
        write_config
        start_daemon || return
        pid="$DAEMON_PID"
        echo "add_event $(now_minute) Гонка $i" >> "$CONFIG"
        kill -HUP "$pid"
        if ! _poll 3 shown_count_is "Гонка $i" 1; then
            lost=$((lost + 1))
            _log INFO "попытка $i: SIGHUP потерян (демон жив: $(is_alive "$pid" && echo да || echo нет))"
        fi
        kill -TERM "$pid"
        _poll 3 is_dead "$pid"
    done
    expect_eq "ни один SIGHUP не потерян и не убил демон" "$lost" 0
}
run_case H06 "Стресс: SIGHUP сразу после появления pid-файла не теряется (10 запусков)" case_H06

finish
