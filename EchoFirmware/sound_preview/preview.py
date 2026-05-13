#!/usr/bin/env python3
"""
Offline render of EchoFirmware/AudioSynth.cpp DSP path (no firmware edits).
Uses stdlib + numpy only; WAV via wave module.

Tables and numeric literals used by the preview are read from the real firmware
sources under EchoFirmware/ (Config.h, Globals.h, Utils.cpp, AudioSynth.cpp).
"""

from __future__ import annotations

import math
import random
import re
import struct
import wave
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np


class FirmwareParseError(RuntimeError):
    pass


def _firmware_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def _read_source(rel: str) -> str:
    path = _firmware_dir() / rel
    if not path.is_file():
        raise FirmwareParseError(f"missing firmware file: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def _strip_c_scalar(tok: str) -> str:
    t = tok.strip().rstrip(";")
    while t and t[-1] in "fFuUlL":
        t = t[:-1]
    return t.strip()


def _parse_float_lit(tok: str) -> float:
    return float(_strip_c_scalar(tok))


def _parse_int_lit(tok: str) -> int:
    return int(float(_strip_c_scalar(tok)))


def _body_after_opening_brace(text: str, open_brace_idx: int) -> str:
    if open_brace_idx < 0 or open_brace_idx >= len(text) or text[open_brace_idx] != "{":
        raise FirmwareParseError("expected '{'")
    depth = 0
    for k in range(open_brace_idx, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace_idx + 1 : k]
    raise FirmwareParseError("unbalanced braces in firmware excerpt")


def _function_inner_body(cpp: str, signature_substr: str) -> str:
    i = cpp.find(signature_substr)
    if i < 0:
        raise FirmwareParseError(f"missing function {signature_substr!r} in source")
    b = cpp.find("{", i)
    return _body_after_opening_brace(cpp, b)


def _if_case_body(inner: str, branch_needle: str) -> str:
    i = inner.find(branch_needle)
    if i < 0:
        raise FirmwareParseError(f"missing branch {branch_needle!r}")
    b = inner.find("{", i)
    return _body_after_opening_brace(inner, b)


def _first_int_array(block: str, array_name: str) -> List[int]:
    esc = re.escape(array_name)
    for prefix in (
        rf"static const int {esc}\[\]",
        rf"int {esc}\[\]",
    ):
        m = re.search(
            rf"{prefix}\s*=\s*\{{(.+?)\}}\s*;",
            block,
            re.DOTALL,
        )
        if not m:
            continue
        parts = [p.strip() for p in m.group(1).replace("\n", " ").split(",")]
        return [int(p) for p in parts if p]
    raise FirmwareParseError(f"int array {array_name}[] not found")


def _first_begin_envelope(block: str) -> Tuple[float, float, float, float]:
    m = re.search(
        r"beginEnvelope\s*\(\s*([\d.f]+)\s*,\s*([\d.f]+)\s*,\s*([\d.f]+)\s*,\s*([\d.f]+)\s*,",
        block,
        re.DOTALL,
    )
    if not m:
        raise FirmwareParseError("beginEnvelope( not found")
    return tuple(_parse_float_lit(m.group(i)) for i in range(1, 5))  # type: ignore[return-value]


def _parse_config_h(text: str) -> Dict[str, float | int]:
    wanted = {"SAMPLE_RATE", "BUFFER_SIZE", "DELAY_SIZE", "AUDIO_GAIN", "MELODY_SLOTS"}
    out: Dict[str, float | int] = {}
    for raw in text.splitlines():
        line = raw.split("//")[0].strip()
        if not line.startswith("#define"):
            continue
        rest = line[len("#define") :].strip()
        parts = rest.split(None, 1)
        if len(parts) < 2:
            continue
        name, val = parts[0], parts[1]
        if name not in wanted:
            continue
        if val.startswith('"'):
            continue
        if name in ("MELODY_SLOTS", "SAMPLE_RATE", "BUFFER_SIZE", "DELAY_SIZE"):
            out[name] = _parse_int_lit(val)
        else:
            out[name] = _parse_float_lit(val)
    missing = wanted - set(out.keys())
    if missing:
        raise FirmwareParseError(f"Config.h missing defines: {sorted(missing)}")
    return out


def _parse_active_echo_model_name(config_text: str) -> str:
    last: str | None = None
    for raw in config_text.splitlines():
        if raw.strip().startswith("//"):
            continue
        line = raw.split("//")[0].strip()
        m = re.match(r'#define\s+ECHO_UNIQUE_MODEL_NAME\s+"([^"]+)"\s*$', line)
        if m:
            last = m.group(1)
    if not last:
        raise FirmwareParseError("ECHO_UNIQUE_MODEL_NAME not found in Config.h")
    return last


def _parse_static_const_k(audio_cpp: str) -> Dict[str, float | int]:
    out: Dict[str, float | int] = {}
    for m in re.finditer(
        r"static const (float|int)\s+(\w+)\s*=\s*([^;]+);",
        audio_cpp,
    ):
        ctype, name, rhs = m.group(1), m.group(2), m.group(3).strip()
        if name == "kSecPerBeat":
            continue
        if ctype == "int":
            out[name] = _parse_int_lit(rhs)
        else:
            if "/" in rhs and "kBpm" in rhs:
                continue
            out[name] = _parse_float_lit(rhs)
    if "kBpm" not in out:
        raise FirmwareParseError("kBpm not found in AudioSynth.cpp")
    out["kSecPerBeat"] = 60.0 / float(out["kBpm"])
    for key in ("kDelayFeedback", "kDelayWet", "kMasterLinear", "kRevCombLen", "kSecPerBeat"):
        if key not in out:
            raise FirmwareParseError(f"{key} not resolved from AudioSynth.cpp")
    return out


def _parse_render_audio_coeffs(render_body: str) -> Dict[str, float]:
    m = re.search(
        r"revIn\s*\*\s*([\d.]+)f\s*\+\s*combTap\s*\*\s*([\d.]+)f",
        render_body,
    )
    if not m:
        raise FirmwareParseError("reverb comb coeffs not found in renderAudio")
    rev_in, rev_fb = float(m.group(1)), float(m.group(2))
    m2 = re.search(r"combTap\s*\*\s*([\d.]+)f", render_body)
    if not m2:
        raise FirmwareParseError("combTap wet not found")
    wet = float(m2.group(1))
    m3 = re.search(r"revWet\s*\*\s*([\d.]+)f", render_body)
    if not m3:
        raise FirmwareParseError("revWet mix not found")
    mix = float(m3.group(1))
    m4 = re.search(r"tanhf\s*\(\s*out\s*\*\s*([\d.]+)f\s*\)", render_body)
    if not m4:
        raise FirmwareParseError("tanh drive not found")
    return {
        "rev_in": rev_in,
        "rev_fb": rev_fb,
        "rev_wet_mul": wet,
        "rev_mix": mix,
        "tanh_drive": float(m4.group(1)),
        "pcm_full_scale": 32767.0,
        "bounce_osc_half": 0.5,
    }


def _parse_schedule_next_grid(sched_body: str) -> Dict[str, float | int]:
    m = re.search(r"1\.0f\s*\+\s*([\d.]+)f", sched_body)
    if not m:
        raise FirmwareParseError("swing + delta not found in scheduleNextGridMs")
    swing = float(m.group(1))
    m2 = re.search(r'if \(type == "MESSY"\)\s*\{\s*beats\s*=\s*([\d.]+)f', sched_body)
    if not m2:
        raise FirmwareParseError("MESSY beats not found")
    messy_beats = float(m2.group(1))
    m3 = re.search(r"random\s*\(\s*(-?\d+)\s*,\s*(\d+)\s*\)", sched_body)
    if not m3:
        raise FirmwareParseError("random jitter range not found")
    rlo, rhi = int(m3.group(1)), int(m3.group(2))
    m4 = re.search(
        r"random\s*\([^)]+\)\s*/\s*1000\.0f\)\s*\*\s*([\d.]+)f",
        sched_body,
    )
    if not m4:
        raise FirmwareParseError("jitter scale not found")
    m5 = re.search(r"if \(ms < ([\d.]+)f\)", sched_body)
    if not m5:
        raise FirmwareParseError("min ms guard not found")
    return {
        "swing_delta": swing,
        "messy_beats": messy_beats,
        "rand_lo": rlo,
        "rand_hi": rhi,
        "jitter_mul": float(m4.group(1)),
        "min_ms": float(m5.group(1)),
    }


def _parse_db_to_linear(audio_cpp: str) -> Tuple[float, float]:
    body = _function_inner_body(audio_cpp, "static float dbToLinear")
    m = re.search(r"return\s+powf\s*\(\s*([\d.]+)f\s*,\s*db\s*/\s*([\d.]+)f\s*\)", body)
    if not m:
        raise FirmwareParseError("dbToLinear powf not found")
    return float(m.group(1)), float(m.group(2))


def _parse_midi_to_freq(utils_cpp: str) -> Tuple[float, float, float]:
    body = _function_inner_body(utils_cpp, "float midiToFreq")
    m = re.search(
        r"return\s+([\d.]+)f\s*\*\s*powf\s*\(\s*2\.0f\s*,\s*\(midi\s*-\s*([\d.]+)f\)\s*/\s*([\d.]+)f\s*\)",
        body,
    )
    if not m:
        raise FirmwareParseError("midiToFreq return not found in Utils.cpp")
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


def _parse_setup_i2s_delay_frac(audio_cpp: str) -> float:
    setup_body = _function_inner_body(audio_cpp, "void setupI2S")
    m = re.search(r"kSecPerBeat\s*\*\s*([\d.]+)f\s*\*\s*SAMPLE_RATE", setup_body)
    if not m:
        raise FirmwareParseError("delay tap * kSecPerBeat * ?_f * SAMPLE_RATE not found")
    return float(m.group(1))


def _parse_float_globals_audio_section(audio_cpp: str) -> Dict[str, float]:
    i = audio_cpp.find("// AUDIO GLOBALS")
    if i < 0:
        raise FirmwareParseError("AUDIO GLOBALS marker not found")
    chunk = audio_cpp[i : i + 2500]
    names = (
        "phase1",
        "phase2",
        "freq1",
        "freq2",
        "cutoff",
        "gBrightness",
        "gCalmness",
        "gDensityBias",
    )
    out: Dict[str, float] = {}
    for n in names:
        m = re.search(rf"float\s+{re.escape(n)}\s*=\s*([^;]+);", chunk)
        if not m:
            raise FirmwareParseError(f"float {n} initializer not found near AUDIO GLOBALS")
        out[n] = _parse_float_lit(m.group(1))
    return out


def _parse_static_adsr_defaults(audio_cpp: str) -> Tuple[float, float]:
    m = re.search(r"static float gSusLevel\s*=\s*([^;]+);", audio_cpp)
    if not m:
        raise FirmwareParseError("static float gSusLevel default not found")
    m2 = re.search(r"static float gOscGainLinear\s*=\s*([^;]+);", audio_cpp)
    if not m2:
        raise FirmwareParseError("static float gOscGainLinear default not found")
    return _parse_float_lit(m.group(1)), _parse_float_lit(m2.group(1))


def _parse_echo_voice_enum(globals_h: str) -> List[Tuple[str, int]]:
    m = re.search(
        r"enum\s+class\s+EchoVoice\s*:\s*\w+\s*\{([^}]+)\}",
        globals_h,
        re.DOTALL,
    )
    if not m:
        raise FirmwareParseError("EchoVoice enum not found in Globals.h")
    body = m.group(1)
    next_val = 0
    pairs: List[Tuple[str, int]] = []
    for segment in body.split(","):
        seg = segment.strip()
        if not seg:
            continue
        if "=" in seg:
            name, rhs = seg.split("=", 1)
            name = name.strip()
            rhs = rhs.strip().split()[0]
            next_val = int(rhs.strip(), 0)
        else:
            name = seg.strip()
        py_name = "None_" if name == "None" else name
        pairs.append((py_name, next_val))
        next_val += 1
    return pairs


def _parse_init_melody_semi_rows(audio_cpp: str) -> Dict[str, List[int]]:
    body = _function_inner_body(audio_cpp, "void initEchoMelodyState")
    bounce_b = _if_case_body(body, 'if (String(MY_NAME).indexOf("BOUNCE")')
    shy_b = _if_case_body(body, 'else if (String(MY_NAME).indexOf("SHY")')
    m_final = re.search(r"}\s*else\s*\{\s*int m\[\]\s*=\s*\{", body, re.DOTALL)
    if not m_final:
        raise FirmwareParseError("initEchoMelodyState: final else { int m[] not found")
    eb = body.find("{", m_final.start())
    else_b = _body_after_opening_brace(body, eb)
    return {
        "BOUNCE": _first_int_array(bounce_b, "m"),
        "SHY": _first_int_array(shy_b, "m"),
        "DEFAULT": _first_int_array(else_b, "m"),
    }


def _parse_gate_beats_linear(block: str) -> Tuple[float, float]:
    m = re.search(
        r"gateBeats\s*=\s*([\d.f]+)f\s*\*\s*\(\s*1\.0f\s*-\s*c\s*\)\s*\+\s*([\d.f]+)f\s*\*\s*c",
        block,
        re.DOTALL,
    )
    if not m:
        raise FirmwareParseError("gateBeats = a*(1-c)+b*c not found")
    return _parse_float_lit(m.group(1)), _parse_float_lit(m.group(2))


def _parse_prob_coeffs(inner: str) -> Dict[str, Tuple[float, float]]:
    out: Dict[str, Tuple[float, float]] = {}
    patterns = (
        ("MESSY", r'if \(type == "MESSY"\)\s*\{\s*prob = ([^;]+);'),
        ("SHY", r'else if \(type == "SHY"\)\s*\{\s*prob = ([^;]+);'),
        ("BOUNCE", r'else if \(type == "BOUNCE"\)\s*\{\s*prob = ([^;]+);'),
    )
    for tag, pat in patterns:
        m = re.search(pat, inner)
        if not m:
            raise FirmwareParseError(f"prob line for {tag} not found")
        m2 = re.search(r"([\d.f]+)\s*\+\s*c\s*\*\s*([\d.f]+)", m.group(1))
        if not m2:
            raise FirmwareParseError(f"prob expr for {tag}")
        out[tag] = (_parse_float_lit(m2.group(1)), _parse_float_lit(m2.group(2)))
    return out


def _parse_personality_blocks(audio_cpp: str) -> Tuple[str, Dict[str, dict]]:
    inner = _function_inner_body(audio_cpp, "void triggerPersonality")
    probs = _parse_prob_coeffs(inner)

    mark = "int midiNote = 60;"
    j = inner.find(mark)
    if j < 0:
        raise FirmwareParseError("triggerPersonality: int midiNote = 60; not found")
    main = inner[j:]

    messy = _if_case_body(main, 'if (type == "MESSY")')
    shy = _if_case_body(main, 'else if (type == "SHY")')
    bounce = _if_case_body(main, 'else if (type == "BOUNCE")')

    def common(tag: str, block: str, var_rand: int) -> dict:
        mel = _first_int_array(block, "mel")
        var = _first_int_array(block, "var")
        m_base = re.search(r"random\s*\(\s*0\s*,\s*1000\s*\)\s*<\s*(\d+)", block)
        if not m_base:
            raise FirmwareParseError(f"{tag}: base threshold not found")
        cutoff_m = re.search(r"cutoff\s*=\s*([\d.]+)f\s*\+\s*c\s*\*\s*([\d.]+)f", block)
        if not cutoff_m:
            raise FirmwareParseError(f"{tag}: cutoff not found")
        db_m = re.search(
            r"dbToLinear\s*\(\s*(-?[\d.]+)f\s*\+\s*c\s*\*\s*([\d.]+)f\s*\)",
            block,
        )
        if not db_m:
            raise FirmwareParseError(f"{tag}: dbToLinear not found")
        env = _first_begin_envelope(block)
        return {
            "prob": probs[tag],
            "mel": mel,
            "var": var,
            "mel_mod": len(mel),
            "var_rand": var_rand,
            "base_lt": int(m_base.group(1)),
            "cutoff0": _parse_float_lit(cutoff_m.group(1)),
            "cutoff_slope": _parse_float_lit(cutoff_m.group(2)),
            "db0": _parse_float_lit(db_m.group(1)),
            "db_slope": _parse_float_lit(db_m.group(2)),
            "env": env,
        }

    messy_d = common("MESSY", messy, 4)
    m_c = re.search(r"0\.5f\)\s*\*\s*([\d.]+)f", messy)
    if not m_c:
        m_c = re.search(r"-\s*0\.5f\)\s*\*\s*([\d.]+)f", messy)
    if not m_c:
        raise FirmwareParseError("MESSY: cents multiplier not found")
    messy_d["cents_amp"] = _parse_float_lit(m_c.group(1))
    m_f2 = re.search(
        r"freq2\s*=\s*f0\s*\*\s*powf\s*\(\s*2\.0f\s*,\s*([\d.]+)f\s*/\s*1200\.0f\s*\)",
        messy,
        re.DOTALL,
    )
    if m_f2:
        messy_d["freq2_mul"] = 2.0 ** (float(m_f2.group(1)) / 1200.0)
    else:
        m_f2b = re.search(r"freq2\s*=\s*f0\s*\*\s*([\d.]+)f", messy)
        if not m_f2b:
            raise FirmwareParseError("MESSY: freq2 mul not found")
        messy_d["freq2_mul"] = _parse_float_lit(m_f2b.group(1))
    gbf, gbn = _parse_gate_beats_linear(messy)
    messy_d["gate_far_beats"] = gbf
    messy_d["gate_near_beats"] = gbn

    shy_d = common("SHY", shy, 5)
    m_fs = re.search(
        r"freq2\s*=\s*freq1\s*\*\s*powf\s*\(\s*2\.0f\s*,\s*([\d.]+)f\s*/\s*1200\.0f\s*\)",
        shy,
        re.DOTALL,
    )
    if m_fs:
        shy_d["freq2_mul"] = 2.0 ** (float(m_fs.group(1)) / 1200.0)
    else:
        m_fs2 = re.search(r"freq2\s*=\s*freq1\s*\*\s*([\d.]+)f", shy)
        if not m_fs2:
            raise FirmwareParseError("SHY: freq2 mul not found")
        shy_d["freq2_mul"] = _parse_float_lit(m_fs2.group(1))
    sbf, sbn = _parse_gate_beats_linear(shy)
    shy_d["gate_far_beats"] = sbf
    shy_d["gate_near_beats"] = sbn

    bounce_d = common("BOUNCE", bounce, 6)
    m_cb = re.search(r"-\s*0\.5f\)\s*\*\s*([\d.]+)f", bounce)
    if not m_cb:
        raise FirmwareParseError("BOUNCE: cents multiplier not found")
    bounce_d["cents_amp"] = _parse_float_lit(m_cb.group(1))
    det = [
        float(x)
        for x in re.findall(
            r"powf\s*\(\s*2\.0f\s*,\s*(-?[\d.]+)f\s*/\s*1200\.0f\s*\)",
            bounce,
        )
    ]
    if len(det) < 2:
        raise FirmwareParseError("BOUNCE: two detune powf not found")
    bounce_d["detune_lo_cents"] = det[0]
    bounce_d["detune_hi_cents"] = det[1]
    bbf, bbn = _parse_gate_beats_linear(bounce)
    bounce_d["gate_far_beats"] = bbf
    bounce_d["gate_near_beats"] = bbn

    tables = {"MESSY": messy_d, "SHY": shy_d, "BOUNCE": bounce_d}
    return inner, tables


def _load_firmware_bundle() -> dict:
    cfg_txt = _read_source("Config.h")
    audio_cpp = _read_source("AudioSynth.cpp")
    utils_cpp = _read_source("Utils.cpp")
    globals_h = _read_source("Globals.h")

    cfg = _parse_config_h(cfg_txt)
    k_tbl = _parse_static_const_k(audio_cpp)
    sched = _parse_schedule_next_grid(_function_inner_body(audio_cpp, "static void scheduleNextGridMs"))
    render_body = _function_inner_body(audio_cpp, "void renderAudio")
    render_c = _parse_render_audio_coeffs(render_body)
    db_lin = _parse_db_to_linear(audio_cpp)
    midi_c = _parse_midi_to_freq(utils_cpp)
    delay_frac = _parse_setup_i2s_delay_frac(audio_cpp)
    g_floats = _parse_float_globals_audio_section(audio_cpp)
    g_sus, g_osc_def = _parse_static_adsr_defaults(audio_cpp)
    echo_pairs = _parse_echo_voice_enum(globals_h)
    melody_init = _parse_init_melody_semi_rows(audio_cpp)
    _, pers = _parse_personality_blocks(audio_cpp)
    model_name = _parse_active_echo_model_name(cfg_txt)

    return {
        "firmware_dir": _firmware_dir(),
        "config": cfg,
        "k": k_tbl,
        "schedule": sched,
        "render": render_c,
        "db_linear": db_lin,
        "midi": midi_c,
        "delay_line_frac": delay_frac,
        "global_floats": g_floats,
        "g_sus_default": g_sus,
        "g_osc_default": g_osc_def,
        "echo_voice_pairs": echo_pairs,
        "melody_init_semi": melody_init,
        "personality": pers,
        "default_model_name": model_name,
    }


_FW = _load_firmware_bundle()

SAMPLE_RATE = int(_FW["config"]["SAMPLE_RATE"])
BUFFER_SIZE = int(_FW["config"]["BUFFER_SIZE"])
DELAY_SIZE = int(_FW["config"]["DELAY_SIZE"])
AUDIO_GAIN = float(_FW["config"]["AUDIO_GAIN"])
MELODY_SLOTS = int(_FW["config"]["MELODY_SLOTS"])

kBpm = float(_FW["k"]["kBpm"])
kSecPerBeat = float(_FW["k"]["kSecPerBeat"])
kDelayFeedback = float(_FW["k"]["kDelayFeedback"])
kDelayWet = float(_FW["k"]["kDelayWet"])
kMasterLinear = float(_FW["k"]["kMasterLinear"])
kRevCombLen = int(_FW["k"]["kRevCombLen"])

SCHED = _FW["schedule"]
RENDER = _FW["render"]
DB_BASE, DB_DIV = _FW["db_linear"]
MIDI_A4, MIDI_REF_NOTE, MIDI_DIV = _FW["midi"]
DELAY_TAP_FRAC = _FW["delay_line_frac"]
G_INIT = _FW["global_floats"]
G_SUS_DEFAULT = _FW["g_sus_default"]
G_OSC_DEFAULT = _FW["g_osc_default"]
MELODY_SEMI_BY_UNIT = _FW["melody_init_semi"]
PERSONALITY = _FW["personality"]
DEFAULT_MODEL_NAME = _FW["default_model_name"]

EchoVoice = IntEnum("EchoVoice", _FW["echo_voice_pairs"])  # type: ignore[misc]

gDelayTapSamples = 0
sSwingLong = False
gStepMessy = 0
gStepShy = 0
gStepBounce = 0


class EnvStage(IntEnum):
    Idle = 0
    Attack = 1
    Decay = 2
    Hold = 3
    Release = 4


gEnvStage = EnvStage.Idle
gEnvAmp = 0.0
gEnvSegCounter = 0
gAtkSamples = 1
gDecSamples = 1
gRelSamples = 1
gSusLevel = G_SUS_DEFAULT
gGateSamples = 1
gEnvAge = 0
gReleasePeak = 1.0
gAtkCoef = 0.1
gDecCoef = 0.1
gRelCoef = 0.9995
gOscGainLinear = G_OSC_DEFAULT
gLp1 = 0.0
gLp2 = 0.0
gFcSmoothed = 2400.0
gRevComb: List[float] = [0.0] * kRevCombLen
gRevIdx = 0

phase1 = G_INIT["phase1"]
phase2 = G_INIT["phase2"]
freq1 = G_INIT["freq1"]
freq2 = G_INIT["freq2"]
cutoff = G_INIT["cutoff"]
nextNoteTime = 0
delayBuffer = [0.0] * DELAY_SIZE
delayIndex = 0
gPlayingVoice = EchoVoice.None_
gMelodySemi = [0] * MELODY_SLOTS
gBrightness = G_INIT["gBrightness"]
gCalmness = G_INIT["gCalmness"]
gDensityBias = G_INIT["gDensityBias"]


def clampf(x: float, a: float, b: float) -> float:
    if x < a:
        return a
    if x > b:
        return b
    return x


def db_to_linear(db: float) -> float:
    return DB_BASE ** (db / DB_DIV)


def midi_to_freq(midi: float) -> float:
    return MIDI_A4 * (2.0 ** ((midi - MIDI_REF_NOTE) / MIDI_DIV))


def wave_sine(p: float) -> float:
    return math.sin(p * 2.0 * math.pi)


def wave_piano(p: float) -> float:
    w = 2.0 * math.pi * p
    return (
        0.44 * math.sin(w)
        + 0.24 * math.sin(2.0 * w)
        + 0.14 * math.sin(3.0 * w)
        + 0.10 * math.sin(4.0 * w)
        + 0.08 * math.sin(5.0 * w)
    )


def wave_triangle(p: float) -> float:
    if p < 0.5:
        return -1.0 + p * 4.0
    return 3.0 - p * 4.0


def wave_saw(p: float) -> float:
    return p * 2.0 - 1.0


def advance_phase(p: float, freq_hz: float) -> float:
    p += freq_hz / SAMPLE_RATE
    if p >= 1.0:
        p -= 1.0
    return p


def arduino_random(rng: random.Random, low: int, high: int) -> int:
    return rng.randrange(low, high)


def schedule_next_grid_ms(type_: str, rng: random.Random) -> None:
    global sSwingLong, nextNoteTime
    sd = float(SCHED["swing_delta"])
    swing_mul = (1.0 + sd) if sSwingLong else (1.0 - sd)
    sSwingLong = not sSwingLong
    beats = float(SCHED["messy_beats"]) if type_ == "MESSY" else 1.0
    ms = kSecPerBeat * beats * swing_mul * 1000.0
    ms += (arduino_random(rng, int(SCHED["rand_lo"]), int(SCHED["rand_hi"])) / 1000.0) * float(
        SCHED["jitter_mul"]
    )
    if ms < float(SCHED["min_ms"]):
        ms = float(SCHED["min_ms"])
    nextNoteTime = int(ms)


def two_pole_lowpass(inp: float, fc_hz: float) -> None:
    global gLp1, gLp2
    alpha = 1.0 - math.exp(-2.0 * math.pi * fc_hz / SAMPLE_RATE)
    alpha = clampf(alpha, 0.001, 0.95)
    gLp1 += alpha * (inp - gLp1)
    gLp2 += alpha * (gLp1 - gLp2)


def begin_envelope(
    atk_sec: float,
    dec_sec: float,
    sus_level: float,
    rel_sec: float,
    gate_sec: float,
) -> None:
    global gEnvStage, gEnvAmp, gEnvSegCounter
    global gAtkSamples, gDecSamples, gRelSamples, gSusLevel
    global gGateSamples, gEnvAge
    global gAtkCoef, gDecCoef, gRelCoef
    gAtkSamples = max(1, int(atk_sec * SAMPLE_RATE))
    gDecSamples = max(1, int(dec_sec * SAMPLE_RATE))
    gRelSamples = max(1, int(rel_sec * SAMPLE_RATE))
    gSusLevel = sus_level
    gGateSamples = max(1, int(gate_sec * SAMPLE_RATE))
    gEnvAge = 0
    atk_n = float(gAtkSamples)
    dec_n = float(gDecSamples)
    rel_n = float(gRelSamples)
    gAtkCoef = 1.0 - math.exp(-5.5 / max(1.0, atk_n))
    gDecCoef = 1.0 - math.exp(-5.5 / max(1.0, dec_n))
    gRelCoef = (4e-4) ** (1.0 / max(1.0, rel_n))
    gEnvStage = EnvStage.Attack
    gEnvAmp = 0.0
    gEnvSegCounter = 0


def advance_envelope() -> None:
    global gEnvStage, gEnvAmp, gEnvSegCounter, gReleasePeak, gEnvAge
    if gEnvStage == EnvStage.Idle:
        return

    do_release = gEnvStage == EnvStage.Release
    if not do_release:
        gEnvAge += 1
        if gEnvAge > gGateSamples:
            gReleasePeak = max(gEnvAmp, 1e-6)
            gEnvStage = EnvStage.Release
            gEnvSegCounter = 0
            do_release = True

    if do_release:
        gEnvAmp *= gRelCoef
        gEnvSegCounter += 1
        if gEnvAmp < 1e-4 or gEnvSegCounter >= gRelSamples * 4:
            gEnvAmp = 0.0
            gEnvStage = EnvStage.Idle
        return

    if gEnvStage == EnvStage.Attack:
        gEnvAmp += (1.0 - gEnvAmp) * gAtkCoef
        if gEnvAmp >= 0.999:
            gEnvAmp = 1.0
            gEnvStage = EnvStage.Decay
            gEnvSegCounter = 0
        return
    if gEnvStage == EnvStage.Decay:
        gEnvAmp += (gSusLevel - gEnvAmp) * gDecCoef
        gEnvSegCounter += 1
        if gEnvAmp <= gSusLevel + 0.005 or gEnvSegCounter >= gDecSamples:
            gEnvAmp = gSusLevel
            gEnvStage = EnvStage.Hold
            gEnvSegCounter = 0
        return
    if gEnvStage == EnvStage.Hold:
        gEnvAmp = gSusLevel
        return


def init_echo_melody_state(my_name: str) -> None:
    global gMelodySemi
    if "BOUNCE" in my_name:
        m = MELODY_SEMI_BY_UNIT["BOUNCE"]
    elif "SHY" in my_name:
        m = MELODY_SEMI_BY_UNIT["SHY"]
    else:
        m = MELODY_SEMI_BY_UNIT["DEFAULT"]
    for k in range(MELODY_SLOTS):
        gMelodySemi[k] = m[k]


def trigger_personality(type_: str, closeness: float, rng: random.Random) -> None:
    global gPlayingVoice, gStepMessy, gStepShy, gStepBounce, freq1, freq2, cutoff, gOscGainLinear

    schedule_next_grid_ms(type_, rng)
    c = clampf(closeness, 0.0, 1.0)
    if type_ not in PERSONALITY:
        return
    P = PERSONALITY[type_]
    p0, p1 = P["prob"]
    prob = p0 + c * p1
    if rng.random() >= prob:
        return

    midi_note = 60
    mel = P["mel"]
    var = P["var"]
    mod = P["mel_mod"]
    vr = P["var_rand"]

    if type_ == "MESSY":
        gPlayingVoice = EchoVoice.Messy
        base = arduino_random(rng, 0, 1000) < P["base_lt"]
        midi_note = mel[gStepMessy % mod] if base else var[arduino_random(rng, 0, vr)]
        gStepMessy += 1
        cutoff = P["cutoff0"] + c * P["cutoff_slope"]
        gOscGainLinear = db_to_linear(P["db0"] + c * P["db_slope"])
        cents = ((arduino_random(rng, 0, 1000) / 1000.0) - 0.5) * P["cents_amp"]
        f0 = midi_to_freq(float(midi_note)) * (2.0 ** (cents / 1200.0))
        freq1 = f0
        freq2 = f0 * P["freq2_mul"]
        gbf = P["gate_far_beats"]
        gbn = P["gate_near_beats"]
        gate_beats = clampf(gbf * (1.0 - c) + gbn * c, 0.14, 2.75)
        gate_sec = kSecPerBeat * gate_beats
        atk, dec, sus, rel = P["env"]
        begin_envelope(atk, dec, sus, rel, gate_sec)

    elif type_ == "SHY":
        gPlayingVoice = EchoVoice.Shy
        base = arduino_random(rng, 0, 1000) < P["base_lt"]
        midi_note = mel[gStepShy % mod] if base else var[arduino_random(rng, 0, vr)]
        gStepShy += 1
        cutoff = P["cutoff0"] + c * P["cutoff_slope"]
        gOscGainLinear = db_to_linear(P["db0"] + c * P["db_slope"])
        freq1 = midi_to_freq(float(midi_note))
        freq2 = freq1 * P["freq2_mul"]
        gbf = P["gate_far_beats"]
        gbn = P["gate_near_beats"]
        gate_beats = clampf(gbf * (1.0 - c) + gbn * c, 0.14, 2.75)
        gate_sec = kSecPerBeat * gate_beats
        atk, dec, sus, rel = P["env"]
        begin_envelope(atk, dec, sus, rel, gate_sec)

    elif type_ == "BOUNCE":
        gPlayingVoice = EchoVoice.Bounce
        base = arduino_random(rng, 0, 1000) < P["base_lt"]
        midi_note = mel[gStepBounce % mod] if base else var[arduino_random(rng, 0, vr)]
        gStepBounce += 1
        cutoff = P["cutoff0"] + c * P["cutoff_slope"]
        gOscGainLinear = db_to_linear(P["db0"] + c * P["db_slope"])
        cents = ((arduino_random(rng, 0, 1000) / 1000.0) - 0.5) * P["cents_amp"]
        f0 = midi_to_freq(float(midi_note)) * (2.0 ** (cents / 1200.0))
        freq1 = f0 * (2.0 ** (P["detune_lo_cents"] / 1200.0))
        freq2 = f0 * (2.0 ** (P["detune_hi_cents"] / 1200.0))
        gbf = P["gate_far_beats"]
        gbn = P["gate_near_beats"]
        gate_beats = clampf(gbf * (1.0 - c) + gbn * c, 0.14, 2.75)
        gate_sec = kSecPerBeat * gate_beats
        atk, dec, sus, rel = P["env"]
        begin_envelope(atk, dec, sus, rel, gate_sec)


def render_one_sample() -> int:
    global phase1, phase2, delayIndex, gRevIdx, gFcSmoothed
    advance_envelope()
    osc = 0.0
    voice_idle = (gEnvStage == EnvStage.Idle) and (gEnvAmp <= 1e-5)
    if not voice_idle:
        if gPlayingVoice == EchoVoice.Messy:
            osc = wave_sine(phase1)
        elif gPlayingVoice == EchoVoice.Shy:
            osc = wave_sine(phase1)
        elif gPlayingVoice == EchoVoice.Bounce:
            osc = wave_sine(phase1)
        else:
            osc = wave_sine(phase1)
    if not voice_idle:
        phase1 = advance_phase(phase1, freq1)
        phase2 = advance_phase(phase2, freq2)
    shaped = osc * gEnvAmp * gOscGainLinear
    gFcSmoothed += 0.052 * (cutoff - gFcSmoothed)
    two_pole_lowpass(shaped, gFcSmoothed)
    filtered = gLp2
    shy_voice = gPlayingVoice == EchoVoice.Shy
    delay_in = 0.0
    if (not shy_voice) and (not voice_idle):
        delay_in = filtered
    read_idx = delayIndex - gDelayTapSamples
    while read_idx < 0:
        read_idx += DELAY_SIZE
    tap = delayBuffer[read_idx]
    delay_out = delay_in + tap * kDelayWet
    delayBuffer[delayIndex] = delay_in + tap * kDelayFeedback
    delayIndex += 1
    if delayIndex >= DELAY_SIZE:
        delayIndex = 0
    shy_send = filtered if shy_voice else 0.0
    rev_in = delay_out + shy_send
    comb_tap = gRevComb[gRevIdx]
    rev_wet = comb_tap * RENDER["rev_wet_mul"]
    ri, rf = RENDER["rev_in"], RENDER["rev_fb"]
    gRevComb[gRevIdx] = rev_in * ri + comb_tap * rf
    gRevIdx += 1
    if gRevIdx >= kRevCombLen:
        gRevIdx = 0
    out = delay_out + rev_wet * RENDER["rev_mix"]
    out *= kMasterLinear
    out = float(np.tanh(out * RENDER["tanh_drive"]))
    sample_f = out * RENDER["pcm_full_scale"] * AUDIO_GAIN
    s = int(sample_f)
    if s > 32767:
        s = 32767
    elif s < -32768:
        s = -32768
    return s


def setup_i2s_delay_tap() -> None:
    global gDelayTapSamples
    gDelayTapSamples = int(kSecPerBeat * DELAY_TAP_FRAC * SAMPLE_RATE)
    if gDelayTapSamples < 1:
        gDelayTapSamples = 1
    if gDelayTapSamples >= DELAY_SIZE:
        gDelayTapSamples = DELAY_SIZE - 1


@dataclass
class ResetConfig:
    my_name: str
    rng_seed: int


def reset_all(cfg: ResetConfig) -> random.Random:
    global gDelayTapSamples, sSwingLong, gStepMessy, gStepShy, gStepBounce
    global gEnvStage, gEnvAmp, gEnvSegCounter, gAtkSamples, gDecSamples
    global gRelSamples, gSusLevel, gOscGainLinear
    global gGateSamples, gEnvAge, gReleasePeak
    global gAtkCoef, gDecCoef, gRelCoef
    global gLp1, gLp2, gRevIdx, phase1, phase2, freq1, freq2, cutoff, gFcSmoothed
    global nextNoteTime, delayIndex, gPlayingVoice, gBrightness, gCalmness
    global gDensityBias, delayBuffer, gRevComb

    rng = random.Random(cfg.rng_seed)
    gDelayTapSamples = 0
    sSwingLong = False
    gStepMessy = 0
    gStepShy = 0
    gStepBounce = 0
    gEnvStage = EnvStage.Idle
    gEnvAmp = 0.0
    gEnvSegCounter = 0
    gAtkSamples = 1
    gDecSamples = 1
    gRelSamples = 1
    gGateSamples = 1
    gEnvAge = 0
    gReleasePeak = 1.0
    gAtkCoef = 0.1
    gDecCoef = 0.1
    gRelCoef = 0.9995
    gSusLevel = G_SUS_DEFAULT
    gOscGainLinear = G_OSC_DEFAULT
    gLp1 = 0.0
    gLp2 = 0.0
    gRevComb[:] = [0.0] * kRevCombLen
    gRevIdx = 0
    phase1 = G_INIT["phase1"]
    phase2 = G_INIT["phase2"]
    freq1 = G_INIT["freq1"]
    freq2 = G_INIT["freq2"]
    cutoff = G_INIT["cutoff"]
    gFcSmoothed = float(cutoff)
    nextNoteTime = 0
    delayBuffer[:] = [0.0] * DELAY_SIZE
    delayIndex = 0
    gPlayingVoice = EchoVoice.None_
    gBrightness = G_INIT["gBrightness"]
    gCalmness = G_INIT["gCalmness"]
    gDensityBias = G_INIT["gDensityBias"]
    init_echo_melody_state(cfg.my_name)
    setup_i2s_delay_tap()
    return rng


def render_wav_stereo(
    out_path: str,
    personality_type: str,
    my_name: str,
    closeness: float,
    duration_sec: float,
    rng_seed: int,
) -> None:
    rng = reset_all(ResetConfig(my_name=my_name, rng_seed=rng_seed))
    interval_ms = 300.0 - closeness * 220.0
    total_samples = int(duration_sec * SAMPLE_RATE)
    next_trigger_ms = 0.0
    frames: List[int] = []
    block_idx = 0
    samples_done = 0
    while samples_done < total_samples:
        t_block_ms = block_idx * (BUFFER_SIZE / float(SAMPLE_RATE)) * 1000.0
        while next_trigger_ms <= t_block_ms + 1e-9:
            trigger_personality(personality_type, closeness, rng)
            next_trigger_ms += interval_ms
        chunk = min(BUFFER_SIZE, total_samples - samples_done)
        for _ in range(chunk):
            s = render_one_sample()
            frames.append(s)
            frames.append(s)
        samples_done += chunk
        block_idx += 1
    with wave.open(out_path, "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.setcomptype("NONE", "not compressed")
        packed = struct.pack("<" + "h" * len(frames), *frames)
        wf.writeframesraw(packed)


def main() -> None:
    closeness = 0.53
    duration_sec = 20.0
    print("firmware dir:", _FW["firmware_dir"])
    print("Config.h model:", DEFAULT_MODEL_NAME)

    jobs = [
        ("output_bounce_close_053.wav", "BOUNCE", "ECHO_BOUNCE_001", 10001),
        ("output_shy_close_053.wav", "SHY", "ECHO_SHY_001", 20002),
        ("output_messy_close_053.wav", "MESSY", "ECHO_MESSY_001", 30003),
    ]
    for path, ptype, my_name, seed in jobs:
        render_wav_stereo(path, ptype, my_name, closeness, duration_sec, seed)
        print("wrote", path)
    render_wav_stereo(
        "output.wav",
        "BOUNCE",
        "ECHO_BOUNCE_001",
        closeness,
        duration_sec,
        10001,
    )
    print("wrote output.wav (same render as bounce)")


if __name__ == "__main__":
    main()
