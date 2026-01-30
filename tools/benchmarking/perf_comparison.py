#!/usr/bin/env python3
"""
EtherCAT Stack Performance Comparison Tool
Compares perf stat and perf report outputs from two different runs
"""

import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from enum import Enum


class ColorCode(Enum):
    """ANSI color codes for terminal output"""

    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"


@dataclass
class PerfStats:
    """Container for perf stat metrics"""

    task_clock_ms: float = 0.0
    cpu_utilization: float = 0.0
    context_switches: int = 0
    cpu_migrations: int = 0
    page_faults: int = 0
    cycles: int = 0
    instructions: int = 0
    ipc: float = 0.0
    branches: int = 0
    branch_misses: int = 0
    branch_miss_rate: float = 0.0
    l1_dcache_loads: int = 0
    l1_dcache_misses: int = 0
    l1_dcache_miss_rate: float = 0.0
    llc_loads: int = 0
    llc_misses: int = 0
    llc_miss_rate: float = 0.0
    time_elapsed: float = 0.0


def parse_perf_stat(filepath: Path) -> Optional[PerfStats]:
    """Parse perf stat output file"""
    stats = PerfStats()

    if not filepath.exists():
        print(
            f"{ColorCode.RED.value}Error: File not found: {filepath}{ColorCode.RESET.value}"
        )
        return None

    with open(filepath, "r") as f:
        content = f.read()

    # Helper function to extract numeric values
    def extract_number(pattern: str, text: str) -> Optional[float]:
        match = re.search(pattern, text, re.MULTILINE)
        if not match:
            return None

        raw = match.group(1)

        # Normalize unicode spaces & thin spaces
        raw = raw.replace("\u202f", "").replace("\u00a0", "").replace(" ", "")

        # Convert EU decimal comma to dot
        raw = raw.replace(",", ".")

        try:
            return float(raw)
        except ValueError:
            return None

    # Parse each metric
    stats.task_clock_ms = (
        extract_number(r"([\d., \s]+)\s+msec\s+task-clock", content) or 0.0
    )

    stats.cpu_utilization = (
        extract_number(r"#\s*([\d.,]+)\s+CPUs utilized", content) or 0.0
    )

    stats.context_switches = int(
        extract_number(r"([\d., \s]+)\s+context-switches", content) or 0
    )

    stats.cpu_migrations = int(
        extract_number(r"([\d., \s]+)\s+cpu-migrations", content) or 0
    )

    stats.page_faults = int(extract_number(r"([\d., \s]+)\s+page-faults", content) or 0)

    stats.cycles = int(extract_number(r"([\d., \s]+)\s+cycles", content) or 0)

    stats.instructions = int(
        extract_number(r"([\d., \s]+)\s+instructions", content) or 0
    )

    stats.ipc = (
        extract_number(r"instructions.*?#\s*([\d.,]+)\s+insn per cycle", content) or 0.0
    )

    stats.branches = int(extract_number(r"([\d., \s]+)\s+branches", content) or 0)

    stats.branch_misses = int(
        extract_number(r"([\d., \s]+)\s+branch-misses", content) or 0
    )

    stats.branch_miss_rate = (
        extract_number(r"branch-misses.*?#\s*([\d.,]+)%", content) or 0.0
    )

    stats.l1_dcache_loads = int(
        extract_number(r"([\d., \s]+)\s+L1-dcache-loads", content) or 0
    )

    stats.l1_dcache_misses = int(
        extract_number(r"([\d., \s]+)\s+L1-dcache-load-misses", content) or 0
    )

    stats.l1_dcache_miss_rate = (
        extract_number(r"L1-dcache-load-misses.*?#\s*([\d.,]+)%", content) or 0.0
    )

    stats.llc_loads = int(extract_number(r"([\d., \s]+)\s+LLC-loads", content) or 0)

    stats.llc_misses = int(
        extract_number(r"([\d., \s]+)\s+LLC-load-misses", content) or 0
    )

    stats.llc_miss_rate = (
        extract_number(r"LLC-load-misses.*?#\s*([\d.,]+)%", content) or 0.0
    )

    stats.time_elapsed = (
        extract_number(r"([\d.,]+)\s+seconds time elapsed", content) or 0.0
    )

    return stats


