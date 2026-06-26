# mini-db-kernel — 数据库内核 (C 语言实现)

> 参考 CMU 15-445 Database Systems, Database Internals (Petrov)

## 简介

mini-db-kernel 是一个用 C99 实现的教学级数据库内核，包含存储引擎、索引、事务管理和恢复模块。本项目旨在用最少的外部依赖（仅 libc 和 libm）实现数据库管理系统的核心功能，便于学习数据库内部原理。

## 模块

| 模块 | 头文件 | 实现 | 说明 |
|:---|:---|:---|:---|
| Buffer Pool | `include/buffer_pool.h` | `src/buffer_pool.c` | 内存页面缓存（1024 frames × 4KB），LRU 淘汰 |
| B+Tree | `include/btree.h` | `src/btree.c` | 有序索引，Order 5，支持点查、插入、删除、范围扫描 |
| WAL | `include/wal.h` | `src/wal.c` | Write-Ahead Log，ARIES 恢复（Analysis→Redo→Undo） |
| Lock Manager | `include/lock_manager.h` | `src/lock_manager.c` | 两阶段锁（2PL），死锁检测 |
| MVCC | `include/mvcc.h` | `src/mvcc.c` | 多版本并发控制，快照隔离，GC |

## 构建

```bash
make all
```

生成产物位于 `bin/` 目录：
- `buffer_pool_demo` — 缓冲区池 LRU 淘汰演示
- `btree_demo` — B+Tree 插入/删除/范围扫描演示
- `wal_demo` — WAL 写入与崩溃恢复演示

## 运行

```bash
./bin/buffer_pool_demo
./bin/btree_demo
./bin/wal_demo
```

## 目录结构

```
mini-db-kernel/
├── Makefile
├── README.md
├── include/
│   ├── buffer_pool.h
│   ├── btree.h
│   ├── wal.h
│   ├── lock_manager.h
│   └── mvcc.h
├── src/
│   ├── buffer_pool.c
│   ├── btree.c
│   ├── wal.c
│   ├── lock_manager.c
│   └── mvcc.c
├── examples/
│   ├── buffer_pool_demo.c
│   ├── btree_demo.c
│   └── wal_demo.c
├── demos/
│   ├── mini-storage-engine/
│   │   └── README.md
│   └── mini-transactions/
│       └── README.md
└── docs/
    ├── course-alignment.md
    └── database-kernel-primer.md
```

## 设计原则

- **C99**：使用标准 C99 特性（`stdint.h`, `stdbool.h`, `restrict` 等）
- **最小依赖**：仅依赖 libc + libm，无第三方库
- **单线程**：不涉及多线程并发（简化教学）
- **内存 I/O**：磁盘 I/O 通过内存模拟，不涉及真实文件
- **教学导向**：优先代码可读性，而非性能

## 代码风格

- 函数命名：`snake_case`
- 类型命名：`PascalCase`
- 宏命名：`UPPER_SNAKE_CASE`
- 头文件保护：`#ifndef X_H` / `#define X_H` / `#endif`

## 参考资料

- [CMU 15-445/645 Database Systems](https://15445.courses.cs.cmu.edu/)
- Database Internals (Alex Petrov, O'Reilly 2020)
- Database System Concepts (Silberschatz, Korth, Sudarshan)
- ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking (Mohan et al.)

## 许可

本项目仅用于教学目的。
