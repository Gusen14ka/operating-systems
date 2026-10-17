# shellcheck shell=bash
# Библиотека интеграционных тестов демона reminder. Подключается через `. lib.sh`.
#
# Набор (файл test_*.sh) состоит из кейсов с идентификаторами:
#
#   . "$(dirname "$0")/lib.sh"
#   suite "Название набора"
#
#   case_D01() {
#       write_config "add_event ..."
#       start_daemon || return          # не стартовал — кейс прерывается
#       step "что делаем"
#       expect_eq   "что проверяем" "$actual" "$expected"
#       wait_until  "что ждём" 5 predicate args...
#   }
#   run_case D01 "Что проверяет кейс" case_D01
#
#   finish
#
# Изоляция: каждый кейс получает свой каталог и свой XDG_RUNTIME_DIR, поэтому
# pid-файл тестового демона не пересекается с настоящим демоном пользователя.
# Вместо терминала — заглушка: дописывает текст напоминания в shown.txt,
# а полную командную строку вызова — в terminal-calls.log.
#
# Вывод:
#   консоль                 — кейсы, шаги, проверки (✓ / ✗ / –), итоговая таблица
#   $LOG_DIR/<набор>.log    — то же + временные метки, команды, их stdout/stderr/код
#   $LOG_DIR/<набор>/<ID>/  — артефакты упавшего кейса: конфиг, shown.txt,
#                             состояние процессов, записи демона в журнале
#
# Переменные окружения:
#   REMINDER_TEST_LOG_DIR   куда писать логи (по умолчанию ./test-logs)
#   TEST_CASE="D01 D03"     запустить только перечисленные кейсы
#   NO_COLOR=1              без цветов

set -u

# --- Параметры набора ----------------------------------------------------------

SUITE_NAME="$(basename "$0" .sh)"
DAEMON="$(realpath "${1:?первый аргумент — путь к бинарнику reminder}")"
if [ ! -x "$DAEMON" ]; then
    echo "$SUITE_NAME: $DAEMON не найден или не исполняемый" >&2
    exit 2
fi
ADD_EVENT_SH="$(realpath "$(dirname "$0")/../../add_event.sh")"

LOG_DIR="${REMINDER_TEST_LOG_DIR:-$PWD/test-logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/$SUITE_NAME.log"
ARTIFACT_DIR="$LOG_DIR/$SUITE_NAME"
rm -rf "$ARTIFACT_DIR"
: > "$LOG_FILE"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_OK=$'\033[32m' C_FAIL=$'\033[31m' C_SKIP=$'\033[33m'
    C_DIM=$'\033[2m' C_BOLD=$'\033[1m' C_RST=$'\033[0m'
else
    C_OK="" C_FAIL="" C_SKIP="" C_DIM="" C_BOLD="" C_RST=""
fi

# --- Вывод и лог ----------------------------------------------------------------

_ts() { date '+%H:%M:%S.%3N'; }
_ms() { date '+%s%3N'; }
_fmt_ms() { awk -v ms="$1" 'BEGIN { printf "%.2f с", ms / 1000 }'; }

_log() {
    local level="$1"
    shift
    printf '%s %-6s %s\n' "$(_ts)" "$level" "$*" >> "$LOG_FILE"
}

# Строка в консоль (с цветом) и в лог (без цвета)
_say() {
    local color="$1" level="$2"
    shift 2
    printf '%s%s%s\n' "$color" "$*" "$C_RST"
    _log "$level" "$*"
}

# Многострочный текст — только в лог, с отступом
_log_block() {
    local title="$1" text="$2"
    if [ -z "$text" ]; then
        _log DATA "$title: <пусто>"
        return
    fi
    _log DATA "$title:"
    printf '%s\n' "$text" | sed 's/^/                    | /' >> "$LOG_FILE"
}

# Команда в виде, пригодном для копирования в шелл
_cmdstr() {
    local s
    printf -v s '%q ' "$@"
    printf '%s' "${s% }"
}

_quote() { printf "'%s'" "$1"; }

# --- Состояние --------------------------------------------------------------------

SUITE_START="$(_ms)"
CASE_ID=""
RESULTS=()   # "ID|STATUS|мс|проверок|пропущено|описание"
DAEMON_PIDS=()
HELPER_PIDS=()
SYSLOG_STATE=""

