#!/bin/bash
# Скрипт add_event.sh: проверка ввода и запись в конфиг
. "$(dirname "$0")/lib.sh"

suite "Скрипт add_event.sh"

config_lines() { grep -c '' "$CONFIG" 2>/dev/null || echo 0; }
config_has_line() { grep -qxF -- "$1" "$CONFIG"; }

case_A01() {
    : > "$CONFIG"
    add_event -c "$CONFIG" 2026-12-31 23:59 Новый год
    expect_eq "код выхода 0" "$RUN_RC" 0
    expect_true "в конфиг дописана строка" config_has_line "add_event 2026-12-31 23:59 Новый год"
    expect_eq "ровно одна строка" "$(config_lines)" 1
    expect_contains "пользователь видит, что записано" "$RUN_OUT" "Добавлено в $CONFIG"
}
run_case A01 "Событие без флага дописывается одной строкой" case_A01

case_A02() {
    : > "$CONFIG"
    local flag
    for flag in -h -d -w; do
        add_event -c "$CONFIG" 2026-12-31 09:00 "$flag" повтор "$flag"
        expect_true "флаг $flag: строка записана с флагом" \
            config_has_line "add_event 2026-12-31 09:00 $flag повтор $flag"
    done
}
run_case A02 "Флаги повтора -h / -d / -w сохраняются в строке" case_A02

# reject описание фрагмент_ошибки аргументы...
reject() {
    local what="$1" reason="$2" before
    shift 2
    before="$(md5sum < "$CONFIG")"
    add_event -c "$CONFIG" "$@"
    if [ "$RUN_RC" -ne 0 ] && [[ "$RUN_ERR" == *"$reason"* ]] && [ "$(md5sum < "$CONFIG")" = "$before" ]; then
        _pass "отказ: $what"
    else
        _fail "отказ: $what" "аргументы: $(_cmdstr "$@")" \
            "ожидалось: код ≠ 0, в stderr $(_quote "$reason"), конфиг не изменён" \
            "получено:  код $RUN_RC, stderr $(_quote "$RUN_ERR"), конфиг $( [ "$(md5sum < "$CONFIG")" = "$before" ] && echo 'не изменён' || echo 'ИЗМЕНЁН')"
    fi
}

case_A03() {
    echo "add_event 2026-12-31 10:00 существующее" > "$CONFIG"
    reject "31 февраля"               "несуществующая дата" 2026-02-31 10:00 x
    reject "25 часов"                 "несуществующая дата" 2026-10-12 25:00 x
    reject "60 минут"                 "несуществующая дата" 2026-10-12 10:60 x
    reject "дата в формате ДД.ММ.ГГГГ" "ГГГГ-ММ-ДД ЧЧ:ММ"   12.10.2026 10:00 x
    reject "время без ведущего нуля"  "ГГГГ-ММ-ДД ЧЧ:ММ"   2026-10-12 9:00 x
    reject "неизвестный флаг -x"      "неизвестный флаг"    2026-10-12 10:00 -x текст
    reject "нет текста"               "Использование"       2026-10-12 10:00
    reject "только флаг без текста"   "пустой текст"        2026-10-12 10:00 -w
    reject "текст из одних пробелов"  "пустой текст"        2026-10-12 10:00 "   "
    reject "перевод строки в тексте"  "переводов строки"    2026-10-12 10:00 "строка1
строка2"
    expect_eq "в конфиге по-прежнему одна исходная строка" "$(config_lines)" 1
}
run_case A03 "Некорректный ввод отклоняется с понятной причиной, конфиг не меняется" case_A03

case_A04() {
    printf '# последняя строка без перевода строки' > "$CONFIG"
    add_event -c "$CONFIG" 2026-12-31 10:00 Отдельно
    expect_true "новая запись — на отдельной строке" config_has_line "add_event 2026-12-31 10:00 Отдельно"
    expect_true "последняя строка не испорчена" config_has_line "# последняя строка без перевода строки"
}
run_case A04 "Файл без завершающего перевода строки: запись не склеивается" case_A04

case_A05() {
    : > "$CONFIG"
    add_event -c "$CONFIG" 2026-12-31 11:00 Без демона
    expect_eq "код выхода 0" "$RUN_RC" 0
    expect_true "событие записано" config_has_line "add_event 2026-12-31 11:00 Без демона"
    expect_contains "пользователь предупреждён" "$RUN_OUT" "не запущен"
}
run_case A05 "Демон не запущен: событие записано, пользователь предупреждён" case_A05

case_A06() {
    : > "$CONFIG"
    spawn_helper sleep 300  # sleep завершается по SIGHUP — так увидим ошибочный сигнал
    echo "$HELPER_PID" > "$PIDFILE"
    step "в pid-файле чужой процесс (sleep, pid $HELPER_PID)"
    add_event -c "$CONFIG" 2026-12-31 12:00 Чужой pid
    sleep 0.2
    expect_true "SIGHUP чужому процессу не отправлен (он жив)" is_alive "$HELPER_PID"
    expect_contains "сообщено, что демон не запущен" "$RUN_OUT" "не запущен"
}
run_case A06 "pid-файл указывает на чужой процесс: SIGHUP ему не отправляется" case_A06

case_A07() {
    step "конфиг задан переменной REMINDER_CONFIG, без -c"
    run env REMINDER_CONFIG="$TEST_DIR/from-env.conf" sh "$ADD_EVENT_SH" 2026-12-31 13:00 Из переменной
    expect_eq "код выхода 0" "$RUN_RC" 0
    expect_true "событие записано в файл из REMINDER_CONFIG" \
        grep -qxF "add_event 2026-12-31 13:00 Из переменной" "$TEST_DIR/from-env.conf"
}
run_case A07 "Путь к конфигу из переменной REMINDER_CONFIG" case_A07

finish
