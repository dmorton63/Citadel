# Task_Flow Developer Notes

This short note documents current runtime behavior for Task_Flow and memoization work.

## Supervisor and Queues

- Scheduler progression is driven through `supervisorLoopOnce()`.
- Queue snapshots expose low/medium/high flow pressure.

## Memoization and Predictive Path

- Memoization is signature/input keyed with LRU/LFU policy support.
- Allowlist and safety rules gate cache eligibility.
- Predictive behavior is currently represented by adaptive priority decisions that down-rank highly memoized signatures and boost expensive hot paths.

## Commands

- `memocache`, `memoallow`, and `taskflowviz` remain the primary runtime diagnostics commands.