suite() {
    _say "$C_BOLD" SUITE "══ $SUITE_NAME: $1 ══"
    _log INFO "бинарник: $DAEMON"
    _log INFO "система: $(uname -sr), часовой пояс: $(date +%Z), пользователь: $(id -un)"
    _log INFO "DISPLAY=${DISPLAY:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
    [ -z "${TEST_CASE:-}" ] || _log INFO "фильтр кейсов: $TEST_CASE"
}

# --- Жизненный цикл кейса ---------------------------------------------------------

run_case() {
    local id="$1" desc="$2" fn="$3"
    if [ -n "${TEST_CASE:-}" ] && [[ " $TEST_CASE " != *" $id "* ]]; then
        _log INFO "кейс $id пропущен фильтром TEST_CASE"
        return
    fi
    _case_begin "$id" "$desc"
    "$fn"
    _case_end
}

_case_begin() {
    CASE_ID="$1"
    CASE_DESC="$2"
    CASE_START="$(_ms)"
    CASE_EPOCH="$(date +%s)"
    CASE_CHECKS=0
    CASE_FAILS=0
    CASE_SKIPS=0
    DAEMON_PIDS=()
    HELPER_PIDS=()
    DAEMON_PID=""

    TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/reminder-$CASE_ID.XXXXXX")"
    export XDG_RUNTIME_DIR="$TEST_DIR"
    PIDFILE="$TEST_DIR/reminder.pid"
    CONFIG="$TEST_DIR/reminder.conf"
    SHOWN="$TEST_DIR/shown.txt"
    CALLS="$TEST_DIR/terminal-calls.log"
    : > "$SHOWN"

    cat > "$TEST_DIR/fake-terminal" <<EOF
#!/bin/sh
# Заглушка терминала: argv = -e sh -c <скрипт> sh <текст>
printf '%s\n' "\$*" >> '$CALLS'
shift
printf '%s\n' "\$5" >> '$SHOWN'
EOF
    chmod +x "$TEST_DIR/fake-terminal"

    printf '\n' >> "$LOG_FILE"
    _say "$C_BOLD" CASE "▶ $CASE_ID  $CASE_DESC"
    _log INFO "каталог кейса: $TEST_DIR"
}

_case_end() {
    [ -n "$CASE_ID" ] || return 0
    local status=PASS color="$C_OK"
    if [ "$CASE_FAILS" -gt 0 ]; then
        status=FAIL
        color="$C_FAIL"
        _collect_artifacts
    fi
    _stop_processes

    local ms=$(( $(_ms) - CASE_START ))
    local extra=""
    [ "$CASE_SKIPS" -eq 0 ] || extra=", пропущено: $CASE_SKIPS"
    _say "$color" RESULT "  $status  $CASE_ID  (проверок: $CASE_CHECKS$extra, $(_fmt_ms "$ms"))"
    RESULTS+=("$CASE_ID|$status|$ms|$CASE_CHECKS|$CASE_SKIPS|$CASE_DESC")

    [ "$status" = FAIL ] || rm -rf "$TEST_DIR"
    CASE_ID=""
}

# Всё, что кейс запустил, гасим: демонов — SIGTERM (по-честному), остальное — SIGKILL
_stop_processes() {
    local pid
    for pid in "${DAEMON_PIDS[@]}"; do
        if is_alive "$pid"; then
            kill -TERM "$pid" 2>/dev/null
            _poll 3 is_dead "$pid" || kill -KILL "$pid" 2>/dev/null
            _log INFO "уборка: остановлен демон pid $pid"
        fi
    done
    for pid in "${HELPER_PIDS[@]}"; do
        kill -KILL "$pid" 2>/dev/null && _log INFO "уборка: остановлен вспомогательный процесс pid $pid"
        wait "$pid" 2>/dev/null
    done
}

