#!/bin/bash
# QEMU Output Capture Script for HimuOS
# Usage: ./qemu_capture.sh [timeout_seconds] [output_file]
# Optional env: QEMU_CAPTURE_MODE=host|tcg|custom (default: host)

set -u

TIMEOUT=${1:-30}
OUTPUT_FILE=${2:-/tmp/qemu_output.log}
SUDO_PASS=${SUDO_PASS:-${SUDO_PASSWORD:-liuhuan123}}
BUILD_FLAVOR=${BUILD_FLAVOR:-}
HO_DEMO_TEST_NAME=${HO_DEMO_TEST_NAME:-}
HO_DEMO_TEST_DEFINE=${HO_DEMO_TEST_DEFINE:-}
QEMU_DISPLAY=${QEMU_DISPLAY:-none}
QEMU_CAPTURE_MODE=${QEMU_CAPTURE_MODE:-host}
TMP_DIR=$(mktemp -d /tmp/qemu_capture.XXXXXX)
FIFO_PATH="${TMP_DIR}/output.fifo"
RUNNER_PID=""
RUNNER_PGID=""
TEE_PID=""
WATCHDOG_PID=""
TIMED_OUT=0
UNSET_SENTINEL="__HIMUOS_UNSET__"
RUN_QEMU_ACCEL_MODE="$QEMU_CAPTURE_MODE"
RUN_QEMU_ACCEL_ARGS="$UNSET_SENTINEL"
RUN_QEMU_CPU_FLAGS="$UNSET_SENTINEL"

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

mkfifo "$FIFO_PATH"
tee "$OUTPUT_FILE" <"$FIFO_PATH" &
TEE_PID=$!

# Start make/qemu in its own process group so timeout handling can terminate
# the whole tree instead of only the shell or tee process.
setsid bash -lc '
make_args=(
	run
	SUDO_PASSWORD="$1"
	BUILD_FLAVOR="$2"
	HO_DEMO_TEST_NAME="$3"
	HO_DEMO_TEST_DEFINE="$4"
	QEMU_DISPLAY="$5"
	QEMU_ACCEL_MODE="$6"
)
if [ "$7" != "__HIMUOS_UNSET__" ]; then
	make_args+=(QEMU_ACCEL_ARGS="$7")
fi
if [ "$8" != "__HIMUOS_UNSET__" ]; then
	make_args+=(QEMU_CPU_FLAGS="$8")
fi
exec make "${make_args[@]}"
' _ "$SUDO_PASS" "$BUILD_FLAVOR" "$HO_DEMO_TEST_NAME" "$HO_DEMO_TEST_DEFINE" "$QEMU_DISPLAY" "$RUN_QEMU_ACCEL_MODE" "$RUN_QEMU_ACCEL_ARGS" "$RUN_QEMU_CPU_FLAGS" >"$FIFO_PATH" 2>&1 &
RUNNER_PID=$!
RUNNER_PGID=$(ps -o pgid= -p "$RUNNER_PID" | tr -d '[:space:]')

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
wait "$TEE_PID"

if [ "$RUN_STATUS" -eq 143 ] || [ "$RUN_STATUS" -eq 137 ]; then
	TIMED_OUT=1
fi

echo ""
echo "=== QEMU Output Captured ==="
cat "$OUTPUT_FILE"

if [ "$TIMED_OUT" -eq 1 ]; then
	exit 124
fi

exit "$RUN_STATUS"
