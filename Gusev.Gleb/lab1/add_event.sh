#!/bin/sh
# Добавить напоминание в конфиг демона reminder и попросить его перечитать конфиг.
#
# Использование:
#   ./add_event.sh [-c конфиг] ГГГГ-ММ-ДД ЧЧ:ММ [-h|-d|-w] текст напоминания
#
#   -h / -d / -w — повторять каждый час / день / неделю
#   -c конфиг    — путь к конфигу (по умолчанию $REMINDER_CONFIG или
#                  reminder.conf рядом со скриптом)
#
# Примеры:
#   ./add_event.sh 2026-10-12 18:30 Сдать лабу по ОС
#   ./add_event.sh 2026-10-12 09:00 -d Зарядка
set -eu

usage() {
    sed -n '2,13s/^# \{0,1\}//p' "$0" >&2
    exit 1
}

die() {
    echo "add_event: $*" >&2
    exit 1
}

CONFIG="${REMINDER_CONFIG:-$(dirname "$0")/reminder.conf}"
if [ "${1:-}" = "-c" ]; then
    [ $# -ge 2 ] || usage
    CONFIG="$2"
    shift 2
fi

[ $# -ge 3 ] || usage
DATE="$1"
TIME="$2"
shift 2

FLAG=""
case "$1" in
    -h|-d|-w) FLAG="$1"; shift ;;
    -[a-zA-Z]) die "неизвестный флаг '$1', допустимы -h -d -w" ;;
esac

[ $# -ge 1 ] || die "пустой текст напоминания"
TEXT="$*"

# Перевод строки в тексте сломал бы конфиг: одна строка — одно событие
case "$TEXT" in
    *"
"*) die "текст не должен содержать переводов строки" ;;
esac
[ -n "$(printf '%s' "$TEXT" | tr -d ' \t')" ] || die "пустой текст напоминания"

# Проверка даты: формат по шаблону, а календарь — через date.
# date -d нормализует 2026-02-31 в 2026-03-03, поэтому сравниваем с вводом.
echo "$DATE $TIME" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}$' \
    || die "дата и время должны быть в формате ГГГГ-ММ-ДД ЧЧ:ММ"
NORMALIZED="$(date -d "$DATE $TIME" '+%Y-%m-%d %H:%M' 2>/dev/null)" || NORMALIZED=""
[ "$NORMALIZED" = "$DATE $TIME" ] || die "несуществующая дата или время '$DATE $TIME'"

if [ -n "$FLAG" ]; then
    LINE="add_event $DATE $TIME $FLAG $TEXT"
else
    LINE="add_event $DATE $TIME $TEXT"
fi

# Дописываем строку. Если файл не кончается переводом строки — сначала добавим его,
# иначе новая запись склеится с последней строкой.
if [ -s "$CONFIG" ] && [ "$(tail -c 1 "$CONFIG" | od -An -c | tr -d ' ')" != '\n' ]; then
    echo >> "$CONFIG"
fi
printf '%s\n' "$LINE" >> "$CONFIG" || die "не удалось записать в $CONFIG"
echo "Добавлено в $CONFIG: $LINE"

# Сообщить демону. pid-файл ищем там же, где его создаёт демон.
PIDFILE="${XDG_RUNTIME_DIR:-/tmp}/reminder.pid"

if [ -r "$PIDFILE" ]; then
    PID="$(cat "$PIDFILE")"
    # Тот же контроль, что в демоне: процесс жив и это именно reminder
    if [ "$(cat "/proc/$PID/comm" 2>/dev/null)" = "reminder" ]; then
        kill -HUP "$PID" && echo "Демон (pid $PID) перечитывает конфиг"
        exit 0
    fi
fi
echo "Демон не запущен — событие подхватится при запуске"