_collect_artifacts() {
    local dst="$ARTIFACT_DIR/$CASE_ID" pid
    mkdir -p "$dst"
    cp -a "$TEST_DIR/." "$dst/" 2>/dev/null

    {
        echo "== процессы reminder в системе"
        ps -o pid,ppid,sid,tty,stat,etime,args -C reminder 2>&1
        echo
        echo "== pid-файл ($PIDFILE)"
        cat "$PIDFILE" 2>&1
        for pid in "${DAEMON_PIDS[@]}"; do
            echo
            echo "== /proc/$pid/status"
            grep -E '^(Name|State|PPid|Umask)' "/proc/$pid/status" 2>/dev/null || echo "процесса нет"
        done
    } > "$dst/processes.txt"

    _syslog_probe
    if [ "$SYSLOG_STATE" = ok ]; then
        daemon_journal > "$dst/journal.txt"
    else
        echo "журнал недоступен: $SYSLOG_STATE" > "$dst/journal.txt"
    fi

    _log_block "конфиг" "$(cat "$CONFIG" 2>/dev/null)"
    _log_block "показанные напоминания (shown.txt)" "$(cat "$SHOWN" 2>/dev/null)"
    _log_block "вызовы терминала" "$(cat "$CALLS" 2>/dev/null)"
    _log_block "процессы" "$(cat "$dst/processes.txt")"
    _log_block "журнал демона" "$(cat "$dst/journal.txt")"
    _say "$C_FAIL" INFO "    артефакты: $dst"
}

finish() {
    _case_end

    local total=0 passed=0 failed=0 skipped=0 row id status ms checks skips desc failed_ids=""
    printf '\n' >> "$LOG_FILE"
    _say "$C_BOLD" SUITE "── итог $SUITE_NAME ──"
    for row in "${RESULTS[@]}"; do
        IFS='|' read -r id status ms checks skips desc <<< "$row"
        total=$((total + 1))
        skipped=$((skipped + skips))
        local color="$C_OK"
        if [ "$status" = FAIL ]; then
            failed=$((failed + 1))
            failed_ids="$failed_ids $id"
            color="$C_FAIL"
        else
            passed=$((passed + 1))
        fi
        local line
        printf -v line '  %-4s %-4s %8s  %s' "$id" "$status" "$(_fmt_ms "$ms")" "$desc"
        _say "$color" SUMMARY "$line"
    done

    local color="$C_OK"
    [ "$failed" -eq 0 ] || color="$C_FAIL"
    local extra=""
    [ "$skipped" -eq 0 ] || extra=", пропущено проверок: $skipped"
    _say "$color" SUMMARY "кейсов: $total, PASS: $passed, FAIL: $failed$extra, время: $(_fmt_ms $(( $(_ms) - SUITE_START )))"
    if [ "$failed" -gt 0 ]; then
        _say "$C_FAIL" SUMMARY "упали:$failed_ids"
        _say "$C_DIM" SUMMARY "повторить: TEST_CASE=\"${failed_ids# }\" bash $0 $DAEMON"
    fi
    if [ "$skipped" -gt 0 ]; then
        _say "$C_SKIP" SUMMARY "проверки журнала пропущены: $SYSLOG_STATE"
    fi
    _say "$C_DIM" SUMMARY "подробный лог: $LOG_FILE"

    trap - EXIT
    [ "$total" -gt 0 ] || exit 1
    exit $(( failed > 0 ))
}

# Прерывание (Ctrl+C, таймаут ctest): закрыть кейс как упавший и убрать процессы
_on_interrupt() {
    trap - INT TERM
    if [ -n "$CASE_ID" ]; then
        _fail "кейс прерван сигналом"
    fi
    finish
}
trap _on_interrupt INT TERM
trap '[ -z "$CASE_ID" ] || { _fail "набор завершился посреди кейса"; _case_end; }' EXIT

# --- Шаги и проверки --------------------------------------------------------------

step() { _say "$C_DIM" STEP "    · $*"; }

_pass() {
    CASE_CHECKS=$((CASE_CHECKS + 1))
    _say "$C_OK" PASS "    ✓ $1"
}

# _fail "что проверяли" ["подробность" ...]
_fail() {
    local what="$1" detail
    shift
    CASE_CHECKS=$((CASE_CHECKS + 1))
    CASE_FAILS=$((CASE_FAILS + 1))
    _say "$C_FAIL" FAIL "    ✗ $what"
    for detail in "$@"; do
        _say "$C_FAIL" FAIL "        $detail"
    done
}