def format_number(num: int) -> str:
    """Format large numbers with commas"""
    return f"{num:,}"


def format_percentage(val: float) -> str:
    """Format percentage with 3 decimal places"""
    return f"{val:.3f}%"


def compare_metric(
    val1: float,
    val2: float,
    unit: str = "",
    lower_is_better: bool = True,
    threshold: float = 5.0,
) -> str:
    """Compare two metrics and return colored output"""
    if val1 == 0 and val2 == 0:
        return f"{ColorCode.CYAN.value}Both: 0{unit}{ColorCode.RESET.value}"

    if val1 == 0 or val2 == 0:
        return f"{ColorCode.YELLOW.value}N/A{ColorCode.RESET.value}"

    diff_pct = ((val2 - val1) / val1) * 100

    # Determine if this is an improvement
    if lower_is_better:
        is_better = diff_pct < 0
    else:
        is_better = diff_pct > 0

    # Color code
    if abs(diff_pct) < threshold:
        color = ColorCode.CYAN.value  # Similar
        indicator = "≈"
    elif is_better:
        color = ColorCode.GREEN.value
        indicator = "✓"
    else:
        color = ColorCode.RED.value
        indicator = "✗"

    return f"{color}{indicator} {diff_pct:+.1f}%{ColorCode.RESET.value}"


def print_header(text: str):
    """Print a formatted header"""
    print(
        f"\n{ColorCode.BOLD.value}{ColorCode.BLUE.value}{'='*80}{ColorCode.RESET.value}"
    )
    print(
        f"{ColorCode.BOLD.value}{ColorCode.BLUE.value}{text:^80}{ColorCode.RESET.value}"
    )
    print(
        f"{ColorCode.BOLD.value}{ColorCode.BLUE.value}{'='*80}{ColorCode.RESET.value}\n"
    )


