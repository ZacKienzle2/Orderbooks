-- One row a preset, workload and metric from the perf stat -j files the topdown rule writes, whose
-- paths the summary rule sets in the variable files. perf writes one JSON object a line, and the line
-- of a metric carries its value, its unit and, where the metric's own events decide it, its
-- threshold.
SELECT
    regexp_extract(filename, 'results/topdown/([^/]+)/', 1) AS preset,
    regexp_extract(filename, 'results/topdown/[^/]+/([^/]+)/', 1) AS workload,
    trim(regexp_replace("metric-unit", '^%', '')) AS metric,
    try_cast("metric-value" AS DOUBLE) AS percent,
    "metric-threshold" AS threshold
FROM read_json(
    getvariable('files'),
    format = 'newline_delimited',
    filename = true,
    union_by_name = true
)
WHERE "metric-value" IS NOT NULL
ORDER BY workload, metric, preset;