skip_check() {
    CASE_SKIPS=$((CASE_SKIPS + 1))
    _say "$C_SKIP" SKIP "    – $1 (пропущено: $2)"
}

expect_eq() {  # описание фактическое ожидаемое
    if [ "$2" = "$3" ]; then
        _pass "$1"
    else
        _fail "$1" "ожидалось: $(_quote "$3")" "получено:  $(_quote "$2")"
    fi
}

expect_ne() {  # описание фактическое нежелательное
    if [ "$2" != "$3" ]; then
        _pass "$1"
    else
        _fail "$1" "значение не должно быть $(_quote "$3"), но оно такое"
    fi
}

expect_true() {  # описание команда...
    local what="$1"
    shift
    if "$@"; then
        _pass "$what"
    else
        _fail "$what" "условие ложно: $(_cmdstr "$@")"
    fi
}

expect_false() {  # описание команда...
    local what="$1"
    shift
    if ! "$@"; then
        _pass "$what"
    else
        _fail "$what" "условие должно быть ложным: $(_cmdstr "$@")"
    fi
}

expect_contains() {  # описание текст подстрока
    if [[ "$2" == *"$3"* ]]; then
        _pass "$1"
    else
        _fail "$1" "ожидалась подстрока: $(_quote "$3")" "в тексте:           $(_quote "$2")"
    fi
}

# Тихо ждать условия: _poll <таймаут, с> команда...
_poll() {
    local timeout="$1"
    shift
    local deadline=$(( $(_ms) + timeout * 1000 ))
    until "$@"; do
        [ "$(_ms)" -lt "$deadline" ] || return 1
        sleep 0.1
    done
}

# Ждать условия с отчётом и временем ожидания: wait_until описание таймаут команда...
wait_until() {
    local what="$1" timeout="$2"
    shift 2
    local start
    start="$(_ms)"
    if _poll "$timeout" "$@"; then
        _pass "$what (через $(_fmt_ms $(( $(_ms) - start ))))"
        return 0
    fi
    _fail "$what" "не выполнилось за $timeout с: $(_cmdstr "$@")"
    return 1
}

# Выполнить команду, сохранив stdout/stderr/код в RUN_OUT/RUN_ERR/RUN_RC и в лог.
#   run [-C каталог] команда...
run() {
    local dir=""
    if [ "$1" = "-C" ]; then
        dir="$2"
        shift 2
    fi
    # Вывод — в файлы, а не через $(...): иначе захват ждал бы закрытия
    # всех копий дескриптора, включая унаследованные демоном до dup2
    if [ -n "$dir" ]; then
        (cd "$dir" && "$@") > "$TEST_DIR/.run.out" 2> "$TEST_DIR/.run.err"
    else
        "$@" > "$TEST_DIR/.run.out" 2> "$TEST_DIR/.run.err"
    fi
    RUN_RC=$?
    RUN_OUT="$(cat "$TEST_DIR/.run.out")"
    RUN_ERR="$(cat "$TEST_DIR/.run.err")"
    _log CMD "\$ ${dir:+(cd $dir) }$(_cmdstr "$@")  → код $RUN_RC"
    [ -z "$RUN_OUT" ] || _log_block "stdout" "$RUN_OUT"
    [ -z "$RUN_ERR" ] || _log_block "stderr" "$RUN_ERR"
}

# --- Демон и окружение ----------------------------------------------------------

# write_config "строка" ... — конфиг с заглушкой терминала и переданными строками
write_config() {
    printf 'terminal = %s -e\n' "$TEST_DIR/fake-terminal" > "$CONFIG"
    local line
    for line in "$@"; do
        printf '%s\n' "$line" >> "$CONFIG"
    done
    _log_block "конфиг записан" "$(cat "$CONFIG")"
}

