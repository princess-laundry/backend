#!/usr/bin/env bash
# End to end check of the session and stats endpoints against a running
# Compose stack. It signs in a fresh device, uploads ten sample sessions and
# compares GET /api/v1/stats/daily with totals computed here from the
# uploaded payloads. The samples include a sequential retry, one session sent
# five times concurrently, one that crosses Seoul midnight, one written in
# UTC that falls on the next Seoul day, and days with no classified posture
# time. It also covers listing, detail, delete, ownership and the main
# validation errors.
#
# Usage: scripts/smoke-test.sh [base_url]
# The base URL defaults to http://localhost:8080. Requires bash, curl and jq.
set -euo pipefail

BASE_URL="${1:-${BASE_URL:-http://localhost:8080}}"
API="$BASE_URL/api/v1"
FROM=2026-03-01
TO=2026-03-31

for tool in curl jq; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "smoke-test needs $tool" >&2
        exit 2
    fi
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
checks=0

pass() {
    checks=$((checks + 1))
    printf 'ok    %s\n' "$1"
}

fail() {
    checks=$((checks + 1))
    failures=$((failures + 1))
    printf 'FAIL  %s\n' "$1"
}

# expect <description> <expected> <actual>
expect() {
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1: expected '$2', got '$3'"
    fi
}

new_uuid() {
    if command -v uuidgen >/dev/null 2>&1; then
        uuidgen | tr '[:upper:]' '[:lower:]'
    else
        cat /proc/sys/kernel/random/uuid
    fi
}

# request <token> <method> <path> [body]
# Prints the status code and leaves the response body in $work/body.
request() {
    local token=$1 method=$2 path=$3 body=${4-}
    local args=(-sS -o "$work/body" -w '%{http_code}' -X "$method")
    if [ -n "$token" ]; then
        args+=(-H "Authorization: Bearer $token")
    fi
    if [ -n "$body" ]; then
        args+=(-H 'Content-Type: application/json' --data-binary "$body")
    fi
    curl "${args[@]}" "$API$path"
}

body() {
    jq -r "$1" "$work/body"
}

sign_in() {
    curl -fsS -X POST "$API/auth/device" \
        -H 'Content-Type: application/json' \
        -d "{\"device_id\":\"$(new_uuid)\"}" | jq -r .token
}

echo "waiting for $API/health"
for _ in $(seq 1 60); do
    if curl -fsS "$API/health" >/dev/null 2>&1; then
        break
    fi
    sleep 1
done
curl -fsS "$API/health" >/dev/null

TOKEN=$(sign_in)
OTHER_TOKEN=$(sign_in)

# Sample sessions. start is the Seoul wall clock time the session began; zone
# says how started_at and ended_at are written in the upload. Each session
# lasts normal + fhp + lean + away seconds.
cat >"$work/samples.json" <<'EOF'
[
  {"start": "2026-03-02T09:00:00", "zone": "seoul", "normal": 2400, "fhp": 600, "lean": 300, "away": 300, "alerts": 3},
  {"start": "2026-03-02T14:00:00", "zone": "seoul", "normal": 1800, "fhp": 900, "lean": 0, "away": 120, "alerts": 5},
  {"start": "2026-03-02T23:30:00", "zone": "seoul", "normal": 3000, "fhp": 600, "lean": 300, "away": 300, "alerts": 2},
  {"start": "2026-03-03T10:00:00", "zone": "seoul", "normal": 1200, "fhp": 1200, "lean": 600, "away": 0, "alerts": 8},
  {"start": "2026-03-03T20:00:00", "zone": "seoul", "normal": 0, "fhp": 0, "lean": 0, "away": 600, "alerts": 0},
  {"start": "2026-03-04T00:30:00", "zone": "utc", "normal": 900, "fhp": 100, "lean": 50, "away": 0, "alerts": 1},
  {"start": "2026-03-04T08:15:30", "zone": "seoul", "normal": 2000, "fhp": 400, "lean": 100, "away": 500, "alerts": 4},
  {"start": "2026-03-05T19:00:00", "zone": "seoul", "normal": 0, "fhp": 500, "lean": 500, "away": 0, "alerts": 6},
  {"start": "2026-03-06T13:00:00", "zone": "seoul", "normal": 0, "fhp": 0, "lean": 0, "away": 300, "alerts": 0},
  {"start": "2026-03-07T16:00:00", "zone": "seoul", "normal": 3600, "fhp": 0, "lean": 0, "away": 0, "alerts": 0}
]
EOF

