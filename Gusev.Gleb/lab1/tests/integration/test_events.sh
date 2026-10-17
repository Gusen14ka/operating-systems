#!/bin/bash
# Срабатывание событий.
#
# Приём «маркер»: в конфиг кладём событие на текущую минуту. Его показ означает,
# что тик уже прошёл, — после этого можно проверять, что лишнего не показано,
# не дожидаясь вслепую.
. "$(dirname "$0")/lib.sh"

suite "Срабатывание напоминаний"

case_E01() {
    write_config "add_event $(now_minute) Событие E01"
    start_daemon || return
    wait_until "напоминание показано (первый тик — сразу при запуске)" 3 \
        shown_count_is "Событие E01" 1 || return
    expect_contains "терминал вызван как '<терминал> -e sh -c <скрипт> sh <текст>'" \
        "$(head -n 1 "$CALLS")" "-e sh -c"
    expect_syslog "показ записан в журнал" "напоминание \(pid окна [0-9]+\): Событие E01"
}
run_case E01 "Событие на текущую минуту показывается сразу после запуска" case_E01

case_E02() {
    write_config "add_event $(now_minute) Событие E02"
    start_daemon || return
    wait_until "напоминание показано" 3 shown_count_is "Событие E02" 1 || return
    step "ждём 11 с — дольше одного периода проверки (10 с)"
    sleep 11
    expect_eq "показано ровно один раз" "$(shown_count "Событие E02")" 1
    expect_true "демон работает" is_alive "$DAEMON_PID"
}
run_case E02 "Разовое событие не повторяется на следующих тиках" case_E02

case_E03() {
    write_config \
        "add_event $(minute_at '+1 day') Завтра" \
        "add_event 2020-01-01 10:00 Давно прошло" \
        "add_event $(minute_at '-5 min') Пять минут назад" \
        "add_event $(now_minute) Маркер"
    start_daemon || return
    wait_until "маркер показан — тик прошёл" 3 shown_count_is "Маркер" 1 || return
    expect_eq "будущее событие не показано" "$(shown_count "Завтра")" 0
    expect_eq "событие 2020 года не показано" "$(shown_count "Давно прошло")" 0
    expect_eq "событие 5 минут назад не показано (запас — 60 с)" "$(shown_count "Пять минут назад")" 0
    expect_syslog "статистика загрузки в журнале" "событий 2, в прошлом пропущено 2"
}
run_case E03 "Будущие и прошедшие разовые события не показываются" case_E03

case_E04() {
    write_config \
        "add_event $(minute_at '-3 hours') -h Каждый час" \
        "add_event $(minute_at '-7 days') -w Каждую неделю" \
        "add_event $(minute_at '-1 day -5 min') -d Каждый день" \
        "add_event $(now_minute) Маркер"
    start_daemon || return
    wait_until "маркер показан — тик прошёл" 3 shown_count_is "Маркер" 1 || return
    expect_eq "-h с началом 3 ч назад догнал текущий час — показан 1 раз" "$(shown_count "Каждый час")" 1
    expect_eq "-w с началом неделю назад — показан 1 раз" "$(shown_count "Каждую неделю")" 1
    expect_eq "-d, время сегодня уже прошло — перенесён на завтра, не показан" \
        "$(shown_count "Каждый день")" 0
}
run_case E04 "Повторяемые события из прошлого переносятся на ближайшее срабатывание" case_E04

case_E05() {
    write_config \
        "add_event 2026-02-31 10:00 Плохая дата" \
        "мусорная строка" \
        "add_events $(now_minute) Опечатка в ключе" \
        "add_event $(now_minute) Маркер"
    start_daemon || return
    wait_until "корректное событие показано" 3 shown_count_is "Маркер" 1 || return
    expect_true "демон работает" is_alive "$DAEMON_PID"
    expect_eq "строка с опечаткой в ключе не показана" "$(shown_count "Опечатка в ключе")" 0
    # Строка 1 конфига — terminal, поэтому плохие строки — 2, 3, 4
    expect_syslog "предупреждение о строке 2 (дата)" "строка 2: некорректные дата/время"
    expect_syslog "предупреждение о строке 3 (мусор)" "строка 3: неизвестная запись"
    expect_syslog "предупреждение о строке 4 (опечатка)" "строка 4: неизвестная запись 'add_events"
}
run_case E05 "Некорректные строки пропускаются с предупреждением, остальное работает" case_E05

case_E06() {
    local text="Сдать \"лабу\" it's \$(touch $TEST_DIR/pwned) \`touch $TEST_DIR/pwned2\` ; | & *"
    write_config "add_event $(now_minute) $text"
    start_daemon || return
    wait_until "напоминание показано с текстом без изменений" 3 shown_count_is "$text" 1 || return
    expect_false "\$(...) из текста не выполнился" file_exists "$TEST_DIR/pwned"
    expect_false "\`...\` из текста не выполнился" file_exists "$TEST_DIR/pwned2"
}
run_case E06 "Спецсимволы в тексте передаются как есть и не исполняются" case_E06

case_E07() {
    local now
    now="$(now_minute)"
    write_config "add_event $now Первое" "add_event $now Второе" "add_event $now Третье" \
        "add_event $now Первое"
    start_daemon || return
    wait_until "показаны все три события" 3 shown_count_is "Третье" 1 || return
    expect_eq "«Первое» показано 1 раз, хотя строка в конфиге дважды" "$(shown_count "Первое")" 1
    expect_eq "«Второе» показано 1 раз" "$(shown_count "Второе")" 1
    expect_eq "всего окон — 3" "$(wc -l < "$SHOWN")" 3
}
run_case E07 "Несколько событий на одну минуту показываются все, дубликаты — один раз" case_E07

finish
