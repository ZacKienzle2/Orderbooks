-- Layout runs grouped by the node they ran on, one row a node, preset and load, with the medians of
-- the saturated rate and the latency p50 of the runs in the group. The rule sets the variables
-- presets, loads, binaries and repeats from config/config.yaml.
WITH lines AS (
    SELECT filename, line
    FROM read_csv('results/layout/*/*/*/*.txt', columns = {'line': 'VARCHAR'}, header = false,
        delim = '\t', filename = true)
),
runs AS (
    SELECT
        regexp_extract(filename, 'results/layout/([^/]+)/([0-9]+)/([^/]+)/([0-9]+)\.txt$',
            ['preset', 'seed', 'load', 'rep']) AS run,
        max(regexp_extract(line, '^node=(.+)$', 1)) AS node,
        max(try_cast(regexp_extract(line, '([0-9.]+) Morders/s', 1) AS DOUBLE)) AS morders_per_s,
        max(try_cast(regexp_extract(line, 'p50=([0-9]+)', 1) AS BIGINT)) AS p50
    FROM lines
    GROUP BY filename
)
SELECT
    node,
    run.preset AS preset,
    run.load AS load,
    count(DISTINCT run.seed) AS binaries,
    count(*) AS runs,
    median(morders_per_s) AS morders_per_s,
    median(p50) AS p50
FROM runs
WHERE list_contains(getvariable('presets'), run.preset)
    AND list_contains(getvariable('loads'), run.load)
    AND run.seed::INTEGER BETWEEN 1 AND getvariable('binaries')
    AND run.rep::INTEGER < getvariable('repeats')
GROUP BY ALL
ORDER BY ALL;