def print_stats_comparison(
    stack1_name: str, stats1: PerfStats, stack2_name: str, stats2: PerfStats
):
    """Print formatted comparison of perf stats"""

    print_header("PERF STAT COMPARISON")

    # Table header
    print(f"{'Metric':<35} {stack1_name:>15} {stack2_name:>15} {'Difference':>15}")
    print("-" * 85)

    # Time & CPU Cost
    print(
        f"{'Task Clock (ms)':<35} {stats1.task_clock_ms:>15.2f} {stats2.task_clock_ms:>15.2f} "
        f"{compare_metric(stats1.task_clock_ms, stats2.task_clock_ms, lower_is_better=True)}"
    )
    print(
        f"{'CPU Utilization':<35} {stats1.cpu_utilization:>15} "
        f"{stats2.cpu_utilization:>15} "
        f"{compare_metric(stats1.cpu_utilization, stats2.cpu_utilization, lower_is_better=True)}"
    )

    print()

    # Scheduling Overhead
    print(
        f"{'Context Switches':<35} {format_number(stats1.context_switches):>15} "
        f"{format_number(stats2.context_switches):>15} "
        f"{compare_metric(stats1.context_switches, stats2.context_switches, lower_is_better=True)}"
    )
    print(
        f"{'CPU Migrations':<35} {format_number(stats1.cpu_migrations):>15} "
        f"{format_number(stats2.cpu_migrations):>15} "
        f"{compare_metric(stats1.cpu_migrations, stats2.cpu_migrations, lower_is_better=True)}"
    )
    print(
        f"{'Page Faults':<35} {format_number(stats1.page_faults):>15} "
        f"{format_number(stats2.page_faults):>15} "
        f"{compare_metric(stats1.page_faults, stats2.page_faults, lower_is_better=True)}"
    )

    print()

    # CPU Work (Lower = better)
    print(
        f"{'CPU Cycles':<35} {format_number(stats1.cycles):>15} "
        f"{format_number(stats2.cycles):>15} "
        f"{compare_metric(stats1.cycles, stats2.cycles, lower_is_better=True)}"
    )
    print(
        f"{'Instructions':<35} {format_number(stats1.instructions):>15} "
        f"{format_number(stats2.instructions):>15} "
        f"{compare_metric(stats1.instructions, stats2.instructions, lower_is_better=True)}"
    )

    # Efficiency (Higher = better)
    print(
        f"{'IPC (Instructions/Cycle)':<35} {stats1.ipc:>15.2f} {stats2.ipc:>15.2f} "
        f"{compare_metric(stats1.ipc, stats2.ipc, lower_is_better=False)}"
    )

    print()

    # Branching
    print(
        f"{'Branches':<35} {format_number(stats1.branches):>15} "
        f"{format_number(stats2.branches):>15} "
        f"{compare_metric(stats1.branches, stats2.branches, lower_is_better=True)}"
    )
    print(
        f"{'Branch Misses':<35} {format_number(stats1.branch_misses):>15} "
        f"{format_number(stats2.branch_misses):>15} "
        f"{compare_metric(stats1.branch_misses, stats2.branch_misses, lower_is_better=True)}"
    )
    print(
        f"{'Branch Miss Rate':<35} {format_percentage(stats1.branch_miss_rate):>15} "
        f"{format_percentage(stats2.branch_miss_rate):>15} "
        f"{compare_metric(stats1.branch_miss_rate, stats2.branch_miss_rate, lower_is_better=True)}"
    )

    print()

    # Cache Traffic & Efficiency
    print(
        f"{'L1 D-Cache Loads':<35} {format_number(stats1.l1_dcache_loads):>15} "
        f"{format_number(stats2.l1_dcache_loads):>15} "
        f"{compare_metric(stats1.l1_dcache_loads, stats2.l1_dcache_loads, lower_is_better=True)}"
    )
    print(
        f"{'L1 D-Cache Misses':<35} {format_number(stats1.l1_dcache_misses):>15} "
        f"{format_number(stats2.l1_dcache_misses):>15} "
        f"{compare_metric(stats1.l1_dcache_misses, stats2.l1_dcache_misses, lower_is_better=True)}"
    )
    print(
        f"{'L1 D-Cache Miss Rate':<35} {format_percentage(stats1.l1_dcache_miss_rate):>15} "
        f"{format_percentage(stats2.l1_dcache_miss_rate):>15} "
        f"{compare_metric(stats1.l1_dcache_miss_rate, stats2.l1_dcache_miss_rate, lower_is_better=True)}"
    )

    print()

    print(
        f"{'LLC Loads':<35} {format_number(stats1.llc_loads):>15} "
        f"{format_number(stats2.llc_loads):>15} "
        f"{compare_metric(stats1.llc_loads, stats2.llc_loads, lower_is_better=True)}"
    )
    print(
        f"{'LLC Misses':<35} {format_number(stats1.llc_misses):>15} "
        f"{format_number(stats2.llc_misses):>15} "
        f"{compare_metric(stats1.llc_misses, stats2.llc_misses, lower_is_better=True)}"
    )
    print(
        f"{'LLC Miss Rate':<35} {format_percentage(stats1.llc_miss_rate):>15} "
        f"{format_percentage(stats2.llc_miss_rate):>15} "
        f"{compare_metric(stats1.llc_miss_rate, stats2.llc_miss_rate, lower_is_better=True)}"
    )

    print()
    print(
        f"{'Test Duration (seconds)':<35} {stats1.time_elapsed:>15.2f} {stats2.time_elapsed:>15.2f}"
    )


