package org.ckarta.bench;

public final class BenchmarkTarget {
    private BenchmarkTarget() {
    }

    public static long consume(long value) {
        return value + 1L;
    }
}
