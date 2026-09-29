-- One row a preset, path and offered load from the load generator outputs the latency rule
-- writes, whose paths the summary rule sets in the variable files. A run prints a throughput line,
-- a latency line and a percentile line, and an open run a generator lag line. The quartiles are
-- across runs, each run's percentile being one value.
WITH lines AS (
    SELECT
        regexp_extract(filename, 'results/latency/([^/]+)/', 1) AS preset,
        regexp_extract(filename, 'results/latency/[^/]+/([^/]+)/', 1) AS path,
        regexp_extract(filename, 'results/latency/[^/]+/[^/]+/([^/]+)/', 1) AS load,
        filename,
        line
    FROM read_csv(
        getvariable('files'),
        columns = {'line': 'VARCHAR'},
        header = false,
        delim = '\t',
        filename = true
    )
),
runs AS (
    SELECT
        preset,
        path,
        load,
        filename,
        max(try_cast(regexp_extract(line, '([0-9.]+) Morders/s', 1) AS DOUBLE))
            FILTER (WHERE line LIKE 'throughput:%') AS morders,
        max(try_cast(regexp_extract(line, 'dropped=([0-9]+)', 1) AS BIGINT))
            FILTER (WHERE line LIKE 'latency:%') AS dropped,
        max(try_cast(regexp_extract(line, 'p50=([0-9]+)', 1) AS BIGINT))
            FILTER (WHERE line LIKE 'end-to-end%') AS p50,
        max(try_cast(regexp_extract(line, 'p99=([0-9]+)', 1) AS BIGINT))
            FILTER (WHERE line LIKE 'end-to-end%') AS p99,
        max(try_cast(regexp_extract(line, 'p99\.9=([0-9]+)', 1) AS BIGINT))
            FILTER (WHERE line LIKE 'end-to-end%') AS p999,
        max(try_cast(regexp_extract(line, 'mean=([0-9]+)', 1) AS BIGINT))
            FILTER (WHERE line LIKE 'generator lag%') AS lag_mean
    FROM lines
    GROUP BY ALL
)
SELECT
    preset,
    path,
    load,
    count(*) AS runs,
    min(p50) AS p50_min,
    quantile_cont(p50, 0.25) AS p50_q1,
    median(p50) AS p50_median,
    quantile_cont(p50, 0.75) AS p50_q3,
    max(p50) AS p50_max,
    quantile_cont(p99, 0.25) AS p99_q1,
    median(p99) AS p99_median,
    quantile_cont(p99, 0.75) AS p99_q3,
    median(p999) AS p999_median,
    median(morders) AS morders_median,
    median(dropped) AS dropped_median,
    median(lag_mean) AS lag_mean_median
FROM runs
GROUP BY ALL
ORDER BY load, path, preset;