def print_executive_summary(
    stack1_name: str, stats1: PerfStats, stack2_name: str, stats2: PerfStats
):
    """
    Executive performance summary comparing stack2 vs stack1
    """

    def pct_change(a, b):
        if a == 0:
            return None
        return ((b - a) / a) * 100

    print_header("EXECUTIVE PERFORMANCE SUMMARY")

    summary_lines = []

    # CPU cost summary
    cpu_time_change = pct_change(stats1.task_clock_ms, stats2.task_clock_ms)
    if cpu_time_change is not None:
        factor = (
            stats1.task_clock_ms / stats2.task_clock_ms
            if stats2.task_clock_ms
            else None
        )
        if cpu_time_change < 0:
            summary_lines.append(
                f"✓ {stack2_name} uses {abs(cpu_time_change):.1f}% less CPU time "
                f"(~{factor:.2f}× more efficient)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} uses {cpu_time_change:.1f}% more CPU time "
                f"(~{1/factor:.2f}× less efficient)"
            )

    # Scheduling / RT behavior
    ctx_change = pct_change(stats1.context_switches, stats2.context_switches)
    mig_change = pct_change(stats1.cpu_migrations, stats2.cpu_migrations)

    if ctx_change is not None:
        if ctx_change < 0:
            summary_lines.append(
                f"✓ {stack2_name} triggers {abs(ctx_change):.1f}% fewer context switches "
                "(better real-time behavior)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} triggers {ctx_change:.1f}% more context switches "
                "(more scheduler overhead)"
            )

    if mig_change is not None:
        if mig_change < 0:
            summary_lines.append(
                f"✓ {stack2_name} performs {abs(mig_change):.1f}% fewer CPU migrations "
                "(better CPU locality)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} performs {mig_change:.1f}% more CPU migrations "
                "(worse cache locality)"
            )

    # Efficiency
    ipc_change = pct_change(stats1.ipc, stats2.ipc)
    if ipc_change is not None:
        if ipc_change > 0:
            summary_lines.append(
                f"✓ {stack2_name} has {ipc_change:.1f}% higher IPC "
                "(better CPU efficiency)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} has {abs(ipc_change):.1f}% lower IPC "
                "(worse CPU efficiency)"
            )

    # Cache behavior
    l1_miss_change = pct_change(stats1.l1_dcache_miss_rate, stats2.l1_dcache_miss_rate)
    llc_miss_change = pct_change(stats1.llc_miss_rate, stats2.llc_miss_rate)

    if l1_miss_change is not None:
        if l1_miss_change < 0:
            summary_lines.append(
                f"✓ {stack2_name} has better L1 cache locality "
                f"({abs(l1_miss_change):.1f}% fewer misses)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} has worse L1 cache locality "
                f"({l1_miss_change:.1f}% more misses)"
            )

    if llc_miss_change is not None:
        if llc_miss_change < 0:
            summary_lines.append(
                f"✓ {stack2_name} has better LLC cache locality "
                f"({abs(llc_miss_change):.1f}% fewer misses)"
            )
        else:
            summary_lines.append(
                f"✗ {stack2_name} has worse LLC cache locality "
                f"({llc_miss_change:.1f}% more misses)"
            )

    # Print summary
    print(
        f"{ColorCode.BOLD.value}{stack2_name} vs {stack1_name} — Key Findings:{ColorCode.RESET.value}\n"
    )

    for line in summary_lines:
        color = ColorCode.GREEN.value if line.startswith("✓") else ColorCode.RED.value
        print(f"{color}{line}{ColorCode.RESET.value}")

    print()


def main():
    """Main entry point"""
    if len(sys.argv) < 5:
        print(
            f"Usage: {sys.argv[0]} <stack1_name> <stack1_stat> <stack2_name> <stack2_stat>"
        )
        print(f"\nExample:")
        print(f"  {sys.argv[0]} KickCAT kickcat_perf_stat.txt SOEM soem_perf_stat.txt")
        sys.exit(1)

    stack1_name = sys.argv[1]
    stack1_stat_path = Path(sys.argv[2])
    stack2_name = sys.argv[3]
    stack2_stat_path = Path(sys.argv[4])

    print(
        f"\n{ColorCode.BOLD.value}{ColorCode.CYAN.value}EtherCAT Stack Performance Comparison Tool{ColorCode.RESET.value}"
    )
    print(
        f"Comparing: {ColorCode.BOLD.value}{stack1_name}{ColorCode.RESET.value} vs "
        f"{ColorCode.BOLD.value}{stack2_name}{ColorCode.RESET.value}\n"
    )

    # Parse perf stat files
    print("Parsing perf stat files...")
    stats1 = parse_perf_stat(stack1_stat_path)
    stats2 = parse_perf_stat(stack2_stat_path)

    if not stats1 or not stats2:
        print(
            f"{ColorCode.RED.value}Error: Failed to parse perf stat files{ColorCode.RESET.value}"
        )
        sys.exit(1)

    # Print comparisons
    print_stats_comparison(stack1_name, stats1, stack2_name, stats2)

    # Executive summary
    print_executive_summary(stack1_name, stats1, stack2_name, stats2)

    print(
        f"\n{ColorCode.BOLD.value}{ColorCode.GREEN.value}Analysis complete!{ColorCode.RESET.value}\n"
    )


if __name__ == "__main__":
    main()
