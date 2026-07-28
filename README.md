# M5StopWatch-UserDemo
M5Stack StopWatch user demo for hardware evaluation.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

StopWatch wearable (esp32s3, default):

```bash
idf.py build
```

M5Paper v1.1 (esp32) — Aqua Timer only, e-ink UI:

```bash
idf.py -B build.m5paper -DIDF_TARGET=esp32 -DSDKCONFIG=sdkconfig.m5paper build
```

### Flash

```bash
idf.py flash                                              # StopWatch
idf.py -B build.m5paper -DSDKCONFIG=sdkconfig.m5paper flash   # M5Paper
```
