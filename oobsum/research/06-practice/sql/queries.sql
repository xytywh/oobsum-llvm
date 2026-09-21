-- Infer 数据库速查
--   sqlite3 infer-out/capture.db  < queries.sql     （只有 capture 部分会有结果）
--   sqlite3 infer-out/results.db  < queries.sql     （只有 results 部分会有结果）
-- 说明：所有 BLOB 列都是 OCaml Marshal 二进制，SQL 读不出内容；
--       要看摘要内容请用 infer debug --procedures-summary（见 02-可观测点手册）。
.headers on
.mode column

-- ══════════ capture.db ══════════

-- 捕获了哪些源文件、本轮是否新捕获（增量分析看这个）
SELECT source_file, freshly_captured FROM source_files ORDER BY source_file LIMIT 50;

-- capture 成功率自检：这个数应当接近 compile_commands.json 的条目数
SELECT count(*) AS captured_files FROM source_files;

-- 捕获了多少过程，CFG 最大的是哪些（找超大函数 / 分析热点）
SELECT proc_uid,
       length(cfg)      AS cfg_bytes,
       length(callees)  AS callees_bytes
FROM procedures
ORDER BY cfg_bytes DESC
LIMIT 20;

-- 只看有 CFG 的（即有定义的过程；只有声明的过程 cfg 为 NULL）
SELECT count(*) AS defined_procs FROM procedures WHERE cfg IS NOT NULL;

-- 可读 IR：需要 capture 时加 --store-textual，否则 textual 列为空
SELECT source_file, substr(textual, 1, 600) AS textual_head
FROM source_files WHERE textual IS NOT NULL LIMIT 1;


-- ══════════ results.db ══════════

-- 产出了多少份摘要
SELECT count(*) AS specs_rows FROM specs;

-- 每个过程产出了哪些分析的摘要（1 = 该 payload 非空）
SELECT proc_uid,
       BufferOverrunAnalysis IS NOT NULL AS bo_analysis,
       BufferOverrunChecker  IS NOT NULL AS bo_checker,
       Pulse                 IS NOT NULL AS pulse,
       RacerD                IS NOT NULL AS racerd
FROM specs LIMIT 50;

-- 摘要体积排行：状态膨胀最严重的过程（Pulse 列大 = 析取多）
SELECT proc_uid,
       coalesce(length(BufferOverrunChecker), 0) AS bo_bytes,
       coalesce(length(Pulse), 0)                AS pulse_bytes,
       length(report_summary)                    AS report_bytes
FROM specs
ORDER BY bo_bytes + pulse_bytes DESC
LIMIT 20;

-- 哪些过程产生了告警（report_summary 明显偏大的通常就是）
SELECT proc_uid, length(report_summary) AS report_bytes
FROM specs ORDER BY report_bytes DESC LIMIT 20;

-- 序列化格式自检：应当是 8495A6BE（OCaml Marshal 的 Intext_magic_number_small）
SELECT proc_uid, hex(substr(report_summary, 1, 4)) AS marshal_magic FROM specs LIMIT 3;

-- 文件级分析的告警（RacerD/SIOF 这类走这张表；BufferOverrun 走 specs.report_summary）
SELECT checker, source_file, length(issue_log) AS bytes FROM issue_logs LIMIT 20;
