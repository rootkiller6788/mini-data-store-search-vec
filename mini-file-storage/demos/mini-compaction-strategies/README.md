# mini-compaction-strategies — Compaction 策略深度对比

> 参考 LevelDB, RocksDB, ScyllaDB, Bitcask

---

## 目录

1. [Compaction 的核心矛盾](#核心矛盾)
2. [放大因子的数学模型](#放大因子)
3. [Leveled Compaction](#leveled-compaction)
4. [Tiered / Universal Compaction](#tiered--universal-compaction)
5. [FIFO Compaction](#fifo-compaction)
6. [Hybrid 策略](#hybrid-策略)
7. [Compaction 调度与限速](#调度与限速)
8. [实际工程优化](#实际工程优化)
9. [选择 Compaction 策略的决策树](#决策树)
10. [本实现中的 Compaction](#本实现中的-compaction)

---

## 核心矛盾

LSM-Tree 存储引擎面临一个三难问题 (Trilemma)，通常称为 **RUM Conjecture**（Read, Update, Memory）：

| 维度 | 含义 | 典型指标 |
|------|------|----------|
| **写放大 (Write Amplification, WA)** | 每次写入到磁盘上实际产生的 I/O 字节数 | WA = 写入磁盘的总字节 / 用户写入字节 |
| **读放大 (Read Amplification, RA)** | 每次点查需要访问的文件/块数量 | 最坏情况: Σ(每层文件数) |
| **空间放大 (Space Amplification, SA)** | 实际使用空间与有效数据空间的比值 | SA = 总磁盘占用 / 有效数据大小 |

**不可能同时优化三角**，Compaction 策略的选择本质上是在三者之间分配优化权重：

```
        写放大 (Write Amp)
           /\
          /  \
         /    \
        /      \
       /________\
读放大            空间放大
(Read Amp)    (Space Amp)
```

### 放大的直觉理解

- **写放大高**: 每次用户写入 1MB，Compaction 可能产生 10-30MB 的磁盘写入（反复合并重写），对 SSD 寿命不利
- **读放大高**: 每次读取需要检查多个 SSTable 文件 → 性能差
- **空间放大高**: 文件中有大量过期数据 (Tombstone, 旧版本) → 浪费空间

---

## 放大因子的数学模型

### Leveled Compaction 的放大

对于 L 层、每层大小增长因子为 T 的 Leveled Compaction：

```
写放大 = T * (L - 1)     (近似, 不考虑 Level 0)
读放大 = L               (每层检查 ~1 个文件)
空间放大 = (T + 1) / T   (≈ 1.1 当 T=10)
```

**推导**: 
- 第 N 层有一个文件被合并时，它与 N+1 层中约 T 个文件重叠
- 这 T+1 个文件全部被读取、合并、写回 N+1 layer
- 因此写放大 ≈ T（每个字节平均被写入 T 次）
- 跨越 L 层，总放大 ≈ T * L

### Tiered Compaction 的放大

对于 tiered，假设每次合并 K 个文件：

```
写放大 = log_K(N)         (N = 总文件数)
读放大 ≈ K                (最坏需要检查 K 个文件)
空间放大 = K              (合并前 K 个文件可能都包含过时数据)
```

**推导**:
- 每层合并 K 个文件 → 写 1 个文件，读 K 个
- 总层数 ≈ log_K(N)
- 每层写 1 次 → 总写放大 ≈ log_K(N)

---

## Leveled Compaction

### 算法描述

```
Level 0:  [SST-A] [SST-B] [SST-C] [SST-D]    ← key 范围可能重叠
Level 1:  [SST-E                    ] [SST-F  ] ← 有序不重叠
Level 2:  [SST-G                                  ] ← 更大
...
Level 6:  [...]                                              ← 最大
```

**触发条件**:
- Level N 的总大小 > 10^N MB (LevelDB 默认)
- Level N 的文件数 > 阈值

**执行流程** (Level N → Level N+1):

1. 从 Level N 中选择一个 SSTable 文件 (通常用 Round-Robin)
2. 在 Level N+1 中找到与该文件 key 范围重叠的所有文件
3. 打开所有选中文件的迭代器
4. 多路归并排序，写入 Level N+1 的新文件
5. 删除 Level N 和 Level N+1 中的旧文件
6. 更新 Manifest

### 优点
- **读放大低**: 每层只有一个文件包含目标 key (Level 0 除外)
- **空间放大低**: 删除及时回收
- **可预测性**: 性能稳定

### 缺点
- **写放大高**: 一个 key 可能被反复合并重写（10-30x）
- **Compaction 可能成为瓶颈**: 大 Compaction 影响前台读写

### RocksDB 的 Leveled Compaction 改进

RocksDB 对 Leveled Compaction 做了多项优化：
- **Target file size**: 不同 level 可用不同的 target file size
- **Parallel compaction**: 多个 Compaction 可并行
- **Subcompaction**: 一个 job 可拆分为多个子 job
- **Max bytes for level**: 更灵活的 level 大小控制

---

## Tiered / Universal Compaction

### 算法描述

Universal Compaction (RocksDB) / Size-Tiered Compaction (Cassandra) 的核心思想是：**不保证 Level 间的有序性，而是让文件按大小 tiers 组织**。

```
Tier 0: [SST-1MB] [SST-1MB] [SST-1MB] [SST-1MB]    ← 4 个同级文件
            ↓ 合并
Tier 1: [SST-4MB] [SST-4MB] [SST-4MB] [SST-4MB]
            ↓ 合并
Tier 2: [SST-16MB] [SST-16MB] [SST-16MB]
            ↓ 合并
Tier 3: [SST-48MB]
```

**触发条件** (RocksDB 的 Universal Compaction 四种策略):

| 模式 | 说明 | 触发条件 |
|------|------|---------|
| kSizeRatio | 按文件大小比触发 | largest / smallest > ratio |
| kCompensatedSizeRatio | 带补偿的大小比 | 考虑文件创建顺序 |
| kSimilarSize | 同大小文件合并 | N 个连续同大小文件 |
| kReadAmp | 按读放大触发 | 文件数 > max_read_amp |

### 优点
- **写放大低**: 合并次数少，4-10x (vs 10-30x)
- **Compaction 更轻量**: 一次合并 N 小为大，不涉及跨层重叠查找
- **适合写密集场景**: 数据大量流入时更高效

### 缺点
- **读放大较高**: 需要检查同 tier 内所有文件
- **空间放大较高**: 过时数据保留更久
- **需要更多临时空间**: 合并时需要同时保持输入和输出文件

### Size-Tiered Compaction (Cassandra/ScyllaDB)

ScyllaDB 使用 Size-Tiered Compaction Strategy (STCS)：

```
大小桶 1 (0-160MB):  4 个 SSTable
大小桶 2 (160MB-1.6GB): 3 个 SSTable
大小桶 3 (>1.6GB):  2 个 SSTable
```

当某个桶内 SSTable 数量达阈值 (默认 4)，触发 Compaction。

**ScyllaDB 的 Compaction 特性**:
- Per-shard Compaction: 每个 logic core 独立 Compaction
- Compaction Controller: 动态调整 Compaction 优先级
- Off-strategy: 文件先写入 staging，Compaction 通过后才入 Main SSTable set

---

## FIFO Compaction

FIFO Compaction 是最简单的策略：无真正 Compaction，仅按时间 (TTL) 删除旧文件。

```
SST-1 (oldest)
SST-2
...
SST-N (newest, < TTL)
```

当总大小超过阈值时，从最旧的文件开始删除。

- 写放大 ≈ 1
- 读放大 = N (所有文件)
- 空间放大 = TTL-based

**适用**: 时序数据、日志、不需要更新的工作负载。

---

## Hybrid 策略

### RocksDB: Dynamic Level Sizing

允许 Level 0 变大（不阻塞写入），动态调整各 Level 大小：
```
max_bytes_for_level_base → 动态计算
    ↓
如果 Level N 太大 → 增大 target size for Level N+1
```

### Leveled-N Compaction (ScyllaDB)

介于 Leveled 和 Size-Tiered 之间：
- 每个 Level 内，文件按 key 范围分区
- 每个分区内使用 Size-Tiered 策略
- 跨分区使用 Leveled 策略

### Tiered+Leveled

在 Cassandra 中称为 TWCS (Time Window Compaction Strategy):
- 按时间窗口分组 (tiered)
- 时间窗口内使用 Size-Tiered 或 Leveled

---

## 调度与限速

### Compaction 优先级

| 优先级 | 触发条件 | 原因 |
|--------|---------|------|
| 最高 | Write Stall | 前端写入被阻塞 |
| 高 | Level 0 文件数 > 阈值 | Level 0 影响读放最大 |
| 中 | 其他 Level 超限 | 维持 Level 比例 |
| 低 | 数据清理 (Tombstone 清除) | 优化空间 |

### Compaction Rate Limiting

防止 Compaction 消耗过多 I/O 带宽影响前台服务：

1. **Rate Limiter**: 限制 Compaction 每秒读写字节数
```
options.rate_limiter = NewGenericRateLimiter(100 * 1024 * 1024); // 100MB/s limit
```

2. **Write Stall**: 当 Level 0 文件数超过阈值，主动减速写入
```
if level0_file_count > soft_limit:
    sleep(delay)  // 写入延迟
if level0_file_count > hard_limit:
    block until compaction completes
```

3. **Compaction Debt**: ScyllaDB 使用 Compaction Backlog 作为积压度量
```
backlog = Σ (bytes_in_level - bytes_ideal_in_level)
```

### Write Stall 信号

RocksDB Write Stall 等级：

| Stall 条件 | 延迟 |
|-----------|------|
| Level 0 files > level0_slowdown_writes_trigger | 1ms delay / write |
| Level 0 files > level0_stop_writes_trigger | 阻塞直到 compaction |
| Memtable count > max_write_buffer_number | 阻塞 |
| Pending compaction bytes > soft_pending_compaction_bytes_limit | 减速 |

---

## 实际工程优化

### 1. 并行 Compaction

LevelDB: 单线程 Compaction
RocksDB: 支持 parallel compactions + subcompactions
```
max_background_compactions = 4   // 4 个并行 compaction job
max_subcompactions = 2           // 每个 job 可拆为 2 个子任务
```

### 2. 分层过滤 (Layered Bloom Filter)

对于跨多个 Level 的 Compaction 输出，使用分层的 Bloom Filter：
- 旧数据使用高 bits_per_key (低 FPR)
- 新数据可使用较低的 bits_per_key (用 cache 弥补)

### 3. Compaction Filter

允许用户在 Compaction 期间过滤/转换数据：
```cpp
// RocksDB CompactionFilter
virtual bool Filter(int level, const Slice& key,
                    const Slice& existing_value,
                    std::string* new_value,
                    bool* value_changed) const {
    // e.g., 删除过期的 TTL 数据
    return true; // 删除这个 key
}
```

### 4. Intra-L0 Compaction

Level 0 内部文件也可互相压缩，减少 Level 0 文件数，而不触发 Level 0 → Level 1 compaction。

### 5. Bottommost Level Optimization

对于最底层 (Level N)，设置不同的压缩策略：
```cpp
options.bottommost_compression = kZSTD; // 更激进的压缩
options.bottommost_compression_opts.level = 10;
```

---

## 决策树

选择 Compaction 策略时考虑：

```
你的工作负载是?
│
├── 读多写少 (Read-heavy)
│   └── Leveled Compaction
│       - 低读放大
│       - 接受高写放大 (写入不多)
│
├── 写多读少 (Write-heavy)
│   ├── 需要范围扫描?
│   │   ├── 是 → Universal / Tiered + Bloom Filter
│   │   └── 否 → FIFO (纯时序)
│   └── 有时间语义?
│       └── TWCS (时间窗口分层)
│
├── SSD / NVMe
│   └── Universal (写放大对 SSD 寿命有害)
│
├── HDD
│   └── Leveled (读放大对 HDD 寻道更不友好)
│
└── 混合负载
    └── Leveled-N 或 Dynamic Level
```

### 实际案例

| 产品 | 策略 | 原因 |
|------|------|------|
| LevelDB | Leveled | 默认选择, 均衡设计 |
| RocksDB (MyRocks) | Leveled | 读密集型, 范围扫描多 |
| RocksDB (ZippyDB) | Leveled | 混合负载 |
| RocksDB (UDB) | Universal | 写密集型, 时序数据 |
| ScyllaDB | Size-Tiered (默认) | 写密集, 大规模 |
| Cassandra | Size-Tiered / Leveled (可选) | 灵活 |
| Apache HBase | Leveled | 读密集 (基于 BigTable) |
| InfluxDB | TSM (Time-Structured Merge) | 时序专用 |
| Bitcask | 无 Compaction | 极简, 全内存索引 |

---

## 本实现中的 Compaction

### 实现策略

`mini-file-storage` 支持两种 Compaction 策略 (通过 `CompactionPicker` 枚举选择):

**Leveled** (`COMPACTION_LEVELED`):
```c
/* 触发条件 */
if (level0_num_files > LSM_LEVEL0_FILE_LIMIT)
    compact_level(tree, 0);

/* 逐层检查 */
for (each level N) {
    if (level[N].num_files > LSM_LEVEL_SIZE_RATIO^(N+1))
        compact_level(tree, N);
}
```

**Tiered / Universal** (`COMPACTION_TIERED`):
```c
if (level0_num_files >= 4)
    compact_level(tree, 0);  // 合并所有 Level 0 文件到 Level 1
```

### Compaction 执行

```c
CompactionJob job = {
    .input_tables = level[N].files,
    .num_inputs   = level[N].num_files,
    .level_n      = N,
    .level_np1    = N + 1
};

compaction_merge(&job, &stats);
```

`compaction_merge` 做多路归并：
1. 创建 Merge Iterator，堆排序合并所有输入文件
2. 去重 (Last-Write-Wins)
3. 写入输出 SSTable
4. 删除输入文件，更新 Level 元数据

### Compaction 统计

```c
typedef struct {
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint32_t input_files;
    uint32_t output_files;
    double   duration_ms;
} CompactionStats;
```

每次 Compaction 执行后打印统计，帮助理解各策略的实际效果。

### 学习实验

你可以修改 `examples/lsm_demo.c` 中的 CompactionPicker 参数来对比不同策略：

```c
// Leveled Compaction
LSMTree *tree = lsm_open("./data", COMPACTION_LEVELED);

// Universal/Tiered Compaction
LSMTree *tree = lsm_open("./data", COMPACTION_TIERED);
```

观察不同策略下的 Level 文件分布和 Read/Write 性能差异。

---

## 参考资料

- [RocksDB Compaction Wiki](https://github.com/facebook/rocksdb/wiki/Compaction)
- [ScyllaDB Compaction Strategies](https://opensource.docs.scylladb.com/stable/architecture/compaction/compaction-strategies.html)
- [Cassandra Compaction](https://cassandra.apache.org/doc/latest/cassandra/operating/compaction/index.html)
- [LevelDB Compaction 源码分析](https://leveldb-handbook.readthedocs.io/zh/latest/compaction.html)
- [WiscKey (FAST'16)](https://www.usenix.org/system/files/conference/fast16/fast16-papers-lu.pdf)
- [Dostoevsky: Better Space-Time Trade-Offs for LSM-Tree (SIGMOD'18)](https://stratos.seas.harvard.edu/files/stratos/files/dostoevsky.pdf)
- [LSM-based Storage Techniques: A Survey (VLDB'19)](https://arxiv.org/abs/1812.07527)
- [Designing Data-Intensive Applications, Chapter 3](https://dataintensive.net/)

---

*mini-file-storage — Compaction 策略学习指南*