# start_daemon [аргумент-конфиг] — запуск из каталога кейса, ожидание нового pid
start_daemon() {
    local cfg="${1:-reminder.conf}" prev
    prev="$(daemon_pid)"
    step "запуск: reminder $cfg"
    run -C "$TEST_DIR" "$DAEMON" "$cfg"
    if [ "$RUN_RC" -ne 0 ]; then
        _fail "демон запустился" "код выхода $RUN_RC" "stderr: $(_quote "$RUN_ERR")"
        return 1
    fi
    if ! _poll 5 _new_daemon_ready "$prev"; then
        _fail "демон запустился" "за 5 с в $PIDFILE не появился pid нового живого процесса" \
            "в pid-файле: $(_quote "$(daemon_pid)")"
        return 1
    fi
    DAEMON_PID="$(daemon_pid)"
    DAEMON_PIDS+=("$DAEMON_PID")
    _log INFO "демон запущен, pid $DAEMON_PID"
}

_new_daemon_ready() {
    local pid
    pid="$(daemon_pid)"
    [ -n "$pid" ] && [ "$pid" != "$1" ] && is_alive "$pid"
}

# Вспомогательный фоновый процесс, который кейс уберёт за собой
spawn_helper() {
    "$@" &
    HELPER_PID=$!
    HELPER_PIDS+=("$HELPER_PID")
    _log INFO "вспомогательный процесс pid $HELPER_PID: $(_cmdstr "$@")"
}

add_event() { run sh "$ADD_EVENT_SH" "$@"; }

# --- Предикаты --------------------------------------------------------------------

daemon_pid() { cat "$PIDFILE" 2>/dev/null; }

# Поле N из /proc/<pid>/stat (нумерация как в man proc: 3 — состояние, 4 — ppid ...).
# Имя процесса в скобках может содержать пробелы, поэтому режем по последней ')'.
proc_field() { sed -E 's/^.*\) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f$(( $2 - 2 )); }

is_alive() {
    local state
    state="$(proc_field "$1" 3)"
    [ -n "$state" ] && [ "$state" != Z ]
}
is_dead() { ! is_alive "$1"; }

file_exists() { [ -e "$1" ]; }

shown_count() { grep -cxF -- "$1" "$SHOWN"; }
shown_count_is() { [ "$(shown_count "$1")" -eq "$2" ]; }

now_minute() { date '+%Y-%m-%d %H:%M'; }
minute_at() { date -d "$1" '+%Y-%m-%d %H:%M'; }

# --- Системный журнал ---------------------------------------------------------------
# Проверки журнала выполняются, только если тест может читать свои же записи:
# пробная запись через logger должна найтись в journalctl. Иначе — SKIP с причиной.

_syslog_probe() {
    [ -z "$SYSLOG_STATE" ] || return 0
    if ! command -v journalctl >/dev/null 2>&1 || ! command -v logger >/dev/null 2>&1; then
        SYSLOG_STATE="нет journalctl/logger"
    else
        local tag="reminder-test-probe-$$"
        logger -t "$tag" "probe" 2>/dev/null
        if _poll 3 _journal_has_probe "$tag"; then
            SYSLOG_STATE=ok
        else
            SYSLOG_STATE="тест не видит своих записей в journalctl (нет syslog или прав на чтение журнала: группа adm / systemd-journal)"
        fi
    fi
    _log INFO "доступ к журналу: $SYSLOG_STATE"
}

_journal_has_probe() {
    journalctl -q --no-pager -t "$1" --since "-5min" -o cat 2>/dev/null | grep -q probe
}

# Записи демонов текущего кейса (фильтр по их pid)
daemon_journal() {
    [ "${#DAEMON_PIDS[@]}" -gt 0 ] || return 0
    local pids
    pids="$(IFS='|'; echo "${DAEMON_PIDS[*]}")"
    journalctl -q --no-pager -t reminder --since "@$CASE_EPOCH" -o short-iso 2>/dev/null |
        grep -E "reminder\[($pids)\]:"
}

_journal_matches() { daemon_journal | grep -qE -- "$1"; }

# expect_syslog описание регулярное_выражение [таймаут]
expect_syslog() {
    _syslog_probe
    if [ "$SYSLOG_STATE" != ok ]; then
        skip_check "$1" "журнал недоступен"
        return
    fi
    if wait_until "$1" "${3:-5}" _journal_matches "$2"; then
        _log_block "найдено в журнале" "$(daemon_journal | grep -E -- "$2")"
    else
        _log_block "записи демона в журнале" "$(daemon_journal)"
    fi
}