ids_json=$(for _ in $(seq 1 10); do new_uuid; done | jq -R . | jq -s .)

# Builds the upload payloads, then derives each session's Seoul date from the
# started_at string that is actually sent.
jq --argjson ids "$ids_json" '
  def seoul_epoch: (.start + "Z" | fromdateiso8601) - 32400;
  def iso($epoch; $zone):
    if $zone == "utc" then $epoch | todateiso8601
    else (($epoch + 32400) | strftime("%Y-%m-%dT%H:%M:%S")) + "+09:00"
    end;
  def instant:
    if endswith("Z") then fromdateiso8601
    else (.[0:19] + "Z" | fromdateiso8601)
      - ((.[20:22] | tonumber) * 3600 + (.[23:25] | tonumber) * 60)
        * (if .[19:20] == "-" then -1 else 1 end)
    end;
  def segments:
    [{state: "NORMAL", sec: .normal}, {state: "FHP", sec: .fhp},
     {state: "LEAN", sec: .lean}, {state: "AWAY", sec: .away}]
    | reduce .[] as $s ({at: 0, out: []};
        if $s.sec > 0 then
          .out += [{state: $s.state, start_ms: .at,
                    end_ms: (.at + $s.sec * 1000)}
                   + (if $s.state == "AWAY" then {}
                      else {avg_ratio: 0.18, avg_angle: 12.5} end)]
          | .at += $s.sec * 1000
        else . end)
    | .out;
  to_entries
  | map(.key as $i | .value
      | seoul_epoch as $start
      | {payload: {
           client_session_id: $ids[$i],
           started_at: iso($start; .zone),
           ended_at: iso($start + .normal + .fhp + .lean + .away; .zone),
           threshold_version: 1,
           app_version: "smoke-test",
           summary: {study_sec: (.normal + .fhp + .lean), away_sec: .away,
                     normal_sec: .normal, fhp_sec: .fhp, lean_sec: .lean,
                     alert_count: .alerts},
           segments: segments}}
      | .date = ((.payload.started_at | instant) + 32400
                 | strftime("%Y-%m-%d")))
' "$work/samples.json" >"$work/sessions.json"

sample() {
    jq -c ".[$1].payload" "$work/sessions.json"
}

# Expected /stats/daily entries for the sessions in $work/sessions.json whose
# index is not in the JSON array $1.
expected_days() {
    jq --argjson skip "$1" '
      to_entries
      | map(select(.key as $k | $skip | index($k) | not) | .value)
      | group_by(.date)
      | map({date: .[0].date,
             study_sec: (map(.payload.summary.study_sec) | add),
             normal_sec: (map(.payload.summary.normal_sec) | add),
             fhp_sec: (map(.payload.summary.fhp_sec) | add),
             lean_sec: (map(.payload.summary.lean_sec) | add),
             session_count: length})
      | map(. + {normal_ratio:
               ((.normal_sec + .fhp_sec + .lean_sec) as $t
                | if $t > 0 then .normal_sec / $t else null end)})
    ' "$work/sessions.json"
}

