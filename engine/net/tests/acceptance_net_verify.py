#!/usr/bin/env python3
"""Acceptance gate for fr-net-005/fr-net-009/fr-net-010 (tester-owned, NOT a unit test).

End-to-end driver: configures + builds engine/net in a temp dir, runs the
full ctest suite, asserts the per-AC check names are present and green,
exercises tools/net_codegen/codegen.py for .proto/.fbs/.json schemas, and
asserts the device-lab external-gate READMEs state their procedures+gates.

Usage:  python3 engine/net/tests/acceptance_net_verify.py   (from checkout root)
Exit 0 = all acceptance gates hold; non-zero = failure with reason.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__) + "/..")))
# ROOT resolves to <checkout>/engine; checkout root is one level up.
CHECKOUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def run(cmd, cwd=CHECKOUT, timeout=300):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    return p


# Per-AC required check names (must appear as PASS in binary output).
REQUIRED = {
    # AC-NET-008 (FR-NET-005): transitions survive + reactive signals.
    "webtransport_test": [
        "check_migration_emits_migrating_then_connected_in_order",
        "check_inflight_across_flap_neither_lost_nor_duplicated",
        "check_duplicate_redelivery_deduped",
    ],
    "webrtc_session_test": [
        "check_flap_yields_connected_reconnecting_connected",
        "check_reactive_signal_per_transition_exact_strings",
        "check_second_flap_recovers_again",
    ],
    "websocket_test": [
        "check_ordered_text_binary_echo_deflate_on",
        "check_deflate_off_matches_deflate_on_bytes",
        "check_close_emits_kClosed_with_reason",
    ],
    # AC-NET-017 (FR-NET-009): gRPC interop + subscriptions/APQ/batching.
    "grpc_interop_test": [
        "check_unary_golden_interop_byte_identical",
        "check_server_stream_delivers_3_messages_in_order",
        "check_n_concurrent_streams_share_one_pooled_connection",
        "check_trailer_error_surfaces_non_ok_never_throws",
    ],
    "graphql_subscription_test": [
        "check_subscription_delivers_ordered_updates",
        "check_resume_replays_only_missed_in_order_no_dupes",
        "check_resume_with_non_numeric_ids_dedupes_by_string_equality",
        "check_apq_sends_hash_first_then_full_on_not_found",
        "check_batching_coalesces_two_queries_into_one_call",
        "check_query_ignores_batching_single_call_unchanged",
    ],
    # AC-NET-018/019/020 (FR-NET-010): fuzz, zero-copy, round-trips.
    "codec_fuzz_test": [
        "fuzz_all_codecs_structured_errors_only",
        "fuzz_envelope_parser_rejects_before_interpreting",
    ],
    "zero_copy_payload_test": [
        "check_envelope_shape",
        "check_consume_in_place_zero_copy",
        "check_consume_in_place_zero_allocs_post_setup",
        "check_ring_consume_after_eviction_is_empty",
    ],
    "codec_roundtrip_test": [
        "all_five_ids_round_trip",
        "malformed_yields_structured_errors_never_throws",
        "check_encode_malformed_returns_empty_for_all_codecs",
        "check_get_bad_id_throws_out_of_range",
        "codegen_black_box_golden",
    ],
    # Required-dep seams: transport-agnostic API behaviour.
    "transport_api_test": [
        "check_same_sequence_through_two_transports_is_identical",
        "check_setTransport_swaps_mid_lifetime",
    ],
    "rtc_harness_test": [
        "check_same_sequence_through_two_transports_is_identical",
        "check_setTransport_swaps_mid_lifetime",
    ],
}

failures = []


def main():
    build_dir = tempfile.mkdtemp(prefix="accept-net-")
    try:
        print(f"[accept] configuring+building in {build_dir}")
        p = run(["cmake", "-S", "engine/net", "-B", build_dir])
        if p.returncode != 0:
            print(p.stdout[-3000:] + p.stderr[-3000:])
            return fail("cmake configure failed")
        p = run(["cmake", "--build", build_dir])
        if p.returncode != 0:
            print(p.stdout[-3000:] + p.stderr[-3000:])
            return fail("cmake build failed")

        print("[accept] running full ctest suite")
        p = run(["ctest", "--test-dir", build_dir])
        print("\n".join(p.stdout.strip().splitlines()[-4:]))
        if p.returncode != 0 or "0 tests failed" not in p.stdout and "tests passed" not in p.stdout:
            if p.returncode != 0:
                print(p.stdout[-3000:])
                return fail("ctest suite failed")

        # Per-binary per-AC check assertions.
        for binary, checks in REQUIRED.items():
            exe = os.path.join(build_dir, binary)
            if not os.path.isfile(exe):
                return fail(f"missing test binary: {binary}")
            p = run([exe])
            if p.returncode != 0:
                print(p.stdout[-2000:])
                return fail(f"{binary} exited non-zero")
            for c in checks:
                if f"PASS {c}" not in p.stdout:
                    return fail(f"{binary}: required check missing/not PASS: {c}")
            print(f"[accept] {binary}: {len(checks)} AC checks PASS")

        # Codegen pipeline end-to-end for all three schema kinds.
        cg = os.path.join(CHECKOUT, "tools", "net_codegen", "codegen.py")
        for ext, body in [
            ("proto", 'syntax="proto3"; message Hi { string name = 1; }'),
            ("fbs", "table Hi { name:string; } root_type Hi;"),
            ("json", '{"messages": [{"name": "Hi", "fields": [{"name": "name", "type": "string"}]}]}'),
        ]:
            d = tempfile.mkdtemp(prefix="accept-cg-")
            try:
                schema = os.path.join(d, f"s.{ext}")
                with open(schema, "w") as f:
                    f.write(body)
                p = run([sys.executable, cg, "--schema", schema,
                         "--out-cpp", os.path.join(d, "cpp"),
                         "--out-ts", os.path.join(d, "ts")])
                if p.returncode != 0:
                    print(p.stdout[-1000:] + p.stderr[-1000:])
                    return fail(f"codegen.py failed for .{ext}")
                if not (os.path.isdir(os.path.join(d, "cpp")) and os.path.isdir(os.path.join(d, "ts"))):
                    return fail(f"codegen.py emitted no bindings for .{ext}")
                print(f"[accept] codegen .{ext}: bindings emitted")
            finally:
                shutil.rmtree(d, ignore_errors=True)

        # Device-lab external gates must name procedure + pass gate.
        for lab, marker in [
            ("device-lab/rtc-transitions/README.md", "TC-NET-008"),
            ("device-lab/rpc-interop/README.md", "TC-NET-017"),
        ]:
            path = os.path.join(CHECKOUT, lab)
            if not os.path.isfile(path):
                return fail(f"missing external-gate doc: {lab}")
            text = open(path).read()
            for needle in [marker, "Pass gate", "Procedure"]:
                if needle not in text:
                    return fail(f"{lab} lacks '{needle}'")
            print(f"[accept] {lab}: procedure + gate present")

        # Reactive-signal contract string present in implementation.
        webrtc_src = open(os.path.join(CHECKOUT, "engine", "net", "src", "webrtc.cc")).read()
        if "rtc.state=" not in webrtc_src:
            return fail("webrtc.cc never emits rtc.state= reactive signals")
        print("[accept] webrtc.cc emits rtc.state=<state> signals")

        print("[accept] ALL GATES HOLD")
        return 0
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


def fail(msg):
    print(f"[accept] FAIL: {msg}")
    failures.append(msg)
    return 1


if __name__ == "__main__":
    sys.exit(main())
