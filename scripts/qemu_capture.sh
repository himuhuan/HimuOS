#!/bin/bash
# QEMU Output Capture Script for HimuOS
# Usage: ./qemu_capture.sh [timeout_seconds] [output_file]
# Optional env: QEMU_CAPTURE_MODE=host|tcg|custom (default: host)
# Optional env: QEMU_CAPTURE_EXIT_ON=<substring> to stop QEMU early once the
#               capture log shows a success anchor.

set -u

TIMEOUT=${1:-30}
OUTPUT_FILE=${2:-/tmp/qemu_output.log}
SUDO_PASS=${SUDO_PASS:-${SUDO_PASSWORD:-liuhuan123}}
BUILD_FLAVOR=${BUILD_FLAVOR:-}
HO_DEMO_TEST_NAME=${HO_DEMO_TEST_NAME:-}
HO_DEMO_TEST_DEFINE=${HO_DEMO_TEST_DEFINE:-}
QEMU_DISPLAY=${QEMU_DISPLAY:-none}
QEMU_CAPTURE_MODE=${QEMU_CAPTURE_MODE:-host}
QEMU_SENDKEY_PLAN=${QEMU_SENDKEY_PLAN:-}
QEMU_CAPTURE_EXIT_ON=${QEMU_CAPTURE_EXIT_ON:-}
TMP_DIR=$(mktemp -d /tmp/qemu_capture.XXXXXX)
FIFO_PATH="${TMP_DIR}/output.fifo"
MONITOR_SOCKET=""
RUNNER_PID=""
RUNNER_PGID=""
TEE_PID=""
WATCHDOG_PID=""
SENDKEY_PID=""
EXIT_ON_WATCH_PID=""
TIMED_OUT=0
SENDKEY_STATUS=0
EXIT_ON_MATCHED=0
UNSET_SENTINEL="__HIMUOS_UNSET__"
RUN_QEMU_ACCEL_MODE="$QEMU_CAPTURE_MODE"
RUN_QEMU_ACCEL_ARGS="$UNSET_SENTINEL"
RUN_QEMU_CPU_FLAGS="$UNSET_SENTINEL"
EXIT_ON_SENTINEL="${TMP_DIR}/exit-on.matched"

case "$QEMU_CAPTURE_MODE" in
	host|tcg)
		;;
	custom)
		if [ "${QEMU_ACCEL_ARGS+x}" = x ]; then
			RUN_QEMU_ACCEL_ARGS="$QEMU_ACCEL_ARGS"
		fi
		if [ "${QEMU_CPU_FLAGS+x}" = x ]; then
			RUN_QEMU_CPU_FLAGS="$QEMU_CPU_FLAGS"
		fi
		;;
	*)
		echo "Unsupported QEMU_CAPTURE_MODE=${QEMU_CAPTURE_MODE}. Use host, tcg, or custom." >&2
		exit 2
		;;
esac

cleanup() {
	rm -f "$FIFO_PATH"
	if [ -n "$MONITOR_SOCKET" ]; then
		rm -f "$MONITOR_SOCKET"
	fi
	rmdir "$TMP_DIR" 2>/dev/null || true
}

kill_runner_group() {
	local signal=$1

	if [ -n "$RUNNER_PGID" ] && kill -0 "$RUNNER_PID" 2>/dev/null; then
		kill "-${signal}" -- "-$RUNNER_PGID" 2>/dev/null || true
	fi
}

trap cleanup EXIT
trap 'kill_runner_group TERM; sleep 1; kill_runner_group KILL; exit 130' INT TERM

echo "Starting QEMU with ${TIMEOUT}s timeout..."
echo "Output will be saved to: ${OUTPUT_FILE}"
if [ -n "$BUILD_FLAVOR" ]; then
	echo "Using BUILD_FLAVOR=${BUILD_FLAVOR}"
fi
if [ -n "$HO_DEMO_TEST_NAME" ] && [ -n "$HO_DEMO_TEST_DEFINE" ]; then
	echo "Using demo profile ${HO_DEMO_TEST_NAME} (${HO_DEMO_TEST_DEFINE})"
fi
echo "Using QEMU_DISPLAY=${QEMU_DISPLAY}"
echo "Using QEMU_CAPTURE_MODE=${QEMU_CAPTURE_MODE}"
if [ "$QEMU_CAPTURE_MODE" = "custom" ]; then
	if [ "$RUN_QEMU_ACCEL_ARGS" != "$UNSET_SENTINEL" ]; then
		echo "Using custom QEMU_ACCEL_ARGS=${RUN_QEMU_ACCEL_ARGS}"
	else
		echo "Using custom QEMU_ACCEL_ARGS=<makefile default>"
	fi
	if [ "$RUN_QEMU_CPU_FLAGS" != "$UNSET_SENTINEL" ]; then
		echo "Using custom QEMU_CPU_FLAGS=${RUN_QEMU_CPU_FLAGS}"
	else
		echo "Using custom QEMU_CPU_FLAGS=<makefile default>"
	fi
fi
if [ -n "$QEMU_SENDKEY_PLAN" ]; then
	echo "Using QEMU_SENDKEY_PLAN=${QEMU_SENDKEY_PLAN}"
	MONITOR_SOCKET="${TMP_DIR}/monitor.sock"
