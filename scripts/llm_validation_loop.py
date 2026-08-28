#!/usr/bin/env python3
"""
Generate -> curlee check/run -> pass-rate validation loop (issue #239).

Sends the system prompt (docs/llm-system-prompt.md, #237) plus a set of
task descriptions to an OpenAI-chat-completions-compatible endpoint (works
against OpenRouter, or a local server such as llama-server/vLLM), extracts
the returned Curlee code, and checks it against the REAL compiler -- both
`curlee check` (verification) and `curlee run` (execution, compared
against an expected result) where applicable.

This does not ship a curated task set -- #238 tracks building one. A small
starter set covering arithmetic, contracts, control flow, and arrays lives
in tests/llm_validation/tasks.json as a working example; extend it or
point --tasks at your own file.

Usage:
  export OPENROUTER_API_KEY=...   # or point --endpoint at a local server
  python3 scripts/llm_validation_loop.py \
    --endpoint https://openrouter.ai/api/v1/chat/completions \
    --model deepseek/deepseek-chat \
    --tasks tests/llm_validation/tasks.json

  # Against a local OpenAI-compatible server (e.g. llama-server):
  python3 scripts/llm_validation_loop.py \
    --endpoint http://localhost:8080/v1/chat/completions \
    --model local-model --no-auth \
    --tasks tests/llm_validation/tasks.json
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SYSTEM_PROMPT_FILE = REPO_ROOT / "docs" / "llm-system-prompt.md"
DEFAULT_CURLEE_BIN = REPO_ROOT / "build" / "linux-debug" / "curlee"


def load_system_prompt(path: Path) -> str:
    text = path.read_text()
    marker = "## System prompt text (copy everything below this line)"
    if marker in text:
        text = text.split(marker, 1)[1]
    # Stop before the maintainers-only trailer, if present.
    text = text.split("## Notes for maintainers", 1)[0]
    return text.strip()


def extract_code(text: str):
    m = re.search(r"```curlee\s*\n(.*?)```", text, re.DOTALL)
    if m:
        return m.group(1)
    m = re.search(r"```\s*\n(.*?)```", text, re.DOTALL)
    return m.group(1) if m else None


def generate(endpoint: str, model: str, system_prompt: str, task_prompt: str, api_key: str | None, max_tokens: int):
    import urllib.request

    headers = {"content-type": "application/json"}
    if api_key:
        headers["authorization"] = f"Bearer {api_key}"
    body = json.dumps(
        {
            "model": model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": task_prompt},
            ],
            "temperature": 0,
            "max_tokens": max_tokens,
        }
    ).encode()
    req = urllib.request.Request(endpoint, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=300) as resp:
        data = json.loads(resp.read())
    msg = data["choices"][0]["message"]
    return (msg.get("content") or "") + "\n" + (msg.get("reasoning_content") or "")


def check_and_run(curlee_bin: Path, code: str, path: Path, check_only: bool, caps: list, expected_result):
    path.write_text(code)
    check = subprocess.run([str(curlee_bin), "check", str(path)], capture_output=True, text=True, timeout=30)
    if check.returncode != 0:
        return "CHECK_FAIL", check.stdout + check.stderr
    if check_only:
        return "PASS", "check passed (no VM execution required for this task)"
    cmd = [str(curlee_bin), "run"]
    for cap in caps or []:
        cmd += ["--cap", cap]
    cmd.append(str(path))
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if run.returncode != 0:
        return "RUN_FAIL", run.stdout + run.stderr
    if expected_result is None:
        return "PASS", run.stdout
    m = re.search(r"curlee run: result (-?\d+)", run.stdout)
    if not m or int(m.group(1)) != expected_result:
        return "WRONG_RESULT", f"expected {expected_result}, got: {run.stdout}"
    return "PASS", run.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True, help="OpenAI-chat-completions-compatible URL")
    parser.add_argument("--model", required=True)
    parser.add_argument("--tasks", default=str(REPO_ROOT / "tests" / "llm_validation" / "tasks.json"))
    parser.add_argument("--system-prompt", default=str(DEFAULT_SYSTEM_PROMPT_FILE))
    parser.add_argument("--curlee-bin", default=str(DEFAULT_CURLEE_BIN))
    parser.add_argument("--max-tokens", type=int, default=2000)
    parser.add_argument("--no-auth", action="store_true", help="skip the Authorization header (local servers)")
    parser.add_argument("--scratch-dir", default="/tmp")
    args = parser.parse_args()

    system_prompt = load_system_prompt(Path(args.system_prompt))
    tasks = json.loads(Path(args.tasks).read_text())
    api_key = None if args.no_auth else os.environ.get("OPENROUTER_API_KEY")
    if not args.no_auth and not api_key:
        print("warning: OPENROUTER_API_KEY not set and --no-auth not passed; requests will likely be rejected", file=sys.stderr)

    results = []
    for task in tasks:
        try:
            text = generate(args.endpoint, args.model, system_prompt, task["prompt"], api_key, args.max_tokens)
        except Exception as exc:  # noqa: BLE001 - report and continue
            results.append((task["id"], "REQUEST_FAILED", str(exc)))
            continue
        code = extract_code(text)
        if code is None:
            results.append((task["id"], "NO_CODE_EMITTED", text[-300:]))
            continue
        status, detail = check_and_run(
            Path(args.curlee_bin),
            code,
            Path(args.scratch_dir) / f"llmval_{task['id']}.curlee",
            task.get("check_only", False),
            task.get("caps"),
            task.get("expected_result"),
        )
        results.append((task["id"], status, detail))

    passed = sum(1 for _, s, _ in results if s == "PASS")
    print(f"\n=== LLM VALIDATION: {args.model} ===")
    for task_id, status, detail in results:
        mark = "PASS" if status == "PASS" else "FAIL"
        print(f"[{mark}] {task_id}: {status} -- {detail[:150]}")
    print(f"\nSCORE: {passed}/{len(results)}")
    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
