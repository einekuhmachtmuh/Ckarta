# Ckarta JNI Thread Benchmark

這是獨立於正式 Ckarta runtime 的第一階段 microbenchmark（微基準測試），用來比較 direct attachment 與 JNI bridge 的 thread overhead（執行緒額外成本）。

## 建置

Linux + OpenJDK 21：

```sh
make JDK_HOME=/path/to/jdk-21
```

`JDK_HOME` 預設由 `javac` 的實際路徑推導。

## 執行

直接 attach：

```sh
./build/ckarta-jni-thread-bench direct 2 100000
```

單一 bridge thread：

```sh
./build/ckarta-jni-thread-bench bridge 2 100000 1
```

多 bridge threads：

```sh
./build/ckarta-jni-thread-bench bridge 4 100000 2
```

輸出包含 elapsed time、throughput、平均／最小／最大 operation latency 與 queue wait 的平均值。

## 目前限制

- 只測 primitive `long` JNI invocation。
- 不包含 Servlet container。
- 不包含 C request struct → Java object 物件化。
- 不包含真正 Ckarta connection、HTTP、TLS 或 response path。
- 目前沒有完整 histogram，因此 p95/p99 尚未由 harness 直接輸出。
- `BenchmarkTarget.consume()` 是固定極小 workload；因此結果主要反映 thread／queue／JNI crossing overhead，而不是實際 Servlet workload。

不得用此 benchmark 宣稱 Ckarta 整體效能。

## 本機首次校驗

Codex 測試環境曾成功建置並執行三種 harness 模式：

```sh
make
./build/ckarta-jni-thread-bench direct 2 10000
./build/ckarta-jni-thread-bench bridge 2 10000 1
./build/ckarta-jni-thread-bench bridge 2 10000 2
```

當時環境為 Linux x86_64、GCC 14.2.0、OpenJDK 21.0.11；這段記錄只證明 harness 可執行，不把未完整保存硬體、kernel、Ckarta commit、warm-up 與 repetitions 的性能數字當作可重現基準。

`docs/THREAD_BENCHMARK_PLAN.md` 定義正式 benchmark 必須補齊的環境、統計與 workload 條件。
