-- One bench_ci values object for one binary of the layout experiment: the per-run value of one
-- percentile over the load generator outputs the layout rule wrote for that binary and load, whose
-- paths the rule sets in the variable files. bench_ci averages the values of each object and puts
-- its interval across the objects (Kalibera and Jones, ISMM 2013), so a binary is the top level of
-- the experiment and a run the level below it.
SELECT
    getvariable('system') AS system,
    getvariable('benchmark') AS benchmark,
    list(try_cast(regexp_extract(line, getvariable('pattern'), 1) AS BIGINT)) AS "values"
FROM read_csv(
    getvariable('files'),
    columns = {'line': 'VARCHAR'},
    header = false,
    delim = '\t'
)
WHERE line LIKE 'end-to-end%';
