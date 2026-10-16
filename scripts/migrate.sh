#!/bin/sh
# Applies migrations/V<n>__<name>.sql files in version order. Each file runs in
# its own transaction together with its schema_migrations row, so a failed
# migration leaves no partial state behind. Connection settings come from the
# standard libpq variables (PGHOST, PGPORT, PGUSER, PGPASSWORD, PGDATABASE).
set -eu

dir="${MIGRATIONS_DIR:-/migrations}"
export PGOPTIONS="${PGOPTIONS:--c client_min_messages=warning}"

run_psql() {
    psql -X -q -v ON_ERROR_STOP=1 "$@"
}

run_psql -c "CREATE TABLE IF NOT EXISTS schema_migrations (
    version    INT         PRIMARY KEY,
    name       TEXT        NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
)"

files=$(ls "$dir" | grep -E '^V[0-9]+__[A-Za-z0-9_]+\.sql$' | sed 's/^V//' | sort -n | sed 's/^/V/')

if [ -z "$files" ]; then
    echo "no migrations found in $dir"
    exit 0
fi

for file in $files; do
    base="${file%.sql}"
    version="${base%%__*}"
    version="${version#V}"
    name="${base#*__}"

    applied=$(run_psql -tA -c "SELECT 1 FROM schema_migrations WHERE version = $version")
    if [ "$applied" = "1" ]; then
        continue
    fi

    echo "applying $file"
    {
        echo "BEGIN;"
        cat "$dir/$file"
        echo ";"
        echo "INSERT INTO schema_migrations (version, name) VALUES ($version, '$name');"
        echo "COMMIT;"
    } | run_psql
done

echo "migrations up to date"