fi
if [ -n "$QEMU_CAPTURE_EXIT_ON" ]; then
	echo "Using QEMU_CAPTURE_EXIT_ON=${QEMU_CAPTURE_EXIT_ON}"
fi

mkfifo "$FIFO_PATH"
tee "$OUTPUT_FILE" <"$FIFO_PATH" &
TEE_PID=$!

# Start make/qemu in its own process group so timeout handling can terminate
# the whole tree instead of only the shell or tee process.
setsid bash -lc '
make_args=(
	run
	SUDO_PASSWORD="$1"
	QEMU_DISPLAY="$5"
	QEMU_ACCEL_MODE="$6"
	QEMU_MONITOR_SOCKET="$9"
)
if [ -n "$2" ]; then
	make_args+=(BUILD_FLAVOR="$2")
fi
if [ -n "$3" ]; then
	make_args+=(HO_DEMO_TEST_NAME="$3")
fi
if [ -n "$4" ]; then
	make_args+=(HO_DEMO_TEST_DEFINE="$4")
fi
if [ "$7" != "__HIMUOS_UNSET__" ]; then
	make_args+=(QEMU_ACCEL_ARGS="$7")
fi
if [ "$8" != "__HIMUOS_UNSET__" ]; then
	make_args+=(QEMU_CPU_FLAGS="$8")
fi
exec make "${make_args[@]}"
' _ "$SUDO_PASS" "$BUILD_FLAVOR" "$HO_DEMO_TEST_NAME" "$HO_DEMO_TEST_DEFINE" "$QEMU_DISPLAY" "$RUN_QEMU_ACCEL_MODE" "$RUN_QEMU_ACCEL_ARGS" "$RUN_QEMU_CPU_FLAGS" "$MONITOR_SOCKET" >"$FIFO_PATH" 2>&1 &
RUNNER_PID=$!
RUNNER_PGID=$(ps -o pgid= -p "$RUNNER_PID" | tr -d '[:space:]')

if [ -n "$QEMU_SENDKEY_PLAN" ]; then
	if [ -n "$SUDO_PASS" ]; then
		printf '%s\n' "$SUDO_PASS" | sudo -S -p '' python3 scripts/qemu_sendkeys.py \
			--socket "$MONITOR_SOCKET" \
			--log "$OUTPUT_FILE" \
			--plan "$QEMU_SENDKEY_PLAN" &
	else
		sudo -p '' python3 scripts/qemu_sendkeys.py \
			--socket "$MONITOR_SOCKET" \
			--log "$OUTPUT_FILE" \
			--plan "$QEMU_SENDKEY_PLAN" &
	fi
	SENDKEY_PID=$!
fi

if [ -n "$QEMU_CAPTURE_EXIT_ON" ]; then
	(
		while kill -0 "$RUNNER_PID" 2>/dev/null; do
			if [ -f "$OUTPUT_FILE" ] && grep -Fq "$QEMU_CAPTURE_EXIT_ON" "$OUTPUT_FILE"; then
				: >"$EXIT_ON_SENTINEL"
				echo ""
				echo "QEMU exit pattern observed, terminating process group ${RUNNER_PGID}..."
				kill_runner_group TERM
				sleep 2
				kill_runner_group KILL
				exit 0
			fi

			sleep 0.1
		done
	) &
	EXIT_ON_WATCH_PID=$!
fi

(
	sleep "$TIMEOUT"
	if kill -0 "$RUNNER_PID" 2>/dev/null; then
		TIMED_OUT=1
		echo ""
		echo "QEMU exceeded ${TIMEOUT}s, terminating process group ${RUNNER_PGID}..."
		kill_runner_group TERM
		sleep 5
		kill_runner_group KILL
	fi
) &
WATCHDOG_PID=$!

wait "$RUNNER_PID"
RUN_STATUS=$?

kill "$WATCHDOG_PID" 2>/dev/null || true
wait "$WATCHDOG_PID" 2>/dev/null || true
if [ -n "$EXIT_ON_WATCH_PID" ]; then
	kill "$EXIT_ON_WATCH_PID" 2>/dev/null || true
	wait "$EXIT_ON_WATCH_PID" 2>/dev/null || true
fi
wait "$TEE_PID"

if [ -n "$SENDKEY_PID" ]; then
	wait "$SENDKEY_PID"
	SENDKEY_STATUS=$?
fi

if [ "$RUN_STATUS" -eq 143 ] || [ "$RUN_STATUS" -eq 137 ]; then
	TIMED_OUT=1
fi

if [ -f "$EXIT_ON_SENTINEL" ]; then
	EXIT_ON_MATCHED=1
	TIMED_OUT=0
	RUN_STATUS=0
fi

echo ""
echo "=== QEMU Output Captured ==="
cat "$OUTPUT_FILE"

if [ "$SENDKEY_STATUS" -ne 0 ]; then
	exit "$SENDKEY_STATUS"
fi

if [ "$TIMED_OUT" -eq 1 ]; then
	exit 124
fi

exit "$RUN_STATUS"