check_stats() {
    local label=$1 skip=$2 status
    expected_days "$skip" >"$work/expected.json"
    status=$(request "$TOKEN" GET "/stats/daily?from=$FROM&to=$TO")
    expect "$label: GET /stats/daily status" 200 "$status"
    if jq -e --slurpfile want "$work/expected.json" '
        def close($a; $b):
          ($a == null and $b == null)
          or ($a != null and $b != null
              and (if $a > $b then $a - $b else $b - $a end) < 1e-9);
        .days as $got | $want[0] as $want
        | ($got | length) == ($want | length)
          and ([$got, $want] | transpose | all(
                .[0] as $g | .[1] as $w
                | $g.date == $w.date and $g.study_sec == $w.study_sec
                  and $g.normal_sec == $w.normal_sec
                  and $g.fhp_sec == $w.fhp_sec
                  and $g.lean_sec == $w.lean_sec
                  and $g.session_count == $w.session_count
                  and close($g.normal_ratio; $w.normal_ratio)))
      ' "$work/body" >/dev/null; then
        pass "$label: /stats/daily matches the script totals ($(jq length "$work/expected.json") days)"
    else
        fail "$label: /stats/daily differs from the script totals"
        echo "expected:"
        jq -c '.[]' "$work/expected.json"
        echo "got:"
        jq -c '.days[]?' "$work/body"
    fi
}

echo "uploading samples"
session_ids=()
for i in $(seq 0 8); do
    date=$(jq -r ".[$i].date" "$work/sessions.json")
    running=$(jq ".[0:$((i + 1))] | map(select(.date == \"$date\")
                  | .payload.summary.study_sec) | add" "$work/sessions.json")
    status=$(request "$TOKEN" POST /sessions "$(sample "$i")")
    expect "upload $((i + 1)) status" 201 "$status"
    expect "upload $((i + 1)) daily.date" "$date" "$(body .daily.date)"
    expect "upload $((i + 1)) daily.study_sec" "$running" "$(body .daily.study_sec)"
    session_ids[i]=$(body .session_id)
done

expect "midnight crossing session counts on its start day" 2026-03-02 \
    "$(jq -r '.[2].date' "$work/sessions.json")"
expect "UTC session counts on the next Seoul day" 2026-03-04 \
    "$(jq -r '.[5].date' "$work/sessions.json")"

day_total=$(jq '.[1].date as $d | .[0:9] | map(select(.date == $d)
                 | .payload.summary.study_sec) | add' "$work/sessions.json")
status=$(request "$TOKEN" POST /sessions "$(sample 1)")
expect "retry of upload 2 status" 200 "$status"
expect "retry of upload 2 returns the stored id" "${session_ids[1]}" "$(body .session_id)"
expect "retry of upload 2 leaves the day unchanged" "$day_total" "$(body .daily.study_sec)"

echo "uploading sample 10 five times at once"
payload=$(sample 9)
for n in 1 2 3 4 5; do
    curl -sS -o "$work/concurrent.$n" -w '%{http_code}' -X POST \
        -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
        --data-binary "$payload" "$API/sessions" >"$work/concurrent.$n.status" &
done
wait
statuses=$(for n in 1 2 3 4 5; do
    cat "$work/concurrent.$n.status"
    echo
done | sort | tr -d '\n')
expect "concurrent uploads: one 201 and four 200" 200200200200201 "$statuses"
expect "concurrent uploads share one session id" 1 \
    "$(jq -s 'map(.session_id) | unique | length' "$work"/concurrent.[1-5])"
session_ids[9]=$(jq -r .session_id "$work/concurrent.1")

check_stats "after uploads" '[]'

for date in 2026-03-02 2026-03-03 2026-03-04 2026-03-08; do
    want=$(jq "map(select(.date == \"$date\")) | length" "$work/sessions.json")
    status=$(request "$TOKEN" GET "/sessions?date=$date")
    expect "list $date status" 200 "$status"
    expect "list $date count" "$want" "$(body '.sessions | length')"
done
expect "list is ordered by start time" \
    "$(jq -c '[.[0:3][] | .payload.client_session_id]' "$work/sessions.json")" \
    "$(request "$TOKEN" GET '/sessions?date=2026-03-02' >/dev/null; body '[.sessions[].client_session_id] | tojson')"
expect "list shows Seoul times" "2026-03-02T23:30:00+09:00" \
    "$(body '.sessions[2].started_at')"
expect "list shows the end past midnight" "2026-03-03T00:40:00+09:00" \
    "$(body '.sessions[2].ended_at')"

