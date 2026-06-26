# Demos — lake_test_mini

The demo programs are located in `../examples/`:

1. **demo_cache_bench** — Cache miss curve construction and power-law fitting
2. **demo_lake_workload** — Full data lake workload analysis with hardware recommendations
3. **demo_perf_model** — Performance modeling: Roofline, scalability laws, ML prediction

Build all demos:
```bash
make examples
```

Run a specific demo:
```bash
./bin/demo_cache_bench
./bin/demo_lake_workload
./bin/demo_perf_model
```
