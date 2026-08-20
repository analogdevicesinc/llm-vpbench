# LLM-VPBench

A benchmark suite for evaluating SystemC TLM-2.0 virtual platform model generation. Measures functional correctness and non-functional quality (code quality, security, performance) across 32 parameters.

## Prerequisites

- **SystemC 2.3.4+** (set `SYSTEMC_HOME` environment variable)
- **CMake 3.14+**
- **C++20 compiler** (GCC 11+ or Clang 14+)
- **Python 3.8+**
- **lizard** (`pip install lizard`) for cyclomatic complexity analysis

## Benchmarks

| IP | Tier | Test Cases | Assertions | Description |
|----|------|-----------|------------|-------------|
| risc-v-aclint | Simple | 131 | 893 | RISC-V Advanced Core Local Interruptor (MSWI, MTIMER, SSWI) |
| noc-router | Simple | 33 | 301 | Network-on-Chip router with routing, caching, and arbitration |
| plic | Medium | 12 | 168 | RISC-V Platform-Level Interrupt Controller |
| dma | Medium | 27 | 233 | Multi-Channel DMA Controller |
| axi4-interconnect | Medium | 7 | 139 | AMBA AXI4 2x2 Interconnect with arbitration |

## Directory Structure

```
LLM-VPBench/
├── benchmarks/
│   └── <ip-name>/
│       ├── CMakeLists.txt     # Build file (uses SUBMISSION_DIR)
│       ├── benchmark.json     # Metadata and register map
│       ├── spec/              # IP specification (Markdown)
│       ├── testbench/         # Hidden testbench source (.cpp)
│       ├── golden/            # Reference implementation (.h files)
│       └── interface/         # Interface contracts
└── scripts/
    └── run_benchmark.py       # Evaluation script
```

## Usage

### Full evaluation

```bash
python3 scripts/run_benchmark.py \
  --benchmark benchmarks/risc-v-aclint \
  --submission benchmarks/risc-v-aclint/golden \
  --systemc-home /path/to/systemc \
  --output results.json
```

### Manual CMake build

```bash
export SYSTEMC_HOME=/path/to/systemc
cd benchmarks/risc-v-aclint
mkdir build && cd build
cmake .. -DSUBMISSION_DIR=/path/to/your/headers
make -j$(nproc)
./aclint_testbench
```

## Required Submission Files

| Benchmark | Required Headers |
|-----------|-----------------|
| risc-v-aclint | `aclint_mswi.h`, `aclint_mtimer.h`, `aclint_sswi.h` |
| noc-router | `noc_router.h`, `noc_arbiter.h`, `noc_cache.h` |
| plic | `plic.h` |
| dma | `dma_controller.h` |
| axi4-interconnect | `axi4_interconnect.h` |

## Evaluation Parameters (32 across 7 categories)

- **Compilation & Build (4):** Compiles cleanly, no warnings, header-only, correct includes
- **Functional Correctness (6):** Assertion pass rate, register behavior, interrupt logic, reset handling
- **TLM-2.0 Compliance (5):** Socket usage, b_transport, response status, byte-enable, access-type
- **Spec Compliance (3):** Register map accuracy, address decode, feature completeness
- **Code Quality (7):** Cyclomatic complexity, comment density, magic numbers, formatting, modularity
- **Security (5):** Bounds checking, no UB, safe casts, sanitizer clean build
- **Performance (2):** Stress throughput, peak memory

## Writing a Submission

1. Read the spec in `benchmarks/<ip>/spec/`
2. Implement header-only SystemC modules matching the interface
3. Run evaluation against your submission
4. Iterate until tests pass

## License

Apache-2.0
