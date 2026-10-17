"""Host regression tests: python tests/run_regressions.py (requires g++ or CXX).

Compile the production functions under test with deterministic hardware/network
stubs. Functions are extracted, never reimplemented; the ESP32 build remains the
integration check. No printer connection, flash write, or motor movement occurs.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = Path(__file__).resolve().parent


def function(source, signature):
    start = source.index(signature)
    # Ignore braces in comments and string/character literals.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m[0]), source, flags=re.S)
    brace = masked.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    return source[start:end]


def functions(file, signatures):
    source = (ROOT / "src" / file).read_text(encoding="utf-8")
    return "\n\n".join(function(source, signature) for signature in signatures)


with tempfile.TemporaryDirectory(prefix="pbf-regressions-") as directory:
    build = Path(directory)
    settings = functions("settings.cpp", [
        "template <typename T>\nT clampValue", "void sanitize(Settings &s)",
        "template <typename T>\nbool assignInteger", "bool assignSteps(",
        "bool applyValue(", "bool validate(", "bool set(",
    ])
    feeder = functions("feeder.cpp", [
        "float pathMm()", "float fullUnloadLimit()", "bool bufferFull(", "bool bufferEmpty(",
        "float relaxLimitMm()", "void relaxTick(", "void updateUnloading(",
        "bool locked()", "String lockText()", "String lockReason()",
        "bool printerHasActivePrint()", "void ptestEnd(", "const char *testRefusal()",
        "void btestEnd(", "void stopTests(", "void freeTick(",
        "bool actionRunning()", "void stopAction(", "bool filamentAtPrinter()",
        "void upClick()", "void backSingleClick()", "void backDoubleClick()",
        "void stageByUpDoubleClick()", "void handleKey(", "void serviceClicks()",
        "bool maybeStartFollowing()", "void handleKeys()",
    ])
    (build / "settings_under_test.inc").write_text(settings, encoding="utf-8")
    (build / "feeder_under_test.inc").write_text(feeder, encoding="utf-8")
    (build / "net_under_test.inc").write_text(
        functions("net.cpp", ["void handleSettingsPost()"]), encoding="utf-8")
    (build / "ui_under_test.inc").write_text(
        functions("ui.cpp", ["void menuClose()"]), encoding="utf-8")
    executable = build / ("regressions.exe" if os.name == "nt" else "regressions")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra",
                    "-I", str(TESTS / "stubs"), "-I", str(ROOT / "src"), "-I", str(build),
                    str(TESTS / "regressions.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
