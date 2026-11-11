# Agents

This file is for any coding agent pointed at the repo (Cursor, Claude Code,
Codex, and others). Paths below are relative to the repository root.

Start with [docs/agent-workflow.md](docs/agent-workflow.md). It covers the
disc you supply locally, the build, recording your own replays, one task
per session, and the report to send back.

Settled choices are in [docs/decisions.md](docs/decisions.md). The build and
the USA disc check are in [README.md](README.md). The harness is
[docs/netplay-determinism.md](docs/netplay-determinism.md).

[HANDOFF.md](HANDOFF.md) is maintainer-local. It holds one machine's branch
tip, absolute paths, worker, HDD snapshot, and guest names. Read it only
when you are continuing that machine. Do not copy those paths into the files
above.

Claude Code also reads [CLAUDE.md](CLAUDE.md), which points here.

The sections above remain the disc, build, replay, and report procedure, and machine paths stay in HANDOFF.md.

# Agent instructions

## Project discovery

Do not assume commands or conventions. Find them in the repo.
- On the first task of a session, read what exists: README, CONTRIBUTING, package manifests and scripts (`package.json`, `pyproject.toml`, `Cargo.toml`, `go.mod`, `Makefile`, `justfile`), CI config (`.github/workflows`, etc.), linter and formatter configs, and a few existing source and test files.
- From that, work out: how to install, build, test (all and single file), lint, and type check; where source, tests, and config live; naming, import, and error handling style.
- Follow the project's established practice over general best practice. Use the tools it already uses. Do not introduce a new tool, framework, or style.
- Where the project is silent, follow widely accepted best practice for that stack and say which choice you made.
- If sources conflict (README says one command, CI runs another), prefer what CI runs. If you still cannot tell, ask one question.
- Save what you found under "Discovered facts" in `NOTES.md` so later chats do not repeat the search. If `NOTES.md` already has it, trust it, and correct it if a command fails.
- Anything under "Project overrides" below beats discovery.

## Project overrides

Optional. Add only what discovery gets wrong or cannot know.

## Known gotchas

Things that have gone wrong in this repo. Add a line every time the agent repeats a mistake.

## Writing style

- Do not use em dashes or en dashes as punctuation. Use a period, comma, colon, or parentheses.
- Skip filler openers and closers ("Great question", "I hope this helps", "Let me know if you'd like...").
- Avoid stock phrasing: "delve", "leverage", "robust", "seamless", "it's worth noting".
- Do not narrate what you are about to do or recap what you just did. State results and next steps only.
- Write plainly. Short sentences, common words.
- This applies to code comments, commit messages, and docs too.

## Task queue

- Keep a running todo list for the session.
- A correction to the current work ("that's wrong, use X") applies now. Do not queue it.
- A new request or unrelated tweak goes to the end of the list. Say "queued" and keep working on the current item. Switch only if I say "do this now".
- If a queued item needs a plan (see below), post the plan and keep working on other items while it waits for "go". If nothing else is left, stop and wait.
- Stop only when every item is done or blocked on me. End with a short summary of what was completed and what is blocked.

## Handling mistakes

- Do not apologize or say "my mistake".
- When something goes wrong, state what happened, which instruction (or missing instruction) allowed it, and the specific rule or file change that would prevent it. Then propose that change.

## Scope

- Do the simplest thing that meets the request: fewer files, fewer abstractions, less code.
- Do not build for hypothetical future needs.
- Do not add features, refactors, or abstractions I did not ask for. List worthwhile ideas under "Suggested scope changes" at the end and wait for approval.
- Before starting, judge where the project sits between prototype and production. Match effort to that level and say which level you assumed.

## Ask vs. assume

- If a wrong guess would cost more than a question, ask one question.
- Otherwise state the assumption in one line and proceed.
- Do not invent APIs, flags, config keys, or package names. If unsure, check the docs or the installed version. Say "I don't know" rather than guess.

## Plan, then execute

- For any task touching more than one file or involving a design choice, write a short plan first and wait for me to say "go".
- Do not plan and implement in the same step.
- For small, clear edits (one file, obvious change), skip the plan and just do it.
- Plans state: goal, files affected, steps, how we will know it is done.

## Editing existing code

- Match the existing style, even if you would write it differently.
- Keep diffs minimal. Do not reformat, rename, or reorganize code unrelated to the task.
- Do not touch files outside the task. If one needs changing, say why and ask.

## Verification

- Before saying a task is done, run the relevant checks discovered above: tests, lint, type check, build.
- If you could not run them, say so and say why.
- Never weaken, skip, or delete a test to make it pass. If a test looks wrong, report it and ask.
- Report results as pass or fail with the failing names. Do not paste full output.

## Debugging

- Reproduce the problem first. If you cannot, say so.
- Find the root cause. Do not patch symptoms.
- After 3 failed attempts at the same problem, stop. Report what you tried, what you learned, and what you suspect. Do not keep looping.

## Permissions

Ask before doing any of these:
- Deleting files or directories.
- Destructive git commands (`reset --hard`, `clean`, force push, branch deletion, history rewrite).
- Running migrations or anything that changes a database or external service.
- Installing, upgrading, or removing dependencies.
- Reading, editing, or printing `.env` files, keys, or other secrets.
- Network calls beyond package registries and docs.
- Pushing to a remote.

## Commits

- Commit only when I ask. When I do, or when I say "commit as you go", work in small commits: one logical change each, tests passing at each commit.
- Do not mix refactors, formatting, and behavior changes in one commit.
- If I have not asked for commits, suggest a commit point after each finished piece and list what it would contain.
- Follow the project's commit message convention if it has one. Otherwise: short imperative subject line, body only if the why is not obvious.

## Delegation

Delegate only when all three are true:
1. The task is independent of what the main agent is doing.
2. It can be stated in a short, complete brief.
3. The result can be verified (tests pass, file exists, search returned results).
Do not delegate small changes. Writing the brief and reading the result often costs more than doing the work directly. Never hand design decisions or ambiguous problems to a subagent.
Every brief states:
- Goal, in one sentence.
- Files or directories involved.
- What "done" means.
- What to report back, and how short.
- Constraints (do not touch X, follow conventions in this file).
If a delegated result is wrong, check the brief first.

## Context and usage hygiene

- Do not read whole files when a search or a line range will do.
- Do not paste large outputs back into the chat. Summarize them.
- When the chat has grown long or the task has changed, suggest a fresh chat. Before that, write the open todo items, decisions made, and current state to `NOTES.md` so the next chat can pick up.
- At the start of a fresh chat, read `NOTES.md` if it exists.

## Review

- When a set of changes is finished, do a complexity check before reporting done: could any part be deleted, merged, or written more simply without losing function? If so, simplify it now.
- If complexity had crept in, end with one suggested change to these instructions that would prevent it next time.
- Subagent review applies only when the change is large (more than about 5 files or 200 changed lines) or touches auth, data, money, or migrations. For those, if subagents are available, spawn one on the cheapest tier with only the diff and the goal. Ask it to find bugs, risky assumptions, and anything unclear. Report its findings.
- If subagents are not available for such a change, list the three riskiest parts instead.
- For smaller changes, the complexity check is enough.