status=$(request "$TOKEN" GET "/sessions/${session_ids[0]}")
expect "detail status" 200 "$status"
expect "detail summary matches the upload" \
    "$(jq -cS '.[0].payload.summary' "$work/sessions.json")" \
    "$(jq -cS '.summary' "$work/body")"
expect "detail segments match the upload" \
    "$(jq -cS '.[0].payload.segments' "$work/sessions.json")" \
    "$(jq -cS '.segments | map(with_entries(select(.value != null)))' "$work/body")"
expect "UTC upload is returned in Seoul time" "2026-03-04T00:30:00+09:00" \
    "$(request "$TOKEN" GET "/sessions/${session_ids[5]}" >/dev/null; body .started_at)"

expect "other user cannot read the session" 404 \
    "$(request "$OTHER_TOKEN" GET "/sessions/${session_ids[0]}")"
expect "other user cannot delete the session" 404 \
    "$(request "$OTHER_TOKEN" DELETE "/sessions/${session_ids[3]}")"
expect "other user sees no stats" 0 \
    "$(request "$OTHER_TOKEN" GET "/stats/daily?from=$FROM&to=$TO" >/dev/null; body '.days | length')"
expect "unknown session id" 404 "$(request "$TOKEN" GET /sessions/999999999999)"
expect "malformed session id" 400 "$(request "$TOKEN" GET /sessions/abc)"

expect "delete upload 4" 204 "$(request "$TOKEN" DELETE "/sessions/${session_ids[3]}")"
expect "deleted session is gone" 404 "$(request "$TOKEN" GET "/sessions/${session_ids[3]}")"
expect "second delete" 404 "$(request "$TOKEN" DELETE "/sessions/${session_ids[3]}")"
expect "delete upload 8, the only session on its day" 204 \
    "$(request "$TOKEN" DELETE "/sessions/${session_ids[7]}")"
check_stats "after deletes" '[3, 7]'

echo "checking rejected requests"
reject() {
    local label=$1 filter=$2 status
    status=$(request "$TOKEN" POST /sessions "$(sample 0 | jq -c "$filter")")
    expect "$label" "400 INVALID_REQUEST" "$status $(body .error.code)"
}
fresh='.client_session_id = "'$(new_uuid)'"'
reject "ended_at before started_at" "$fresh | .ended_at = \"2026-03-02T08:59:59+09:00\""
reject "unknown threshold_version" "$fresh | .threshold_version = 999"
reject "segments out of order" "$fresh | .segments |= reverse"
reject "segment past the session end" "$fresh | .segments[-1].end_ms += 1000"
reject "negative summary value" "$fresh | .summary.fhp_sec = -1"
reject "unknown posture state" "$fresh | .segments[0].state = \"SLOUCH\""
reject "timestamp without offset" "$fresh | .started_at = \"2026-03-02T09:00:00\""
reject "string where a number belongs" "$fresh | .summary.study_sec = \"3300\""
expect "non JSON body" 400 \
    "$(curl -sS -o /dev/null -w '%{http_code}' -X POST -H "Authorization: Bearer $TOKEN" \
        -H 'Content-Type: application/json' --data-binary 'not json' "$API/sessions")"
expect "missing token" 401 "$(request "" POST /sessions "$(sample 0)")"
expect "missing token on stats" 401 "$(request "" GET "/stats/daily?from=$FROM&to=$TO")"
expect "list without date" 400 "$(request "$TOKEN" GET /sessions)"
expect "inverted stats range" 400 "$(request "$TOKEN" GET '/stats/daily?from=2026-03-02&to=2026-03-01')"
expect "stats range of 367 days" 400 "$(request "$TOKEN" GET '/stats/daily?from=2026-01-01&to=2027-01-02')"
expect "stats range of 366 days" 200 "$(request "$TOKEN" GET '/stats/daily?from=2026-01-01&to=2027-01-01')"
expect "stats without range" 400 "$(request "$TOKEN" GET /stats/daily)"
check_stats "after rejected requests" '[3, 7]'

echo
if [ "$failures" -eq 0 ]; then
    echo "all $checks checks passed"
else
    echo "$failures of $checks checks failed"
    exit 1
fi
