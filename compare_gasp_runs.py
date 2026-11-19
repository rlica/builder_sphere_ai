#!/usr/bin/env python3
"""Compare GASP binary run outputs between old and new builder_sphere runs.

Reads the data structure dynamically from config.json (mirroring
gaspDataWriter::initializeWriterStructures and addBuiltEvent in
src/dataWriter.cpp), validates GASP block/event framing, computes per-type and
per-LUT statistics, and performs resynchronized stream alignment to classify
and display structural differences across the first N events.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import difflib
import json
from pathlib import Path
import struct
import sys
from typing import Any, Iterator

BLOCK_WORDS = 8192
BLOCK_BYTES = BLOCK_WORDS * 2
RECORD_HEADER_WORDS = 16
FILE_HEADER_MAGIC = 0x4748  # "GH" (little-endian 0x48, 0x47)
RECORD_HEADER_MAGIC = 0x4758  # "GX" (little-endian 0x58, 0x47)
EVENT_MARKER_MASK = 0xF000
EVENT_SIZE_MASK = 0x0FFF
EXCLUDED_DISCOVERY_SUFFIXES = {".json", ".txt", ".py", ".md", ".cap"}


@dataclass(frozen=True)
class OutputTypeSpec:
    mask_index: int
    config_type: int
    name: str
    constituent_types: tuple[int, ...]
    data_types: tuple[str, ...]
    field_labels: tuple[str, ...]
    constituent_slices: tuple[tuple[int, tuple[int, ...]], ...]
    words_per_hit: int


@dataclass(frozen=True)
class ChannelInfo:
    det_type: int
    lut: int
    board: int
    channel: int
    delay: int
    coinc_pair: int


@dataclass
class GaspSchema:
    config_path: Path
    output_types: list[OutputTypeSpec]
    type_to_mask_index: dict[int, int]
    event_mask_size: int
    channel_map: dict[tuple[int, int], ChannelInfo]
    configured_luts_by_type: dict[int, set[int]]
    partner_map: dict[tuple[int, int, int], ChannelInfo]
    coinc_window: int
    time_unit_multiplier: int
    coinc_window_ps: int
    coinc_mask: list[list[int]]
    type_priority: list[int]
    anti_coinc_flag: list[int]
    absolute_time: dict[str, Any]

    def format_channel(self, cfg_type: int, lut: int) -> str:
        spec = self.output_types[self.type_to_mask_index[cfg_type]]
        unique_ctypes = []
        for ct in spec.constituent_types:
            if ct not in unique_ctypes:
                unique_ctypes.append(ct)

        parts: list[str] = []
        primary = self.channel_map.get((cfg_type, lut))
        if primary is not None:
            parts.append(
                f"brd={primary.board} ch={primary.channel} delay={primary.delay} pair={primary.coinc_pair}"
            )
            for ct in unique_ctypes:
                if ct == cfg_type:
                    continue
                partner = self.partner_map.get((cfg_type, lut, ct))
                if partner is not None:
                    parts.append(
                        f"t{ct}:(brd={partner.board} ch={partner.channel} delay={partner.delay})"
                    )
        else:
            parts.append("unconfigured in detector map")
        return ", ".join(parts)


@dataclass(frozen=True)
class DetectorHit:
    mask_index: int
    config_type: int
    type_name: str
    lut: int
    fields: tuple[int, ...]


@dataclass
class GaspEvent:
    event_index: int
    block_index: int
    word_offset: int
    byte_offset: int
    declared_size: int
    scaled_time: int
    event_mask: tuple[int, ...]
    hits_by_type: tuple[tuple[DetectorHit, ...], ...]
    raw_words: tuple[int, ...]
    issues: tuple[str, ...] = ()

    def hit_map(self) -> dict[tuple[int, int], DetectorHit]:
        result: dict[tuple[int, int], DetectorHit] = {}
        for type_hits in self.hits_by_type:
            for hit in type_hits:
                result[(hit.config_type, hit.lut)] = hit
        return result


@dataclass
class FileHeaderInfo:
    file_path: Path
    file_size: int
    total_blocks: int
    remainder_bytes: int
    run_number: int
    magic: int
    header_words: tuple[int, ...]
    valid: bool
    issues: list[str] = field(default_factory=list)


@dataclass
class ReadResult:
    header: FileHeaderInfo
    events: list[GaspEvent]
    blocks_read: int
    total_event_words: int
    block_issues: list[str]
    violation_counts: Counter[str]
    eof_reached: bool


def load_gasp_schema(config_path: Path) -> GaspSchema:
    with config_path.open("r", encoding="utf-8") as fh:
        cfg = json.load(fh)

    out_fmt = cfg.get("output", {}).get("fileFormat")
    if out_fmt != "gasp":
        raise ValueError(
            f"Expected output.fileFormat == 'gasp' in {config_path}, got {out_fmt!r}"
        )

    gasp_cfg = cfg["gasp"]
    type_output_format: list[str] = gasp_cfg["typeOutputFormat"]
    detectors_cfg: dict[str, dict[str, Any]] = gasp_cfg["detectors"]

    output_types: list[OutputTypeSpec] = []
    type_to_mask_index: dict[int, int] = {}

    for cfg_type, fmt_name in enumerate(type_output_format):
        det_spec = detectors_cfg[fmt_name]
        c_types = tuple(int(x) for x in det_spec["type"])
        d_types = tuple(str(x) for x in det_spec["dataType"])
        if len(c_types) == 0:
            continue
        if len(c_types) != len(d_types):
            raise ValueError(
                f"Mismatched type/dataType lengths for gasp.detectors[{fmt_name!r}]"
            )

        distinct_types = set(c_types)
        if len(distinct_types) > 1:
            labels = tuple(f"t{ct}:{dt}" for ct, dt in zip(c_types, d_types))
        else:
            labels = d_types

        grouped_indices: dict[int, list[int]] = defaultdict(list)
        for idx, ct in enumerate(c_types):
            grouped_indices[ct].append(idx)
        c_slices = tuple((ct, tuple(idxs)) for ct, idxs in grouped_indices.items())

        mask_idx = len(output_types)
        type_to_mask_index[cfg_type] = mask_idx
        output_types.append(
            OutputTypeSpec(
                mask_index=mask_idx,
                config_type=cfg_type,
                name=fmt_name,
                constituent_types=c_types,
                data_types=d_types,
                field_labels=labels,
                constituent_slices=c_slices,
                words_per_hit=1 + len(d_types),
            )
        )

    det_cfg = cfg["detector"]
    types_grid: list[list[int]] = det_cfg["type"]
    luts_grid: list[list[int]] = det_cfg["lut"]
    delays_grid: list[list[int]] = det_cfg["delay"]
    pairs_grid: list[list[int]] = det_cfg["coincPairs"]

    channel_map: dict[tuple[int, int], ChannelInfo] = {}
    configured_luts_by_type: dict[int, set[int]] = defaultdict(set)
    pair_lookup: dict[tuple[int, int], ChannelInfo] = {}

    for brd, (t_row, l_row, d_row, p_row) in enumerate(
        zip(types_grid, luts_grid, delays_grid, pairs_grid)
    ):
        for ch, (dt, lut, delay, pair) in enumerate(zip(t_row, l_row, d_row, p_row)):
            if dt == -1:
                continue
            info = ChannelInfo(
                det_type=int(dt),
                lut=int(lut),
                board=brd,
                channel=ch,
                delay=int(delay),
                coinc_pair=int(pair),
            )
            channel_map[(info.det_type, info.lut)] = info
            configured_luts_by_type[info.det_type].add(info.lut)
            if info.coinc_pair != 0:
                pair_lookup[(info.det_type, info.coinc_pair)] = info

    partner_map: dict[tuple[int, int, int], ChannelInfo] = {}
    for (dt, lut), info in channel_map.items():
        if info.coinc_pair == 0:
            continue
        for (p_type, p_pair), p_info in pair_lookup.items():
            if p_pair == info.coinc_pair and p_type != dt:
                partner_map[(dt, lut, p_type)] = p_info

    build_cfg = cfg["build"]
    general_cfg = cfg["general"]
    coinc_window = int(build_cfg["coincWindow"])
    time_unit_mult = int(general_cfg["timeUnitMultiplier"])

    return GaspSchema(
        config_path=config_path,
        output_types=output_types,
        type_to_mask_index=type_to_mask_index,
        event_mask_size=len(output_types),
        channel_map=channel_map,
        configured_luts_by_type=dict(configured_luts_by_type),
        partner_map=partner_map,
        coinc_window=coinc_window,
        time_unit_multiplier=time_unit_mult,
        coinc_window_ps=coinc_window * time_unit_mult,
        coinc_mask=[[int(x) for x in row] for row in build_cfg["coincMask"]],
        type_priority=[int(x) for x in build_cfg["typePriority"]],
        anti_coinc_flag=[int(x) for x in build_cfg.get("antiCoincFlag", [])],
        absolute_time=dict(build_cfg.get("absoluteTime", {})),
    )


def _validate_file_header(file_path: Path, block0: bytes) -> FileHeaderInfo:
    file_size = file_path.stat().st_size
    total_blocks = file_size // BLOCK_BYTES
    remainder_bytes = file_size % BLOCK_BYTES
    issues: list[str] = []

    if len(block0) < BLOCK_BYTES:
        issues.append(f"truncated file header block ({len(block0)} bytes)")
        return FileHeaderInfo(
            file_path=file_path,
            file_size=file_size,
            total_blocks=total_blocks,
            remainder_bytes=remainder_bytes,
            run_number=-1,
            magic=0,
            header_words=(),
            valid=False,
            issues=issues,
        )

    words = struct.unpack(f"<{BLOCK_WORDS}H", block0)
    hdr = words[:5]
    run_number = hdr[2]
    magic = hdr[3]
    if hdr[0] != 0x0010 or hdr[1] != 0x0001 or magic != FILE_HEADER_MAGIC or hdr[4] != 0x0010:
        issues.append(
            f"unexpected file header words {[hex(x) for x in hdr]} (expected 0x10,0x1,run,0x4748,0x10)"
        )
    if any(w != 0 for w in words[5:]):
        issues.append("non-zero bytes found in block 0 padding tail")
    if remainder_bytes != 0:
        issues.append(f"file size not a multiple of {BLOCK_BYTES} bytes (remainder={remainder_bytes})")

    return FileHeaderInfo(
        file_path=file_path,
        file_size=file_size,
        total_blocks=total_blocks,
        remainder_bytes=remainder_bytes,
        run_number=run_number,
        magic=magic,
        header_words=hdr,
        valid=(len(issues) == 0),
        issues=issues,
    )


def _check_coinc_mask(
    schema: GaspSchema, hits_by_type: tuple[tuple[DetectorHit, ...], ...]
) -> bool:
    if not schema.coinc_mask:
        return True
    max_len = max(len(row) for row in schema.coinc_mask)
    counts = [0] * max_len
    for spec, type_hits in zip(schema.output_types, hits_by_type):
        for hit in type_hits:
            for c_type, field_idxs in spec.constituent_slices:
                if c_type < max_len and any(hit.fields[i] != 0 for i in field_idxs):
                    counts[c_type] += 1
    for row in schema.coinc_mask:
        if all(counts[i] >= req for i, req in enumerate(row)):
            return True
    return False


def _decode_event(
    schema: GaspSchema,
    event_index: int,
    block_index: int,
    pos: int,
    words: tuple[int, ...],
) -> tuple[GaspEvent | None, int, str | None]:
    w0 = words[pos]
    if (w0 & EVENT_MARKER_MASK) != EVENT_MARKER_MASK:
        return (
            None,
            pos,
            f"block {block_index} word {pos}: invalid event marker {w0:#06x}",
        )

    declared_size = w0 & EVENT_SIZE_MASK
    total_words = 1 + declared_size
    if pos + total_words > BLOCK_WORDS:
        return (
            None,
            pos,
            f"block {block_index} word {pos}: event size {declared_size} crosses block boundary",
        )

    raw_words = words[pos : pos + total_words]
    issues: list[str] = []

    if declared_size < 1 + schema.event_mask_size:
        issues.append(
            f"event too small ({declared_size} words < header+mask {1 + schema.event_mask_size})"
        )
        ev = GaspEvent(
            event_index=event_index,
            block_index=block_index,
            word_offset=pos,
            byte_offset=block_index * BLOCK_BYTES + pos * 2,
            declared_size=declared_size,
            scaled_time=raw_words[1] if len(raw_words) > 1 else 0,
            event_mask=(),
            hits_by_type=(),
            raw_words=raw_words,
            issues=tuple(issues),
        )
        return ev, pos + total_words, None

    scaled_time = raw_words[1]
    event_mask = raw_words[2 : 2 + schema.event_mask_size]
    expected_size = 1 + schema.event_mask_size + sum(
        cnt * spec.words_per_hit for cnt, spec in zip(event_mask, schema.output_types)
    )
    if declared_size != expected_size:
        issues.append(f"size_mismatch(declared={declared_size}, expected={expected_size})")

    cursor = 2 + schema.event_mask_size
    hits_by_type_list: list[tuple[DetectorHit, ...]] = []

    for cnt, spec in zip(event_mask, schema.output_types):
        type_hits: list[DetectorHit] = []
        prev_lut = -1
        configured_luts = schema.configured_luts_by_type.get(spec.config_type, set())
        for _ in range(cnt):
            if cursor + spec.words_per_hit > len(raw_words):
                issues.append(f"payload_truncated({spec.name})")
                break
            lut = raw_words[cursor]
            fields = raw_words[cursor + 1 : cursor + spec.words_per_hit]
            cursor += spec.words_per_hit

            if lut <= prev_lut:
                issues.append(f"non_ascending_lut({spec.name}:{prev_lut}->{lut})")
            prev_lut = lut

            if lut not in configured_luts:
                issues.append(f"unconfigured_lut({spec.name}:lut={lut})")

            type_hits.append(
                DetectorHit(
                    mask_index=spec.mask_index,
                    config_type=spec.config_type,
                    type_name=spec.name,
                    lut=lut,
                    fields=fields,
                )
            )
        hits_by_type_list.append(tuple(type_hits))

    hits_by_type = tuple(hits_by_type_list)
    if declared_size == expected_size and not _check_coinc_mask(schema, hits_by_type):
        issues.append("coinc_mask_violation")

    ev = GaspEvent(
        event_index=event_index,
        block_index=block_index,
        word_offset=pos,
        byte_offset=block_index * BLOCK_BYTES + pos * 2,
        declared_size=declared_size,
        scaled_time=scaled_time,
        event_mask=event_mask,
        hits_by_type=hits_by_type,
        raw_words=raw_words,
        issues=tuple(issues),
    )
    return ev, pos + total_words, None


def iter_gasp_events(
    file_path: Path, schema: GaspSchema
) -> tuple[FileHeaderInfo, Iterator[GaspEvent], dict[str, Any]]:
    """Open a GASP binary file and stream decoded events block-by-block."""
    fh = file_path.open("rb")
    block0 = fh.read(BLOCK_BYTES)
    header = _validate_file_header(file_path, block0)
    state: dict[str, Any] = {
        "blocks_read": 1 if block0 else 0,
        "total_event_words": 0,
        "block_issues": list(header.issues),
        "violation_counts": Counter(),
        "eof_reached": False,
    }

    def _generator() -> Iterator[GaspEvent]:
        event_idx = 0
        block_idx = 1
        try:
            while True:
                raw_block = fh.read(BLOCK_BYTES)
                if not raw_block:
                    state["eof_reached"] = True
                    break
                state["blocks_read"] += 1
                if len(raw_block) < BLOCK_BYTES:
                    state["block_issues"].append(
                        f"block {block_idx}: truncated block ({len(raw_block)} bytes)"
                    )
                    break
                words = struct.unpack(f"<{BLOCK_WORDS}H", raw_block)
                rec_hdr = words[:5]
                if (
                    rec_hdr[0] != 0x0010
                    or rec_hdr[1] != 0x0000
                    or rec_hdr[2] != 0x0000
                    or rec_hdr[3] != RECORD_HEADER_MAGIC
                    or rec_hdr[4] != RECORD_HEADER_WORDS
                ):
                    state["block_issues"].append(
                        f"block {block_idx}: unexpected record header {[hex(x) for x in rec_hdr]}"
                    )
                if any(w != 0 for w in words[5:RECORD_HEADER_WORDS]):
                    state["block_issues"].append(
                        f"block {block_idx}: non-zero words in record header padding [5..15]"
                    )

                pos = RECORD_HEADER_WORDS
                while pos < BLOCK_WORDS:
                    if words[pos] == 0x0000:
                        if any(w != 0 for w in words[pos:]):
                            state["block_issues"].append(
                                f"block {block_idx}: non-zero trailing word after zero padding at word {pos}"
                            )
                        break
                    ev, next_pos, err = _decode_event(schema, event_idx, block_idx, pos, words)
                    if err is not None:
                        state["block_issues"].append(err)
                        break
                    assert ev is not None
                    state["total_event_words"] += len(ev.raw_words)
                    for issue in ev.issues:
                        tag = issue.split("(", 1)[0]
                        state["violation_counts"][tag] += 1
                    yield ev
                    event_idx += 1
                    pos = next_pos

                block_idx += 1
        finally:
            fh.close()

    return header, _generator(), state


def read_gasp_events(
    file_path: Path, schema: GaspSchema, max_events: int
) -> ReadResult:
    header, gen, state = iter_gasp_events(file_path, schema)
    events: list[GaspEvent] = []
    try:
        for ev in gen:
            events.append(ev)
            if len(events) >= max_events:
                break
    finally:
        gen.close()

    return ReadResult(
        header=header,
        events=events,
        blocks_read=state["blocks_read"],
        total_event_words=state["total_event_words"],
        block_issues=state["block_issues"],
        violation_counts=state["violation_counts"],
        eof_reached=state["eof_reached"],
    )


def compute_aggregate_stats(
    events: list[GaspEvent], schema: GaspSchema
) -> dict[str, Any]:
    n = len(events)
    if n == 0:
        return {
            "count": 0,
            "word_size": {"min": 0, "mean": 0.0, "max": 0, "total_words": 0},
            "scaled_time": {"min": 0, "max": 0, "ge_0x8000": 0},
            "by_type": {},
            "lut_counts": {},
            "field_ranges": {},
        }

    sizes = [len(ev.raw_words) for ev in events]
    scaled_times = [ev.scaled_time for ev in events]

    by_type: dict[str, dict[str, Any]] = {}
    lut_counts: dict[str, Counter[int]] = {}
    field_mins: dict[tuple[str, str], int] = {}
    field_maxs: dict[tuple[str, str], int] = {}
    field_highbit: Counter[tuple[str, str]] = Counter()

    for spec in schema.output_types:
        m = spec.mask_index
        mults = [ev.event_mask[m] if m < len(ev.event_mask) else 0 for ev in events]
        total_hits = sum(mults)
        events_with_hit = sum(1 for c in mults if c > 0)
        constituent_nonzero: dict[int, int] = {ct: 0 for ct, _ in spec.constituent_slices}
        luts: Counter[int] = Counter()

        for ev in events:
            if m >= len(ev.hits_by_type):
                continue
            for hit in ev.hits_by_type[m]:
                luts[hit.lut] += 1
                for ct, idxs in spec.constituent_slices:
                    if any(hit.fields[i] != 0 for i in idxs):
                        constituent_nonzero[ct] += 1
                for label, val in zip(spec.field_labels, hit.fields):
                    key = (spec.name, label)
                    if key not in field_mins or val < field_mins[key]:
                        field_mins[key] = val
                    if key not in field_maxs or val > field_maxs[key]:
                        field_maxs[key] = val
                    if val >= 0x8000:
                        field_highbit[key] += 1

        by_type[spec.name] = {
            "config_type": spec.config_type,
            "mask_index": m,
            "total_hits": total_hits,
            "events_with_hit": events_with_hit,
            "mean_mult": total_hits / n,
            "max_mult": max(mults),
            "constituent_nonzero": constituent_nonzero,
            "distinct_luts": len(luts),
            "min_lut": min(luts) if luts else None,
            "max_lut": max(luts) if luts else None,
        }
        lut_counts[spec.name] = luts

    field_ranges = {
        f"{tname}.{fname}": {
            "min": field_mins[(tname, fname)],
            "max": field_maxs[(tname, fname)],
            "ge_0x8000": field_highbit[(tname, fname)],
        }
        for (tname, fname) in field_mins
    }

    return {
        "count": n,
        "word_size": {
            "min": min(sizes),
            "mean": sum(sizes) / n,
            "max": max(sizes),
            "total_words": sum(sizes),
        },
        "scaled_time": {
            "min": min(scaled_times),
            "max": max(scaled_times),
            "ge_0x8000": sum(1 for x in scaled_times if x >= 0x8000),
        },
        "by_type": by_type,
        "lut_counts": {k: dict(sorted(v.items())) for k, v in lut_counts.items()},
        "field_ranges": field_ranges,
    }


def _primary_anchor_signature(
    ev: GaspEvent, schema: GaspSchema
) -> tuple[tuple[int, int, int], ...]:
    """Build a primary-detector (type, lut, energy) signature for event pairing."""
    anchors: list[tuple[int, int, int]] = []
    priority_types = schema.type_priority or [spec.config_type for spec in schema.output_types]
    for p_type in priority_types:
        m = schema.type_to_mask_index.get(p_type)
        if m is None or m >= len(ev.hits_by_type):
            continue
        spec = schema.output_types[m]
        energy_idx = 0
        for idx, dt in enumerate(spec.data_types):
            if dt == "energy" and spec.constituent_types[idx] == p_type:
                energy_idx = idx
                break
        for hit in ev.hits_by_type[m]:
            anchors.append((p_type, hit.lut, hit.fields[energy_idx]))
        if anchors:
            break
    if not anchors:
        for type_hits in ev.hits_by_type:
            for hit in type_hits:
                anchors.append((hit.config_type, hit.lut, hit.fields[0] if hit.fields else 0))
    return tuple(anchors)


def _pair_replace_block(
    old_slice: list[GaspEvent], new_slice: list[GaspEvent], schema: GaspSchema
) -> list[tuple[GaspEvent | None, GaspEvent | None]]:
    """Pair old and new events inside a non-matching SequenceMatcher slice."""
    old_anchors = [_primary_anchor_signature(ev, schema) for ev in old_slice]
    new_anchors = [_primary_anchor_signature(ev, schema) for ev in new_slice]

    used_new: set[int] = set()
    matched_pairs: dict[int, int] = {}

    # Pass 1: exact primary-anchor signature match (same primary detector LUT+energy)
    new_by_anchor: dict[tuple[tuple[int, int, int], ...], list[int]] = defaultdict(list)
    for j, anc in enumerate(new_anchors):
        if anc:
            new_by_anchor[anc].append(j)
    for i, anc in enumerate(old_anchors):
        candidates = new_by_anchor.get(anc)
        if candidates:
            while candidates and candidates[0] in used_new:
                candidates.pop(0)
            if candidates:
                j = candidates.pop(0)
                matched_pairs[i] = j
                used_new.add(j)

    # Pass 2: partial anchor overlap (e.g., an added/dropped primary-type hit in same event)
    old_sets = [set(a) for a in old_anchors]
    new_sets = [set(a) for a in new_anchors]
    for i, o_set in enumerate(old_sets):
        if i in matched_pairs or not o_set:
            continue
        best_j = -1
        best_score = 0
        for j, n_set in enumerate(new_sets):
            if j in used_new or not n_set:
                continue
            overlap = len(o_set & n_set)
            if overlap > best_score:
                best_score = overlap
                best_j = j
        if best_j != -1:
            matched_pairs[i] = best_j
            used_new.add(best_j)

    # Pass 3: positional fallback only if remaining counts match and any detector hit overlaps
    unmatched_old = [i for i in range(len(old_slice)) if i not in matched_pairs]
    unmatched_new = [j for j in range(len(new_slice)) if j not in used_new]
    if len(unmatched_old) == len(unmatched_new):
        for i, j in zip(unmatched_old, unmatched_new):
            o_keys = set(old_slice[i].hit_map().keys())
            n_keys = set(new_slice[j].hit_map().keys())
            if o_keys & n_keys:
                matched_pairs[i] = j
                used_new.add(j)

    # Emit pairs preserving chronological stream order
    result: list[tuple[GaspEvent | None, GaspEvent | None]] = []
    next_new_to_emit = 0
    for i, o_ev in enumerate(old_slice):
        if i in matched_pairs:
            j = matched_pairs[i]
            while next_new_to_emit < j:
                if next_new_to_emit not in used_new:
                    result.append((None, new_slice[next_new_to_emit]))
                next_new_to_emit += 1
            result.append((o_ev, new_slice[j]))
            if next_new_to_emit == j:
                next_new_to_emit += 1
        else:
            result.append((o_ev, None))
    while next_new_to_emit < len(new_slice):
        if next_new_to_emit not in used_new:
            result.append((None, new_slice[next_new_to_emit]))
        next_new_to_emit += 1

    return result


@dataclass
class DiffRecord:
    category: str  # modified_detectors, modified_values_only, only_in_old, only_in_new
    old_ev: GaspEvent | None
    new_ev: GaspEvent | None
    dropped_hits: list[DetectorHit] = field(default_factory=list)
    added_hits: list[DetectorHit] = field(default_factory=list)
    changed_fields: list[str] = field(default_factory=list)


def compare_event_streams(
    old_events: list[GaspEvent], new_events: list[GaspEvent], schema: GaspSchema
) -> dict[str, Any]:
    # Index-by-index check
    min_len = min(len(old_events), len(new_events))
    index_identical = 0
    first_index_diff: int | None = None
    index_mask_diffs = 0
    index_value_diffs = 0
    for idx in range(min_len):
        o_ev = old_events[idx]
        n_ev = new_events[idx]
        if o_ev.raw_words == n_ev.raw_words:
            index_identical += 1
        else:
            if first_index_diff is None:
                first_index_diff = idx
            if o_ev.event_mask != n_ev.event_mask:
                index_mask_diffs += 1
            else:
                index_value_diffs += 1

    # Stream alignment (autojunk=False is essential so common single-HPGe events are preserved)
    old_sigs = [ev.raw_words for ev in old_events]
    new_sigs = [ev.raw_words for ev in new_events]
    matcher = difflib.SequenceMatcher(None, old_sigs, new_sigs, autojunk=False)

    exact_aligned = 0
    exact_shifted = 0
    diff_counts: Counter[str] = Counter()
    dropped_by_det: Counter[tuple[int, str, int]] = Counter()
    added_by_det: Counter[tuple[int, str, int]] = Counter()
    changed_field_counts: Counter[str] = Counter()
    diff_records: list[DiffRecord] = []

    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            span = i2 - i1
            exact_aligned += span
            if i1 != j1:
                exact_shifted += span
            continue

        if tag == "delete":
            for ev in old_events[i1:i2]:
                diff_counts["only_in_old"] += 1
                for type_hits in ev.hits_by_type:
                    for h in type_hits:
                        dropped_by_det[(h.config_type, h.type_name, h.lut)] += 1
                diff_records.append(DiffRecord("only_in_old", ev, None))
            continue

        if tag == "insert":
            for ev in new_events[j1:j2]:
                diff_counts["only_in_new"] += 1
                for type_hits in ev.hits_by_type:
                    for h in type_hits:
                        added_by_det[(h.config_type, h.type_name, h.lut)] += 1
                diff_records.append(DiffRecord("only_in_new", None, ev))
            continue

        # tag == "replace"
        pairs = _pair_replace_block(old_events[i1:i2], new_events[j1:j2], schema)
        for o_ev, n_ev in pairs:
            if o_ev is not None and n_ev is None:
                diff_counts["only_in_old"] += 1
                for type_hits in o_ev.hits_by_type:
                    for h in type_hits:
                        dropped_by_det[(h.config_type, h.type_name, h.lut)] += 1
                diff_records.append(DiffRecord("only_in_old", o_ev, None))
            elif o_ev is None and n_ev is not None:
                diff_counts["only_in_new"] += 1
                for type_hits in n_ev.hits_by_type:
                    for h in type_hits:
                        added_by_det[(h.config_type, h.type_name, h.lut)] += 1
                diff_records.append(DiffRecord("only_in_new", None, n_ev))
            elif o_ev is not None and n_ev is not None:
                o_map = o_ev.hit_map()
                n_map = n_ev.hit_map()
                o_keys = set(o_map.keys())
                n_keys = set(n_map.keys())
                dropped_keys = sorted(o_keys - n_keys)
                added_keys = sorted(n_keys - o_keys)
                common_keys = sorted(o_keys & n_keys)

                changed_fields: list[str] = []
                if o_ev.scaled_time != n_ev.scaled_time:
                    changed_fields.append("scaledTime")
                    changed_field_counts["scaledTime"] += 1

                for k in common_keys:
                    o_hit = o_map[k]
                    n_hit = n_map[k]
                    spec = schema.output_types[o_hit.mask_index]
                    for label, ov, nv in zip(spec.field_labels, o_hit.fields, n_hit.fields):
                        if ov != nv:
                            f_tag = f"{spec.name}.{label}"
                            changed_fields.append(f_tag)
                            changed_field_counts[f_tag] += 1

                if dropped_keys or added_keys:
                    cat = "modified_detectors"
                    dropped_hits = [o_map[k] for k in dropped_keys]
                    added_hits = [n_map[k] for k in added_keys]
                    for h in dropped_hits:
                        dropped_by_det[(h.config_type, h.type_name, h.lut)] += 1
                    for h in added_hits:
                        added_by_det[(h.config_type, h.type_name, h.lut)] += 1
                else:
                    cat = "modified_values_only"
                    dropped_hits = []
                    added_hits = []

                diff_counts[cat] += 1
                diff_records.append(
                    DiffRecord(
                        category=cat,
                        old_ev=o_ev,
                        new_ev=n_ev,
                        dropped_hits=dropped_hits,
                        added_hits=added_hits,
                        changed_fields=changed_fields,
                    )
                )

    return {
        "index_cmp": {
            "compared": min_len,
            "identical": index_identical,
            "first_diff": first_index_diff,
            "mask_diffs": index_mask_diffs,
            "value_diffs": index_value_diffs,
        },
        "aligned_cmp": {
            "exact_aligned": exact_aligned,
            "exact_shifted": exact_shifted,
            "diff_counts": dict(diff_counts),
            "dropped_by_det": dropped_by_det,
            "added_by_det": added_by_det,
            "changed_field_counts": dict(changed_field_counts),
            "diff_records": diff_records,
        },
    }


def format_event_lines(
    ev: GaspEvent, schema: GaspSchema, indent: str = "    "
) -> list[str]:
    mask_str = ", ".join(
        f"{spec.name}={cnt}" for spec, cnt in zip(schema.output_types, ev.event_mask)
    )
    lines = [
        f"{indent}idx={ev.event_index} blk={ev.block_index} word={ev.word_offset} "
        f"(byte=0x{ev.byte_offset:x}) size={ev.declared_size} "
        f"scaledTime={ev.scaled_time} mask=[{mask_str}]"
    ]
    if ev.issues:
        lines.append(f"{indent}  ISSUES: {', '.join(ev.issues)}")
    for spec, type_hits in zip(schema.output_types, ev.hits_by_type):
        for hit in type_hits:
            fields_str = " ".join(
                f"{lbl}={val}" for lbl, val in zip(spec.field_labels, hit.fields)
            )
            chan_str = schema.format_channel(hit.config_type, hit.lut)
            lines.append(
                f"{indent}  {spec.name:<8} LUT={hit.lut:<3} {fields_str}  [{chan_str}]"
            )
    return lines


def find_first_divergence(
    old_path: Path, new_path: Path, schema: GaspSchema
) -> dict[str, Any]:
    _, old_gen, _ = iter_gasp_events(old_path, schema)
    _, new_gen, _ = iter_gasp_events(new_path, schema)
    idx = 0
    try:
        while True:
            o_ev = next(old_gen, None)
            n_ev = next(new_gen, None)
            if o_ev is None and n_ev is None:
                return {"status": "identical_eof", "events_checked": idx}
            if o_ev is None or n_ev is None:
                return {
                    "status": "eof_mismatch",
                    "event_index": idx,
                    "old_ev": o_ev,
                    "new_ev": n_ev,
                }
            if o_ev.raw_words != n_ev.raw_words:
                return {
                    "status": "diverged",
                    "event_index": idx,
                    "old_ev": o_ev,
                    "new_ev": n_ev,
                }
            idx += 1
    finally:
        old_gen.close()
        new_gen.close()


def print_schema_banner(schema: GaspSchema) -> None:
    print("=" * 88)
    print(f"GASP Schema loaded from: {schema.config_path}")
    print(
        f"Coinc window: {schema.coinc_window} * {schema.time_unit_multiplier} = "
        f"{schema.coinc_window_ps} ps | eventMaskSize = {schema.event_mask_size} | "
        f"coincMask = {schema.coinc_mask} | typePriority = {schema.type_priority}"
    )
    print("-" * 88)
    print(
        f"{'mask[i]':<8} {'cfgType':<8} {'format':<10} {'words/hit':<10} "
        f"{'LUTs (cfg)':<14} {'boards':<12} fields"
    )
    for spec in schema.output_types:
        luts = sorted(schema.configured_luts_by_type.get(spec.config_type, set()))
        lut_span = f"{luts[0]}..{luts[-1]} ({len(luts)})" if luts else "none"
        boards = sorted(
            {
                schema.channel_map[(spec.config_type, lut)].board
                for lut in luts
                if (spec.config_type, lut) in schema.channel_map
            }
        )
        brd_str = ",".join(str(b) for b in boards) if boards else "-"
        print(
            f"  [{spec.mask_index}]    {spec.config_type:<8} {spec.name:<10} "
            f"{spec.words_per_hit:<10} {lut_span:<14} {brd_str:<12} "
            f"{', '.join(spec.field_labels)}"
        )
    print("=" * 88)


def _print_lut_diffs(
    old_stats: dict[str, Any], new_stats: dict[str, Any], schema: GaspSchema
) -> None:
    print("\nPer-LUT Hit Distribution Comparison:")
    any_lut_diff = False
    for spec in schema.output_types:
        o_luts: dict[int, int] = old_stats["lut_counts"].get(spec.name, {})
        n_luts: dict[int, int] = new_stats["lut_counts"].get(spec.name, {})
        all_luts = sorted(set(o_luts.keys()) | set(n_luts.keys()))
        o_range = f"{min(o_luts)}..{max(o_luts)} ({len(o_luts)} active)" if o_luts else "none"
        n_range = f"{min(n_luts)}..{max(n_luts)} ({len(n_luts)} active)" if n_luts else "none"
        diffs = [lut for lut in all_luts if o_luts.get(lut, 0) != n_luts.get(lut, 0)]
        print(
            f"  {spec.name:<9} (cfgType={spec.config_type}): "
            f"Old LUTs=[{o_range}] | New LUTs=[{n_range}] | differing LUTs={len(diffs)}"
        )
        if diffs:
            any_lut_diff = True
            for lut in diffs:
                oc = o_luts.get(lut, 0)
                nc = n_luts.get(lut, 0)
                chan_str = schema.format_channel(spec.config_type, lut)
                print(
                    f"    LUT={lut:<3} old={oc:<5} new={nc:<5} delta={nc - oc:+5}  [{chan_str}]"
                )
    if not any_lut_diff:
        print("  All LUT hit counts match identically.")


def print_run_report(
    run_name: str,
    old_res: ReadResult,
    new_res: ReadResult,
    old_stats: dict[str, Any],
    new_stats: dict[str, Any],
    cmp_res: dict[str, Any],
    schema: GaspSchema,
    num_events: int,
    max_diffs: int,
    dump_events: int,
    find_first_flag: bool,
) -> dict[str, Any] | None:
    print(f"\n### RUN: {run_name} (comparing first {num_events} events) ###")

    oh = old_res.header
    nh = new_res.header
    size_delta = nh.file_size - oh.file_size
    blk_delta = nh.total_blocks - oh.total_blocks
    print("\n1. File & Structural Integrity:")
    print(
        f"  Old: {oh.file_path} | {oh.file_size:,} B ({oh.total_blocks:,} blocks) | "
        f"hdr(run={oh.run_number}, magic={oh.magic:#06x}, valid={oh.valid}) | "
        f"parsed={len(old_res.events)} events across {old_res.blocks_read} block(s)"
    )
    print(
        f"  New: {nh.file_path} | {nh.file_size:,} B ({nh.total_blocks:,} blocks) | "
        f"hdr(run={nh.run_number}, magic={nh.magic:#06x}, valid={nh.valid}) | "
        f"parsed={len(new_res.events)} events across {new_res.blocks_read} block(s)"
    )
    print(
        f"  Delta (New - Old): {size_delta:+,} B ({blk_delta:+,} blocks)"
    )
    if old_res.block_issues or old_res.violation_counts:
        print(
            f"  Old violations: block_issues={old_res.block_issues} "
            f"event_issues={dict(old_res.violation_counts)}"
        )
    else:
        print("  Old violations: none")
    if new_res.block_issues or new_res.violation_counts:
        print(
            f"  New violations: block_issues={new_res.block_issues} "
            f"event_issues={dict(new_res.violation_counts)}"
        )
    else:
        print("  New violations: none")

    if dump_events > 0:
        k = min(dump_events, len(old_res.events), len(new_res.events))
        print(f"\n  First {k} Decoded Events:")
        for idx in range(k):
            print(f"  -- Event {idx} --")
            print("    [OLD]")
            for line in format_event_lines(old_res.events[idx], schema, indent="      "):
                print(line)
            print("    [NEW]")
            for line in format_event_lines(new_res.events[idx], schema, indent="      "):
                print(line)

    print("\n2. Aggregate Statistics (first N events):")
    ow = old_stats["word_size"]
    nw = new_stats["word_size"]
    print(
        f"  Event word size: Old min/mean/max = {ow['min']}/{ow['mean']:.2f}/{ow['max']} "
        f"(total {ow['total_words']} words) | "
        f"New min/mean/max = {nw['min']}/{nw['mean']:.2f}/{nw['max']} "
        f"(total {nw['total_words']} words, delta={nw['total_words'] - ow['total_words']:+})"
    )
    ost = old_stats["scaled_time"]
    nst = new_stats["scaled_time"]
    print(
        f"  scaledTime (uint16): Old [{ost['min']}..{ost['max']}] (>=0x8000: {ost['ge_0x8000']}) | "
        f"New [{nst['min']}..{nst['max']}] (>=0x8000: {nst['ge_0x8000']})"
    )

    print(
        f"\n  {'type':<10} {'old_hits':>9} {'new_hits':>9} {'delta':>8} "
        f"{'old_ev%':>8} {'new_ev%':>8} {'old_mean':>9} {'new_mean':>9} {'max_m':>7}"
    )
    for spec in schema.output_types:
        ob = old_stats["by_type"][spec.name]
        nb = new_stats["by_type"][spec.name]
        o_pct = 100.0 * ob["events_with_hit"] / max(1, old_stats["count"])
        n_pct = 100.0 * nb["events_with_hit"] / max(1, new_stats["count"])
        print(
            f"  {spec.name:<10} {ob['total_hits']:>9} {nb['total_hits']:>9} "
            f"{nb['total_hits'] - ob['total_hits']:>+8} "
            f"{o_pct:>7.1f}% {n_pct:>7.1f}% "
            f"{ob['mean_mult']:>9.3f} {nb['mean_mult']:>9.3f} "
            f"{ob['max_mult']}/{nb['max_mult']:>4}"
        )
        if len(spec.constituent_slices) > 1:
            c_parts = []
            for ct, _ in spec.constituent_slices:
                oc = ob["constituent_nonzero"].get(ct, 0)
                nc = nb["constituent_nonzero"].get(ct, 0)
                c_parts.append(f"t{ct}: old={oc} new={nc} ({nc - oc:+})")
            print(f"    constituent non-zero sub-hits: {', '.join(c_parts)}")

    _print_lut_diffs(old_stats, new_stats, schema)

    # Field ranges
    all_field_keys = sorted(
        set(old_stats["field_ranges"].keys()) | set(new_stats["field_ranges"].keys())
    )
    if all_field_keys:
        print("\n  Field Ranges (uint16):")
        for fk in all_field_keys:
            ofr = old_stats["field_ranges"].get(fk)
            nfr = new_stats["field_ranges"].get(fk)
            o_s = f"[{ofr['min']}..{ofr['max']}]" if ofr else "-"
            n_s = f"[{nfr['min']}..{nfr['max']}]" if nfr else "-"
            hb_s = ""
            if (ofr and ofr["ge_0x8000"]) or (nfr and nfr["ge_0x8000"]):
                hb_s = (
                    f" (>=0x8000: old={ofr['ge_0x8000'] if ofr else 0}, "
                    f"new={nfr['ge_0x8000'] if nfr else 0})"
                )
            print(f"    {fk:<22} Old={o_s:<16} New={n_s:<16}{hb_s}")

    # Index & aligned diffs
    ic = cmp_res["index_cmp"]
    ac = cmp_res["aligned_cmp"]
    print("\n3. Index-by-Index & Resynchronized Stream Alignment:")
    print(
        f"  Index-by-index: identical={ic['identical']}/{ic['compared']}, "
        f"first_diff={ic['first_diff']}, mask_diffs={ic['mask_diffs']}, "
        f"value_only_diffs={ic['value_diffs']}"
    )
    print(
        f"  Stream-aligned: exact_aligned={ac['exact_aligned']} "
        f"(index_shifted={ac['exact_shifted']}), "
        f"diff_categories={ac['diff_counts'] or {'none': 0}}"
    )

    dropped_by_det: Counter[tuple[int, str, int]] = ac["dropped_by_det"]
    added_by_det: Counter[tuple[int, str, int]] = ac["added_by_det"]
    if dropped_by_det:
        print("  Top dropped detector hits (present in Old, missing in New):")
        for (ctype, tname, lut), cnt in dropped_by_det.most_common(15):
            print(
                f"    {tname:<9} LUT={lut:<3} count={cnt:<5} [{schema.format_channel(ctype, lut)}]"
            )
    if added_by_det:
        print("  Top added detector hits (missing in Old, present in New):")
        for (ctype, tname, lut), cnt in added_by_det.most_common(15):
            print(
                f"    {tname:<9} LUT={lut:<3} count={cnt:<5} [{schema.format_channel(ctype, lut)}]"
            )
    if ac["changed_field_counts"]:
        print(f"  Changed fields inside matched hits: {ac['changed_field_counts']}")

    diff_records: list[DiffRecord] = ac["diff_records"]
    if diff_records and max_diffs > 0:
        show_n = min(max_diffs, len(diff_records))
        print(f"\n4. Detailed Side-by-Side Diffs (showing {show_n} of {len(diff_records)}):")
        for k, rec in enumerate(diff_records[:show_n]):
            print(f"  -- Diff #{k + 1} [{rec.category}] --")
            if rec.dropped_hits:
                d_str = ", ".join(f"{h.type_name}(LUT={h.lut})" for h in rec.dropped_hits)
                print(f"    Dropped from New: {d_str}")
            if rec.added_hits:
                a_str = ", ".join(f"{h.type_name}(LUT={h.lut})" for h in rec.added_hits)
                print(f"    Added in New    : {a_str}")
            if rec.changed_fields:
                print(f"    Changed fields  : {', '.join(rec.changed_fields)}")
            if rec.old_ev is not None:
                print("    [OLD]")
                for line in format_event_lines(rec.old_ev, schema, indent="      "):
                    print(line)
            if rec.new_ev is not None:
                print("    [NEW]")
                for line in format_event_lines(rec.new_ev, schema, indent="      "):
                    print(line)
    elif not diff_records:
        print("\n4. Detailed Diffs: First N events are 100% bit-for-bit identical.")

    first_div_info: dict[str, Any] | None = None
    if not diff_records and size_delta != 0:
        if find_first_flag:
            print("\n5. Scanning full files for first event-level divergence...")
            first_div_info = find_first_divergence(oh.file_path, nh.file_path, schema)
            if first_div_info["status"] == "diverged":
                print(
                    f"  First divergence at event index {first_div_info['event_index']}:"
                )
                print("    [OLD]")
                for line in format_event_lines(
                    first_div_info["old_ev"], schema, indent="      "
                ):
                    print(line)
                print("    [NEW]")
                for line in format_event_lines(
                    first_div_info["new_ev"], schema, indent="      "
                ):
                    print(line)
            else:
                print(f"  Scan result: {first_div_info}")
        else:
            print(
                "  (Hint: file sizes differ; re-run with --find-first-diff to locate "
                "where divergence starts beyond event N.)"
            )
    elif find_first_flag and diff_records:
        first_idx = ic["first_diff"]
        print(f"\n5. First divergence already within first N events at index {first_idx}.")

    return first_div_info


def discover_runs(old_dir: Path, new_dir: Path, requested_runs: list[str] | None) -> list[str]:
    if requested_runs:
        missing = [
            r for r in requested_runs if not (old_dir / r).is_file() or not (new_dir / r).is_file()
        ]
        if missing:
            raise FileNotFoundError(
                f"Requested run(s) missing in {old_dir} or {new_dir}: {missing}"
            )
        return requested_runs

    def _valid_run_files(directory: Path) -> set[str]:
        names: set[str] = set()
        for p in directory.iterdir():
            if not p.is_file():
                continue
            if p.name.startswith(".") or p.suffix in EXCLUDED_DISCOVERY_SUFFIXES:
                continue
            names.add(p.name)
        return names

    common = sorted(_valid_run_files(old_dir) & _valid_run_files(new_dir))
    if not common:
        raise FileNotFoundError(
            f"No common run files discovered between {old_dir} and {new_dir}"
        )
    return common


def _serialize_json_report(
    schema: GaspSchema,
    run_reports: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "config_path": str(schema.config_path),
        "event_mask_size": schema.event_mask_size,
        "output_types": [
            {
                "mask_index": s.mask_index,
                "config_type": s.config_type,
                "name": s.name,
                "words_per_hit": s.words_per_hit,
                "field_labels": list(s.field_labels),
            }
            for s in schema.output_types
        ],
        "runs": run_reports,
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare old and new GASP builder runs using config.json."
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("output/config.json"),
        help="Path to config.json (default: output/config.json)",
    )
    parser.add_argument(
        "--old-dir",
        type=Path,
        default=Path("output/old"),
        help="Directory with old builder outputs (default: output/old)",
    )
    parser.add_argument(
        "--new-dir",
        type=Path,
        default=Path("output"),
        help="Directory with new builder outputs (default: output)",
    )
    parser.add_argument(
        "--runs",
        nargs="*",
        default=None,
        help="Run filenames to compare (default: auto-discover matching files)",
    )
    parser.add_argument(
        "-n",
        "--num-events",
        type=int,
        default=1000,
        help="Number of events to inspect per run (default: 1000)",
    )
    parser.add_argument(
        "--max-diffs",
        type=int,
        default=10,
        help="Max detailed side-by-side event diffs per run (default: 10)",
    )
    parser.add_argument(
        "--dump-events",
        type=int,
        default=0,
        help="Print the first K decoded events side-by-side (default: 0)",
    )
    parser.add_argument(
        "--find-first-diff",
        action="store_true",
        help="Scan beyond N events if the first N are identical while file sizes differ",
    )
    parser.add_argument(
        "--json-report",
        type=Path,
        default=None,
        help="Optional output path to save structured comparison summary as JSON",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.num_events <= 0:
        print("--num-events must be positive", file=sys.stderr)
        return 2

    schema = load_gasp_schema(args.config)
    runs = discover_runs(args.old_dir, args.new_dir, args.runs)

    print_schema_banner(schema)
    json_runs: list[dict[str, Any]] = []

    for run_name in runs:
        old_path = args.old_dir / run_name
        new_path = args.new_dir / run_name

        old_res = read_gasp_events(old_path, schema, args.num_events)
        new_res = read_gasp_events(new_path, schema, args.num_events)

        old_stats = compute_aggregate_stats(old_res.events, schema)
        new_stats = compute_aggregate_stats(new_res.events, schema)
        cmp_res = compare_event_streams(old_res.events, new_res.events, schema)

        first_div_info = print_run_report(
            run_name=run_name,
            old_res=old_res,
            new_res=new_res,
            old_stats=old_stats,
            new_stats=new_stats,
            cmp_res=cmp_res,
            schema=schema,
            num_events=args.num_events,
            max_diffs=args.max_diffs,
            dump_events=args.dump_events,
            find_first_flag=args.find_first_diff,
        )

        if args.json_report is not None:
            ac = cmp_res["aligned_cmp"]
            json_entry: dict[str, Any] = {
                "run": run_name,
                "old": {
                    "file": str(old_path),
                    "file_size": old_res.header.file_size,
                    "total_blocks": old_res.header.total_blocks,
                    "events_read": len(old_res.events),
                    "block_issues": old_res.block_issues,
                    "violation_counts": dict(old_res.violation_counts),
                    "stats": old_stats,
                },
                "new": {
                    "file": str(new_path),
                    "file_size": new_res.header.file_size,
                    "total_blocks": new_res.header.total_blocks,
                    "events_read": len(new_res.events),
                    "block_issues": new_res.block_issues,
                    "violation_counts": dict(new_res.violation_counts),
                    "stats": new_stats,
                },
                "index_cmp": cmp_res["index_cmp"],
                "aligned_cmp": {
                    "exact_aligned": ac["exact_aligned"],
                    "exact_shifted": ac["exact_shifted"],
                    "diff_counts": ac["diff_counts"],
                    "dropped_by_det": [
                        {
                            "config_type": ct,
                            "type_name": tn,
                            "lut": lut,
                            "count": cnt,
                            "channel": schema.format_channel(ct, lut),
                        }
                        for (ct, tn, lut), cnt in ac["dropped_by_det"].most_common()
                    ],
                    "added_by_det": [
                        {
                            "config_type": ct,
                            "type_name": tn,
                            "lut": lut,
                            "count": cnt,
                            "channel": schema.format_channel(ct, lut),
                        }
                        for (ct, tn, lut), cnt in ac["added_by_det"].most_common()
                    ],
                    "changed_field_counts": ac["changed_field_counts"],
                },
            }
            if first_div_info is not None:
                json_entry["first_divergence"] = {
                    "status": first_div_info["status"],
                    "event_index": first_div_info.get("event_index"),
                }
            json_runs.append(json_entry)

    if args.json_report is not None:
        payload = _serialize_json_report(schema, json_runs)
        args.json_report.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nJSON report written to: {args.json_report}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
